/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * Démonstrateur Wi-Fi 6 : carte ObCP (STM32L476RG) + X-NUCLEO-67W61M1.
  * Fichier généré par STM32CubeMX (ObCP_WiFi_Demo.ioc) : les étiquettes des
  * broches ci-dessous (« Private defines ») viennent du .ioc et sont reprises
  * par Core/Inc/board.h. Ne rien écrire hors des sections USER CODE.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#define User1_Pin GPIO_PIN_13
#define User1_GPIO_Port GPIOC
#define BLE_Reset_Pin GPIO_PIN_2
#define BLE_Reset_GPIO_Port GPIOC
#define NCP_UART_TX_Pin GPIO_PIN_2
#define NCP_UART_TX_GPIO_Port GPIOA
#define NCP_UART_RX_Pin GPIO_PIN_3
#define NCP_UART_RX_GPIO_Port GPIOA
#define NCP_SCK_Pin GPIO_PIN_5
#define NCP_SCK_GPIO_Port GPIOA
#define NCP_MISO_Pin GPIO_PIN_6
#define NCP_MISO_GPIO_Port GPIOA
#define NCP_MOSI_Pin GPIO_PIN_7
#define NCP_MOSI_GPIO_Port GPIOA
#define ACC_CS_Pin GPIO_PIN_5
#define ACC_CS_GPIO_Port GPIOC
#define PWM_Motor_Pin GPIO_PIN_1
#define PWM_Motor_GPIO_Port GPIOB
#define NCP_BOOT_Pin GPIO_PIN_10
#define NCP_BOOT_GPIO_Port GPIOB
#define Led_Red_Pin GPIO_PIN_6
#define Led_Red_GPIO_Port GPIOC
#define Led_Green_Pin GPIO_PIN_8
#define Led_Green_GPIO_Port GPIOC
#define Led_Blue_Pin GPIO_PIN_9
#define Led_Blue_GPIO_Port GPIOC
#define NCP_SW1_Pin GPIO_PIN_9
#define NCP_SW1_GPIO_Port GPIOA
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
#define NCP_RDY_Pin GPIO_PIN_3
#define NCP_RDY_GPIO_Port GPIOB
#define NCP_CHIP_EN_Pin GPIO_PIN_4
#define NCP_CHIP_EN_GPIO_Port GPIOB
#define NCP_CS_Pin GPIO_PIN_6
#define NCP_CS_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
