/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.h
 * @brief          : Header for main.c file.
 *                   This file contains the common defines of the application.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2022 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_hal.h"

#include "hci_tl_interface.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */


/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define B1_Pin GPIO_PIN_13
#define B1_GPIO_Port GPIOC
#define B1_EXTI_IRQn EXTI15_10_IRQn
#define BLE_Reset_Pin GPIO_PIN_2
#define BLE_Reset_GPIO_Port GPIOC
#define USART_TX_Pin GPIO_PIN_2
#define USART_TX_GPIO_Port GPIOA
#define USART_RX_Pin GPIO_PIN_3
#define USART_RX_GPIO_Port GPIOA
#define ACC_CS_Pin GPIO_PIN_5
#define ACC_CS_GPIO_Port GPIOC
#define PWM_Motor_Pin GPIO_PIN_1
#define PWM_Motor_GPIO_Port GPIOB
#define BLE_Reset_Line_Pin GPIO_PIN_2
#define BLE_Reset_Line_GPIO_Port GPIOB
#define BLE_EXTI_Line_Pin GPIO_PIN_12
#define BLE_EXTI_Line_GPIO_Port GPIOB
#define BLE_EXTI_Line_EXTI_IRQn EXTI15_10_IRQn
#define BLE_SCK_Pin GPIO_PIN_13
#define BLE_SCK_GPIO_Port GPIOB
#define BLE_MISO_Pin GPIO_PIN_14
#define BLE_MISO_GPIO_Port GPIOB
#define BLE_MOSI_Pin GPIO_PIN_15
#define BLE_MOSI_GPIO_Port GPIOB
#define Led_Red_Pin GPIO_PIN_6
#define Led_Red_GPIO_Port GPIOC
#define Led_Green_Pin GPIO_PIN_8
#define Led_Green_GPIO_Port GPIOC
#define Led_Blue_Pin GPIO_PIN_9
#define Led_Blue_GPIO_Port GPIOC
#define TMS_Pin GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14
#define TCK_GPIO_Port GPIOA
#define ACC_SCK_Pin GPIO_PIN_10
#define ACC_SCK_GPIO_Port GPIOC
#define ACC_MISO_Pin GPIO_PIN_11
#define ACC_MISO_GPIO_Port GPIOC
#define ACC_MOSI_Pin GPIO_PIN_12
#define ACC_MOSI_GPIO_Port GPIOC

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
