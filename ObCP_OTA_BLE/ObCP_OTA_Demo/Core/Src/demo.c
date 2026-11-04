/**
 ******************************************************************************
 * @file    demo.c
 * @brief   Démonstration : LED clignotante et retour au chargeur BLE.
 ******************************************************************************
 *
 *  EXERCICE PROPOSÉ
 *  ----------------
 *   1. Compilez et transférez ce programme par Bluetooth : la LED clignote
 *      en VERT.
 *   2. Remplacez DEMO_VERSION 1 par 2, compilez et transférez de nouveau :
 *      la LED clignote en BLEU. Le nouveau programme a bien remplacé
 *      l'ancien, sans câble ni bouton « Prog ».
 *
 *  Tout le code est « non bloquant » : aucune attente avec HAL_Delay().
 *  On compare l'heure courante (HAL_GetTick(), en millisecondes depuis le
 *  démarrage) à l'heure du dernier changement : la boucle principale reste
 *  libre pour d'autres tâches.
 ******************************************************************************
 */

#include <stdbool.h>

#include "main.h"               /* HAL et noms des broches (LED_xxx, USER_BUTTON) */
#include "demo.h"
#include "obcp_ota_request.h"   /* obcp_ota_enter_loader() */

/* ------------------------------------------------------------------------- */
/* Réglages                                                                  */
/* ------------------------------------------------------------------------- */

/* Version du programme : 1 = vert, 2 = bleu, 3 = rouge, 4 = jaune,
 * 5 = cyan, 6 = violet (puis on recommence).
 * (#ifndef permet aussi de la choisir à la compilation avec -DDEMO_VERSION=2) */
#ifndef DEMO_VERSION
#define DEMO_VERSION            1
#endif

#define BLINK_SLOW_MS           500U    /* demi-période du clignotement lent   */
#define BLINK_FAST_MS           100U    /* demi-période du clignotement rapide */
#define BUTTON_DEBOUNCE_MS      50U     /* durée minimale d'un appui           */
#define BUTTON_LONG_PRESS_MS    2000U   /* appui long : retour au chargeur     */

/* ------------------------------------------------------------------------- */
/* Couleurs : une couleur = trois interrupteurs (rouge, vert, bleu).         */
/* ------------------------------------------------------------------------- */
typedef struct
{
    bool red;
    bool green;
    bool blue;
} color_t;

static const color_t colors[] =
{
    { false, true,  false },    /* 1 : vert   */
    { false, false, true  },    /* 2 : bleu   */
    { true,  false, false },    /* 3 : rouge  */
    { true,  true,  false },    /* 4 : jaune  */
    { false, true,  true  },    /* 5 : cyan   */
    { true,  false, true  },    /* 6 : violet */
};

/* Nombre d'éléments d'un tableau : taille totale / taille d'un élément. */
#define NB_COLORS   (sizeof(colors) / sizeof(colors[0]))

static const color_t white = { true, true, true };
static const color_t black = { false, false, false };

/* ------------------------------------------------------------------------- */
/* Variables de la démonstration                                             */
/* ------------------------------------------------------------------------- */
static uint32_t blink_period = BLINK_SLOW_MS;  /* vitesse actuelle            */
static uint32_t last_toggle = 0U;              /* heure du dernier changement */
static bool     led_on = false;                /* LED allumée ?               */
static bool     button_was_pressed = false;    /* état au tour précédent      */
static uint32_t button_press_time = 0U;        /* heure de l'appui            */

/* Allume la LED RVB dans la couleur demandée. */
static void led_show(color_t c)
{
    HAL_GPIO_WritePin(LED_RED_GPIO_Port,   LED_RED_Pin,   c.red   ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, c.green ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port,  LED_BLUE_Pin,  c.blue  ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Le bouton USER relie PC13 à la masse : enfoncé = niveau bas (0). */
static bool button_is_pressed(void)
{
    return HAL_GPIO_ReadPin(USER_BUTTON_GPIO_Port, USER_BUTTON_Pin) == GPIO_PIN_RESET;
}

void demo_init(void)
{
    led_show(black);
    last_toggle = HAL_GetTick();
    /* Si USER est encore enfoncé au démarrage, on attend son relâchement
     * avant de compter un appui.                                           */
    button_was_pressed = true;
    button_press_time = HAL_GetTick();
}

void demo_process(void)
{
    uint32_t now = HAL_GetTick();
    bool pressed = button_is_pressed();

    /* --- 1) Bouton USER ---------------------------------------------------- */
    if (pressed && !button_was_pressed)
    {
        button_press_time = now;                 /* début d'un appui */
    }

    if (pressed)
    {
        uint32_t duration = now - button_press_time;

        led_show(white);                         /* retour visuel pendant l'appui */
        if (duration >= BUTTON_LONG_PRESS_MS)
        {
            obcp_ota_enter_loader();             /* ne revient pas */
        }
    }
    else if (button_was_pressed)
    {
        /* Le bouton vient d'être relâché : appui court ? */
        uint32_t duration = now - button_press_time;
        if (duration >= BUTTON_DEBOUNCE_MS)
        {
            blink_period = (blink_period == BLINK_SLOW_MS) ? BLINK_FAST_MS : BLINK_SLOW_MS;
        }
        led_on = false;
        led_show(black);
        last_toggle = now;
    }
    button_was_pressed = pressed;

    /* --- 2) Clignotement (seulement si le bouton est relâché) ------------- */
    if (!pressed && ((now - last_toggle) >= blink_period))
    {
        last_toggle = now;
        led_on = !led_on;
        led_show(led_on ? colors[(DEMO_VERSION - 1) % NB_COLORS] : black);
    }
}
