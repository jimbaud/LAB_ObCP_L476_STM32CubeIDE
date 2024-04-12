/**
 ******************************************************************************
 * @file    Bluetooth_low_enrgy.c
 *      Author: Joel IMBAUD
 *      SUPMICROTECH ENSMM www.supmicrotech.fr
 *      inspired from the ST example
 * @date    03-10-2023
 ******************************************************************************
 */


#include "Bluetooth_low_energy.h"

// Variables globales
volatile uint8_t BLE_connected = FALSE; // Indique si le périphérique est connecté en BLE
volatile uint8_t set_connectable = 1;    // Permet de rendre le périphérique connectable
volatile uint16_t connection_handle = 0;  // Gère la poignée de connexion BLE
volatile uint8_t notification_enabled = FALSE; // Indique si les notifications sont activées
volatile uint8_t start_read_tx_char_handle = FALSE; // Indique le début de la lecture d'une caractéristique TX
volatile uint8_t start_read_rx_char_handle = FALSE; // Indique le début de la lecture d'une caractéristique RX
volatile uint8_t end_read_tx_char_handle = FALSE;   // Indique la fin de la lecture d'une caractéristique TX
volatile uint8_t end_read_rx_char_handle = FALSE;   // Indique la fin de la lecture d'une caractéristique RX

uint16_t tx_handle_params;   // Gère la poignée de caractéristique TX
uint16_t rx_handle;          // Gère la poignée de caractéristique RX
uint16_t sampleServHandle, TXParCharHandle, RXCharHandle; // Gère les poignées de service et de caractéristiques
tBleStatus ret;              // Stocke le statut BLE
uint8_t bnrg_expansion_board = IDB05A1; // Type de carte d'extension BLE (IDB05A1 par défaut)

// Fonction pour ajouter un service nRF UART
//  UUIDs:
//  NRF UART CHARACTERISTIC (https://developer.nordicsemi.com/nRF_Connect_SDK/doc/1.4.0/nrf/include/bluetooth/services/nus.html#characteristics)
//  Compatible with BLE Terminal, nRF connect, nRF UART... on Android (see in playstore)
//  Main UUID : 6E400001-B5A3-F393-­E0A9-­E50E24DCCA9E
//  TX		: 6E400002-B5A3-F393-­E0A9-­E50E24DCCA9E
//  RX		: 6E400003-B5A3-F393-­E0A9-­E50E24DCCA9E
tBleStatus Add_Sample_Service(void)
{
    const uint8_t UART_service_uuid[16] = {0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e};
    const uint8_t TxUuid[16] = {0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e};
    const uint8_t RxUuid[16] = {0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e};

    // Ajoute un service et ses caractéristiques
    ret = aci_gatt_add_serv(UUID_TYPE_128, UART_service_uuid, PRIMARY_SERVICE, 9, &sampleServHandle);
    if (ret != BLE_STATUS_SUCCESS) goto fail;

    ret = aci_gatt_add_char(sampleServHandle, UUID_TYPE_128, TxUuid, SIZEOF_TX, CHAR_PROP_NOTIFY | CHAR_PROP_READ, ATTR_PERMISSION_NONE, 0, 16, 1, &TXParCharHandle);
    if (ret != BLE_STATUS_SUCCESS) goto fail;

    ret = aci_gatt_add_char(sampleServHandle, UUID_TYPE_128, RxUuid, SIZEOF_RX, CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RESP, ATTR_PERMISSION_NONE, GATT_NOTIFY_ATTRIBUTE_WRITE, 16, 1, &RXCharHandle);
    if (ret != BLE_STATUS_SUCCESS) goto fail;

    return BLE_STATUS_SUCCESS;

fail:
    return BLE_STATUS_ERROR;
}

// Fonction pour rendre le périphérique connectable
void Make_Connection(void)
{
    const char local_name[] = {AD_TYPE_COMPLETE_LOCAL_NAME, 'U', 'A', 'R', 'T', '-', 'B', 'L', 'E'};

    // Désactive la réponse de balayage
    hci_le_set_scan_resp_data(0, NULL);

    // Configure le périphérique pour être découvrable
    ret = aci_gap_set_discoverable(ADV_DATA_TYPE, ADV_INTERV_MIN, ADV_INTERV_MAX, PUBLIC_ADDR, NO_WHITE_LIST_USE, sizeof(local_name), local_name, 0, NULL, 0, 0);
}

// Fonction pour recevoir des données
void receiveData(uint8_t* data_buffer, uint8_t Nb_bytes, uint16_t CharHandle)
{
    BLE_command_received(data_buffer);
}

// Fonction pour envoyer des données
void sendData(uint8_t* data_buffer, uint8_t Nb_bytes, uint16_t CharHandle)
{
    ret = aci_gatt_update_char_value(sampleServHandle, CharHandle, 0, Nb_bytes, data_buffer);
}

// Fonction pour envoyer des données avec un statut de retour
tBleStatus sendDataDef(uint8_t* data_buffer, uint8_t Nb_bytes)
{
    ret = aci_gatt_update_char_value(sampleServHandle, TXParCharHandle, 0, Nb_bytes, data_buffer);
    return ret;
}

// Fonction pour activer les notifications
void enableNotification(void)
{
    uint8_t client_char_conf_data[] = {0x01, 0x00};
    uint32_t tickstart = HAL_GetTick();

    // Active les notifications
    while (aci_gatt_write_charac_descriptor(connection_handle, tx_handle_params + 2, 2, client_char_conf_data) == BLE_STATUS_NOT_ALLOWED)
    {
        if ((HAL_GetTick() - tickstart) > (10 * HCI_DEFAULT_TIMEOUT_MS)) break;
    }

    notification_enabled = TRUE;
}

// Fonction pour gérer les attributs modifiés
void Attribute_Modified_CB(uint16_t handle, uint8_t data_length, uint8_t *att_data)
{
    if (handle == RXCharHandle + 1)
    {
        receiveData(att_data, data_length, handle);
    }
    else if (handle == TXParCharHandle + 2)
    {
        if (att_data[0] == 0x01)
            notification_enabled = TRUE;
    }
}

// Fonction appelée lorsqu'une connexion est établie
void GAP_ConnectionComplete_CB(uint8_t addr[6], uint16_t handle)
{
    BLE_connected = TRUE;
    connection_handle = handle;
}

// Fonction appelée lorsqu'une déconnexion se produit
void GAP_DisconnectionComplete_CB(void)
{
    BLE_connected = FALSE;
    set_connectable = TRUE;
    notification_enabled = FALSE;
    start_read_tx_char_handle = FALSE;
    start_read_rx_char_handle = FALSE;
    end_read_tx_char_handle = FALSE;
    end_read_rx_char_handle = FALSE;
}

// Fonction appelée lorsqu'il y a une notification du serveur
void GATT_Notification_CB(uint16_t attr_handle, uint8_t attr_len, uint8_t *attr_value)
{
    if (attr_handle == tx_handle_params + 1)
    {
        receiveData(attr_value, attr_len, TXParCharHandle);
    }
}

// Fonction pour traiter les événements ACI
void user_notify(void *pData)
{
    hci_uart_pckt *hci_pckt = pData;
    hci_event_pckt *event_pckt = (hci_event_pckt *)hci_pckt->data;

    if (hci_pckt->type != HCI_EVENT_PKT)
        return;

    switch (event_pckt->evt)
    {
        // Gestion de la déconnexion
        case EVT_DISCONN_COMPLETE:
        {
            GAP_DisconnectionComplete_CB();
        }
        break;

        // Gestion des événements LE Meta
        case EVT_LE_META_EVENT:
        {
            evt_le_meta_event *evt = (void *)event_pckt->data;

            switch (evt->subevent)
            {
                // Gestion de la connexion BLE établie
                case EVT_LE_CONN_COMPLETE:
                {
                    evt_le_connection_complete *cc = (void *)evt->data;
                    GAP_ConnectionComplete_CB(cc->peer_bdaddr, cc->handle);
                }
                break;
            }
        }
        break;

        // Gestion des événements du fournisseur
        case EVT_VENDOR:
        {
            evt_blue_aci *blue_evt = (void *)event_pckt->data;

            switch (blue_evt->ecode)
            {
                // Gestion de la modification d'attribut GATT
                case EVT_BLUE_GATT_ATTRIBUTE_MODIFIED:
                {
                    if (bnrg_expansion_board == IDB05A1)
                    {
                        evt_gatt_attr_modified_IDB05A1 *evt = (evt_gatt_attr_modified_IDB05A1 *)blue_evt->data;
                        Attribute_Modified_CB(evt->attr_handle, evt->data_length, evt->att_data);
                    }
                    else
                    {
                        evt_gatt_attr_modified_IDB04A1 *evt = (evt_gatt_attr_modified_IDB04A1 *)blue_evt->data;
                        Attribute_Modified_CB(evt->attr_handle, evt->data_length, evt->att_data);
                    }
                }
                break;
                // Gestion des notifications GATT
                case EVT_BLUE_GATT_NOTIFICATION:
                {
                    evt_gatt_attr_notification *evt = (evt_gatt_attr_notification *)blue_evt->data;
                    GATT_Notification_CB(evt->attr_handle, evt->event_data_length - 2, evt->attr_value);
                }
                break;
            }
        }
        break;
    }
}

// Initialisation BlueNRG-MS
void MX_BlueNRG_MS_Init(void)
{
    uint8_t bdaddr[BD_ADDR_SIZE];
    uint16_t service_handle, dev_name_char_handle, appearance_char_handle;
    uint8_t hwVersion;
    uint16_t fwVersion;

    hci_init(user_notify, NULL);
    getBlueNRGVersion(&hwVersion, &fwVersion);
    hci_reset();
    HAL_Delay(100);

    if (hwVersion > 0x30)
    {
        bnrg_expansion_board = IDB05A1;
    }

    // Génère une adresse MAC aléatoire pour les octets 2 à 6
    for (int i = 1; i < BD_ADDR_SIZE; i++)
    {
        bdaddr[i] = rand() % 256;
    }

    // Le premier octet dépend du type d'adresse (publique ou privée)
    // Pour une adresse privée, le bit LSB doit être 1, donc le premier octet est impair (bit de poids faible à 1)
    bdaddr[0] = (bdaddr[0] & 0xFE) | 0x01; // Assure que le premier octet est impair

    ret = aci_hal_write_config_data(CONFIG_DATA_PUBADDR_OFFSET, CONFIG_DATA_PUBADDR_LEN, bdaddr);
    ret = aci_gatt_init();

    if (bnrg_expansion_board == IDB05A1)
    {
        ret = aci_gap_init_IDB05A1(GAP_PERIPHERAL_ROLE_IDB05A1, 0, 0x07, &service_handle, &dev_name_char_handle, &appearance_char_handle);
    }
    else
    {
        ret = aci_gap_init_IDB04A1(GAP_PERIPHERAL_ROLE_IDB04A1, &service_handle, &dev_name_char_handle, &appearance_char_handle);
    }

    ret = aci_gap_set_auth_requirement(MITM_PROTECTION_REQUIRED, OOB_AUTH_DATA_ABSENT, NULL, 7, 16, USE_FIXED_PIN_FOR_PAIRING, 123456, BONDING);
    ret = Add_Sample_Service();
    ret = aci_hal_set_tx_power_level(1, 4);
}

/****END OF FILE****/
