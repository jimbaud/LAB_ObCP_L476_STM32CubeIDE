/*
 * Bluetooth_low_energy.h
 *
 *  Created on: 03 oct. 2023
 *      Author: Joel IMBAUD
 *      SUPMICROTECH ENSMM www.supmicrotech.fr
 *      inspired from the ST example
 */

#ifndef INC_BLUETOOTH_LOW_ENERGY_H_
#define INC_BLUETOOTH_LOW_ENERGY_H_

/* Includes ------------------------------------------------------------------*/

// General
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "Driver.h"


//BLE
#include "bluenrg_gap.h"
#include "bluenrg_aci_const.h"
#include "hci.h"
#include "hci_le.h"
#include "sm.h"
#include "hci_tl.h"
#include "bluenrg_utils.h"
#include "bluenrg_gatt_server.h"
#include "bluenrg_gap_aci.h"
#include "bluenrg_gatt_aci.h"
#include "bluenrg_hal_aci.h"
#include "Bluetooth_low_energy.h"

#define IDB04A1 0
#define IDB05A1 1
#define SIZEOF_TX 20
#define SIZEOF_RX 20
// Tx buffer size must be at least as big as the Rx
#if SIZEOF_TX < SIZEOF_RX
#define SIZEOF_TX SIZEOF_RX
#endif

/* Private variables ---------------------------------------------------------*/

extern volatile uint8_t BLE_connected;
extern volatile uint8_t set_connectable;
extern volatile uint16_t connection_handle;
extern volatile uint8_t notification_enabled;
extern volatile uint8_t start_read_tx_char_handle;
extern volatile uint8_t start_read_rx_char_handle;
extern volatile uint8_t end_read_tx_char_handle;
extern volatile uint8_t end_read_rx_char_handle;
extern uint16_t tx_handle_params;
extern uint16_t rx_handle;
extern uint16_t sampleServHandle, TXParCharHandle, RXCharHandle;
extern tBleStatus ret;
extern volatile uint16_t EXTI_PORT;
extern uint8_t bnrg_expansion_board;

/* Function prototypes -----------------------------------------------*/


tBleStatus Add_Sample_Service(void);

void Make_Connection(void);

void receiveData(uint8_t* data_buffer, uint8_t Nb_bytes, uint16_t CharHandle);

void sendData(uint8_t* data_buffer, uint8_t Nb_bytes, uint16_t CharHandlebytes);

tBleStatus sendDataDef(uint8_t* data_buffer, uint8_t Nb_bytes);

void enableNotification(void);

void Attribute_Modified_CB(uint16_t handle, uint8_t data_length, uint8_t *att_data);

void GAP_ConnectionComplete_CB(uint8_t addr[6], uint16_t handle);

void GAP_DisconnectionComplete_CB(void);

void GATT_Notification_CB(uint16_t attr_handle, uint8_t attr_len, uint8_t *attr_value);

void user_notify(void * pData);

void MX_BlueNRG_MS_Init(void);

#endif /* INC_BLUETOOTH_LOW_ENERGY_H_ */
