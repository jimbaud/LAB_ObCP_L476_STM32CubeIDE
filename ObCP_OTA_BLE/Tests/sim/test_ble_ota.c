/**
 * @file    test_ble_ota.c
 * @brief   Tests sur PC de ble_ota.c, la partie Bluetooth du chargeur.
 *
 *  ble_ota.c est compilé tel quel. Autour de lui :
 *   - un FAUX MODULE BlueNRG : les fonctions aci_xxx() et hci_xxx() sont
 *     écrites ci-dessous ; elles enregistrent ce qu'on leur demande ;
 *   - des ÉVÉNEMENTS fabriqués par le test (connexion, écriture du PC,
 *     déconnexion, redémarrage du module...), remis à la fonction de
 *     traitement de ble_ota.c au moment voulu ;
 *   - une HORLOGE simulée : HAL_GetTick() avance d'1 ms à chaque appel ;
 *   - les vrais ota.c, crc32.c et app_image.c, avec la flash simulée.
 *
 *  Lancement : ./run_tests.sh (étape 1).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "hci.h"
#include "hci_le.h"
#include "hci_tl.h"
#include "bluenrg_aci.h"
#include "bluenrg_gatt_server.h"
#include "bluenrg_utils.h"

#include "ble_ota.h"
#include "boot.h"
#include "crc32.h"
#include "loader_config.h"
#include "ota.h"
#include "ota_protocol.h"
#include "sim_api.h"

/* ------------------------------------------------------------------------- */
/* Petit outil de vérification                                               */
/* ------------------------------------------------------------------------- */
static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("  ÉCHEC ligne %d : %s\n", __LINE__, #cond);              \
        }                                                                    \
    } while (0)

/* Handles attribués par le faux module (voir aci_gatt_add_char) :
 * CTRL : déclaration 0x0D, valeur 0x0E, CCCD 0x0F
 * DATA : déclaration 0x10, valeur 0x11 ; INFO : déclaration 0x12.          */
#define H_CTRL        0x000DU
#define H_CTRL_VALUE  0x000EU
#define H_CTRL_CCCD   0x000FU
#define H_DATA_VALUE  0x0011U
#define H_INFO        0x0012U

/* ------------------------------------------------------------------------- */
/* État du faux module et de la fausse HAL                                   */
/* ------------------------------------------------------------------------- */
static struct
{
    uint32_t now;                   /* horloge simulée (ms)                  */
    int      irq_enabled;           /* interruption du module démasquée ?    */
    void   (*evt_rx)(void *);       /* fonction enregistrée par hci_init()   */
    tHciIO   io;                    /* fonctions d'entrée-sortie enregistrées */
    int      hci_init_count;        /* nombre d'initialisations complètes    */
    int      spi_sends;             /* envois SPI réels                       */
    int      spi_sends_irq_on;      /* ... dont avec l'interruption démasquée */
    int      adv_count;             /* lancements des annonces               */
    tBleStatus adv_status;          /* réponse du module à ces lancements    */
    int      terminates;            /* coupures demandées par la carte       */
    int      param_requests;        /* demandes de paramètres de connexion   */
    uint16_t next_handle;
    uint8_t  info_char_len;         /* longueur déclarée de INFO             */
    uint8_t  info_char_variable;
    uint8_t  info[OTA_INFO_LEN];    /* dernière valeur de INFO               */
    int      info_writes;
    uint8_t  written_addr[6];       /* adresse imposée au module             */
    int      nrsp;                  /* réponses notifiées sur CTRL           */
    uint8_t  rsp_code;              /* dernière réponse : code...            */
    uint32_t rsp_value;             /* ... et valeur                         */
    int      ctrl_busy;             /* prochains envois refusés (tampons pleins) */
    int      ctrl_refused;
} m;

/* File des événements que le module « enverra » : chacun a sa date. */
#define MAX_EVENTS 128
static struct { uint32_t at; uint8_t len; uint8_t bytes[64]; } events[MAX_EVENTS];
static int ev_head = 0;
static int ev_tail = 0;

static void push_event(uint32_t delay_ms, const uint8_t *bytes, uint8_t len)
{
    if ((ev_tail < MAX_EVENTS) && (len <= sizeof(events[0].bytes)))
    {
        events[ev_tail].at = m.now + delay_ms;
        events[ev_tail].len = len;
        memcpy(events[ev_tail].bytes, bytes, len);
        ev_tail++;
    }
}

/* Événement « module démarré » (après chaque reset du module). */
static void ev_hal_initialized(uint32_t delay_ms, uint8_t reason)
{
    const uint8_t p[] = { HCI_EVENT_PKT, EVT_VENDOR, 3, 0x01, 0x00, reason };
    push_event(delay_ms, p, sizeof(p));
}

/* Connexion d'un PC (status 0) ou connexion ratée (status non nul). */
static void ev_conn_complete(uint32_t delay_ms, uint8_t status)
{
    const uint8_t p[] = { HCI_EVENT_PKT, EVT_LE_META_EVENT, 19, EVT_LE_CONN_COMPLETE,
                          status, 0x01, 0x08, 0x01, 0x00,
                          0x11, 0x22, 0x33, 0x44, 0x55, 0x66,   /* adresse du PC */
                          24, 0, 0, 0, 0x90, 0x01, 0x00 };       /* 30 ms, 0, 4 s */
    push_event(delay_ms, p, sizeof(p));
}

/* Fin de connexion, avec sa cause (code d'erreur HCI). */
static void ev_disconn(uint32_t delay_ms, uint8_t status, uint8_t reason)
{
    const uint8_t p[] = { HCI_EVENT_PKT, EVT_DISCONN_COMPLETE, 4, status, 0x01, 0x08, reason };
    push_event(delay_ms, p, sizeof(p));
}

/* Le PC écrit 'len' octets dans l'attribut 'attr'. */
static void ev_write(uint32_t delay_ms, uint16_t attr, const uint8_t *data, uint8_t len)
{
    uint8_t p[64];
    p[0] = HCI_EVENT_PKT;
    p[1] = EVT_VENDOR;
    p[2] = (uint8_t)(2U + 7U + len);
    p[3] = 0x01;                              /* EVT_BLUE_GATT_ATTRIBUTE_MODIFIED */
    p[4] = 0x0C;
    p[5] = 0x01; p[6] = 0x08;                 /* connexion 0x0801                */
    p[7] = (uint8_t)attr; p[8] = (uint8_t)(attr >> 8);
    p[9] = len; p[10] = 0; p[11] = 0;         /* longueur, position              */
    memcpy(&p[12], data, len);
    push_event(delay_ms, p, (uint8_t)(12U + len));
}

/* ------------------------------------------------------------------------- */
/* Fausse HAL                                                                */
/* ------------------------------------------------------------------------- */
GPIO_TypeDef fake_gpioa, fake_gpiob, fake_gpioc;
const uint8_t fake_uid[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };

uint32_t HAL_GetTick(void)                   { return m.now++; }
void HAL_Delay(uint32_t delay_ms)            { m.now += delay_ms; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    (void)port; (void)pin;
    return GPIO_PIN_RESET;                   /* broche IRQ au repos */
}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    (void)port; (void)pin; (void)state;
}
void HAL_NVIC_EnableIRQ(IRQn_Type irq)       { if (irq == BLE_IRQ_EXTI_IRQn) m.irq_enabled = 1; }
void HAL_NVIC_DisableIRQ(IRQn_Type irq)      { if (irq == BLE_IRQ_EXTI_IRQn) m.irq_enabled = 0; }
boot_reason_t boot_get_reason(void)          { return BOOT_REASON_BUTTON; }

/* ------------------------------------------------------------------------- */
/* Faux pilote SPI et fausse couche HCI                                      */
/* ------------------------------------------------------------------------- */
int32_t HCI_TL_SPI_Init(void *pConf)         { (void)pConf; return 0; }
int32_t HCI_TL_SPI_DeInit(void)              { return 0; }
int32_t HCI_TL_SPI_Reset(void)               { return 0; }
int32_t HCI_TL_SPI_Receive(uint8_t *buffer, uint16_t size) { (void)buffer; (void)size; return 0; }
int32_t HCI_TL_SPI_Send(uint8_t *buffer, uint16_t size)
{
    (void)buffer; (void)size;
    m.spi_sends++;
    if (m.irq_enabled)
    {
        m.spi_sends_irq_on++;                /* envoi exposé à l'interruption */
    }
    return 0;
}
int32_t BSP_GetTick(void)                    { return (int32_t)m.now; }

void hci_init(void (*UserEvtRx)(void *pData), void *pConf)
{
    (void)pConf;
    m.evt_rx = UserEvtRx;
    m.hci_init_count++;
    /* Comme le vrai hci_init : fonctions du pilote de ST, puis reset
     * matériel du module, qui annonce son démarrage un peu plus tard.     */
    memset(&m.io, 0, sizeof(m.io));
    m.io.Send = HCI_TL_SPI_Send;
    ev_hal_initialized(5, RESET_NORMAL);
}

void hci_register_io_bus(tHciIO *fops)       { m.io = *fops; }
void hci_tl_lowlevel_isr(void)               { }

/* Remet au chargeur les événements dont la date est arrivée, dans l'ordre. */
void hci_user_evt_proc(void)
{
    while ((ev_head < ev_tail) && ((int32_t)(m.now - events[ev_head].at) >= 0))
    {
        int i = ev_head++;
        if (m.evt_rx != NULL)
        {
            m.evt_rx(events[i].bytes);
        }
    }
    if (ev_head == ev_tail)
    {
        ev_head = ev_tail = 0;
    }
}

/* ------------------------------------------------------------------------- */
/* Faux module : commandes ACI                                               */
/* ------------------------------------------------------------------------- */
uint8_t getBlueNRGVersion(uint8_t *hwVersion, uint16_t *fwVersion)
{
    *hwVersion = 0x31;
    *fwVersion = 0x0721;
    return 0;
}

int hci_reset(void)
{
    ev_hal_initialized(1, RESET_NORMAL);     /* le module redémarre */
    return 0;
}

tBleStatus aci_hal_read_config_data(uint8_t offset, uint16_t data_len,
                                    uint8_t *data_len_out_p, uint8_t *data)
{
    static const uint8_t addr[6] = { 0xBB, 0xBE, 0x44, 0x33, 0x22, 0xC1 };  /* C1:22:33:44:BE:BB */
    (void)offset; (void)data_len;
    memcpy(data, addr, sizeof(addr));
    *data_len_out_p = sizeof(addr);
    return BLE_STATUS_SUCCESS;
}

tBleStatus aci_hal_write_config_data(uint8_t offset, uint8_t len, const uint8_t *val)
{
    (void)offset;
    memcpy(m.written_addr, val, (len < 6U) ? len : 6U);
    return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gatt_init(void)               { return BLE_STATUS_SUCCESS; }

tBleStatus aci_gap_init_IDB05A1(uint8_t role, uint8_t privacy_enabled, uint8_t device_name_char_len,
                                uint16_t *service_handle, uint16_t *dev_name_char_handle,
                                uint16_t *appearance_char_handle)
{
    (void)role; (void)privacy_enabled; (void)device_name_char_len;
    *service_handle = 0x0005;
    *dev_name_char_handle = 0x0006;
    *appearance_char_handle = 0x0008;
    return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gatt_add_serv(uint8_t service_uuid_type, const uint8_t *service_uuid,
                             uint8_t service_type, uint8_t max_attr_records,
                             uint16_t *serviceHandle)
{
    (void)service_uuid_type; (void)service_uuid; (void)service_type; (void)max_attr_records;
    *serviceHandle = 0x000C;
    m.next_handle = 0x000D;
    return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gatt_add_char(uint16_t serviceHandle, uint8_t charUuidType, const uint8_t *charUuid,
                             uint8_t charValueLen, uint8_t charProperties, uint8_t secPermissions,
                             uint8_t gattEvtMask, uint8_t encryKeySize, uint8_t isVariable,
                             uint16_t *charHandle)
{
    (void)serviceHandle; (void)charUuidType; (void)secPermissions; (void)gattEvtMask;
    (void)encryKeySize;
    *charHandle = m.next_handle;
    m.next_handle = (uint16_t)(m.next_handle + (((charProperties & CHAR_PROP_NOTIFY) != 0U) ? 3U : 2U));
    if (charUuid[12] == 0x04)                /* INFO (UUID 9E090004-...) */
    {
        m.info_char_len = charValueLen;
        m.info_char_variable = isVariable;
    }
    return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gatt_update_char_value(uint16_t servHandle, uint16_t charHandle,
                                      uint8_t charValOffset, uint8_t charValueLen,
                                      const void *charValue)
{
    const uint8_t *v = charValue;
    (void)servHandle; (void)charValOffset;
    if (charHandle == H_CTRL)
    {
        if (m.ctrl_busy > 0)                 /* tampons d'émission pleins */
        {
            m.ctrl_busy--;
            m.ctrl_refused++;
            return BLE_STATUS_INSUFFICIENT_RESOURCES;
        }
        m.nrsp++;
        m.rsp_code = v[0];
        m.rsp_value = (uint32_t)v[1] | ((uint32_t)v[2] << 8) | ((uint32_t)v[3] << 16)
                    | ((uint32_t)v[4] << 24);
    }
    else if (charHandle == H_INFO)
    {
        memcpy(m.info, v, (charValueLen < OTA_INFO_LEN) ? charValueLen : OTA_INFO_LEN);
        m.info_writes++;
    }
    return BLE_STATUS_SUCCESS;
}

tBleStatus aci_hal_set_tx_power_level(uint8_t en_high_power, uint8_t pa_level)
{
    (void)en_high_power; (void)pa_level;
    return BLE_STATUS_SUCCESS;
}

int hci_le_set_scan_resp_data(uint8_t length, const uint8_t data[])
{
    (void)length; (void)data;
    return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gap_set_discoverable(uint8_t AdvType, uint16_t AdvIntervMin, uint16_t AdvIntervMax,
                                    uint8_t OwnAddrType, uint8_t AdvFilterPolicy, uint8_t LocalNameLen,
                                    const char *LocalName, uint8_t ServiceUUIDLen,
                                    uint8_t *ServiceUUIDList, uint16_t SlaveConnIntervMin,
                                    uint16_t SlaveConnIntervMax)
{
    (void)AdvType; (void)AdvIntervMin; (void)AdvIntervMax; (void)OwnAddrType;
    (void)AdvFilterPolicy; (void)LocalNameLen; (void)LocalName; (void)ServiceUUIDLen;
    (void)ServiceUUIDList; (void)SlaveConnIntervMin; (void)SlaveConnIntervMax;
    m.adv_count++;
    return m.adv_status;
}

tBleStatus aci_l2cap_connection_parameter_update_request(uint16_t conn_handle, uint16_t interval_min,
                                                         uint16_t interval_max, uint16_t slave_latency,
                                                         uint16_t timeout_multiplier)
{
    (void)conn_handle; (void)interval_min; (void)interval_max; (void)slave_latency;
    (void)timeout_multiplier;
    m.param_requests++;
    return BLE_STATUS_SUCCESS;
}

tBleStatus aci_gap_terminate(uint16_t conn_handle, uint8_t reason)
{
    (void)conn_handle; (void)reason;
    m.terminates++;
    ev_disconn(2, BLE_STATUS_SUCCESS, 0x16);  /* « coupé par l'hôte local » */
    return BLE_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------- */
/* Outils des scénarios                                                      */
/* ------------------------------------------------------------------------- */

/* Fait tourner la partie Bluetooth pendant 'ms' millisecondes. */
static void run(uint32_t ms)
{
    uint32_t end = m.now + ms;
    while ((int32_t)(end - m.now) > 0)
    {
        ble_ota_process();
        m.now++;
    }
}

static void write_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Petit programme plausible (table des vecteurs correcte) ; renvoie son CRC. */
static uint8_t image[100];
static uint32_t make_image(void)
{
    for (uint32_t i = 0; i < sizeof(image); i++)
    {
        image[i] = (uint8_t)(i * 7U + 3U);
    }
    write_u32(&image[0], 0x20018000UL);
    write_u32(&image[4], APP_ADDRESS + 0x41UL);
    return crc32_update(0U, image, sizeof(image));
}

static void connect_pc(void)
{
    const uint8_t enable_notify[2] = { 0x01, 0x00 };
    ev_conn_complete(1, BLE_STATUS_SUCCESS);
    ev_write(5, H_CTRL_CCCD, enable_notify, 2);
    run(20);
}

static void send_start(uint32_t crc)
{
    uint8_t start[OTA_START_LEN];
    start[0] = OTA_CMD_START;
    write_u32(&start[1], sizeof(image));
    write_u32(&start[5], crc);
    ev_write(1, H_CTRL_VALUE, start, sizeof(start));
    run(10);
}

static void send_data(uint32_t upto)
{
    uint8_t seq = 0;
    for (uint32_t pos = 0; pos < upto; pos += OTA_DATA_PAYLOAD)
    {
        uint8_t packet[OTA_DATA_MAX_LEN];
        uint32_t n = ((upto - pos) < OTA_DATA_PAYLOAD) ? (upto - pos) : OTA_DATA_PAYLOAD;
        packet[0] = seq++;
        memcpy(&packet[1], &image[pos], n);
        ev_write(0, H_DATA_VALUE, packet, (uint8_t)(n + 1U));
        run(2);
    }
}

static void send_cmd(uint8_t code)
{
    ev_write(1, H_CTRL_VALUE, &code, 1);
    run(10);
}

/* ------------------------------------------------------------------------- */
/* Scénarios                                                                 */
/* ------------------------------------------------------------------------- */

static void test_init(void)
{
    printf("Initialisation du module\n");
    sim_flash_reset(0xFF);
    m.irq_enabled = 1;
    CHECK(ble_ota_init());
    CHECK(ble_ota_is_ready());
    CHECK(strcmp(ble_ota_get_name(), "ObCP-OTA-BEBB") == 0);
    CHECK(m.written_addr[0] == 0xBB && m.written_addr[5] == 0xC0);   /* C0:22:33:44:BE:BB */
    CHECK(m.info_char_len == OTA_INFO_LEN && m.info_char_variable == 0);
    CHECK(m.info_writes >= 1);
    CHECK(m.info[0] == OTA_PROTOCOL_VERSION);
    CHECK(m.info[1] == LOADER_VERSION_MAJOR && m.info[2] == LOADER_VERSION_MINOR);
    CHECK(m.info[3] == 0);                               /* pas d'application */
    CHECK(m.info[15] == BOOT_REASON_BUTTON);
    CHECK(m.info[OTA_INFO_DISCONNECT_REASON] == 0 && m.info[OTA_INFO_MODULE_RESTART] == 0);

    /* Les événements « module démarré » produits par les resets de
     * l'initialisation ne doivent pas être pris pour un redémarrage imprévu. */
    run(300);
    CHECK(m.hci_init_count == 1);
    CHECK(m.adv_count == 1);                             /* annonces lancées une fois */
    CHECK(!ble_ota_is_connected());
}

static void test_guarded_send(void)
{
    printf("Envoi SPI à l'abri de l'interruption du module\n");
    uint8_t command[4] = { 0x01, 0x03, 0x0C, 0x00 };   /* HCI_Reset */
    CHECK(m.io.Send != NULL && m.io.Send != HCI_TL_SPI_Send);
    CHECK(m.io.Receive == HCI_TL_SPI_Receive && m.io.Reset == HCI_TL_SPI_Reset);
    CHECK(m.io.Init == HCI_TL_SPI_Init && m.io.GetTick == BSP_GetTick);
    int before = m.spi_sends;
    m.irq_enabled = 1;
    m.io.Send(command, sizeof(command));
    CHECK(m.spi_sends == before + 1);
    CHECK(m.spi_sends_irq_on == 0);                      /* masquée pendant l'envoi */
    CHECK(m.irq_enabled == 1);                           /* puis démasquée */
}

static void test_transfer_then_run(void)
{
    printf("Transfert complet, puis RUN : le PC se déconnecte de lui-même\n");
    uint32_t crc = make_image();
    connect_pc();
    CHECK(ble_ota_is_connected());
    CHECK(m.param_requests == 1);                        /* échanges accélérés demandés */

    send_start(crc);
    CHECK(m.rsp_code == OTA_RSP_READY && m.rsp_value == sizeof(image));
    CHECK(m.info[3] == 0);
    send_data(sizeof(image));
    CHECK(m.rsp_code == OTA_RSP_ACK && m.rsp_value == sizeof(image));
    send_cmd(OTA_CMD_END);
    CHECK(m.rsp_code == OTA_RSP_DONE && m.rsp_value == crc);
    CHECK(m.info[3] == 1);                               /* INFO mise à jour avant DONE */
    send_cmd(OTA_CMD_RUN);
    CHECK(ota_take_run_request());

    /* loader.c appelle alors ble_ota_disconnect() ; le PC se déconnecte de
     * lui-même 40 ms plus tard : la carte n'a pas besoin de couper.       */
    ev_disconn(40, BLE_STATUS_SUCCESS, 0x13);
    uint32_t start = m.now;
    ble_ota_disconnect();
    CHECK(!ble_ota_is_connected());
    CHECK(m.terminates == 0);
    CHECK((m.now - start) < LOADER_PC_DISCONNECT_WAIT_MS);
    CHECK(m.info[OTA_INFO_DISCONNECT_REASON] == 0x13);  /* « demandée par le PC » */
}

static void test_run_pc_stays(void)
{
    printf("RUN : le PC reste connecté, la carte coupe après le délai\n");
    run(200);
    connect_pc();
    CHECK(ble_ota_is_connected());
    uint32_t start = m.now;
    ble_ota_disconnect();
    CHECK(m.terminates == 1);
    CHECK((m.now - start) >= LOADER_PC_DISCONNECT_WAIT_MS);
    CHECK(!ble_ota_is_connected());
    CHECK(m.info[OTA_INFO_DISCONNECT_REASON] == 0x16);  /* « coupée par la carte » */
}

static void test_link_lost_during_transfer(void)
{
    printf("Liaison perdue pendant les données, puis après END\n");
    uint32_t crc = make_image();
    sim_flash_reset(0xFF);
    run(200);
    connect_pc();
    send_start(crc);
    send_data(57);                                       /* 3 paquets sur 6 */
    CHECK(ota_get_state() == OTA_STATE_RECEIVING);
    int adv = m.adv_count;
    ev_disconn(1, BLE_STATUS_SUCCESS, 0x08);             /* plus d'échange radio */
    run(10);
    CHECK(!ble_ota_is_connected());
    CHECK(ota_get_state() == OTA_STATE_IDLE);            /* transfert abandonné */
    CHECK(m.info[OTA_INFO_DISCONNECT_REASON] == 0x08);
    CHECK(m.info[3] == 0);                               /* jamais de programme incomplet */
    run(200);
    CHECK(m.adv_count == adv + 1);                       /* de nouveau visible */

    /* Coupure juste après END : le programme a été validé avant la coupure,
     * le PC le verra dans INFO après s'être reconnecté.                   */
    connect_pc();
    send_start(crc);
    send_data(sizeof(image));
    send_cmd(OTA_CMD_END);
    CHECK(m.rsp_code == OTA_RSP_DONE);
    ev_disconn(1, BLE_STATUS_SUCCESS, 0x08);
    run(10);
    CHECK(m.info[3] == 1 && m.info[OTA_INFO_DISCONNECT_REASON] == 0x08);
    run(200);
}

static void test_odd_events(void)
{
    printf("Événements inhabituels\n");
    /* Demande de déconnexion refusée (status non nul) : on reste connecté. */
    connect_pc();
    ev_disconn(1, 0x0C, 0x13);
    run(10);
    CHECK(ble_ota_is_connected());
    ev_disconn(1, BLE_STATUS_SUCCESS, 0x13);
    run(200);
    CHECK(!ble_ota_is_connected());

    /* Connexion ratée : la carte relance ses annonces. */
    int adv = m.adv_count;
    ev_conn_complete(1, 0x3E);
    run(300);
    CHECK(!ble_ota_is_connected());
    CHECK(m.adv_count == adv + 1);

    /* Annonces déjà en cours (« commande refusée ») : pas de nouvel essai. */
    m.adv_status = ERR_COMMAND_DISALLOWED;
    ev_conn_complete(1, 0x3E);
    run(1000);
    CHECK(m.adv_count == adv + 2);

    /* Autre échec : nouvel essai toutes les 100 ms jusqu'au succès. */
    m.adv_status = BLE_STATUS_FAILED;
    ev_conn_complete(1, 0x3E);
    run(1000);
    CHECK(m.adv_count >= adv + 2 + 8);
    m.adv_status = BLE_STATUS_SUCCESS;
    int after = m.adv_count;
    run(1000);
    CHECK(m.adv_count == after + 1);                     /* succès, puis plus rien */
}

static void test_notify_retry(void)
{
    printf("Tampons d'émission pleins : la réponse est réessayée\n");
    connect_pc();
    int n = m.nrsp;
    m.ctrl_busy = 3;                                     /* 3 refus, puis accepté */
    send_cmd(0x7F);                                      /* commande inconnue */
    CHECK(m.ctrl_refused == 3);
    CHECK(m.nrsp == n + 1 && m.rsp_code == OTA_ERR_COMMAND);

    m.ctrl_busy = 1000000;                               /* toujours refusé */
    uint32_t start = m.now;
    send_cmd(0x7F);
    CHECK(m.nrsp == n + 1);                              /* abandon... */
    CHECK((m.now - start) >= 200U && (m.now - start) < 1000U);   /* ... après 200 ms */
    m.ctrl_busy = 0;
    ev_disconn(1, BLE_STATUS_SUCCESS, 0x13);
    run(200);
}

static void test_module_restart(void)
{
    printf("Redémarrage imprévu du module pendant un transfert\n");
    uint32_t crc = make_image();
    connect_pc();
    send_start(crc);
    send_data(38);
    CHECK(ota_get_state() == OTA_STATE_RECEIVING);
    uint8_t reason_before = m.info[OTA_INFO_DISCONNECT_REASON];
    int inits = m.hci_init_count;
    int adv = m.adv_count;
    ev_hal_initialized(1, 8);                            /* 8 = plantage du module */
    run(300);
    CHECK(m.hci_init_count == inits + 1);                /* tout a été refait */
    CHECK(ble_ota_is_ready());
    CHECK(!ble_ota_is_connected());
    CHECK(ota_get_state() == OTA_STATE_IDLE);
    CHECK(m.info[OTA_INFO_MODULE_RESTART] == 8);
    CHECK(m.info[OTA_INFO_DISCONNECT_REASON] == reason_before);
    CHECK(m.adv_count == adv + 1);                       /* de nouveau visible */
    run(1000);
    CHECK(m.hci_init_count == inits + 1);                /* pas de boucle */

    /* Le chargeur fonctionne normalement ensuite. */
    connect_pc();
    send_start(crc);
    send_data(sizeof(image));
    send_cmd(OTA_CMD_END);
    CHECK(m.rsp_code == OTA_RSP_DONE && m.rsp_value == crc);
}

int main(void)
{
    test_init();
    test_guarded_send();
    test_transfer_then_run();
    test_run_pc_stays();
    test_link_lost_during_transfer();
    test_odd_events();
    test_notify_retry();
    test_module_restart();
    printf("%d vérifications, %d échec(s)\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
