/**
 ******************************************************************************
 * @file    boot.c
 * @brief   Décision de démarrage et saut vers l'application (voir boot.h).
 ******************************************************************************
 *
 *  Ce fichier travaille AVANT HAL_Init() : il n'utilise donc pas les
 *  fonctions HAL (HAL_GPIO_ReadPin, HAL_Delay...), mais accède directement
 *  aux registres du microcontrôleur, à travers les structures définies par
 *  ST dans stm32l476xx.h (RCC, GPIOC, SysTick, SCB...). C'est l'occasion de
 *  voir ce que font les fonctions HAL « sous le capot ».
 *
 *  Registres utilisés :
 *   - RCC->AHB2ENR : active l'horloge du port GPIOC (sans horloge, un
 *                    périphérique ne fonctionne pas) ;
 *   - GPIOC->MODER : 2 bits par broche : 00 entrée, 01 sortie, 11 analogique
 *                    (11 est l'état après un reset) ;
 *   - GPIOC->IDR   : niveau lu sur les broches en entrée ;
 *   - GPIOC->BSRR  : met des broches à 1 (bits 0 à 15) ou à 0 (bits 16 à 31) ;
 *   - SysTick      : compteur du cœur, utilisé ici pour attendre ;
 *   - SCB->VTOR    : adresse de la table des vecteurs (interruptions).
 ******************************************************************************
 */

#include <stdbool.h>

#include "main.h"              /* registres (CMSIS) et noms des broches */
#include "boot.h"
#include "app_image.h"
#include "loader_config.h"

/* Fréquence de l'horloge au démarrage : oscillateur interne MSI à 4 MHz. */
#define BOOT_CORE_CLOCK_HZ     4000000UL

/* Numéros des broches utilisées (PC13 = bouton USER, PC2 = reset du module BLE).
 * USER_BUTTON_Pin et BLE_RST_Pin viennent de main.h (généré par CubeMX) : ce
 * sont des masques (1 << numéro). On en déduit le numéro avec __builtin_ctz
 * (« count trailing zeros » : nombre de zéros à droite du bit à 1).        */
#define BUTTON_PIN_NUMBER      ((uint32_t)__builtin_ctz(USER_BUTTON_Pin))
#define BLE_RST_PIN_NUMBER     ((uint32_t)__builtin_ctz(BLE_RST_Pin))

static boot_reason_t boot_reason = BOOT_REASON_NONE;

/* ------------------------------------------------------------------------- */
/* Attente active de 'ms' millisecondes avec le compteur SysTick.            */
/* Le compteur descend de LOAD à 0 en 1 ms, puis lève le drapeau COUNTFLAG.  */
/* ------------------------------------------------------------------------- */
static void boot_delay_ms(uint32_t ms)
{
    SysTick->LOAD = (BOOT_CORE_CLOCK_HZ / 1000UL) - 1UL;   /* 1 ms */
    SysTick->VAL  = 0UL;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;

    while (ms > 0UL)
    {
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0UL)
        {
            /* on attend la fin de la milliseconde */
        }
        ms--;
    }

    /* Remise du SysTick dans son état de démarrage. */
    SysTick->CTRL = 0UL;
    SysTick->LOAD = 0UL;
    SysTick->VAL  = 0UL;
}

/* ------------------------------------------------------------------------- */
/* Lit la « boîte aux lettres » de la SRAM2, puis l'efface.                  */
/* Renvoie true si l'application a demandé le chargeur.                      */
/* ------------------------------------------------------------------------- */
static bool boot_mailbox_take(void)
{
    volatile uint32_t *mailbox = (volatile uint32_t *)OTA_REQUEST_ADDRESS;

    /* Le mot magique ET son complément : une valeur aléatoire présente en
     * RAM à la mise sous tension n'a pratiquement aucune chance de passer. */
    bool requested = (mailbox[0] == OTA_REQUEST_MAGIC)
                  && (mailbox[1] == (uint32_t)~OTA_REQUEST_MAGIC);

    mailbox[0] = 0UL;          /* la demande ne sert qu'une fois */
    mailbox[1] = 0UL;
    return requested;
}

/* ------------------------------------------------------------------------- */
/* Lit le bouton USER (PC13). Sur l'ObCP, R1 (4,7 kΩ) tire la broche à 3,3 V */
/* et le bouton la relie à la masse : enfoncé = niveau 0.                    */
/* ------------------------------------------------------------------------- */
static bool boot_button_pressed(void)
{
    uint32_t pressed_count = 0UL;

    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;     /* horloge du port C          */
    (void)RCC->AHB2ENR;                      /* relecture : laisse le temps à l'horloge de démarrer */

    /* PC13 en entrée : MODER13 = 00. */
    USER_BUTTON_GPIO_Port->MODER &= ~(3UL << (BUTTON_PIN_NUMBER * 2UL));

    boot_delay_ms(BOOT_BUTTON_SETTLE_MS);    /* tension stabilisée          */

    /* Trois lectures espacées d'1 ms : on évite d'être trompé par un
     * parasite ou par le rebond du contact.                               */
    for (int i = 0; i < 3; i++)
    {
        if ((USER_BUTTON_GPIO_Port->IDR & USER_BUTTON_Pin) == 0UL)
        {
            pressed_count++;
        }
        boot_delay_ms(1UL);
    }

    /* PC13 revient en mode analogique (état de reset). */
    USER_BUTTON_GPIO_Port->MODER |= (3UL << (BUTTON_PIN_NUMBER * 2UL));

    return (pressed_count == 3UL);
}

/* ------------------------------------------------------------------------- */
/* Impulsion de reset sur le module BLE (PC2 à 0 pendant 5 ms).              */
/* Si le chargeur tournait juste avant (et faisait de la publicité BLE), le  */
/* module continuerait sinon à annoncer « ObCP-OTA-xxxx » alors que          */
/* l'application ne s'en sert peut-être pas. Après le reset, le module       */
/* attend sagement des ordres. La résistance R18 (10 kΩ) ramène ensuite la   */
/* ligne de reset à 1 quand PC2 repasse en mode analogique.                  */
/* ------------------------------------------------------------------------- */
static void boot_reset_ble_module(void)
{
    BLE_RST_GPIO_Port->BSRR = (uint32_t)BLE_RST_Pin << 16;    /* niveau 0 prêt */
    BLE_RST_GPIO_Port->MODER = (BLE_RST_GPIO_Port->MODER & ~(3UL << (BLE_RST_PIN_NUMBER * 2UL)))
                             | (1UL << (BLE_RST_PIN_NUMBER * 2UL));   /* sortie */

    boot_delay_ms(BOOT_BLE_RESET_PULSE_MS);

    BLE_RST_GPIO_Port->MODER |= (3UL << (BLE_RST_PIN_NUMBER * 2UL));  /* analogique */
}

/* ------------------------------------------------------------------------- */
/* Saut vers l'application. Trois étapes :                                   */
/*  1. SCB->VTOR = 0x08080000 : les interruptions utiliseront désormais la    */
/*     table des vecteurs de l'application ;                                 */
/*  2. le pointeur de pile (MSP) prend la valeur du mot 0 de cette table ;   */
/*  3. on saute à l'adresse du mot 1 (Reset_Handler de l'application).       */
/* Les étapes 2 et 3 sont écrites en assembleur, dans une seule instruction  */
/* asm : une fois la pile changée, le code C ne doit plus rien y lire.       */
/* ------------------------------------------------------------------------- */
static void boot_jump_to_application(void)
{
    uint32_t initial_sp    = *(volatile const uint32_t *)(APP_ADDRESS);
    uint32_t reset_handler = *(volatile const uint32_t *)(APP_ADDRESS + 4UL);

    SCB->VTOR = APP_ADDRESS;
    __DSB();                   /* attend la fin de l'écriture dans VTOR */
    __ISB();                   /* vide le pipeline du processeur        */

    __asm volatile (
        "msr msp, %0 \n"       /* MSP <- pointeur de pile de l'application */
        "bx  %1      \n"       /* saut vers Reset_Handler de l'application */
        :
        : "r" (initial_sp), "r" (reset_handler)
        : "memory");

    while (1)
    {
        /* jamais atteint */
    }
}

/* ------------------------------------------------------------------------- */
/* Fonctions publiques                                                       */
/* ------------------------------------------------------------------------- */

void boot_check_and_jump(void)
{
    bool requested = boot_mailbox_take();
    bool button    = boot_button_pressed();
    bool app_ok    = app_image_is_installed();

    if (!app_ok)
    {
        boot_reason = BOOT_REASON_NO_APP;
    }
    else if (button)
    {
        boot_reason = BOOT_REASON_BUTTON;
    }
    else if (requested)
    {
        boot_reason = BOOT_REASON_REQUEST;
    }
    else
    {
        /* Rien ne retient le chargeur : on rend la main à l'application. */
        boot_reset_ble_module();
        RCC->AHB2ENR &= ~RCC_AHB2ENR_GPIOCEN;    /* horloge du port C coupée */
        boot_jump_to_application();              /* ne revient pas */
    }

    /* On revient dans main() : le chargeur BLE va démarrer. */
}

boot_reason_t boot_get_reason(void)
{
    return boot_reason;
}
