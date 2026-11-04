/**
 ******************************************************************************
 * @file    ota_switch.c
 * @brief   Passage au chargeur Bluetooth (commande « #ota »), voir ota_switch.h.
 ******************************************************************************
 *
 *  Une « machine à états » : la variable s_step dit à quelle étape on en est,
 *  et chaque appel de ota_switch_process() regarde si l'étape est terminée.
 *  C'est la façon habituelle d'écrire une suite d'actions qui prend du temps
 *  sans bloquer la boucle principale : pendant l'attente, l'USB et le
 *  Bluetooth continuent d'être servis (et envoient justement les réponses).
 *
 *      IDLE ──#ota──▶ SENDING ──réponses parties (ou 500 ms)──▶ DISCONNECTING
 *                                                                    │
 *        connexion fermée et message parti (ou 500 ms) : redémarrage │
 *                    dans le chargeur, obcp_ota_enter_loader()  ◀────┘
 ******************************************************************************
 */
#include "ota_switch.h"

#include <stdint.h>

#include "app_config.h"
#include "ble_uart.h"
#include "main.h"                 /* HAL_GetTick() */
#include "obcp_ota_request.h"     /* obcp_ota_enter_loader() */
#include "usb_console.h"

/* Étapes du passage au chargeur. */
typedef enum
{
  OTA_STEP_IDLE = 0,       /* rien de demandé */
  OTA_STEP_SENDING,        /* les réponses partent vers le PC et le téléphone */
  OTA_STEP_DISCONNECTING   /* la connexion Bluetooth se ferme */
} ota_step_t;

static ota_step_t s_step = OTA_STEP_IDLE;
static uint32_t   s_step_start_ms;          /* début de l'étape en cours */

/* Passe à l'étape step et note l'heure, pour mesurer sa durée. */
static void go_to(ota_step_t step)
{
  s_step = step;
  s_step_start_ms = HAL_GetTick();
}

/* Vrai si l'étape en cours dure depuis au moins timeout_ms millisecondes.
 * La soustraction reste juste quand HAL_GetTick() repasse par 0.       */
static bool step_longer_than(uint32_t timeout_ms)
{
  return (HAL_GetTick() - s_step_start_ms) >= timeout_ms;
}

/* Vrai quand plus rien n'attend d'être envoyé, ni au PC ni au téléphone. */
static bool replies_sent(void)
{
  return !usb_console_tx_pending() && !ble_uart_tx_pending();
}

/* Voir ota_switch.h */
void ota_switch_request(void)
{
  if (s_step == OTA_STEP_IDLE)
  {
    go_to(OTA_STEP_SENDING);
  }
}

/* Voir ota_switch.h */
void ota_switch_process(void)
{
  switch (s_step)
  {
    case OTA_STEP_SENDING:
      /* 1. Les réponses sont-elles parties ? (Si le PC ne lit plus le port
       *    COM, elles ne partiront jamais : d'où le délai maximal.)        */
      if (replies_sent() || step_longer_than(OTA_REPLY_TIMEOUT_MS))
      {
        ble_uart_disconnect();          /* sans effet si aucun téléphone n'est là */
        go_to(OTA_STEP_DISCONNECTING);
      }
      break;

    case OTA_STEP_DISCONNECTING:
      /* 2. La connexion Bluetooth est-elle fermée, et le message « BLE
       *    disconnected » qui l'annonce est-il parti vers le PC ?          */
      if ((!ble_uart_is_connected() && replies_sent())
          || step_longer_than(OTA_DISCONNECT_TIMEOUT_MS))
      {
        /* 3. Redémarrage dans le chargeur. Sur la carte, cette fonction ne
         *    revient jamais. Dans les tests sur PC, elle est simulée et
         *    revient : on se remet alors au repos.                        */
        obcp_ota_enter_loader();
        go_to(OTA_STEP_IDLE);
      }
      break;

    case OTA_STEP_IDLE:
    default:
      break;                            /* rien à faire */
  }
}

/* Voir ota_switch.h */
bool ota_switch_is_pending(void)
{
  return s_step != OTA_STEP_IDLE;
}
