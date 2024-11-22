/**
 ******************************************************************************
 * @file    BLE_service.c
 * @brief   Implémentation des services Bluetooth Low Energy pour STM32.
 * @author  Joel IMBAUD
 * @date    20-11-2024
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
// Inclusion des fichiers nécessaires à la configuration et à la gestion du BLE.
#include <includes.h>

/* Private variables ---------------------------------------------------------*/
// Variables globales pour gérer l'état de la connexion BLE et les handles des caractéristiques.
volatile uint8_t BLE_connected = FALSE;             // Indique si le périphérique est connecté via BLE.
volatile uint8_t set_connectable = 1;               // Permet de rendre le périphérique connectable.
volatile uint16_t connection_handle = 0;            // Handle de connexion BLE actuel.
volatile uint8_t notification_enabled = FALSE;      // Indique si les notifications BLE sont activées.
volatile uint8_t start_read_tx_char_handle = FALSE; // Début de lecture du handle TX.
volatile uint8_t start_read_rx_char_handle = FALSE; // Début de lecture du handle RX.
volatile uint8_t end_read_tx_char_handle = FALSE;   // Fin de lecture du handle TX.
volatile uint8_t end_read_rx_char_handle = FALSE;   // Fin de lecture du handle RX.

uint8_t bdaddr[] = {0x00, 0x00, 0x00, 0xE1, 0x80, 0x02};
uint16_t tx_handle_params;                          // Handle pour la caractéristique TX.
uint16_t rx_handle;                                 // Handle pour la caractéristique RX.
uint16_t sampleServHandle, TXParCharHandle, RXCharHandle; // Handles pour le service et les caractéristiques.
tBleStatus ret;                                     // Statut des opérations BLE.

/**
 * @brief  Ajoute un service BLE avec des caractéristiques TX et RX.
 * @details Cette fonction configure un service UART BLE compatible avec des outils
 *          comme nRF Connect. Elle crée également des caractéristiques pour envoyer
 *          (TX) et recevoir (RX) des données.
 * @return BLE_STATUS_SUCCESS en cas de succès, sinon BLE_STATUS_ERROR.
 */
tBleStatus Add_UART_BLE_Service(void)
{
	uint8_t step = 0; // Variable pour suivre les étapes d'ajout.

	// Définition des UUIDs pour le service et ses caractéristiques.
	/*
	 * UUIDs:
	 * NRF UART CHARACTERISTIC (https://developer.nordicsemi.com/nRF_Connect_SDK/doc/1.4.0/nrf/include/bluetooth/services/nus.html#characteristics)
	 * Compatible with BLE Terminal, nRF connect, nRF UART... on Android (see in playstore)
	 * Main UUID : 6E400001-B5A3-F393-­E0A9-­E50E24DCCA9E
	 * TX		: 6E400002-B5A3-F393-­E0A9-­E50E24DCCA9E
	 * RX		: 6E400003-B5A3-F393-­E0A9-­E50E24DCCA9E
	 */
	const uint8_t UART_service_uuid[16] = {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x6e};
	const uint8_t TxUuid[16] = {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x02,0x00,0x40,0x6e};
	const uint8_t RxUuid[16] = {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x03,0x00,0x40,0x6e};

	// Ajout du service principal BLE.
	step++;
	ret = aci_gatt_add_serv(UUID_TYPE_128, UART_service_uuid, PRIMARY_SERVICE, 9, &sampleServHandle);
	if (ret != BLE_STATUS_SUCCESS) goto fail;

	// Ajout de la caractéristique TX (envoi de données).
	step++;
	ret = aci_gatt_add_char(sampleServHandle, UUID_TYPE_128, TxUuid, SIZEOF_TX,
			CHAR_PROP_NOTIFY | CHAR_PROP_READ, ATTR_PERMISSION_NONE, 0,
			16, 1, &TXParCharHandle);
	if (ret != BLE_STATUS_SUCCESS) goto fail;

	// Ajout de la caractéristique RX (réception de données).
	step++;
	ret = aci_gatt_add_char(sampleServHandle, UUID_TYPE_128, RxUuid, SIZEOF_RX,
			CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RESP, ATTR_PERMISSION_NONE,
			GATT_NOTIFY_ATTRIBUTE_WRITE, 16, 1, &RXCharHandle);
	if (ret != BLE_STATUS_SUCCESS) goto fail;

	return BLE_STATUS_SUCCESS; // Service ajouté avec succès.

	fail:
	return BLE_STATUS_ERROR; // Échec lors de l'ajout du service ou des caractéristiques.
}

/**
 * @brief  Configure le périphérique BLE pour qu'il soit visible et connectable.
 */
void Make_Connection(void)
{
	// Nom local qui sera utilisé dans les paquets de publicité BLE.
	const char local_name[] = {AD_TYPE_COMPLETE_LOCAL_NAME, 'O', 'B', 'C', 'P', '-', 'U', 'S', 'B', '-', 'B', 'L', 'E'};

	// Réinitialisation des données de réponse au scan.
	hci_le_set_scan_resp_data(0, NULL);

	// Configuration des paramètres de publicité BLE.
	aci_gap_set_discoverable(ADV_DATA_TYPE, ADV_INTERV_MIN, ADV_INTERV_MAX, PUBLIC_ADDR,
			NO_WHITE_LIST_USE, sizeof(local_name), local_name, 0, NULL, 0, 0);
}

/**
 * @brief  Fonction appelée lors de la réception de données via la caractéristique RX.
 * @param  data_buffer : Pointeur vers les données reçues.
 * @param  Nb_bytes    : Nombre d'octets reçus.
 * @param  CharHandle  : Handle de la caractéristique concernée.
 */
void receiveData(uint8_t* data_buffer, uint8_t Nb_bytes, uint16_t CharHandle)
{
	SendDataBLEToUSB(data_buffer); // Appelle une fonction externe pour traiter les données.
}

/**
 * @brief  Envoie des données via une caractéristique BLE.
 * @param  data_buffer : Pointeur vers les données à envoyer.
 * @param  Nb_bytes    : Nombre d'octets à envoyer.
 * @param  CharHandle  : Handle de la caractéristique concernée.
 */
void sendData(uint8_t* data_buffer, uint8_t Nb_bytes, uint16_t CharHandle)
{
	ret = aci_gatt_update_char_value(sampleServHandle, CharHandle, 0, Nb_bytes, data_buffer);
}

/**
 * @brief  Envoie des données via la caractéristique TX par défaut.
 * @param  data_buffer : Pointeur vers les données à envoyer.
 * @param  Nb_bytes    : Nombre d'octets à envoyer.
 * @return Statut de l'opération.
 */
tBleStatus sendDataDef(uint8_t* data_buffer, uint8_t Nb_bytes)
{
	ret = aci_gatt_update_char_value(sampleServHandle, TXParCharHandle, 0, Nb_bytes, data_buffer);
	return ret;
}

/**
 * @brief  Active les notifications sur une caractéristique.
 */
void enableNotification(void)
{
	uint8_t client_char_conf_data[] = {0x01, 0x00}; // Active les notifications.
	uint32_t tickstart = HAL_GetTick();

	// Tente d'écrire dans le descripteur pour activer les notifications.
	while (aci_gatt_write_charac_descriptor(connection_handle, tx_handle_params + 2, 2, client_char_conf_data) == BLE_STATUS_NOT_ALLOWED) {
		/* Vérifie si la radio est occupée */
		if ((HAL_GetTick() - tickstart) > (10 * HCI_DEFAULT_TIMEOUT_MS)) break; // Timeout après 10 intervalles.
	}

	notification_enabled = TRUE; // Indique que les notifications sont activées.
}

/**
 * @brief  Callback appelé lors de la modification d'un attribut GATT.
 * @param  handle      : Handle de l'attribut modifié.
 * @param  data_length : Taille des données modifiées.
 * @param  att_data    : Pointeur vers les données modifiées.
 */
void Attribute_Modified_CB(uint16_t handle, uint8_t data_length, uint8_t *att_data)
{
	if (handle == RXCharHandle + 1) {
		// Si les données concernent la caractéristique RX.
		receiveData(att_data, data_length, handle);
	} else if (handle == TXParCharHandle + 2) {
		// Si les données concernent la caractéristique TX.
		if (att_data[0] == 0x01) {
			notification_enabled = TRUE;
		}
	}
}

/**
 * @brief  Callback appelé lorsqu'une connexion BLE est établie.
 * @param  addr   : Adresse du périphérique connecté.
 * @param  handle : Handle de la connexion.
 */
void GAP_ConnectionComplete_CB(uint8_t addr[6], uint16_t handle)
{
	BLE_connected = TRUE; // Marque l'état comme connecté.
	connection_handle = handle;

	if (USB_Connected) {
		char Temp[64];
		snprintf(Temp, sizeof(Temp), "Generated BLE Address: %02X:%02X:%02X:%02X:%02X:%02X\n\r",
				bdaddr[5], bdaddr[4], bdaddr[3], bdaddr[2], bdaddr[1], bdaddr[0]);
		CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		HAL_Delay(5);
		snprintf(Temp, sizeof(Temp), "Connected to device: %02X:%02X:%02X:%02X:%02X:%02X\n\r",
				addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
		CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
	}
}

/**
 * @brief  Callback appelé lorsqu'une connexion BLE est terminée.
 * @details Réinitialise l'état de connexion et rend l'appareil de nouveau connectable.
 */
void GAP_DisconnectionComplete_CB(void)
{
	BLE_connected = FALSE; // Marque l'état comme déconnecté.
	set_connectable = TRUE; // Rend l'appareil de nouveau connectable.
	notification_enabled = FALSE; // Désactive les notifications.
	start_read_tx_char_handle = FALSE; // Réinitialise les lectures de handles TX et RX.
	start_read_rx_char_handle = FALSE;
	end_read_tx_char_handle = FALSE;
	end_read_rx_char_handle = FALSE;

	if (USB_Connected) {
		CDC_Transmit_FS((uint8_t*)"BLE disconnected :p\n\r", strlen("BLE disconnected :p\n\r"));
	}

	reset_OBCP_functions(); // Réinitialise les timers ou états associés aux fonctions internes.
}

/**
 * @brief  Callback appelé lorsqu'une notification GATT est reçue.
 * @param  attr_handle : Handle de l'attribut concerné par la notification.
 * @param  attr_len    : Longueur des données envoyées dans la notification.
 * @param  attr_value  : Pointeur vers les données de la notification.
 */
void GATT_Notification_CB(uint16_t attr_handle, uint8_t attr_len, uint8_t *attr_value)
{
	if (attr_handle == tx_handle_params + 1) {
		// Traite les données reçues via la caractéristique TX.
		receiveData(attr_value, attr_len, TXParCharHandle);
	}
}

/**
 * @brief  Fonction principale pour gérer les événements BLE.
 * @details Analyse les paquets d'événements et appelle les callbacks appropriés.
 * @param  pData : Pointeur vers les données du paquet HCI.
 */
void user_notify(void *pData)
{
	hci_uart_pckt *hci_pckt = pData; // Paquet HCI reçu via UART.
	hci_event_pckt *event_pckt = (hci_event_pckt*)hci_pckt->data; // Extraction du paquet d'événement.

	if (hci_pckt->type != HCI_EVENT_PKT)
		return; // Ignore si ce n'est pas un paquet d'événement HCI.

	// Gestion des différents types d'événements.
	switch (event_pckt->evt) {

	case EVT_DISCONN_COMPLETE:
		// Événement de déconnexion.
		GAP_DisconnectionComplete_CB();
		break;

	case EVT_LE_META_EVENT: {
		// Événements spécifiques à BLE.
		evt_le_meta_event *evt = (void *)event_pckt->data;

		switch (evt->subevent) {
		case EVT_LE_CONN_COMPLETE: {
			// Événement de connexion BLE réussie.
			evt_le_connection_complete *cc = (void *)evt->data;
			GAP_ConnectionComplete_CB(cc->peer_bdaddr, cc->handle);
		}
		break;
		}
	}
	break;

	case EVT_VENDOR: {
		// Événements spécifiques au fabricant.
		evt_blue_aci *blue_evt = (void*)event_pckt->data;

		switch (blue_evt->ecode) {

		case EVT_BLUE_GATT_ATTRIBUTE_MODIFIED: {
			// Modification d'un attribut GATT.
			evt_gatt_attr_modified_IDB05A1 *evt = (evt_gatt_attr_modified_IDB05A1*)blue_evt->data;
			Attribute_Modified_CB(evt->attr_handle, evt->data_length, evt->att_data);
		}
		break;

		case EVT_BLUE_GATT_NOTIFICATION: {
			// Notification GATT reçue.
			evt_gatt_attr_notification *evt = (evt_gatt_attr_notification*)blue_evt->data;
			GATT_Notification_CB(evt->attr_handle, evt->event_data_length - 2, evt->attr_value);
		}
		break;
		}
	}
	break;
	}
}

/**
 * @brief  Initialise les composants BLE de la pile BlueNRG.
 * @details Configure l'adresse MAC, initialise les services et les caractéristiques.
 */

void MX_BlueNRG_MS_Init(void)
{

	uint16_t service_handle, dev_name_char_handle, appearance_char_handle;
	uint8_t  bdaddr_len_out;
	//Initialisation des composants BLE.
	user_button_init_state = HAL_GPIO_ReadPin(BLUE_BUTTON_PORT, BLUE_BUTTON_PIN);

	hci_init(user_notify, NULL);
	hci_reset();
	HAL_Delay(100);

	// Lecture de l'adresse MAC aléatoire stockée dans le module | commentez cette ligne pour une adresse fixe définie par bdaddr
	aci_hal_read_config_data(CONFIG_DATA_RANDOM_ADDRESS,CONFIG_DATA_PUBADDR_LEN, &bdaddr_len_out, bdaddr);
	// Configuration de l'adresse BLE.
	aci_hal_write_config_data(CONFIG_DATA_PUBADDR_OFFSET,CONFIG_DATA_PUBADDR_LEN, bdaddr);

	// Initialisation des services et des caractéristiques.
	aci_gatt_init();

	aci_gap_init_IDB05A1(GAP_PERIPHERAL_ROLE_IDB05A1, 0, 0x0c, &service_handle, &dev_name_char_handle, &appearance_char_handle);

	aci_gap_set_auth_requirement(MITM_PROTECTION_REQUIRED,
			OOB_AUTH_DATA_ABSENT,
			NULL,
			7,
			16,
			USE_FIXED_PIN_FOR_PAIRING,
			123456,
			BONDING);
	Add_UART_BLE_Service();
	aci_hal_set_tx_power_level(1, 4);
}
