/**
 ******************************************************************************
 * @file    ble_uart.c
 * @brief   Liaison Bluetooth « UART » avec le module BlueNRG-M0 (voir
 *          ble_uart.h pour le principe et le service).
 *
 *  Organisation du fichier :
 *    1. constantes et état du module ;
 *    2. démarrage du module (ble_uart_init) et adresse Bluetooth ;
 *    3. publicité (« advertising ») ;
 *    4. évènements envoyés par le module (ble_event_handler) ;
 *    5. réception des commandes, émission des notifications ;
 *    6. tâche de fond (ble_uart_process).
 *
 *  Toutes les fonctions de ce fichier s'exécutent dans la boucle principale
 *  (jamais en interruption) : ble_event_handler() est appelée par
 *  hci_user_evt_proc(), elle-même appelée par ble_uart_process(). Il n'y a
 *  donc pas de conflit d'accès entre elles.
 *
 *  Différences avec BLE_service.c (2024), même service, mêmes messages :
 *    - le texte reçu du téléphone est copié et terminé par '\0' (avant, il
 *      était lu au-delà de sa longueur : « r5 » reçu après « r65535 »
 *      pouvait donner une autre valeur) ;
 *    - les commandes sont traitées dans la boucle principale et non dans la
 *      fonction d'évènement, et l'envoi ne bloque plus (file d'attente) ;
 *    - une connexion qui échoue relance la publicité ; une publicité
 *      refusée est réessayée ; un redémarrage inattendu du module le remet
 *      en service ;
 *    - chaque commande envoyée au module est vérifiée ;
 *    - le nom « OBCP-CROC » est aussi écrit dans la caractéristique
 *      « Device Name » (un téléphone connecté pouvait afficher « BlueNRG »).
 ******************************************************************************
 */
#include "ble_uart.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "fifo.h"
#include "main.h"                   /* HAL, étiquette BLE_IRQ de PB12 */

/* Bibliothèque BlueNRG-MS (X-CUBE-BLE1) */
#include "hci.h"
#include "hci_le.h"
#include "hci_tl.h"
#include "sm.h"
#include "bluenrg_aci_const.h"
#include "bluenrg_gap.h"
#include "bluenrg_gap_aci.h"
#include "bluenrg_gatt_aci.h"
#include "bluenrg_gatt_server.h"
#include "bluenrg_hal_aci.h"
#include "bluenrg_utils.h"

/* ========================================================================== */
/* 1. Constantes et état                                                      */
/* ========================================================================== */

/* UUID de 128 bits, écrits octet de poids faible en premier (ordre attendu
 * par le module) : 6E400001-B5A3-F393-E0A9-E50E24DCCA9E se lit ici de la
 * fin vers le début.                                                      */
static const uint8_t UART_SERVICE_UUID[16] =
  { 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E };
static const uint8_t UART_TX_UUID[16] =        /* ...02 : carte -> téléphone */
  { 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E };
static const uint8_t UART_RX_UUID[16] =        /* ...03 : téléphone -> carte */
  { 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E };

/* Nombre d'« attributs » réservés pour le service : déclaration du service
 * (1) + TX (déclaration, valeur, descripteur d'abonnement CCCD : 3) + RX
 * (déclaration, valeur : 2) = 6 utilisés, 9 réservés comme en 2024.      */
#define UART_SERVICE_MAX_ATTRIBUTES  9U

/* Sécurité : comme en 2024, appairage possible avec le code fixe 123456 si
 * le téléphone le demande ; les caractéristiques n'exigent pas
 * d'appairage (ATTR_PERMISSION_NONE).                                    */
#define BLE_FIXED_PIN               123456U
#define BLE_MIN_KEY_SIZE            7U
#define BLE_MAX_KEY_SIZE            16U

/* Puissance d'émission : mode « haute puissance » (1), niveau 4, soit
 * environ -2 dBm (tableau de aci_hal_set_tx_power_level, UM1865). Comme
 * en 2024.                                                               */
#define BLE_TX_HIGH_POWER           1U
#define BLE_TX_PA_LEVEL             4U

/* Adresse par défaut si le module n'en fournit pas (voir make_address). */
static uint8_t s_address[6] = { 0x00, 0x00, 0x00, 0xE1, 0x80, 0x02 };

/* Repères (« handles ») attribués par le module au service et aux
 * caractéristiques : on s'en sert pour les désigner ensuite.             */
static uint16_t s_service_handle;
static uint16_t s_tx_char_handle;
static uint16_t s_rx_char_handle;

/* État de la liaison. s_conn_handle et s_notify_enabled ne servent pas au
 * programme, mais on peut les observer avec le débogueur (fenêtre « Live
 * Expressions » de STM32CubeIDE).                                        */
static bool     s_ready;            /* ble_uart_init() a réussi */
static bool     s_connected;        /* un téléphone est connecté */
static uint16_t s_conn_handle;      /* repère de la connexion */
static bool     s_notify_enabled;   /* le téléphone s'est abonné aux notifications */
static bool     s_adv_needed;       /* publicité à (re)lancer */
static uint32_t s_adv_retry_at;     /* instant (ms) du prochain essai */
static bool     s_restart_needed;   /* le module a redémarré seul : tout refaire */

/* File des évènements de connexion pour l'application */
#define EVENT_QUEUE_LEN  4U
typedef struct
{
  ble_uart_event_t type;
  uint8_t          peer[6];
} ble_event_item_t;
static ble_event_item_t s_events[EVENT_QUEUE_LEN];
static uint8_t          s_event_head;   /* prochaine case à lire */
static uint8_t          s_event_count;  /* nombre d'évènements en attente */

/* File des messages reçus du téléphone (une case = une écriture) */
typedef struct
{
  uint8_t len;
  char    data[BLE_CHUNK_SIZE + 1U];
} ble_rx_msg_t;
static ble_rx_msg_t   s_rx_queue[BLE_RX_QUEUE_LEN];
static uint8_t        s_rx_head;        /* prochaine case à lire */
static uint8_t        s_rx_count;       /* nombre de messages en attente */
static ble_rx_msg_t   s_rx_msg;         /* message en cours de découpage en lignes */
static uint8_t        s_rx_pos;         /* prochain caractère de s_rx_msg */
static bool           s_rx_active;      /* s_rx_msg en cours de lecture */
static line_builder_t s_line;

/* File d'émission : chaque message est rangé avec sa longueur (2 octets,
 * poids faible en premier) devant lui, pour savoir où il finit.          */
static uint8_t  s_tx_storage[BLE_TX_FIFO_SIZE];
static fifo_t   s_tx = FIFO_INIT(s_tx_storage);
static uint16_t s_tx_left;          /* octets restant à envoyer du message en cours */
static bool     s_tx_blocked;       /* module saturé : attendre avant de réessayer */
static uint32_t s_tx_blocked_at;    /* instant (ms) de la saturation */

/* ========================================================================== */
/* 2. Démarrage du module                                                     */
/* ========================================================================== */

static void ble_event_handler(void *pData);

/* Vrai si a[] a la forme d'une adresse « statique aléatoire » valable : les
 * 2 bits de poids fort à 1, les 46 autres ni tous à 0 ni tous à 1
 * (norme Bluetooth, Core Specification vol. 6, partie B, § 1.3.2.1).     */
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

/* Adresse de secours, fabriquée à partir de l'identifiant unique du STM32
 * (96 bits gravés en usine, différents d'une puce à l'autre) : deux cartes
 * ont ainsi deux adresses différentes. Le « mélange » utilisé est le
 * hachage FNV-1a :
 * https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
 * Les 2 bits de poids fort à 1 lui donnent la forme d'une adresse
 * statique aléatoire.                                                    */
static void make_address_from_uid(uint8_t a[6])
{
  const uint32_t uid[3] = { HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2() };
  uint32_t h1 = 2166136261U;          /* valeurs de départ de FNV-1a */
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

/* Choisit l'adresse de la carte, comme en 2024 : l'adresse « statique
 * aléatoire » que le module a tirée au sort et gardée dans sa mémoire
 * (lue à l'emplacement CONFIG_DATA_RANDOM_ADDRESS). Elle est différente
 * d'un module à l'autre et ne change pas d'un démarrage à l'autre.
 * Sur un module neuf, elle n'existe qu'après le premier aci_gap_init()
 * (note DT0070 de ST) : on prend alors l'adresse de secours.             */
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

/* Crée le service UART et ses deux caractéristiques (mêmes réglages qu'en
 * 2024, pour que les téléphones retrouvent les mêmes repères).           */
static tBleStatus add_uart_service(void)
{
  tBleStatus ret;

  ret = aci_gatt_add_serv(UUID_TYPE_128, UART_SERVICE_UUID, PRIMARY_SERVICE,
                          UART_SERVICE_MAX_ATTRIBUTES, &s_service_handle);
  if (ret != BLE_STATUS_SUCCESS)
  {
    return ret;
  }

  /* TX : la carte y écrit, le téléphone la lit ou s'y abonne (notification).
   * Longueur variable, 20 octets au plus.                                 */
  ret = aci_gatt_add_char(s_service_handle, UUID_TYPE_128, UART_TX_UUID, BLE_CHUNK_SIZE,
                          CHAR_PROP_NOTIFY | CHAR_PROP_READ, ATTR_PERMISSION_NONE,
                          GATT_DONT_NOTIFY_EVENTS, BLE_MAX_KEY_SIZE, 1U, &s_tx_char_handle);
  if (ret != BLE_STATUS_SUCCESS)
  {
    return ret;
  }

  /* RX : le téléphone y écrit ses commandes (avec ou sans accusé de
   * réception) ; GATT_NOTIFY_ATTRIBUTE_WRITE demande au module de nous
   * prévenir à chaque écriture (évènement « attribut modifié »).          */
  return aci_gatt_add_char(s_service_handle, UUID_TYPE_128, UART_RX_UUID, BLE_CHUNK_SIZE,
                           CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RESP, ATTR_PERMISSION_NONE,
                           GATT_NOTIFY_ATTRIBUTE_WRITE, BLE_MAX_KEY_SIZE, 1U, &s_rx_char_handle);
}

/* Remet à zéro l'état de la liaison et les files. */
static void reset_link_state(void)
{
  s_connected      = false;
  s_conn_handle    = 0U;
  s_notify_enabled = false;
  s_adv_needed     = false;
  s_restart_needed = false;
  s_event_head     = 0U;
  s_event_count    = 0U;
  s_rx_head        = 0U;
  s_rx_count       = 0U;
  s_rx_active      = false;
  line_builder_reset(&s_line);
  fifo_clear(&s_tx);
  s_tx_left        = 0U;
  s_tx_blocked     = false;
}

/* Voir ble_uart.h */
ble_uart_status_t ble_uart_init(void)
{
  uint8_t  hw_version;
  uint16_t fw_version;
  uint16_t gap_service_handle;
  uint16_t dev_name_char_handle;
  uint16_t appearance_char_handle;
  uint8_t  name_len = (uint8_t)strlen(BLE_DEVICE_NAME);

  s_ready = false;
  reset_link_state();

  /* 1) Liaison SPI avec le module et impulsion sur sa broche de reset.
   *    ble_event_handler() recevra les évènements du module.
   *    L'interruption du module est masquée pendant que hci_init() prépare
   *    ses files de paquets ; hci_init() la réactive ensuite (fonction
   *    hci_tl_lowlevel_init, dans hci_tl_interface.c).                    */
  HAL_NVIC_DisableIRQ(BLE_IRQ_EXTI_IRQn);
  hci_init(ble_event_handler, NULL);

  /* 2) Le module répond-il ? (lecture de ses numéros de version) */
  if (getBlueNRGVersion(&hw_version, &fw_version) != BLE_STATUS_SUCCESS)
  {
    return BLE_UART_ERR_NO_MODULE;
  }

  /* 3) Redémarrage logiciel : l'adresse doit être écrite avant tout
   *    réglage GAP/GATT. Le module met environ 100 ms à repartir.        */
  (void)hci_reset();
  HAL_Delay(100U);

  /* 4) Adresse de la carte (voir choose_address), écrite comme adresse
   *    « publique » (même choix qu'en 2024).                              */
  choose_address();
  if (aci_hal_write_config_data(CONFIG_DATA_PUBADDR_OFFSET, CONFIG_DATA_PUBADDR_LEN,
                                s_address) != BLE_STATUS_SUCCESS)
  {
    return BLE_UART_ERR_ADDRESS;
  }

  /* 5) Couches GATT (base des services) et GAP (rôle, nom). */
  if (aci_gatt_init() != BLE_STATUS_SUCCESS)
  {
    return BLE_UART_ERR_GATT_INIT;
  }
  if (aci_gap_init_IDB05A1(GAP_PERIPHERAL_ROLE_IDB05A1, 0U, name_len, &gap_service_handle,
                           &dev_name_char_handle, &appearance_char_handle) != BLE_STATUS_SUCCESS)
  {
    return BLE_UART_ERR_GAP_INIT;
  }
  if (aci_gatt_update_char_value(gap_service_handle, dev_name_char_handle, 0U, name_len,
                                 (const uint8_t *)BLE_DEVICE_NAME) != BLE_STATUS_SUCCESS)
  {
    return BLE_UART_ERR_DEVICE_NAME;
  }

  /* 6) Sécurité (identique à 2024). */
  if (aci_gap_set_auth_requirement(MITM_PROTECTION_REQUIRED, OOB_AUTH_DATA_ABSENT, NULL,
                                   BLE_MIN_KEY_SIZE, BLE_MAX_KEY_SIZE,
                                   USE_FIXED_PIN_FOR_PAIRING, BLE_FIXED_PIN,
                                   BONDING) != BLE_STATUS_SUCCESS)
  {
    return BLE_UART_ERR_SECURITY;
  }

  /* 7) Service UART et puissance d'émission. */
  if (add_uart_service() != BLE_STATUS_SUCCESS)
  {
    return BLE_UART_ERR_SERVICE;
  }
  if (aci_hal_set_tx_power_level(BLE_TX_HIGH_POWER, BLE_TX_PA_LEVEL) != BLE_STATUS_SUCCESS)
  {
    return BLE_UART_ERR_TX_POWER;
  }

  /* 8) Prêt : la publicité démarre au premier ble_uart_process(). Les
   *    évènements dus au redémarrage du module (étape 3) sont lus et
   *    ignorés tout de suite.                                             */
  hci_user_evt_proc();
  s_ready        = true;
  s_adv_needed   = true;
  s_adv_retry_at = HAL_GetTick();
  return BLE_UART_OK;
}

/* ========================================================================== */
/* 3. Publicité (« advertising »)                                             */
/* ========================================================================== */

/* Rend la carte visible et connectable, sous le nom BLE_DEVICE_NAME. Les
 * intervalles de publicité ADV_INTERV_MIN et ADV_INTERV_MAX (1,28 à 2,56 s)
 * sont réglés dans CubeMX (BlueNRG-MS/Target/bluenrg_conf.h).            */
static tBleStatus start_advertising(void)
{
  char    local_name[1U + sizeof(BLE_DEVICE_NAME)];
  uint8_t name_len = (uint8_t)strlen(BLE_DEVICE_NAME);

  local_name[0] = AD_TYPE_COMPLETE_LOCAL_NAME;      /* type du champ : nom complet */
  memcpy(&local_name[1], BLE_DEVICE_NAME, name_len);

  (void)hci_le_set_scan_resp_data(0U, NULL);         /* pas de réponse au scan */
  return aci_gap_set_discoverable(ADV_DATA_TYPE, ADV_INTERV_MIN, ADV_INTERV_MAX, PUBLIC_ADDR,
                                  NO_WHITE_LIST_USE, (uint8_t)(name_len + 1U), local_name,
                                  0U, NULL, 0U, 0U);
}

/* ========================================================================== */
/* 4. Évènements envoyés par le module                                        */
/* ========================================================================== */

/* Ajoute un évènement de connexion pour l'application (perdu si la file,
 * de 4 places, est pleine : cela n'arrive pas en usage normal).           */
static void push_event(ble_uart_event_t type, const uint8_t *peer)
{
  ble_event_item_t *item;

  if (s_event_count >= EVENT_QUEUE_LEN)
  {
    return;
  }
  item = &s_events[(s_event_head + s_event_count) % EVENT_QUEUE_LEN];
  item->type = type;
  if (peer != NULL)
  {
    memcpy(item->peer, peer, 6U);
  }
  else
  {
    memset(item->peer, 0, 6U);
  }
  s_event_count++;
}

/* Le téléphone a écrit dans une caractéristique (ou dans un descripteur).
 * handle = repère de ce qui a été écrit :
 *   - valeur de RX (repère de la caractéristique + 1) : une commande ;
 *   - descripteur CCCD de TX (repère + 2) : abonnement aux notifications. */
static void on_attribute_modified(uint16_t handle, const uint8_t *data, uint8_t len)
{
  if (handle == (uint16_t)(s_rx_char_handle + 1U))
  {
    ble_rx_msg_t *msg;

    if (s_rx_count >= BLE_RX_QUEUE_LEN)
    {
      return;                           /* file pleine : message perdu */
    }
    if (len > BLE_CHUNK_SIZE)
    {
      len = BLE_CHUNK_SIZE;
    }
    msg = &s_rx_queue[(s_rx_head + s_rx_count) % BLE_RX_QUEUE_LEN];
    memcpy(msg->data, data, len);
    msg->data[len] = '\0';              /* la version 2024 oubliait ce '\0' */
    msg->len = len;
    s_rx_count++;
  }
  else if (handle == (uint16_t)(s_tx_char_handle + 2U))
  {
    s_notify_enabled = (len > 0U) && ((data[0] & 0x01U) != 0U);
  }
}

/* Fonction de traitement des évènements, donnée à hci_init() : la
 * bibliothèque l'appelle pour chaque paquet reçu du module, depuis
 * hci_user_evt_proc(). Un paquet commence par son type (évènement HCI),
 * puis son code d'évènement et ses données.                               */
static void ble_event_handler(void *pData)
{
  const hci_uart_pckt  *hci_pckt   = (const hci_uart_pckt *)pData;
  const hci_event_pckt *event_pckt = (const hci_event_pckt *)hci_pckt->data;

  if (hci_pckt->type != HCI_EVENT_PKT)
  {
    return;
  }

  switch (event_pckt->evt)
  {
    case EVT_DISCONN_COMPLETE:          /* fin de connexion */
      if (s_connected)
      {
        push_event(BLE_EVENT_DISCONNECTED, NULL);
      }
      s_connected      = false;
      s_conn_handle    = 0U;
      s_notify_enabled = false;
      fifo_clear(&s_tx);                /* plus personne pour recevoir */
      s_tx_left        = 0U;
      s_tx_blocked     = false;
      s_adv_needed     = true;          /* redevenir visible */
      s_adv_retry_at   = HAL_GetTick();
      break;

    case EVT_LE_META_EVENT:             /* évènements propres au BLE */
    {
      const evt_le_meta_event *meta = (const evt_le_meta_event *)event_pckt->data;

      if (meta->subevent == EVT_LE_CONN_COMPLETE)
      {
        const evt_le_connection_complete *cc = (const evt_le_connection_complete *)meta->data;

        if (cc->status == 0U)           /* connexion réussie */
        {
          s_connected   = true;
          s_conn_handle = cc->handle;
          push_event(BLE_EVENT_CONNECTED, cc->peer_bdaddr);
        }
        else                            /* échec : la publicité s'est arrêtée */
        {
          s_adv_needed   = true;
          s_adv_retry_at = HAL_GetTick();
        }
      }
      break;
    }

    case EVT_VENDOR:                    /* évènements propres au BlueNRG */
    {
      const evt_blue_aci *blue_evt = (const evt_blue_aci *)event_pckt->data;

      switch (blue_evt->ecode)
      {
        case EVT_BLUE_GATT_ATTRIBUTE_MODIFIED:
        {
          const evt_gatt_attr_modified_IDB05A1 *evt =
            (const evt_gatt_attr_modified_IDB05A1 *)blue_evt->data;
          on_attribute_modified(evt->attr_handle, evt->att_data, evt->data_length);
          break;
        }

        case EVT_BLUE_GATT_TX_POOL_AVAILABLE:   /* tampons libres : on peut renvoyer */
          s_tx_blocked = false;
          break;

        case EVT_BLUE_HAL_INITIALIZED:          /* le module vient de (re)démarrer */
        {
          const evt_hal_initialized *evt = (const evt_hal_initialized *)blue_evt->data;
          /* Redémarrage normal : celui demandé par ble_uart_init(). Les
           * autres causes (chien de garde, plantage, chute de tension)
           * effacent le service : il faut tout refaire.                  */
          if (s_ready && (evt->reason_code >= RESET_WATCHDOG))
          {
            s_restart_needed = true;
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
/* 5. Réception et émission                                                   */
/* ========================================================================== */

/* Copie la ligne assemblée dans la destination de l'appelant. */
static void copy_line(char *line, size_t size, line_status_t status)
{
  if (size == 0U)
  {
    return;
  }
  if (status == LINE_READY)
  {
    (void)strncpy(line, s_line.text, size - 1U);
    line[size - 1U] = '\0';
  }
  else
  {
    line[0] = '\0';
  }
}

/* Voir ble_uart.h */
line_status_t ble_uart_read_line(char *line, size_t size)
{
  line_status_t status;

  for (;;)
  {
    if (!s_rx_active)
    {
      if (s_rx_count == 0U)
      {
        return LINE_NONE;               /* rien reçu */
      }
      s_rx_msg   = s_rx_queue[s_rx_head];   /* message suivant de la file */
      s_rx_head  = (uint8_t)((s_rx_head + 1U) % BLE_RX_QUEUE_LEN);
      s_rx_count--;
      s_rx_pos    = 0U;
      s_rx_active = true;
      line_builder_reset(&s_line);
    }

    while (s_rx_pos < s_rx_msg.len)
    {
      status = line_builder_put(&s_line, s_rx_msg.data[s_rx_pos]);
      s_rx_pos++;
      if (status != LINE_NONE)
      {
        copy_line(line, size, status);
        return status;
      }
    }

    /* Fin de l'écriture du téléphone = fin de la ligne. */
    s_rx_active = false;
    status = line_builder_end(&s_line);
    if (status != LINE_NONE)
    {
      copy_line(line, size, status);
      return status;
    }
  }
}

/* Voir ble_uart.h */
bool ble_uart_send(const uint8_t *data, uint16_t len)
{
  uint8_t header[2];

  if (len == 0U)
  {
    return true;
  }
  if (!s_connected || (fifo_free(&s_tx) < (uint16_t)(len + 2U)))
  {
    return false;                       /* pas de téléphone, ou tampon plein */
  }
  header[0] = (uint8_t)len;             /* longueur, poids faible */
  header[1] = (uint8_t)(len >> 8);      /* longueur, poids fort */
  (void)fifo_write(&s_tx, header, 2U);
  (void)fifo_write(&s_tx, data, len);
  return true;
}

/* Voir ble_uart.h */
bool ble_uart_print(const char *text)
{
  size_t len = strlen(text);

  if (len > 0xFFFFU)
  {
    return false;
  }
  return ble_uart_send((const uint8_t *)text, (uint16_t)len);
}

/* Voir ble_uart.h */
bool ble_uart_printf(const char *format, ...)
{
  char    text[128];
  va_list args;
  int     n;

  va_start(args, format);
  n = vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  if (n < 0)
  {
    return false;
  }
  return ble_uart_print(text);
}

/* Envoie au plus BLE_CHUNKS_PER_LOOP notifications de 20 octets au plus.
 * Si le module répond qu'il n'a plus de tampon libre (le téléphone ne
 * suit pas), on attend qu'il en libère un avant de réessayer : rien n'est
 * perdu (la version 2024 abandonnait le morceau au bout de 10 ms).       */
static void send_pending_notifications(void)
{
  uint8_t chunk[BLE_CHUNK_SIZE];
  uint8_t header[2];

  for (uint32_t i = 0U; i < BLE_CHUNKS_PER_LOOP; i++)
  {
    uint16_t   n;                       /* taille du morceau envoyé */
    tBleStatus ret;                     /* réponse du module */

    if (s_tx_blocked)
    {
      if ((HAL_GetTick() - s_tx_blocked_at) < BLE_TX_RETRY_MS)
      {
        return;                         /* encore saturé : on attend */
      }
      s_tx_blocked = false;             /* délai écoulé : nouvel essai */
    }

    if (s_tx_left == 0U)                /* début du message suivant */
    {
      if (fifo_peek(&s_tx, 0U, header, 2U) < 2U)
      {
        return;                         /* plus rien à envoyer */
      }
      fifo_drop(&s_tx, 2U);
      s_tx_left = (uint16_t)(header[0] | ((uint16_t)header[1] << 8));
    }

    n = (s_tx_left < BLE_CHUNK_SIZE) ? s_tx_left : (uint16_t)BLE_CHUNK_SIZE;
    (void)fifo_peek(&s_tx, 0U, chunk, n);

    /* Nouvelle valeur de TX : le module l'envoie en notification si le
     * téléphone s'y est abonné.                                        */
    ret = aci_gatt_update_char_value(s_service_handle, s_tx_char_handle, 0U, (uint8_t)n, chunk);
    if (ret == BLE_STATUS_SUCCESS)
    {
      fifo_drop(&s_tx, n);
      s_tx_left = (uint16_t)(s_tx_left - n);
    }
    else if (ret == BLE_STATUS_INSUFFICIENT_RESOURCES)
    {
      s_tx_blocked    = true;           /* tampons du module pleins */
      s_tx_blocked_at = HAL_GetTick();
      return;
    }
    else
    {
      fifo_drop(&s_tx, s_tx_left);      /* autre erreur : message abandonné */
      s_tx_left = 0U;
    }
  }
}

/* ========================================================================== */
/* 6. Tâche de fond                                                           */
/* ========================================================================== */

/* Sécurité : la bibliothèque n'a que 5 tampons pour les paquets du module.
 * S'ils sont tous pleins quand le module signale de nouvelles données,
 * l'interruption n'en lit pas plus ; or la broche IRQ, restée à 1, ne
 * produira plus de front montant, donc plus d'interruption : les échanges
 * s'arrêteraient. Si IRQ est à 1 ici, on relance la lecture à la main
 * (interruption EXTI15_10 masquée pendant ce temps, pour ne pas lire en
 * même temps qu'elle).                                                   */
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

/* Voir ble_uart.h */
void ble_uart_process(void)
{
  if (!s_ready)
  {
    return;                             /* module absent ou en erreur */
  }

  /* 1) Évènements reçus du module (appellent ble_event_handler). */
  hci_user_evt_proc();
  read_pending_packets();

  /* 2) Le module a redémarré seul : on refait toute l'initialisation. */
  if (s_restart_needed)
  {
    bool was_connected = s_connected;

    if (ble_uart_init() != BLE_UART_OK)
    {
      return;                           /* s_ready est faux : BLE abandonné */
    }
    if (was_connected)
    {
      push_event(BLE_EVENT_DISCONNECTED, NULL);
    }
  }

  /* 3) Publicité à (re)lancer : après le démarrage ou une déconnexion,
   *    avec un nouvel essai toutes les BLE_ADV_RETRY_MS si le module
   *    refuse (la version 2024 n'essayait qu'une fois).                   */
  if (!s_connected && s_adv_needed && ((int32_t)(HAL_GetTick() - s_adv_retry_at) >= 0))
  {
    if (start_advertising() == BLE_STATUS_SUCCESS)
    {
      s_adv_needed = false;
    }
    else
    {
      s_adv_retry_at = HAL_GetTick() + BLE_ADV_RETRY_MS;
    }
  }

  /* 4) Notifications en attente. */
  if (s_connected)
  {
    send_pending_notifications();
  }
}

/* ========================================================================== */
/* Accesseurs                                                                 */
/* ========================================================================== */

/* Voir ble_uart.h */
bool ble_uart_is_ready(void)
{
  return s_ready;
}

/* Voir ble_uart.h */
bool ble_uart_is_connected(void)
{
  return s_connected;
}

/* Voir ble_uart.h */
ble_uart_event_t ble_uart_get_event(uint8_t peer[6])
{
  const ble_event_item_t *item;

  if (s_event_count == 0U)
  {
    return BLE_EVENT_NONE;
  }
  item = &s_events[s_event_head];
  s_event_head = (uint8_t)((s_event_head + 1U) % EVENT_QUEUE_LEN);
  s_event_count--;
  if (peer != NULL)
  {
    memcpy(peer, item->peer, 6U);
  }
  return item->type;
}

/* Voir ble_uart.h */
void ble_uart_get_address(uint8_t address[6])
{
  memcpy(address, s_address, sizeof(s_address));
}
