/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  *  Fichier GÉNÉRÉ par STM32CubeMX (ObCP_Adventure.ioc). Les noms des broches
  *  ci-dessous (« User Label » dans CubeMX : LCD_CS, LCD_DC, TOUCH_CS...) sont
  *  ceux qu'utilise Core/Src/oa_board.c : ne pas les renommer dans CubeMX sans
  *  modifier ce fichier (Tools/verif_cubemx.py le vérifie).
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
#define USART_TX_Pin GPIO_PIN_2
#define USART_TX_GPIO_Port GPIOA
#define USART_RX_Pin GPIO_PIN_3
#define USART_RX_GPIO_Port GPIOA
#define LCD_SCK_Pin GPIO_PIN_5
#define LCD_SCK_GPIO_Port GPIOA
#define LCD_MISO_Pin GPIO_PIN_6
#define LCD_MISO_GPIO_Port GPIOA
#define LCD_MOSI_Pin GPIO_PIN_7
#define LCD_MOSI_GPIO_Port GPIOA
#define ACC_CS_Pin GPIO_PIN_5
#define ACC_CS_GPIO_Port GPIOC
#define BLE_CS_Pin GPIO_PIN_2
#define BLE_CS_GPIO_Port GPIOB
#define EEPROM_SCK_Pin GPIO_PIN_13
#define EEPROM_SCK_GPIO_Port GPIOB
#define EEPROM_MISO_Pin GPIO_PIN_14
#define EEPROM_MISO_GPIO_Port GPIOB
#define EEPROM_MOSI_Pin GPIO_PIN_15
#define EEPROM_MOSI_GPIO_Port GPIOB
#define LED_RED_Pin GPIO_PIN_6
#define LED_RED_GPIO_Port GPIOC
#define LCD_DC_Pin GPIO_PIN_7
#define LCD_DC_GPIO_Port GPIOC
#define LED_GREEN_Pin GPIO_PIN_8
#define LED_GREEN_GPIO_Port GPIOC
#define LED_BLUE_Pin GPIO_PIN_9
#define LED_BLUE_GPIO_Port GPIOC
#define TOUCH_CS_Pin GPIO_PIN_9
#define TOUCH_CS_GPIO_Port GPIOA
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
#define LCD_CS_Pin GPIO_PIN_6
#define LCD_CS_GPIO_Port GPIOB
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
