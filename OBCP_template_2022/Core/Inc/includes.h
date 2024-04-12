/*
 * includes.h
 *
 *  Created on: 20 nov. 2022
 *      Author: Joel IMBAUD
 *      SUPMICROTECH ENSMM www.supmicrotech.fr
 */

#ifndef SRC_INCLUDES_H_
#define SRC_INCLUDES_H_

//Definitions

#define BLUE_BUTTON_PORT 	GPIOC
#define BLUE_BUTTON_PIN 	GPIO_PIN_13
#define BDADDR_SIZE 6

/* Includes ------------------------------------------------------------------*/

// General
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

// HAL drivers

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

	//MCU
	#include "stm32l4xx_hal.h"
	#include "tim.h"
	#include "gpio.h"
	#include "spi.h"

	//USB
	#include "usbd_cdc_if.h"

// Template files

#include "main.h"
#include "Driver_OBCP.h"
#include "LIS3DSH.h"


#endif /* SRC_INCLUDES_H_ */



