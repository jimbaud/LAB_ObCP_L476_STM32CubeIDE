/*
 * Driver.h
 *
 *  Created on: 03 oct. 2023
 *      Author: Joel IMBAUD
 *      SUPMICROTECH ENSMM www.supmicrotech.fr
 */

#ifndef INC_DRIVER_H_
#define INC_DRIVER_H_

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "Bluetooth_low_energy.h"

/* Variables ------------------------------------------------------------------*/
extern uint8_t user_button_pressed;
extern volatile uint8_t user_button_init_state ;


/* Function Prototypes ------------------------------------------------------------------*/

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);

void Printf_BLE(char* data_buffer);

void User_Process(void);

void Bluetooh_LE_Process(void);

void BLE_command_received(uint8_t* data_buffer);


#endif /* INC_DRIVER_H_ */




