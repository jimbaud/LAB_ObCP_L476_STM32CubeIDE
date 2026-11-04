/**
 ******************************************************************************
 * @file    loader.c
 * @brief   Initialisation et boucle principale du chargeur (voir loader.h).
 ******************************************************************************
 *
 *  La boucle principale ne s'arrête jamais longtemps : chaque fonction fait
 *  un petit travail puis rend la main (programmation « non bloquante »).
 *  C'est indispensable pour traiter les événements Bluetooth à temps.
 *
 *  Le chargeur redémarre la carte (et donc lance l'application si elle est
 *  valide) dans trois situations :
 *   - le PC envoie la commande RUN à la fin d'un transfert réussi (la carte
 *     laisse d'abord le PC se déconnecter, voir ble_ota_disconnect) ;
 *   - l'utilisateur fait un appui court sur le bouton USER ;
 *   - personne ne s'est connecté pendant LOADER_IDLE_TIMEOUT_MS (2 min).
 ******************************************************************************
 */

#include <stdbool.h>

#include "main.h"
#include "loader.h"
#include "app_image.h"
#include "ble_ota.h"
#include "leds.h"
#include "loader_config.h"
#include "ota.h"

/* États de la détection d'un appui court sur le bouton USER. */
typedef enum
{
    BUTTON_WAIT_RELEASE,   /* le bouton tenu pendant le reset n'est pas encore relâché */
    BUTTON_IDLE,           /* bouton relâché, attente d'un appui                       */
    BUTTON_PRESSED         /* bouton enfoncé, attente du relâchement                   */
} button_state_t;

static button_state_t button_state = BUTTON_WAIT_RELEASE;
static uint32_t       button_press_time = 0U;  /* instant de l'appui          */
static uint32_t       idle_since = 0U;         /* début de la période sans PC */
static uint32_t       identify_start = 0U;     /* début de l'identification   */
static bool           identifying = false;     /* identification en cours     */

/* ------------------------------------------------------------------------- */
/* Redémarre la carte proprement : LED éteintes, connexion coupée, reset.   */
/* Au redémarrage, boot_check_and_jump() lancera l'application si elle est  */
/* valide (et si le bouton n'est pas enfoncé).                              */
/* ------------------------------------------------------------------------- */
static void loader_restart(void)
{
    leds_set_mode(LEDS_OFF);
    ble_ota_disconnect();
    NVIC_SystemReset();               /* reset logiciel du processeur (CMSIS) */
}

/* ------------------------------------------------------------------------- */
/* Détection d'un appui court (enfoncé au moins 50 ms puis relâché).        */
/* Renvoie true une seule fois par appui.                                   */
/* ------------------------------------------------------------------------- */
static bool button_short_press(void)
{
    bool pressed = (HAL_GPIO_ReadPin(USER_BUTTON_GPIO_Port, USER_BUTTON_Pin)
                    == GPIO_PIN_RESET);               /* bouton actif à 0 */
    uint32_t now = HAL_GetTick();
    bool event = false;

    switch (button_state)
    {
    case BUTTON_WAIT_RELEASE:
        /* Pour entrer dans le chargeur, on a maintenu USER pendant le reset :
         * ce relâchement-là ne doit pas compter comme un appui.            */
        if (!pressed)
        {
            button_state = BUTTON_IDLE;
        }
        break;

    case BUTTON_IDLE:
        if (pressed)
        {
            button_state = BUTTON_PRESSED;
            button_press_time = now;
        }
        break;

    case BUTTON_PRESSED:
    default:
        if (!pressed)
        {
            event = ((now - button_press_time) >= LOADER_BUTTON_DEBOUNCE_MS);
            button_state = BUTTON_IDLE;
        }
        break;
    }
    return event;
}

/* ------------------------------------------------------------------------- */
/* Choisit l'affichage des LED d'après l'état du Bluetooth et du transfert. */
/* ------------------------------------------------------------------------- */
static void update_leds(void)
{
    bool activity = ota_take_activity();      /* un bloc vient d'être acquitté */

    /* Identification demandée par le PC : blanc clignotant pendant 3 s. */
    if (ota_take_identify_request())
    {
        identifying = true;
        identify_start = HAL_GetTick();
    }
    if (identifying && ((HAL_GetTick() - identify_start) >= LOADER_IDENTIFY_MS))
    {
        identifying = false;
    }

    if (!ble_ota_is_ready())                  /* module BLE muet : panne */
    {
        leds_set_mode(LEDS_FAULT);
    }
    else if (identifying)
    {
        leds_set_mode(LEDS_IDENTIFY);
    }
    else if (!ble_ota_is_connected())
    {
        leds_set_mode(app_image_is_installed() ? LEDS_WAITING : LEDS_WAITING_NO_APP);
    }
    else
    {
        switch (ota_get_state())
        {
        case OTA_STATE_RECEIVING:
        case OTA_STATE_COMPLETE:
            leds_set_mode(LEDS_TRANSFER);
            if (activity)
            {
                leds_pulse();
            }
            break;
        case OTA_STATE_DONE:
            leds_set_mode(LEDS_SUCCESS);
            break;
        case OTA_STATE_ERROR:
            leds_set_mode(LEDS_ERROR);
            break;
        case OTA_STATE_IDLE:
        default:
            leds_set_mode(LEDS_CONNECTED);
            break;
        }
    }
    leds_process();
}

/* ------------------------------------------------------------------------- */
/* Fonctions publiques                                                       */
/* ------------------------------------------------------------------------- */

void loader_init(void)
{
    (void)ble_ota_init();        /* en cas d'échec : LED rouge clignotante */
    idle_since = HAL_GetTick();
    button_state = BUTTON_WAIT_RELEASE;
    update_leds();
}

void loader_process(void)
{
    /* 1) Bluetooth : événements reçus du module et annonces (ne fait rien
     *    si le module n'a pas pu démarrer).                               */
    ble_ota_process();

    /* 2) Affichage.                                                       */
    update_leds();

    /* 3) Fin de transfert : le PC a demandé le redémarrage (commande RUN). */
    if (ota_take_run_request())
    {
        loader_restart();
    }

    /* 4) Appui court sur USER : retour à l'application, si elle existe.  */
    if (button_short_press() && app_image_is_installed())
    {
        loader_restart();
    }

    /* 5) Inactivité : personne ne se connecte, l'application reprend.
     *    Le délai ne court ni pendant une connexion, ni pendant un transfert. */
    uint32_t now = HAL_GetTick();
    if (ble_ota_is_connected() || (ota_get_state() != OTA_STATE_IDLE))
    {
        idle_since = now;
    }
    else if ((LOADER_IDLE_TIMEOUT_MS > 0U) && app_image_is_installed()
             && ((now - idle_since) >= LOADER_IDLE_TIMEOUT_MS))
    {
        loader_restart();
    }
}
