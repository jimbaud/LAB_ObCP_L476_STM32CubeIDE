/**
 ******************************************************************************
 * @file    Tests/fakes/fake_bluenrg.c
 * @brief   Remplaçant PC de la bibliothèque BlueNRG-MS et du module
 *          Bluetooth de l'ObCP (BlueNRG-M0 ou SPBTLE-RF, même puce
 *          BlueNRG-MS), pour le programme de démonstration du réseau
 *          maillé : chaque commande ACI est enregistrée ; chaque évènement
 *          injecté est rendu au programme par hci_user_evt_proc(), dans des
 *          tampons réutilisés sans être effacés (comme les 5 tampons de la
 *          vraie bibliothèque).
 *
 *  Règles du vrai module reproduites (UM1865, et réponses de ST) :
 *    - après un redémarrage (hci_init, hci_reset), le module est dans le
 *      mode de pile 2 ; aci_hal_write_config_data n'est accepté qu'avant
 *      toute autre commande (les lectures de configuration ne comptent pas) ;
 *    - le mode 4 n'existe qu'à partir de la pile 7.1b (0x0712) ;
 *    - émettre pendant l'écoute (et l'inverse) n'est possible qu'en mode 4 ;
 *    - en mode 4, écoute et publicité partagent une base de temps dont la
 *      période est l'intervalle d'écoute (PM0237, § 4.2) : une publicité
 *      n'est acceptée pendant l'écoute que si son intervalle plus 5 ms est
 *      un multiple de cette période, et si l'écoute laisse libre un créneau
 *      de 14,6 ms plus les marges entre créneaux ; sinon code 0x12. Règle
 *      ajoutée après l'essai sur carte de la version 1.2.0, refusée ainsi.
 *  La version de la pile se règle (fake_ble_fw_version) : 0x0723 (7.2c,
 *  BlueNRG-M0) par défaut, 0x0715 (7.1e) pour un SPBTLE-RF.
 ******************************************************************************
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_bluenrg.h"
#include "fake_board.h"

#include "hci.h"
#include "hci_le.h"
#include "hci_tl.h"
#include "bluenrg_aci_const.h"
#include "bluenrg_gap.h"
#include "bluenrg_gap_aci.h"
#include "bluenrg_gatt_aci.h"
#include "bluenrg_hal_aci.h"
#include "bluenrg_utils.h"

/* ---- Réglages et relevés -------------------------------------------------- */
bool     fake_ble_present = true;
bool     fake_ble_concurrent_ok = true;
bool     fake_ble_strict_roles;
int      fake_ble_broadcast_fail_next;
uint8_t  fake_ble_broadcast_fail_status = ERR_COMMAND_DISALLOWED;
int      fake_ble_scan_fail_next;
uint8_t  fake_ble_random_addr[6] = { 0xAB, 0x89, 0x67, 0x45, 0x23, 0xC1 };
uint16_t fake_ble_fw_version = 0x0723U;
uint8_t  fake_ble_hw_version = 0x31U;
bool     fake_ble_mode4_refused;
bool     fake_ble_pubaddr_fail_after_mode;

uint8_t  fake_ble_stack_mode = 2U;
int      fake_ble_mode_writes;
int      fake_ble_late_config_writes;
int      fake_ble_init_count;
uint8_t  fake_ble_public_addr[6];
uint8_t  fake_ble_gap_roles;
uint8_t  fake_ble_tx_high, fake_ble_tx_pa;
bool     fake_ble_observing;
int      fake_ble_observe_starts;
uint16_t fake_ble_scan_interval, fake_ble_scan_window;
uint8_t  fake_ble_scan_type, fake_ble_scan_filter_dup;
bool     fake_ble_broadcasting;
int      fake_ble_broadcast_starts;
int      fake_ble_broadcast_stops;
uint16_t fake_ble_adv_interval;
uint8_t  fake_ble_adv_type;
int      fake_ble_rand_calls;
int      fake_ble_time_base_refusals;
int      fake_ble_reports_while_deaf;

int          fake_ble_sent_count;
mesh_frame_t fake_ble_sent[FAKE_BLE_MAX_SENT];
bool         fake_ble_sent_during_scan[FAKE_BLE_MAX_SENT];

static void (*s_user_cb)(void *);

/* File des évènements à rendre au programme */
#define EVT_QUEUE 64
static uint8_t s_evt[EVT_QUEUE][128];
static size_t  s_evt_len[EVT_QUEUE];
static size_t  s_evt_head, s_evt_count;

/* Tampons de paquets réutilisés (5, comme HCI_READ_PACKET_NUM_MAX) */
static uint8_t s_pkt[5][128];
static int     s_pkt_next;

#define CHECK_PRESENT()  do { if (!fake_ble_present) { return BLE_STATUS_TIMEOUT; } } while (0)

/* Commandes autres que la configuration depuis le dernier redémarrage, et
 * mode 4 écrit depuis ce redémarrage. */
static int  s_cmds_since_reset;
static bool s_mode_written;
#define OTHER_COMMAND()  (s_cmds_since_reset++)

/* Redémarrage de la pile : mode 2, configuration de nouveau permise. */
static void stack_reset(void)
{
  fake_ble_stack_mode   = 2U;
  s_cmds_since_reset    = 0;
  s_mode_written        = false;
  fake_ble_observing    = false;
  fake_ble_broadcasting = false;
}

/* Émission et écoute en même temps : seulement en mode 4. */
static bool concurrency_allowed(void)
{
  return fake_ble_concurrent_ok && (fake_ble_stack_mode == 4U);
}

/* Base de temps du mode 4 (PM0237, § 4.2.3), en pas de 0,625 ms : créneau
 * d'un évènement de publicité (14,6 ms) et marge entre deux créneaux. */
#define ADV_SLOT_STEPS   24U
#define GUARD_STEPS      2U

/* Voir fake_bluenrg.h : intervalle réel de la publicité (moyenne, plus
 * 5 ms = 8 pas) multiple de la période, créneau libre suffisant. */
bool fake_ble_time_base_fits(uint16_t scan_interval, uint16_t scan_window, uint16_t adv_min, uint16_t adv_max)
{
  uint32_t period = scan_interval;
  uint32_t adv    = (((uint32_t)adv_min + (uint32_t)adv_max) / 2U) + 8U;

  return (period > 0U) && ((adv % period) == 0U)
         && (((uint32_t)scan_window + ADV_SLOT_STEPS + (2U * GUARD_STEPS)) <= period);
}

/* Vrai si la publicité trouve sa place dans la base de temps de l'écoute en cours. */
static bool adv_fits_time_base(uint16_t adv_min, uint16_t adv_max)
{
  return fake_ble_time_base_fits(fake_ble_scan_interval, fake_ble_scan_window, adv_min, adv_max);
}

void fake_ble_clear_sent(void)
{
  fake_ble_sent_count = 0;
}

static void push_event(const uint8_t *bytes, size_t len)
{
  if (s_evt_count >= EVT_QUEUE)
  {
    return;
  }
  memcpy(s_evt[(s_evt_head + s_evt_count) % EVT_QUEUE], bytes, len);
  s_evt_len[(s_evt_head + s_evt_count) % EVT_QUEUE] = len;
  s_evt_count++;
}

size_t fake_ble_pending_events(void)
{
  return s_evt_count;
}

/* ---- Couche de transport HCI ---------------------------------------------- */
void hci_init(void (*UserEvtRx)(void *pData), void *pConf)
{
  (void)pConf;
  s_user_cb = UserEvtRx;
  fake_ble_init_count++;
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);    /* comme hci_tl_lowlevel_init() */
  stack_reset();                         /* impulsion sur la broche de reset */
  s_evt_head  = 0U;
  s_evt_count = 0U;
}

void hci_user_evt_proc(void)
{
  while (s_evt_count > 0U)
  {
    uint8_t *pkt = s_pkt[s_pkt_next];
    size_t   len = s_evt_len[s_evt_head];

    s_pkt_next = (s_pkt_next + 1) % 5;
    memcpy(pkt, s_evt[s_evt_head], len);  /* la suite du tampon garde ses anciens octets */
    s_evt_head = (s_evt_head + 1U) % EVT_QUEUE;
    s_evt_count--;
    if (s_user_cb != NULL)
    {
      s_user_cb(pkt);
    }
  }
}

void hci_tl_lowlevel_isr(void)
{
  fake_gpio_set_input(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);   /* données lues */
}

int hci_reset(void)
{
  if (!fake_ble_present)
  {
    return -1;
  }
  stack_reset();
  return 0;
}

int hci_le_rand(uint8_t random_number[8])
{
  fake_ble_rand_calls++;
  if (!fake_ble_present)
  {
    return BLE_STATUS_TIMEOUT;
  }
  OTHER_COMMAND();
  for (int i = 0; i < 8; i++)
  {
    random_number[i] = (uint8_t)(0x5AU + (17 * i));
  }
  return 0;
}

int hci_le_set_advertise_enable(uint8_t enable)
{
  OTHER_COMMAND();
  if (!enable)
  {
    fake_ble_broadcasting = false;
  }
  return 0;
}

uint8_t getBlueNRGVersion(uint8_t *hwVersion, uint16_t *fwVersion)
{
  CHECK_PRESENT();
  OTHER_COMMAND();                       /* commande HCI : d'où le hci_reset qui suit */
  *hwVersion = fake_ble_hw_version;
  *fwVersion = fake_ble_fw_version;
  return BLE_STATUS_SUCCESS;
}

/* ---- Commandes HAL --------------------------------------------------------- */
tBleStatus aci_hal_read_config_data(uint8_t offset, uint16_t data_len, uint8_t *data_len_out_p, uint8_t *data)
{
  CHECK_PRESENT();
  if ((offset == CONFIG_DATA_RANDOM_ADDRESS) && (data_len >= 6U))
  {
    memcpy(data, fake_ble_random_addr, 6);
    *data_len_out_p = 6U;
    return BLE_STATUS_SUCCESS;
  }
  return BLE_STATUS_FAILED;
}

tBleStatus aci_hal_write_config_data(uint8_t offset, uint8_t len, const uint8_t *val)
{
  CHECK_PRESENT();
  if (s_cmds_since_reset > 0)
  {
    fake_ble_late_config_writes++;       /* une autre commande est passée avant */
    return ERR_COMMAND_DISALLOWED;
  }
  if ((offset == CONFIG_DATA_MODE_OFFSET) && (len == CONFIG_DATA_MODE_LEN))
  {
    fake_ble_mode_writes++;
    if ((val[0] < 1U) || (val[0] > 4U)
        || ((val[0] == 4U) && ((fake_ble_fw_version < 0x0712U) || fake_ble_mode4_refused)))
    {
      return ERR_INVALID_HCI_CMD_PARAMS; /* mode 4 : pile 7.1b ou plus récente */
    }
    fake_ble_stack_mode = val[0];
    s_mode_written      = true;
    return BLE_STATUS_SUCCESS;
  }
  if ((offset == CONFIG_DATA_PUBADDR_OFFSET) && (len == 6U))
  {
    if (fake_ble_pubaddr_fail_after_mode && s_mode_written)
    {
      fake_ble_pubaddr_fail_after_mode = false;   /* une seule fois */
      return BLE_STATUS_FAILED;
    }
    memcpy(fake_ble_public_addr, val, 6);
  }
  return BLE_STATUS_SUCCESS;
}

tBleStatus aci_hal_get_anchor_period(uint32_t *anchor_period, uint32_t *max_free_slot)
{
  uint32_t period = fake_ble_observing ? fake_ble_scan_interval : 0U;
  uint32_t used   = fake_ble_observing ? ((uint32_t)fake_ble_scan_window + GUARD_STEPS) : 0U;

  CHECK_PRESENT();
  OTHER_COMMAND();
  if (fake_ble_observing && fake_ble_broadcasting)
  {
    used += ADV_SLOT_STEPS + GUARD_STEPS;
  }
  *anchor_period = period;
  *max_free_slot = (period > used) ? (period - used) : 0U;
  return BLE_STATUS_SUCCESS;
}

tBleStatus aci_hal_set_tx_power_level(uint8_t en_high_power, uint8_t pa_level)
{
  CHECK_PRESENT();
  OTHER_COMMAND();
  if ((en_high_power > 1U) || (pa_level > 7U))
  {
    return BLE_STATUS_INVALID_PARAMS;
  }
  fake_ble_tx_high = en_high_power;
  fake_ble_tx_pa   = pa_level;
  return BLE_STATUS_SUCCESS;
}

/* ---- GATT et GAP ------------------------------------------------------------ */
tBleStatus aci_gatt_init(void)
{
  CHECK_PRESENT();
  OTHER_COMMAND();
  return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gap_init_IDB05A1(uint8_t role, uint8_t privacy_enabled, uint8_t device_name_char_len,
                                uint16_t *service_handle, uint16_t *dev_name_char_handle,
                                uint16_t *appearance_char_handle)
{
  (void)privacy_enabled;
  (void)device_name_char_len;
  CHECK_PRESENT();
  OTHER_COMMAND();
  if (fake_ble_strict_roles && (role == (GAP_BROADCASTER_ROLE_IDB05A1 | GAP_OBSERVER_ROLE_IDB05A1)))
  {
    return BLE_STATUS_INVALID_PARAMS;
  }
  fake_ble_gap_roles      = role;
  *service_handle         = 0x0005U;
  *dev_name_char_handle   = 0x0006U;
  *appearance_char_handle = 0x0008U;
  return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gap_start_observation_procedure(uint16_t scan_interval, uint16_t scan_window, uint8_t scan_type,
                                               uint8_t own_address_type, uint8_t filter_duplicates)
{
  (void)own_address_type;
  CHECK_PRESENT();
  OTHER_COMMAND();
  if (fake_ble_scan_fail_next > 0)
  {
    fake_ble_scan_fail_next--;
    return ERR_COMMAND_DISALLOWED;
  }
  if (fake_ble_observing || (fake_ble_broadcasting && !concurrency_allowed()))
  {
    return ERR_COMMAND_DISALLOWED;
  }
  fake_ble_observing       = true;
  fake_ble_observe_starts++;
  fake_ble_scan_interval   = scan_interval;
  fake_ble_scan_window     = scan_window;
  fake_ble_scan_type       = scan_type;
  fake_ble_scan_filter_dup = filter_duplicates;
  return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gap_terminate_gap_procedure(uint8_t procedure_code)
{
  CHECK_PRESENT();
  OTHER_COMMAND();
  if ((procedure_code == GAP_OBSERVATION_PROC_IDB05A1) && fake_ble_observing)
  {
    fake_ble_observing = false;
    fake_ble_procedure_complete(GAP_OBSERVATION_PROC_IDB05A1);   /* confirmation */
    return BLE_STATUS_SUCCESS;
  }
  return ERR_COMMAND_DISALLOWED;
}

tBleStatus aci_gap_set_broadcast_mode(uint16_t adv_interv_min, uint16_t adv_interv_max, uint8_t adv_type,
                                      uint8_t own_addr_type, uint8_t adv_data_length, const uint8_t *adv_data,
                                      uint8_t num_whitelist_entries, const uint8_t *addr_array)
{
  const uint8_t *pdu;
  uint8_t        pdu_len;

  (void)own_addr_type;
  (void)num_whitelist_entries;
  (void)addr_array;
  CHECK_PRESENT();
  OTHER_COMMAND();
  if (fake_ble_broadcast_fail_next > 0)
  {
    fake_ble_broadcast_fail_next--;
    return fake_ble_broadcast_fail_status;
  }
  if (fake_ble_broadcasting || (fake_ble_observing && !concurrency_allowed()) || (adv_interv_min < 0x00A0U)
      || (adv_data_length > 31U))
  {
    return (adv_data_length > 31U || adv_interv_min < 0x00A0U) ? BLE_STATUS_INVALID_PARAMS : ERR_COMMAND_DISALLOWED;
  }
  if (fake_ble_observing && !adv_fits_time_base(adv_interv_min, adv_interv_max))
  {
    fake_ble_time_base_refusals++;
    return ERR_INVALID_HCI_CMD_PARAMS;   /* 0x12 : pas de place dans la base de temps */
  }
  fake_ble_broadcasting = true;
  fake_ble_broadcast_starts++;
  fake_ble_adv_interval = adv_interv_min;
  fake_ble_adv_type     = adv_type;
  if (mesh_adv_find(adv_data, adv_data_length, &pdu, &pdu_len) && (fake_ble_sent_count < FAKE_BLE_MAX_SENT))
  {
    if (mesh_pdu_decode(pdu, pdu_len, &fake_ble_sent[fake_ble_sent_count]))
    {
      fake_ble_sent_during_scan[fake_ble_sent_count] = fake_ble_observing;
      fake_ble_sent_count++;
    }
  }
  return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gap_set_non_discoverable(void)
{
  CHECK_PRESENT();
  OTHER_COMMAND();
  if (fake_ble_broadcasting)
  {
    fake_ble_broadcasting = false;
    fake_ble_broadcast_stops++;
  }
  return BLE_STATUS_SUCCESS;
}

/* ---- Évènements injectés ---------------------------------------------------- */
void fake_ble_adv_report(const uint8_t addr[6], const uint8_t *adv, uint8_t len, int8_t rssi)
{
  uint8_t e[64];
  size_t  n = 0U;

  if (!fake_ble_observing)
  {
    fake_ble_reports_while_deaf++;        /* un vrai module ne l'aurait pas entendue */
    if (getenv("FAKE_BLE_DEAF_LOG") != NULL)
    {
      fprintf(stderr, "rapport injecté écoute arrêtée à %lu ms\n", (unsigned long)HAL_GetTick());
    }
  }

  e[n++] = HCI_EVENT_PKT;
  e[n++] = EVT_LE_META_EVENT;
  e[n++] = (uint8_t)(12U + len);          /* longueur des paramètres */
  e[n++] = EVT_LE_ADVERTISING_REPORT;
  e[n++] = 1U;                            /* un rapport */
  e[n++] = 0x03U;                         /* ADV_NONCONN_IND */
  e[n++] = 0x00U;                         /* adresse publique */
  memcpy(&e[n], addr, 6);
  n += 6U;
  e[n++] = len;
  memcpy(&e[n], adv, len);
  n += len;
  e[n++] = (uint8_t)rssi;
  push_event(e, n);
  fake_gpio_set_input(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
}

void fake_ble_mesh_frame(const mesh_frame_t *f, int8_t rssi)
{
  static const uint8_t addr[6] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0xC6 };
  uint8_t pdu[MESH_PDU_MAX];
  uint8_t adv[MESH_ADV_MAX];
  uint8_t pdu_len = mesh_pdu_encode(f, pdu, sizeof(pdu));
  uint8_t adv_len = mesh_adv_build(pdu, pdu_len, adv, sizeof(adv));

  fake_ble_adv_report(addr, adv, adv_len, rssi);
}

void fake_ble_bad_report(void)
{
  uint8_t e[16] = { HCI_EVENT_PKT, EVT_LE_META_EVENT, 12U, EVT_LE_ADVERTISING_REPORT, 1U, 0x03U, 0x00U,
                    1, 2, 3, 4, 5, 6, 30U /* annonce 30 octets... */, 0x1EU, 0xFFU };
  push_event(e, sizeof(e));             /* ...mais le paquet s'arrête là */
}

void fake_ble_procedure_complete(uint8_t code)
{
  uint8_t e[8] = { HCI_EVENT_PKT, EVT_VENDOR, 4U,
                   (uint8_t)(EVT_BLUE_GAP_PROCEDURE_COMPLETE & 0xFFU),
                   (uint8_t)(EVT_BLUE_GAP_PROCEDURE_COMPLETE >> 8), code, BLE_STATUS_SUCCESS, 0U };
  push_event(e, 7U);
}

void fake_ble_hal_initialized(uint8_t reason)
{
  uint8_t e[6] = { HCI_EVENT_PKT, EVT_VENDOR, 3U, (uint8_t)(EVT_BLUE_HAL_INITIALIZED & 0xFFU),
                   (uint8_t)(EVT_BLUE_HAL_INITIALIZED >> 8), reason };
  push_event(e, sizeof(e));
  if (reason >= RESET_WATCHDOG)
  {
    fake_ble_observing    = false;      /* le module a tout oublié */
    fake_ble_broadcasting = false;
  }
}
