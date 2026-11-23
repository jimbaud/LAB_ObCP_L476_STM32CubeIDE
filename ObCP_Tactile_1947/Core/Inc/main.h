/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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
#define USER_BUTTON_Pin GPIO_PIN_13
#define USER_BUTTON_GPIO_Port GPIOC
#define BLE_RST_Pin GPIO_PIN_2
#define BLE_RST_GPIO_Port GPIOC
#define EEPROM_CS_Pin GPIO_PIN_3
#define EEPROM_CS_GPIO_Port GPIOC
#define TFT_SCK_Pin GPIO_PIN_5
#define TFT_SCK_GPIO_Port GPIOA
#define TFT_MISO_Pin GPIO_PIN_6
#define TFT_MISO_GPIO_Port GPIOA
#define TFT_MOSI_Pin GPIO_PIN_7
#define TFT_MOSI_GPIO_Port GPIOA
#define ACC_CS_Pin GPIO_PIN_5
#define ACC_CS_GPIO_Port GPIOC
#define PWM_MOTOR_Pin GPIO_PIN_1
#define PWM_MOTOR_GPIO_Port GPIOB
#define BLE_CS_Pin GPIO_PIN_2
#define BLE_CS_GPIO_Port GPIOB
#define LED_RED_Pin GPIO_PIN_6
#define LED_RED_GPIO_Port GPIOC
#define TFT_DC_Pin GPIO_PIN_7
#define TFT_DC_GPIO_Port GPIOC
#define LED_GREEN_Pin GPIO_PIN_8
#define LED_GREEN_GPIO_Port GPIOC
#define TOUCH_SPI_CS_Pin GPIO_PIN_9
#define TOUCH_SPI_CS_GPIO_Port GPIOA
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
#define SD_CS_Pin GPIO_PIN_5
#define SD_CS_GPIO_Port GPIOB
#define TFT_CS_Pin GPIO_PIN_6
#define TFT_CS_GPIO_Port GPIOB
#define TOUCH_SCL_Pin GPIO_PIN_8
#define TOUCH_SCL_GPIO_Port GPIOB
#define TOUCH_SDA_Pin GPIO_PIN_9
#define TOUCH_SDA_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
