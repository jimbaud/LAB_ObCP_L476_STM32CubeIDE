/**
 ******************************************************************************
 * @file    BLE_service.c
 *      Author: Joel IMBAUD
 *      SUPMICROTECH ENSMM www.supmicrotech.fr
 *      inspired from the ST example
 * @date    20-11-2022
 ******************************************************************************
 */
/* Includes ------------------------------------------------------------------*/
#include <includes.h>

/* Private variables ---------------------------------------------------------*/
volatile uint8_t BLE_connected = FALSE;
volatile uint8_t set_connectable = 1;
volatile uint16_t connection_handle = 0;
volatile uint8_t notification_enabled = FALSE;
volatile uint8_t start_read_tx_char_handle = FALSE;
volatile uint8_t start_read_rx_char_handle = FALSE;
volatile uint8_t end_read_tx_char_handle = FALSE;
volatile uint8_t end_read_rx_char_handle = FALSE;

uint16_t tx_handle_params;
uint16_t rx_handle;
uint16_t sampleServHandle, TXParCharHandle, RXCharHandle;
tBleStatus ret;

/* Variables ---------------------------------------------------------*/
uint8_t bnrg_expansion_board = IDB05A1; /* at startup, suppose the X-NUCLEO-IDB0A1 is used */

/* Function prototypes -----------------------------------------------*/
// static void User_Process(void);
/**
 * @brief  Add a sample service using a vendor specific profile
 * @param  None
 * @retval Status
 */
tBleStatus Add_Sample_Service(void)
{
	uint8_t step = 0;

	/*
  UUIDs:
  NRF UART CHARACTERISTIC (https://developer.nordicsemi.com/nRF_Connect_SDK/doc/1.4.0/nrf/include/bluetooth/services/nus.html#characteristics)
  Compatible with BLE Terminal, nRF connect, nRF UART... on Android (see in playstore)
  Main UUID : 6E400001-B5A3-F393-­E0A9-­E50E24DCCA9E
  TX		: 6E400002-B5A3-F393-­E0A9-­E50E24DCCA9E
  RX		: 6E400003-B5A3-F393-­E0A9-­E50E24DCCA9E

	 */
	const uint8_t UART_service_uuid[16] = {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x6e};
	const uint8_t TxUuid[16] = {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x02,0x00,0x40,0x6e};
	const uint8_t RxUuid[16] = {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x03,0x00,0x40,0x6e};

	step++;
	ret = aci_gatt_add_serv(UUID_TYPE_128, UART_service_uuid, PRIMARY_SERVICE, 9, &sampleServHandle);
	if (ret != BLE_STATUS_SUCCESS) goto fail;

	step++;
	ret =  aci_gatt_add_char(sampleServHandle, UUID_TYPE_128, TxUuid, SIZEOF_TX, CHAR_PROP_NOTIFY|CHAR_PROP_READ, ATTR_PERMISSION_NONE, 0,
			16, 1, &TXParCharHandle);
	if (ret != BLE_STATUS_SUCCESS) goto fail;

	step++;
	ret =  aci_gatt_add_char(sampleServHandle, UUID_TYPE_128, RxUuid, SIZEOF_RX, CHAR_PROP_WRITE|CHAR_PROP_WRITE_WITHOUT_RESP, ATTR_PERMISSION_NONE, GATT_NOTIFY_ATTRIBUTE_WRITE,
			16, 1, &RXCharHandle);
	if (ret != BLE_STATUS_SUCCESS) goto fail;

	if (USB_Connected)
	{
		char Temp[70];
		sprintf(Temp, "Sample Service added.\n\rTX Char Handle %04X, RX Char Handle %04X\n\r", TXParCharHandle, RXCharHandle);
		CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
	}
	return BLE_STATUS_SUCCESS;

	fail:
	if (USB_Connected)
	{
		char Temp[64];
		sprintf(Temp, "Error while adding Sample Service @step: %i.\n\r", step);
		CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
	}
	return BLE_STATUS_ERROR ;
}

/**
 * @brief  Make the device connectable
 * @param  None
 * @retval None
 */
void Make_Connection(void)
{

	const char local_name[] = {AD_TYPE_COMPLETE_LOCAL_NAME,'C','R','O','C','-','O','B','C','P'};

	/* disable scan response */
	hci_le_set_scan_resp_data(0,NULL);
	if (USB_Connected)
	{
		char Temp[32];
		sprintf(Temp, "General Discoverable Mode ");
		CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
	}

	/*
    Advertising_Event_Type, Adv_Interval_Min, Adv_Interval_Max, Address_Type, Adv_Filter_Policy,
    Local_Name_Length, Local_Name, Service_Uuid_Length, Service_Uuid_List, Slave_Conn_Interval_Min,
    Slave_Conn_Interval_Max
	 */
	ret = aci_gap_set_discoverable(ADV_DATA_TYPE, ADV_INTERV_MIN, ADV_INTERV_MAX, PUBLIC_ADDR,
			NO_WHITE_LIST_USE, sizeof(local_name), local_name, 0, NULL, 0, 0);

	if (USB_Connected)
	{
		char Temp[32];
		sprintf(Temp, "%d\n\r",ret);
		CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
	}

}

/**
 * @brief  This function is used to receive data related to the sample service
 *         (received over the air from the remote board).
 * @param  data_buffer : pointer to store in received data
 * @param  Nb_bytes : number of bytes to be received
 * @retval None
 */
void receiveData(uint8_t* data_buffer, uint8_t Nb_bytes, uint16_t CharHandle)
{
	BLE_command_received(data_buffer);

}

/**
 * @brief  This function is used to send data related to a specified service
 *         (to be sent over the air to the remote board).
 * @param  data_buffer : pointer to data to be sent
 * @param  Nb_bytes : number of bytes to send
 * @retval None
 */
void sendData(uint8_t* data_buffer, uint8_t Nb_bytes, uint16_t CharHandle)
{
	//	tBleStatus
	ret = aci_gatt_update_char_value(sampleServHandle, CharHandle, 0, Nb_bytes, data_buffer);
	if (ret == HAL_OK){
		if (USB_Connected)
		{
			//			char Temp[32];
			//			sprintf(Temp, "DATA SENT\n\r");
			//			CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		}

	}
	else
	{
		if (USB_Connected)
		{
			char Temp[32];
			sprintf(Temp,"ERROR with code: %i\n\r", ret);
			CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		}

	}
}

/**
 * @brief  This function is used to send data related to the default characteristic
 *         (to be sent over the air to the remote board).
 * @param  data_buffer : pointer to data to be sent
 * @param  Nb_bytes : number of bytes to send
 * @retval None
 */
tBleStatus sendDataDef(uint8_t* data_buffer, uint8_t Nb_bytes)
{
	//	tBleStatus
	ret = aci_gatt_update_char_value(sampleServHandle, TXParCharHandle, 0, Nb_bytes, data_buffer);
	if (ret == HAL_OK){
		//		if (USB_Connected)
		//		{
		//			char Temp[32];
		//			sprintf(Temp,"DATA SENT\n\r");
		//			CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		//		}
	}
	else
	{
		if (USB_Connected)
		{
			char Temp[32];
			sprintf(Temp,"ERROR with code: %i\n\r", ret);
			CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		}
	}
	return ret;
}

/**
 * @brief  Enable notification
 * @param  None
 * @retval None
 */
void enableNotification(void)
{
	uint8_t client_char_conf_data[] = {0x01, 0x00}; // Enable notifications
	uint32_t tickstart = HAL_GetTick();
	while(aci_gatt_write_charac_descriptor(connection_handle, tx_handle_params+2, 2, client_char_conf_data)==BLE_STATUS_NOT_ALLOWED){
		/* Radio is busy */
		if ((HAL_GetTick() - tickstart) > (10*HCI_DEFAULT_TIMEOUT_MS)) break;
	}
	notification_enabled = TRUE;
}

/**
 * @brief  This function is called when an attribute gets modified
 * @param  handle : handle of the attribute
 * @param  data_length : size of the modified attribute data
 * @param  att_data : pointer to the modified attribute data
 * @retval None
 */
void Attribute_Modified_CB(uint16_t handle, uint8_t data_length, uint8_t *att_data)
{
	if(handle == RXCharHandle + 1)
	{
		receiveData(att_data, data_length, handle);
	}
	else if (handle == TXParCharHandle + 2)
	{
		if(att_data[0] == 0x01)
			notification_enabled = TRUE;
	}
}

/**
 * @brief  This function is called when there is a LE Connection Complete event.
 * @param  addr : Address of peer device
 * @param  handle : Connection handle
 * @retval None
 */
void GAP_ConnectionComplete_CB(uint8_t addr[6], uint16_t handle)
{
	BLE_connected = TRUE;
	connection_handle = handle;
	if (USB_Connected)
	{
		char Temp[32];
		while(CDC_Transmit_FS((uint8_t*)"Connected to device:", strlen("Connected to device:")));

		for(int i = 5; i > 0; i--)
		{
			sprintf(Temp,"%02X-", addr[i]);
			while(CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp)));
		}
		sprintf(Temp,"%02X\n\r", addr[0]);
		while(CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp)));
	}
}

/**
 * @brief  This function is called when the peer device get disconnected.
 * @param  None
 * @retval None
 */
void GAP_DisconnectionComplete_CB(void)
{
	BLE_connected = FALSE;
	/* Make the device connectable again. */
	set_connectable = TRUE;
	notification_enabled = FALSE;
	start_read_tx_char_handle = FALSE;
	start_read_rx_char_handle = FALSE;
	end_read_tx_char_handle = FALSE;
	end_read_rx_char_handle = FALSE;
	if (USB_Connected)
	{
		CDC_Transmit_FS((uint8_t*)"BLE disconnected :p\n\r", strlen("BLE disconnected :p\n\r"));
	}
	reset_OBCP_functions(); //reset all timers to zero
}

/**
 * @brief  This function is called when there is a notification from the sever.
 * @param  attr_handle Handle of the attribute
 * @param  attr_len    Length of attribute value in the notification
 * @param  attr_value  Attribute value in the notification
 * @retval None
 */
void GATT_Notification_CB(uint16_t attr_handle, uint8_t attr_len, uint8_t *attr_value)
{
	if (attr_handle == tx_handle_params+1) {
		receiveData(attr_value, attr_len, TXParCharHandle);
	}
}

/**
 * @brief  This function is called whenever there is an ACI event to be processed.
 * @note   Inside this function each event must be identified and correctly
 *         parsed.
 * @param  pData  Pointer to the ACI packet
 * @retval None
 */
void user_notify(void * pData)
{
	hci_uart_pckt *hci_pckt = pData;
	/* obtain event packet */
	hci_event_pckt *event_pckt = (hci_event_pckt*)hci_pckt->data;

	if(hci_pckt->type != HCI_EVENT_PKT)
		return;

	switch(event_pckt->evt){

	case EVT_DISCONN_COMPLETE:
	{
		GAP_DisconnectionComplete_CB();
	}
	break;

	case EVT_LE_META_EVENT:
	{
		evt_le_meta_event *evt = (void *)event_pckt->data;

		switch(evt->subevent){
		case EVT_LE_CONN_COMPLETE:
		{
			evt_le_connection_complete *cc = (void *)evt->data;
			GAP_ConnectionComplete_CB(cc->peer_bdaddr, cc->handle);
		}
		break;
		}
	}
	break;

	case EVT_VENDOR:
	{
		evt_blue_aci *blue_evt = (void*)event_pckt->data;
		switch(blue_evt->ecode){

		case EVT_BLUE_GATT_ATTRIBUTE_MODIFIED:
		{
			if (bnrg_expansion_board == IDB05A1) {
				evt_gatt_attr_modified_IDB05A1 *evt = (evt_gatt_attr_modified_IDB05A1*)blue_evt->data;
				Attribute_Modified_CB(evt->attr_handle, evt->data_length, evt->att_data);
			}
			else {
				evt_gatt_attr_modified_IDB04A1 *evt = (evt_gatt_attr_modified_IDB04A1*)blue_evt->data;
				Attribute_Modified_CB(evt->attr_handle, evt->data_length, evt->att_data);
			}

		}
		break;
		case EVT_BLUE_GATT_NOTIFICATION:
		{
			evt_gatt_attr_notification *evt = (evt_gatt_attr_notification*)blue_evt->data;
			GATT_Notification_CB(evt->attr_handle, evt->event_data_length - 2, evt->attr_value);
		}
		break;
		}
	}
	break;
	}
}

void MX_BlueNRG_MS_Init(void)
{
	/* Define the device's address */
	uint8_t bdaddr[BD_ADDR_SIZE] = {0xaa, 0x00, 0x00, 0xE1, 0x80, 0x02};;
	uint16_t service_handle, dev_name_char_handle, appearance_char_handle;

	uint8_t  hwVersion;
	uint16_t fwVersion;
	int ret;


	/* Get the User Button initial state */
	user_button_init_state = HAL_GPIO_ReadPin(BLUE_BUTTON_PORT, BLUE_BUTTON_PIN);

	/* Initialize the hci controller */
	hci_init(user_notify, NULL);

	/* get the BlueNRG HW and FW versions */
	getBlueNRGVersion(&hwVersion, &fwVersion);

	/*
	 * Reset BlueNRG again otherwise we won't
	 * be able to change its MAC address.
	 * aci_hal_write_config_data() must be the first
	 * command after reset otherwise it will fail.
	 */
	hci_reset();

	HAL_Delay(100);
	if (USB_Connected)
	{
		char Temp[32];
		sprintf(Temp,"HWver %d, FWver %d\n\r", hwVersion, fwVersion);
		CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
	}
	if (hwVersion > 0x30) { /* X-NUCLEO-IDB05A1 expansion board is used */
		bnrg_expansion_board = IDB05A1;
	}

	ret = aci_hal_write_config_data(CONFIG_DATA_PUBADDR_OFFSET, CONFIG_DATA_PUBADDR_LEN, bdaddr);
	if (ret) {
		if (USB_Connected)
		{
			char Temp[32];
			sprintf(Temp,"Setting BD_ADDR failed 0x%02x.\n\r", ret);
			CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		}
	}
	ret = aci_gatt_init();
	if (ret) {
		if (USB_Connected)
		{
			char Temp[32];
			sprintf(Temp,"GATT_Init failed.\n\r");
			CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		}
	}
	if (bnrg_expansion_board == IDB05A1) {
		ret = aci_gap_init_IDB05A1(GAP_PERIPHERAL_ROLE_IDB05A1, 0, 0x07, &service_handle, &dev_name_char_handle, &appearance_char_handle);
	}
	else {
		ret = aci_gap_init_IDB04A1(GAP_PERIPHERAL_ROLE_IDB04A1, &service_handle, &dev_name_char_handle, &appearance_char_handle);
	}

	if (ret != BLE_STATUS_SUCCESS) {
		if (USB_Connected)
		{
			char Temp[32];
			sprintf(Temp,"GAP_Init failed.\n\r");
			CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		}
	}

	ret = aci_gap_set_auth_requirement(MITM_PROTECTION_REQUIRED,
			OOB_AUTH_DATA_ABSENT,
			NULL,
			7,
			16,
			USE_FIXED_PIN_FOR_PAIRING,
			123456,
			BONDING);
	if (ret == BLE_STATUS_SUCCESS) {
		if (USB_Connected)
		{
			char Temp[32];
			sprintf(Temp,"BLE Stack Initialized.\n\r");
			CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		}
	}
	if (USB_Connected)
	{
		char Temp[32];
		sprintf(Temp,"SERVER: BLE Stack Initialized\n\r");
		CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
	}
	ret = Add_Sample_Service();

	if (ret == BLE_STATUS_SUCCESS)
	{
		if (USB_Connected)
		{
			char Temp[32];
			sprintf(Temp,"Service added successfully.\n\r");
			CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		}
	}
	else
	{
		if (USB_Connected)
		{
			char Temp[32];
			sprintf(Temp,"Error while adding service.\n\r");
			CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		}
	}

	ret = aci_hal_set_tx_power_level(1,4);
}

/****END OF FILE****/
