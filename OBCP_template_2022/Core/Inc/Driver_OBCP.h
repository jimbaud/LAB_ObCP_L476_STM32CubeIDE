/*
 * Driver_OBCP.h
 *
 *  Created on: 20 nov. 2022
 *      Author: Joel IMBAUD
 *      SUPMICROTECH ENSMM www.supmicrotech.fr
 */

#ifndef INC_DRIVER_OBCP_H_
#define INC_DRIVER_OBCP_H_

/* Includes ------------------------------------------------------------------*/
#include <includes.h>

/* Variables ------------------------------------------------------------------*/
extern uint8_t Acc_timer;
extern uint8_t user_button_pressed;
extern uint32_t* USB_length;
extern uint8_t* UsbBuffer;
extern int iusb;
extern char UsbBuffer2[10];
extern uint8_t RxUSB_OK;
extern int8_t USB_Connected;
extern volatile uint8_t user_button_init_state ;
extern char help[];

/* Function Prototypes ------------------------------------------------------------------*/
void reset_OBCP_functions(void);

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);

void updateTemperatureChar(float temperature);

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);

void Printf_BLE(char* data_buffer);

void User_Process(void);

void Bluetooh_LE_Process(void);

void BLE_command_received(uint8_t* data_buffer);

void USBCmd(uint8_t* data_buffer);

void receiveDataUSB(void);

#endif /* INC_DRIVER_OBCP_H_ */




