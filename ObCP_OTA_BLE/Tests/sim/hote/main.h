/**
 * @file    main.h  (imitation pour les tests sur PC)
 * @brief   Mêmes noms de broches que Core/Inc/main.h du chargeur (générés par
 *          CubeMX). UID_BASE désigne ici un faux numéro de série en mémoire.
 */

#ifndef MAIN_H
#define MAIN_H

#include "stm32l4xx_hal.h"

#define USER_BUTTON_Pin         GPIO_PIN_13
#define USER_BUTTON_GPIO_Port   GPIOC
#define BLE_RST_Pin             GPIO_PIN_2
#define BLE_RST_GPIO_Port       GPIOC
#define EEPROM_CS_Pin           GPIO_PIN_3
#define EEPROM_CS_GPIO_Port     GPIOC
#define BLE_CS_Pin              GPIO_PIN_2
#define BLE_CS_GPIO_Port        GPIOB
#define BLE_IRQ_Pin             GPIO_PIN_12
#define BLE_IRQ_GPIO_Port       GPIOB
#define BLE_IRQ_EXTI_IRQn       EXTI15_10_IRQn
#define LED_RED_Pin             GPIO_PIN_6
#define LED_RED_GPIO_Port       GPIOC
#define LED_GREEN_Pin           GPIO_PIN_8
#define LED_GREEN_GPIO_Port     GPIOC
#define LED_BLUE_Pin            GPIO_PIN_9
#define LED_BLUE_GPIO_Port      GPIOC

extern const uint8_t fake_uid[12];
#define UID_BASE                ((uintptr_t)fake_uid)

#endif /* MAIN_H */
