/**
 ******************************************************************************
 * @file    Tests/fakes/fake_bluenrg.c
 * @brief   Remplaçant PC de la bibliothèque BlueNRG-MS et du module
 *          BlueNRG-M0 : chaque commande ACI est enregistrée, chaque
 *          évènement injecté est rendu au programme par hci_user_evt_proc(),
 *          dans des tampons réutilisés sans être effacés (comme les 5 tampons
 *          de la vraie bibliothèque).
 ******************************************************************************
 */
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
#include "bluenrg_gatt_server.h"
#include "bluenrg_hal_aci.h"
#include "bluenrg_utils.h"

/* ---- Réglages et relevés -------------------------------------------------- */
bool     fake_ble_present = true;
bool     fake_ble_random_addr_valid = true;
uint8_t  fake_ble_random_addr[6] = { 0xAB, 0x89, 0x67, 0x45, 0x23, 0xC1 };   /* C1:23:45:67:89:AB */
int      fake_ble_busy_next;
int      fake_ble_adv_fail_next;
bool     fake_ble_clean_buffers;

int      fake_ble_init_count;
uint8_t  fake_ble_public_addr[6];
char     fake_ble_device_name[32];
char     fake_ble_adv_name[32];
int      fake_ble_adv_count;
int      fake_ble_adv_attempts;
uint32_t fake_ble_fixed_pin;
uint8_t  fake_ble_service_uuid[16];
uint8_t  fake_ble_tx_uuid[16], fake_ble_rx_uuid[16];
uint8_t  fake_ble_tx_props, fake_ble_rx_props;
int      fake_ble_isr_kicks;
bool     fake_ble_advertising;

int     fake_ble_notif_count;
uint8_t fake_ble_notif_len[FAKE_BLE_MAX_NOTIF];
char    fake_ble_notif[FAKE_BLE_MAX_NOTIF][21];

static void   (*s_user_cb)(void *);
static int    s_char_count;
static bool   s_connected;
static bool   s_subscribed;
static uint16_t s_gap_dev_name_handle = 0x0006U;

/* File des évènements à rendre au programme */
#define EVT_QUEUE 64
static uint8_t s_evt[EVT_QUEUE][128];
static size_t  s_evt_len[EVT_QUEUE];
static size_t  s_evt_head, s_evt_count;

/* Tampons de paquets réutilisés (5, comme HCI_READ_PACKET_NUM_MAX) */
static uint8_t s_pkt[5][128];
static int     s_pkt_next;

#define CHECK_PRESENT()  do { if (!fake_ble_present) { return BLE_STATUS_TIMEOUT; } } while (0)

void fake_ble_clear_notifications(void)
{
  fake_ble_notif_count = 0;
}

/* ---- Couche de transport HCI ---------------------------------------------- */
void hci_init(void (*UserEvtRx)(void *pData), void *pConf)
{
  (void)pConf;
  s_user_cb = UserEvtRx;
  fake_ble_init_count++;
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);    /* comme hci_tl_lowlevel_init() */
  s_char_count = 0;
  s_connected = false;
  s_subscribed = false;
  fake_ble_advertising = false;
}

static void deliver(const uint8_t *bytes, size_t len)
{
  uint8_t *pkt = s_pkt[s_pkt_next];

  s_pkt_next = (s_pkt_next + 1) % 5;
  if (fake_ble_clean_buffers)
  {
    memset(pkt, 0, sizeof(s_pkt[0]));
  }
  memcpy(pkt, bytes, len);                 /* la suite du tampon garde ses anciens octets */
  if (s_user_cb != NULL)
  {
    s_user_cb(pkt);
  }
}

void hci_user_evt_proc(void)
{
  while (s_evt_count > 0U)
  {
    uint8_t buf[128];
    size_t  len = s_evt_len[s_evt_head];

    memcpy(buf, s_evt[s_evt_head], len);
    s_evt_head = (s_evt_head + 1U) % EVT_QUEUE;
    s_evt_count--;
    /* état du « téléphone » mis à jour quand l'évènement est rendu */
    if (buf[1] == EVT_LE_META_EVENT && buf[3] == EVT_LE_CONN_COMPLETE && buf[4] == 0U)
    {
      s_connected = true;
      fake_ble_advertising = false;
    }
    if (buf[1] == EVT_LE_META_EVENT && buf[3] == EVT_LE_CONN_COMPLETE && buf[4] != 0U)
    {
      fake_ble_advertising = false;
    }
    if (buf[1] == EVT_DISCONN_COMPLETE)
    {
      s_connected  = false;
      s_subscribed = false;
    }
    deliver(buf, len);
  }
}

void hci_tl_lowlevel_isr(void)
{
  fake_ble_isr_kicks++;
  fake_gpio_set_input(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);   /* données lues */
}

int hci_reset(void)
{
  return fake_ble_present ? 0 : -1;
}

int hci_le_set_scan_resp_data(uint8_t length, const uint8_t data[])
{
  (void)length;
  (void)data;
  return fake_ble_present ? 0 : -1;
}

uint8_t getBlueNRGVersion(uint8_t *hwVersion, uint16_t *fwVersion)
{
  CHECK_PRESENT();
  *hwVersion = 0x31U;
  *fwVersion = 0x0721U;
  return BLE_STATUS_SUCCESS;
}

/* ---- Commandes ACI ---------------------------------------------------------- */
tBleStatus aci_hal_read_config_data(uint8_t offset, uint16_t data_len, uint8_t *data_len_out_p, uint8_t *data)
{
  CHECK_PRESENT();
  if ((offset != CONFIG_DATA_RANDOM_ADDRESS) || (data_len < 6U) || !fake_ble_random_addr_valid)
  {
    return BLE_STATUS_INVALID_PARAMS;
  }
  memcpy(data, fake_ble_random_addr, 6U);
  *data_len_out_p = 6U;
  return BLE_STATUS_SUCCESS;
}

tBleStatus aci_hal_write_config_data(uint8_t offset, uint8_t len, const uint8_t *val)
{
  CHECK_PRESENT();
  if ((offset == CONFIG_DATA_PUBADDR_OFFSET) && (len == 6U))
  {
    memcpy(fake_ble_public_addr, val, 6U);
  }
  return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gatt_init(void)
{
  CHECK_PRESENT();
  return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gap_init_IDB05A1(uint8_t role, uint8_t privacy_enabled, uint8_t device_name_char_len,
                                uint16_t *service_handle, uint16_t *dev_name_char_handle,
                                uint16_t *appearance_char_handle)
{
  (void)role;
  (void)privacy_enabled;
  (void)device_name_char_len;
  CHECK_PRESENT();
  *service_handle         = 0x0005U;
  *dev_name_char_handle   = s_gap_dev_name_handle;
  *appearance_char_handle = 0x0008U;
  return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gap_set_auth_requirement(uint8_t mitm_mode, uint8_t oob_enable, uint8_t oob_data[16],
                                        uint8_t min_encryption_key_size, uint8_t max_encryption_key_size,
                                        uint8_t use_fixed_pin, uint32_t fixed_pin, uint8_t bonding_mode)
{
  (void)mitm_mode; (void)oob_enable; (void)oob_data; (void)min_encryption_key_size;
  (void)max_encryption_key_size; (void)use_fixed_pin; (void)bonding_mode;
  CHECK_PRESENT();
  fake_ble_fixed_pin = fixed_pin;
  return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gatt_add_serv(uint8_t service_uuid_type, const uint8_t *service_uuid, uint8_t service_type,
                             uint8_t max_attr_records, uint16_t *serviceHandle)
{
  (void)service_uuid_type; (void)service_type; (void)max_attr_records;
  CHECK_PRESENT();
  memcpy(fake_ble_service_uuid, service_uuid, 16U);
  *serviceHandle = 0x000CU;
  return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gatt_add_char(uint16_t serviceHandle, uint8_t charUuidType, const uint8_t *charUuid,
                             uint8_t charValueLen, uint8_t charProperties, uint8_t secPermissions,
                             uint8_t gattEvtMask, uint8_t encryKeySize, uint8_t isVariable,
                             uint16_t *charHandle)
{
  (void)serviceHandle; (void)charUuidType; (void)charValueLen; (void)secPermissions;
  (void)gattEvtMask; (void)encryKeySize; (void)isVariable;
  CHECK_PRESENT();
  if (s_char_count == 0)
  {
    memcpy(fake_ble_tx_uuid, charUuid, 16U);
    fake_ble_tx_props = charProperties;
    *charHandle = FAKE_TX_CHAR_HANDLE;
  }
  else
  {
    memcpy(fake_ble_rx_uuid, charUuid, 16U);
    fake_ble_rx_props = charProperties;
    *charHandle = FAKE_RX_CHAR_HANDLE;
  }
  s_char_count++;
  return BLE_STATUS_SUCCESS;
}

tBleStatus aci_hal_set_tx_power_level(uint8_t en_high_power, uint8_t pa_level)
{
  (void)en_high_power;
  (void)pa_level;
  CHECK_PRESENT();
  return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gap_set_discoverable(uint8_t AdvType, uint16_t AdvIntervMin, uint16_t AdvIntervMax,
                                    uint8_t OwnAddrType, uint8_t AdvFilterPolicy, uint8_t LocalNameLen,
                                    const char *LocalName, uint8_t ServiceUUIDLen, uint8_t *ServiceUUIDList,
                                    uint16_t SlaveConnIntervMin, uint16_t SlaveConnIntervMax)
{
  (void)AdvType; (void)AdvIntervMin; (void)AdvIntervMax; (void)OwnAddrType; (void)AdvFilterPolicy;
  (void)ServiceUUIDLen; (void)ServiceUUIDList; (void)SlaveConnIntervMin; (void)SlaveConnIntervMax;
  CHECK_PRESENT();
  fake_ble_adv_attempts++;
  if (fake_ble_adv_fail_next > 0)
  {
    fake_ble_adv_fail_next--;
    return BLE_STATUS_NOT_ALLOWED;
  }
  if ((LocalNameLen > 1U) && (LocalNameLen < sizeof(fake_ble_adv_name)))
  {
    memcpy(fake_ble_adv_name, &LocalName[1], LocalNameLen - 1U);   /* sans l'octet de type */
    fake_ble_adv_name[LocalNameLen - 1U] = '\0';
  }
  fake_ble_adv_count++;
  fake_ble_advertising = true;
  return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gatt_update_char_value(uint16_t servHandle, uint16_t charHandle, uint8_t charValOffset,
                                      uint8_t charValueLen, const void *charValue)
{
  (void)servHandle;
  (void)charValOffset;
  CHECK_PRESENT();
  if (charHandle == s_gap_dev_name_handle)
  {
    size_t n = (charValueLen < sizeof(fake_ble_device_name) - 1U) ? charValueLen : sizeof(fake_ble_device_name) - 1U;
    memcpy(fake_ble_device_name, charValue, n);
    fake_ble_device_name[n] = '\0';
    return BLE_STATUS_SUCCESS;
  }
  if (fake_ble_busy_next > 0)
  {
    fake_ble_busy_next--;
    return BLE_STATUS_INSUFFICIENT_RESOURCES;
  }
  if ((charHandle == FAKE_TX_CHAR_HANDLE) && s_connected && s_subscribed
      && (fake_ble_notif_count < FAKE_BLE_MAX_NOTIF) && (charValueLen <= 20U))
  {
    memcpy(fake_ble_notif[fake_ble_notif_count], charValue, charValueLen);
    fake_ble_notif[fake_ble_notif_count][charValueLen] = '\0';
    fake_ble_notif_len[fake_ble_notif_count] = charValueLen;
    fake_ble_notif_count++;
  }
  return BLE_STATUS_SUCCESS;
}

/* Utilisée par la version 2024 seulement (code jamais appelé). */
tBleStatus aci_gatt_write_charac_descriptor(uint16_t conn_handle, uint16_t attr_handle,
                                           uint8_t value_len, uint8_t *attr_value)
{
  (void)conn_handle; (void)attr_handle; (void)value_len; (void)attr_value;
  return BLE_STATUS_SUCCESS;
}

/* ---- Évènements ------------------------------------------------------------- */
static void push(const uint8_t *bytes, size_t len)
{
  size_t slot;

  if (s_evt_count >= EVT_QUEUE)
  {
    return;
  }
  slot = (s_evt_head + s_evt_count) % EVT_QUEUE;
  memcpy(s_evt[slot], bytes, len);
  s_evt_len[slot] = len;
  s_evt_count++;
}

size_t fake_ble_pending_events(void)
{
  return s_evt_count;
}

void fake_ble_connect(const uint8_t peer[6], uint8_t status)
{
  uint8_t e[3 + 19] = { HCI_EVENT_PKT, EVT_LE_META_EVENT, 19, EVT_LE_CONN_COMPLETE, status,
                        0x01, 0x08,  /* handle 0x0801 */
                        0x01,        /* rôle : esclave */
                        0x01 };      /* adresse aléatoire */
  memcpy(&e[9], peer, 6U);
  e[15] = 0x18; e[16] = 0x00;        /* intervalle */
  e[17] = 0x00; e[18] = 0x00;        /* latence */
  e[19] = 0xC8; e[20] = 0x00;        /* délai de supervision */
  e[21] = 0x00;
  push(e, sizeof(e));
}

void fake_ble_disconnect(void)
{
  uint8_t e[] = { HCI_EVENT_PKT, EVT_DISCONN_COMPLETE, 4, 0x00, 0x01, 0x08, 0x13 };
  push(e, sizeof(e));
}

static void push_vendor(uint16_t ecode, const uint8_t *data, size_t len)
{
  uint8_t e[128];

  e[0] = HCI_EVENT_PKT;
  e[1] = EVT_VENDOR;
  e[2] = (uint8_t)(2U + len);
  e[3] = (uint8_t)ecode;
  e[4] = (uint8_t)(ecode >> 8);
  memcpy(&e[5], data, len);
  push(e, 5U + len);
}

static void push_attr_modified(uint16_t handle, const uint8_t *data, size_t len)
{
  uint8_t d[7 + 64];

  d[0] = 0x01; d[1] = 0x08;                        /* connexion */
  d[2] = (uint8_t)handle; d[3] = (uint8_t)(handle >> 8);
  d[4] = (uint8_t)len;
  d[5] = 0x00; d[6] = 0x00;                        /* décalage */
  memcpy(&d[7], data, len);
  push_vendor(EVT_BLUE_GATT_ATTRIBUTE_MODIFIED, d, 7U + len);
}

void fake_ble_write(const char *data, size_t len)
{
  push_attr_modified(FAKE_RX_CHAR_HANDLE + 1U, (const uint8_t *)data, len);
}

void fake_ble_write_str(const char *text)
{
  fake_ble_write(text, strlen(text));
}

void fake_ble_subscribe(void)
{
  static const uint8_t on[2] = { 0x01, 0x00 };
  push_attr_modified(FAKE_TX_CHAR_HANDLE + 2U, on, 2U);
  s_subscribed = true;
}

void fake_ble_tx_pool_available(void)
{
  uint8_t d[4] = { 0x01, 0x08, 0x04, 0x00 };
  push_vendor(EVT_BLUE_GATT_TX_POOL_AVAILABLE, d, sizeof(d));
}

void fake_ble_hal_initialized(uint8_t reason)
{
  push_vendor(EVT_BLUE_HAL_INITIALIZED, &reason, 1U);
  s_connected = false;
  fake_ble_advertising = false;
}
