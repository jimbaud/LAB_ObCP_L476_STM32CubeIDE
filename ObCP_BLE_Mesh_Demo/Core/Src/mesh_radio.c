/**
 ******************************************************************************
 * @file    mesh_radio.c
 * @brief   Radio du réseau maillé : diffusion et écoute avec le module
 *          Bluetooth de l'ObCP, BlueNRG-M0 ou SPBTLE-RF (voir mesh_radio.h
 *          pour le principe et les différences entre les deux modules).
 *
 *  Organisation du fichier :
 *    1. constantes et état ;
 *    2. adresse Bluetooth (reprise du programme modèle OBCP_template_2026) ;
 *    3. démarrage du module ;
 *    4. écoute (procédure d'observation) ;
 *    5. émission (mode « broadcast ») ;
 *    6. évènements du module (rapports de publicité...) ;
 *    7. tâche de fond et accesseurs.
 *
 *  Toutes les fonctions de ce fichier s'exécutent dans la boucle principale :
 *  radio_event_handler() est appelée par hci_user_evt_proc(), elle-même
 *  appelée par mesh_radio_process().
 ******************************************************************************
 */
#include "mesh_radio.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "main.h"                   /* HAL, étiquette BLE_IRQ de PB12 */

/* Bibliothèque BlueNRG-MS (X-CUBE-BLE1) */
#include "hci.h"
#include "hci_le.h"
#include "hci_tl.h"
#include "bluenrg_aci_const.h"
#include "bluenrg_gap.h"
#include "bluenrg_gap_aci.h"
#include "bluenrg_gatt_aci.h"
#include "bluenrg_hal_aci.h"
#include "bluenrg_utils.h"

/* ========================================================================== */
/* 1. Constantes et état                                                      */
/* ========================================================================== */

/* Niveaux de puissance du module (UM1865, table 292) : puissance en dBm,
 * bit « haute puissance », niveau de l'amplificateur (0 à 7).             */
typedef struct
{
  int8_t  dbm;
  uint8_t high_power;
  uint8_t pa_level;
} power_level_t;

static const power_level_t k_power_levels[] =
{
  { -18, 0U, 0U }, { -15, 0U, 1U }, { -11, 0U, 2U }, { -8, 0U, 3U }, { -5, 0U, 4U },
  { -2, 1U, 4U },  /* -2 dBm : même réglage que le programme modèle */
  { 2, 0U, 6U },   { 5, 0U, 7U },   { 8, 1U, 7U }
};
#define POWER_LEVEL_COUNT  (sizeof(k_power_levels) / sizeof(k_power_levels[0]))

/* Nombre d'échecs de aci_gap_set_non_discoverable() avant d'essayer la
 * commande HCI standard d'arrêt de la publicité. */
#define TX_STOP_FALLBACK_AFTER  2U

/* Attente maximale de l'évènement « procédure terminée » après un arrêt de
 * l'écoute demandé par le programme, en ms. */
#define SCAN_STOP_TIMEOUT_MS    300U

/* Mode alterné : après une demande d'émission, l'écoute n'est pas relancée
 * pendant ce temps (ms), pour laisser l'émission partir. */
#define TX_WAIT_HOLD_MS         200U

static bool          s_ready;             /* mesh_radio_init() a réussi */
static bool          s_restart_needed;    /* le module a redémarré seul */
static uint8_t       s_hw_version;        /* circuit BlueNRG-MS (0x31...) */
static uint16_t      s_fw_version;        /* pile Bluetooth (0x0723 = 7.2c...) */
static radio_stack_t s_stack = RADIO_STACK_TOO_OLD;
static uint8_t       s_address[6] = { 0x00, 0x00, 0x00, 0xE1, 0x80, 0x02 };
static uint32_t      s_seed;              /* nombre aléatoire du module */
static int8_t        s_tx_dbm = RADIO_TX_POWER_DEFAULT;
static radio_mode_t  s_mode = RADIO_MODE_ALTERNATE;
static bool          s_mode_forced;
static uint8_t       s_last_refusal;      /* code du dernier refus d'émission */
static uint8_t       s_fallback_code;     /* refus qui a fait quitter le mode simultané */
static bool          s_fallback_notice;   /* ce retour reste à signaler */

/* Écoute */
static bool          s_scanning;          /* procédure d'observation en cours */
static radio_mode_t  s_scan_mode;         /* mode dont l'écoute en cours suit les temps */
static bool          s_scan_stopping;     /* arrêt demandé, évènement attendu */
static uint32_t      s_scan_stop_at;
static uint32_t      s_scan_retry_at;     /* prochain essai de lancement */

/* Émission */
static bool          s_tx_waiting;        /* mode alterné : émission attendue */
static uint32_t      s_tx_wait_at;
static bool          s_tx_active;         /* publicité en cours */
static uint32_t      s_tx_start_at;
static uint32_t      s_tx_stop_retry_at;
static uint8_t       s_tx_stop_failures;

/* Trames reçues, en attente de la boucle principale */
static radio_frame_t s_rxq[RADIO_RX_QUEUE_LEN];
static uint8_t       s_rxq_head;
static uint8_t       s_rxq_count;

static radio_stats_t s_stats;             /* gardés après une remise en service */

/* Vrai si l'instant t est atteint ou dépassé. */
static bool time_reached(uint32_t now, uint32_t t)
{
  return (int32_t)(now - t) >= 0;
}

/* ========================================================================== */
/* 2. Adresse Bluetooth                                                       */
/* ========================================================================== */

/* Vrai si a[] a la forme d'une adresse « statique aléatoire » valable : les
 * 2 bits de poids fort à 1, les 46 autres ni tous à 0 ni tous à 1. */
static bool is_static_random_address(const uint8_t a[6])
{
  bool all_zero = ((a[5] & 0x3FU) == 0x00U);
  bool all_one  = ((a[5] & 0x3FU) == 0x3FU);

  for (int i = 0; i < 5; i++)
  {
    all_zero = all_zero && (a[i] == 0x00U);
    all_one  = all_one  && (a[i] == 0xFFU);
  }
  return ((a[5] & 0xC0U) == 0xC0U) && !all_zero && !all_one;
}

/* Adresse de secours tirée de l'identifiant unique du STM32 (hachage
 * FNV-1a), comme dans le programme modèle. */
static void make_address_from_uid(uint8_t a[6])
{
  const uint32_t uid[3] = { HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2() };
  uint32_t h1 = 2166136261U;
  uint32_t h2 = 2166136261U ^ 0x5A5A5A5AU;

  for (int i = 0; i < 12; i++)
  {
    uint8_t b = (uint8_t)(uid[i / 4] >> (8 * (i % 4)));
    h1 = (h1 ^ b) * 16777619U;
    h2 = (h2 ^ (uint8_t)(b + 0x3CU)) * 16777619U;
  }
  a[0] = (uint8_t)h1;
  a[1] = (uint8_t)(h1 >> 8);
  a[2] = (uint8_t)(h1 >> 16);
  a[3] = (uint8_t)(h1 >> 24);
  a[4] = (uint8_t)h2;
  a[5] = (uint8_t)((h2 >> 8) | 0xC0U);
}

/* Adresse « statique aléatoire » gardée par le module, sinon adresse de
 * secours (même choix que le programme modèle, note DT0070 de ST). */
static void choose_address(void)
{
  uint8_t buf[6];
  uint8_t len = 0U;

  if ((aci_hal_read_config_data(CONFIG_DATA_RANDOM_ADDRESS, sizeof(buf), &len, buf) == BLE_STATUS_SUCCESS)
      && (len == sizeof(buf)) && is_static_random_address(buf))
  {
    memcpy(s_address, buf, sizeof(s_address));
  }
  else
  {
    make_address_from_uid(s_address);
  }
}

/* ========================================================================== */
/* 3. Démarrage du module                                                     */
/* ========================================================================== */

static void radio_event_handler(void *pData);

/* Niveau de puissance le plus proche de dbm. */
static const power_level_t *nearest_power_level(int8_t dbm)
{
  const power_level_t *best = &k_power_levels[0];

  for (uint32_t i = 1U; i < POWER_LEVEL_COUNT; i++)
  {
    int d_best = (int)best->dbm - (int)dbm;
    int d_this = (int)k_power_levels[i].dbm - (int)dbm;
    if ((d_this * d_this) < (d_best * d_best))
    {
      best = &k_power_levels[i];
    }
  }
  return best;
}

/* Remet à zéro l'état de la radio (pas les compteurs). */
static void reset_state(void)
{
  s_ready            = false;
  s_restart_needed   = false;
  s_scanning         = false;
  s_scan_stopping    = false;
  s_tx_waiting       = false;
  s_tx_active        = false;
  s_tx_stop_failures = 0U;
  s_rxq_head         = 0U;
  s_rxq_count        = 0U;
}

/* Mode d'écoute choisi automatiquement : alterné, quelle que soit la pile
 * (le mode simultané se demande avec « radio simultane », voir mesh_radio.h). */
static radio_mode_t auto_mode(void)
{
  return RADIO_MODE_ALTERNATE;
}

/* Redémarre la pile du module et la règle, dans l'ordre imposé par ST :
 * les réglages de configuration (aci_hal_write_config_data) doivent
 * suivre directement le redémarrage, avant toute autre commande, sinon le
 * module les refuse. D'abord le mode 4 (si want_mode4), puis l'adresse,
 * puis GATT et GAP. *mode4_ok dit si le module a accepté le mode 4.     */
static radio_status_t start_stack(bool want_mode4, bool *mode4_ok)
{
  static const uint8_t stack_mode = RADIO_STACK_MODE;
  uint16_t             gap_service_handle;
  uint16_t             dev_name_char_handle;
  uint16_t             appearance_char_handle;

  /* a) Redémarrage logiciel : le module met environ 100 ms à repartir. */
  (void)hci_reset();
  HAL_Delay(100U);

  /* b) Mode 4 : émission possible pendant l'écoute (pile 7.1b et plus). */
  *mode4_ok = want_mode4 && (aci_hal_write_config_data(CONFIG_DATA_MODE_OFFSET, CONFIG_DATA_MODE_LEN,
                                                       &stack_mode) == BLE_STATUS_SUCCESS);

  /* c) Adresse de la carte, écrite comme adresse « publique ». */
  choose_address();
  if (aci_hal_write_config_data(CONFIG_DATA_PUBADDR_OFFSET, CONFIG_DATA_PUBADDR_LEN,
                                s_address) != BLE_STATUS_SUCCESS)
  {
    return RADIO_ERR_ADDRESS;
  }

  /* d) GATT (obligatoire avant GAP), puis GAP avec les rôles « diffuseur »
   *    et « observateur » seulement : pas de connexion dans ce programme.
   *    Si le module refuse cette combinaison, on demande tous les rôles. */
  if (aci_gatt_init() != BLE_STATUS_SUCCESS)
  {
    return RADIO_ERR_GATT_INIT;
  }
  if ((aci_gap_init_IDB05A1(GAP_BROADCASTER_ROLE_IDB05A1 | GAP_OBSERVER_ROLE_IDB05A1, 0U, 8U,
                            &gap_service_handle, &dev_name_char_handle,
                            &appearance_char_handle) != BLE_STATUS_SUCCESS)
      && (aci_gap_init_IDB05A1(GAP_PERIPHERAL_ROLE_IDB05A1 | GAP_BROADCASTER_ROLE_IDB05A1
                               | GAP_CENTRAL_ROLE_IDB05A1 | GAP_OBSERVER_ROLE_IDB05A1, 0U, 8U,
                               &gap_service_handle, &dev_name_char_handle,
                               &appearance_char_handle) != BLE_STATUS_SUCCESS))
  {
    return RADIO_ERR_GAP_INIT;
  }
  return RADIO_OK;
}

/* Voir mesh_radio.h */
radio_status_t mesh_radio_init(void)
{
  uint8_t              random[8];
  const power_level_t *level;
  radio_status_t       status;
  bool                 want_mode4;
  bool                 mode4_ok = false;

  reset_state();

  /* 1) Liaison SPI et impulsion de reset (interruption du module masquée
   *    pendant que hci_init() prépare ses files ; il la réactive).       */
  HAL_NVIC_DisableIRQ(BLE_IRQ_EXTI_IRQn);
  hci_init(radio_event_handler, NULL);

  /* 2) Le module répond-il ? Sa version de pile dit si le mode 4 existe. */
  if (getBlueNRGVersion(&s_hw_version, &s_fw_version) != BLE_STATUS_SUCCESS)
  {
    s_hw_version = 0U;
    s_fw_version = 0U;
    return RADIO_ERR_NO_MODULE;
  }
  want_mode4 = (s_fw_version >= RADIO_STACK_MODE_MIN_FW);

  /* 3) Redémarrage et réglage de la pile ; en cas d'échec avec le mode 4,
   *    second essai sans lui (comportement d'avant le mode 4).           */
  status = start_stack(want_mode4, &mode4_ok);
  if ((status != RADIO_OK) && want_mode4)
  {
    status = start_stack(false, &mode4_ok);
  }
  if (status != RADIO_OK)
  {
    return status;
  }
  if (mode4_ok)
  {
    s_stack = RADIO_STACK_MODE4;
  }
  else
  {
    s_stack = want_mode4 ? RADIO_STACK_MODE4_REFUSED : RADIO_STACK_TOO_OLD;
  }
  if (!s_mode_forced)
  {
    s_mode = auto_mode();
  }

  /* 4) Puissance d'émission (celle choisie avant un éventuel redémarrage). */
  level = nearest_power_level(s_tx_dbm);
  if (aci_hal_set_tx_power_level(level->high_power, level->pa_level) != BLE_STATUS_SUCCESS)
  {
    return RADIO_ERR_TX_POWER;
  }
  s_tx_dbm = level->dbm;

  /* 5) Un vrai nombre aléatoire, tiré par le module (bruit de la radio) :
   *    il initialise le générateur aléatoire du programme (node.c).      */
  if (hci_le_rand(random) == 0)
  {
    s_seed = (uint32_t)random[0] | ((uint32_t)random[1] << 8) | ((uint32_t)random[2] << 16)
             | ((uint32_t)random[3] << 24);
    s_seed ^= ((uint32_t)random[4] << 3) ^ ((uint32_t)random[5] << 11)
              ^ ((uint32_t)random[6] << 19) ^ ((uint32_t)random[7] << 27);
  }

  /* 6) Évènements dus au redémarrage du module : lus et ignorés. Puis
   *    l'écoute démarre (et redémarrera seule si elle s'arrête).          */
  hci_user_evt_proc();
  s_ready         = true;
  s_scan_retry_at = HAL_GetTick();
  return RADIO_OK;
}

/* ========================================================================== */
/* 4. Écoute                                                                  */
/* ========================================================================== */

/* Lance la procédure d'observation : écoute passive (le module n'envoie
 * pas de « scan request »), sans filtre des doublons (chaque publicité
 * entendue est rapportée, même si elle ressemble à la précédente). Les
 * temps dépendent du mode : écoute continue en mode alterné, créneau
 * laissé libre pour la publicité en mode simultané (app_config.h).      */
static bool start_scan(void)
{
  bool     sim      = (s_mode == RADIO_MODE_SIMULTANEOUS);
  uint16_t interval = sim ? RADIO_SIM_SCAN_INTERVAL : RADIO_SCAN_INTERVAL;
  uint16_t window   = sim ? RADIO_SIM_SCAN_WINDOW : RADIO_SCAN_WINDOW;

  if (aci_gap_start_observation_procedure(interval, window, PASSIVE_SCAN, PUBLIC_ADDR, 0x00U)
      == BLE_STATUS_SUCCESS)
  {
    s_scanning  = true;
    s_scan_mode = s_mode;
    s_stats.scan_starts++;
    return true;
  }
  s_stats.scan_errors++;
  return false;
}

/* Arrête l'écoute. Le module confirme par l'évènement « procédure
 * terminée », attendu avant de relancer l'écoute (s_scan_stopping). */
static void stop_scan(void)
{
  if (s_scanning)
  {
    (void)aci_gap_terminate_gap_procedure(GAP_OBSERVATION_PROC_IDB05A1);
    s_scanning      = false;
    s_scan_stopping = true;
    s_scan_stop_at  = HAL_GetTick();
  }
}

/* ========================================================================== */
/* 5. Émission                                                                */
/* ========================================================================== */

/* Lance la diffusion de adv (publicité non connectable, intervalle
 * RADIO_ADV_INTERVAL, adresse « publique » de la carte, pas de liste
 * blanche). */
static tBleStatus start_broadcast(const uint8_t *adv, uint8_t adv_len)
{
  return aci_gap_set_broadcast_mode(RADIO_ADV_INTERVAL, RADIO_ADV_INTERVAL, ADV_NONCONN_IND,
                                    PUBLIC_ADDR, adv_len, adv, 0U, NULL);
}

/* Voir mesh_radio.h */
bool mesh_radio_send(const uint8_t *pdu, uint8_t len)
{
  uint8_t    adv[MESH_ADV_MAX];
  uint8_t    adv_len = mesh_adv_build(pdu, len, adv, sizeof(adv));
  tBleStatus ret;

  if (!s_ready || s_tx_active || (adv_len == 0U))
  {
    return false;
  }
  ret = start_broadcast(adv, adv_len);
  if (ret != BLE_STATUS_SUCCESS)
  {
    s_stats.tx_refused++;
    s_last_refusal = ret;
    if (s_mode == RADIO_MODE_SIMULTANEOUS)
    {
      /* Le module refuse d'émettre pendant l'écoute, quel qu'en soit le code
       * (0x12 quand les temps ne respectent pas les règles du mode 4) :
       * retour au mode alterné, signalé une fois dans le terminal. mesh.c
       * réessaiera l'émission une fois l'écoute arrêtée (mesh_radio_tx_ready). */
      s_mode            = RADIO_MODE_ALTERNATE;
      s_mode_forced     = false;
      s_fallback_code   = ret;
      s_fallback_notice = true;
    }
    return false;
  }
  s_tx_waiting       = false;
  s_tx_active        = true;
  s_tx_start_at      = HAL_GetTick();
  s_tx_stop_retry_at = s_tx_start_at;
  s_tx_stop_failures = 0U;
  s_stats.tx_started++;
  return true;
}

/* Arrête la diffusion. Rend true si c'est fait. */
static bool stop_broadcast(void)
{
  if (aci_gap_set_non_discoverable() == BLE_STATUS_SUCCESS)
  {
    return true;
  }
  s_stats.tx_stop_errors++;
  s_tx_stop_failures++;
  if (s_tx_stop_failures >= TX_STOP_FALLBACK_AFTER)
  {
    /* Commande standard du contrôleur Bluetooth : « LE Set Advertise
     * Enable » à 0. */
    return hci_le_set_advertise_enable(0U) == 0;
  }
  return false;
}

/* ========================================================================== */
/* 6. Évènements du module                                                    */
/* ========================================================================== */

/* Rapport de publicité (évènement LE Advertising Report). p pointe sur les
 * données de l'évènement, après le sous-code :
 *   p[0]       nombre de rapports (1 avec le BlueNRG-MS ; seul le premier
 *              est lu)
 *   p[1]       type de publicité (ADV_NONCONN_IND...)
 *   p[2]       type d'adresse
 *   p[3..8]    adresse de l'émetteur (poids faible en premier)
 *   p[9]       L = longueur des données (31 au plus)
 *   p[10..]    données de la publicité (L octets)
 *   p[10 + L]  RSSI, en dBm (entier signé ; 127 = inconnu)
 * avail = nombre d'octets disponibles à partir de p.                     */
static void on_advertising_report(const uint8_t *p, uint32_t avail)
{
  const uint8_t *adv;
  const uint8_t *pdu;
  uint8_t        pdu_len;
  uint8_t        data_len;
  int8_t         rssi;
  radio_frame_t *slot;

  if ((avail < 11U) || (p[0] == 0U))
  {
    return;
  }
  data_len = p[9];
  if ((data_len > 31U) || (avail < (11U + (uint32_t)data_len)))
  {
    return;                               /* rapport tronqué : ignoré */
  }
  s_stats.adv_reports++;
  adv  = &p[10];
  rssi = (int8_t)adv[data_len];
  if (rssi > 20)
  {
    rssi = 0;                             /* 127 : puissance inconnue */
  }
  if (!mesh_adv_find(adv, data_len, &pdu, &pdu_len) || (pdu_len > MESH_PDU_MAX))
  {
    return;                               /* autre publicité (téléphone, balise...) */
  }
  s_stats.mesh_frames++;
  if (s_rxq_count >= RADIO_RX_QUEUE_LEN)
  {
    s_stats.rx_overflow++;
    return;
  }
  slot = &s_rxq[(s_rxq_head + s_rxq_count) % RADIO_RX_QUEUE_LEN];
  memcpy(slot->pdu, pdu, pdu_len);
  slot->len  = pdu_len;
  slot->rssi = rssi;
  s_rxq_count++;
}

/* Fonction de traitement des évènements, donnée à hci_init(). Un paquet :
 * [type HCI_EVENT_PKT][code d'évènement][longueur][données].              */
static void radio_event_handler(void *pData)
{
  const hci_uart_pckt  *hci_pckt   = (const hci_uart_pckt *)pData;
  const hci_event_pckt *event_pckt = (const hci_event_pckt *)hci_pckt->data;

  if (hci_pckt->type != HCI_EVENT_PKT)
  {
    return;
  }

  switch (event_pckt->evt)
  {
    case EVT_LE_META_EVENT:             /* évènements propres au BLE */
    {
      const evt_le_meta_event *meta = (const evt_le_meta_event *)event_pckt->data;

      if ((meta->subevent == EVT_LE_ADVERTISING_REPORT) && (event_pckt->plen >= 1U))
      {
        on_advertising_report(meta->data, (uint32_t)event_pckt->plen - 1U);
      }
      break;
    }

    case EVT_VENDOR:                    /* évènements propres au BlueNRG */
    {
      const evt_blue_aci *blue_evt = (const evt_blue_aci *)event_pckt->data;

      switch (blue_evt->ecode)
      {
        case EVT_BLUE_GAP_PROCEDURE_COMPLETE:
        {
          const evt_gap_procedure_complete *pc = (const evt_gap_procedure_complete *)blue_evt->data;
          if (pc->procedure_code == GAP_OBSERVATION_PROC_IDB05A1)
          {
            if (s_scan_stopping)
            {
              s_scan_stopping = false;  /* arrêt demandé par nous : confirmé */
            }
            else
            {
              s_scanning      = false;  /* arrêt inattendu : on relancera */
              s_scan_retry_at = HAL_GetTick();
            }
          }
          break;
        }

        case EVT_BLUE_HAL_EVENTS_LOST_IDB05A1:  /* boucle trop lente : évènements perdus */
          s_stats.events_lost++;
          break;

        case EVT_BLUE_HAL_INITIALIZED:          /* le module vient de (re)démarrer */
        {
          const evt_hal_initialized *evt = (const evt_hal_initialized *)blue_evt->data;
          if (s_ready && (evt->reason_code >= RESET_WATCHDOG))
          {
            s_restart_needed = true;            /* chien de garde, plantage... */
          }
          break;
        }

        default:
          break;
      }
      break;
    }

    default:
      break;
  }
}

/* ========================================================================== */
/* 7. Tâche de fond et accesseurs                                             */
/* ========================================================================== */

/* Sécurité reprise du programme modèle : si les 5 tampons de paquets de la
 * bibliothèque étaient pleins quand le module a signalé des données, la
 * broche IRQ reste à 1 sans nouveau front, donc sans interruption. On
 * relance alors la lecture à la main. */
static void read_pending_packets(void)
{
  if (HAL_GPIO_ReadPin(BLE_IRQ_GPIO_Port, BLE_IRQ_Pin) == GPIO_PIN_SET)
  {
    HAL_NVIC_DisableIRQ(BLE_IRQ_EXTI_IRQn);
    hci_tl_lowlevel_isr();
    HAL_NVIC_EnableIRQ(BLE_IRQ_EXTI_IRQn);
    hci_user_evt_proc();
  }
}

/* Voir mesh_radio.h */
void mesh_radio_process(void)
{
  uint32_t now;

  if (!s_ready)
  {
    return;
  }

  /* 1) Évènements du module (appellent radio_event_handler). */
  hci_user_evt_proc();
  read_pending_packets();

  /* 2) Redémarrage inattendu du module : tout refaire. */
  if (s_restart_needed)
  {
    s_stats.module_restarts++;
    (void)mesh_radio_init();
    return;
  }

  now = HAL_GetTick();

  /* 3) Fin d'émission au bout de RADIO_TX_TIME_MS. */
  if (s_tx_active && ((now - s_tx_start_at) >= RADIO_TX_TIME_MS) && time_reached(now, s_tx_stop_retry_at))
  {
    if (stop_broadcast())
    {
      s_tx_active     = false;
      s_scan_retry_at = now;              /* mode alterné : l'écoute reprend */
    }
    else
    {
      s_tx_stop_retry_at = now + RADIO_RETRY_MS;
    }
  }

  /* 4) Écoute à (re)lancer, sauf en mode alterné pendant une émission ou
   *    quand une émission est attendue, et après la confirmation d'un
   *    arrêt demandé.                                                   */
  if (s_scan_stopping && ((now - s_scan_stop_at) >= SCAN_STOP_TIMEOUT_MS))
  {
    s_scan_stopping = false;              /* confirmation jamais venue */
  }
  if (s_tx_waiting && ((now - s_tx_wait_at) >= TX_WAIT_HOLD_MS))
  {
    s_tx_waiting = false;                 /* plus d'émission attendue */
  }
  if (!s_scanning && !s_scan_stopping && time_reached(now, s_scan_retry_at)
      && !((s_mode == RADIO_MODE_ALTERNATE) && (s_tx_active || s_tx_waiting)))
  {
    if (!start_scan())
    {
      s_scan_retry_at = now + RADIO_RETRY_MS;
    }
  }
}

/* Voir mesh_radio.h */
bool mesh_radio_is_ready(void)
{
  return s_ready;
}

/* Voir mesh_radio.h. En mode alterné, la radio n'est prête qu'une fois
 * l'écoute arrêtée et l'arrêt confirmé par le module : la première
 * demande arrête l'écoute, et l'écoute n'est pas relancée tant qu'une
 * émission est attendue (TX_WAIT_HOLD_MS). */
bool mesh_radio_tx_ready(void)
{
  if (!s_ready || s_tx_active)
  {
    return false;
  }
  if (s_mode == RADIO_MODE_SIMULTANEOUS)
  {
    return true;
  }
  s_tx_waiting = true;
  s_tx_wait_at = HAL_GetTick();
  if (s_scanning)
  {
    stop_scan();
    return false;
  }
  return !s_scan_stopping;
}

/* Voir mesh_radio.h */
bool mesh_radio_receive(radio_frame_t *out)
{
  if (s_rxq_count == 0U)
  {
    return false;
  }
  *out       = s_rxq[s_rxq_head];
  s_rxq_head = (uint8_t)((s_rxq_head + 1U) % RADIO_RX_QUEUE_LEN);
  s_rxq_count--;
  return true;
}

/* Voir mesh_radio.h */
bool mesh_radio_set_tx_power(int8_t dbm, int8_t *applied)
{
  const power_level_t *level = nearest_power_level(dbm);

  if (s_ready && (aci_hal_set_tx_power_level(level->high_power, level->pa_level) != BLE_STATUS_SUCCESS))
  {
    return false;
  }
  s_tx_dbm = level->dbm;                  /* gardée aussi pour le prochain démarrage */
  if (applied != NULL)
  {
    *applied = s_tx_dbm;
  }
  return true;
}

/* Voir mesh_radio.h */
int8_t mesh_radio_tx_power(void)
{
  return s_tx_dbm;
}

/* Voir mesh_radio.h */
radio_mode_t mesh_radio_mode(void)
{
  return s_mode;
}

/* Voir mesh_radio.h */
void mesh_radio_set_mode(radio_mode_t mode, bool forced)
{
  s_mode_forced     = forced;
  s_mode            = forced ? mode : auto_mode();
  s_tx_waiting      = false;
  s_fallback_code   = 0U;                 /* nouveau choix : on oublie l'ancien repli */
  s_fallback_notice = false;
  if (s_scanning && (s_scan_mode != s_mode))
  {
    stop_scan();                          /* relancée avec les temps du nouveau mode */
  }
}

/* Voir mesh_radio.h */
bool mesh_radio_mode_forced(void)
{
  return s_mode_forced;
}

/* Voir mesh_radio.h */
uint8_t mesh_radio_last_refusal(void)
{
  return s_last_refusal;
}

/* Voir mesh_radio.h */
uint8_t mesh_radio_fallback_code(void)
{
  return s_fallback_code;
}

/* Voir mesh_radio.h */
bool mesh_radio_take_fallback(uint8_t *code)
{
  if (!s_fallback_notice)
  {
    return false;
  }
  s_fallback_notice = false;
  *code             = s_fallback_code;
  return true;
}

/* Voir mesh_radio.h */
bool mesh_radio_time_base(uint32_t *period, uint32_t *free_slot)
{
  return s_ready && (aci_hal_get_anchor_period(period, free_slot) == BLE_STATUS_SUCCESS);
}

/* Voir mesh_radio.h */
radio_stack_t mesh_radio_stack(void)
{
  return s_stack;
}

/* Voir mesh_radio.h */
uint16_t mesh_radio_fw_version(void)
{
  return s_fw_version;
}

/* Voir mesh_radio.h */
uint8_t mesh_radio_hw_version(void)
{
  return s_hw_version;
}

/* Voir mesh_radio.h */
int mesh_radio_version_text(uint16_t fw_version, char *text, unsigned size)
{
  unsigned major = (unsigned)(fw_version >> 8);
  unsigned minor = (unsigned)((fw_version >> 4) & 0x0FU);
  unsigned patch = (unsigned)(fw_version & 0x0FU);

  if (patch == 0U)
  {
    return snprintf(text, size, "%u.%u", major, minor);
  }
  return snprintf(text, size, "%u.%u%c", major, minor, (char)('a' + (int)patch - 1));
}

/* Voir mesh_radio.h */
bool mesh_radio_is_scanning(void)
{
  return s_scanning;
}

/* Voir mesh_radio.h */
void mesh_radio_get_address(uint8_t address[6])
{
  memcpy(address, s_address, sizeof(s_address));
}

/* Voir mesh_radio.h */
uint32_t mesh_radio_random_seed(void)
{
  return s_seed;
}

/* Voir mesh_radio.h */
const radio_stats_t *mesh_radio_stats(void)
{
  return &s_stats;
}

/* Voir mesh_radio.h */
void mesh_radio_stats_clear(void)
{
  memset(&s_stats, 0, sizeof(s_stats));
}

/* Voir mesh_radio.h */
const char *mesh_radio_status_text(radio_status_t status)
{
  switch (status)
  {
    case RADIO_OK:            return "prêt";
    case RADIO_ERR_NO_MODULE: return "le module Bluetooth ne répond pas";
    case RADIO_ERR_ADDRESS:   return "adresse Bluetooth refusée";
    case RADIO_ERR_GATT_INIT: return "démarrage GATT refusé";
    case RADIO_ERR_GAP_INIT:  return "démarrage GAP refusé";
    case RADIO_ERR_TX_POWER:  return "réglage de la puissance refusé";
    default:                  return "erreur inconnue";
  }
}
