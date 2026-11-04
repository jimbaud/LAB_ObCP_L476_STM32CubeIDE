/*
 * Fausse HAL STM32L4 pour compiler l'application sur PC (test_sim) :
 * uniquement les types et fonctions utilisés hors de Core/ et des modules
 * remplacés (console, io, config, board, passerelle UART).
 */
#ifndef FAKE_STM32L4XX_HAL_H
#define FAKE_STM32L4XX_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct { uint32_t idr; } GPIO_TypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
extern GPIO_TypeDef fake_gpioa, fake_gpiob, fake_gpioc;
#define GPIOA (&fake_gpioa)
#define GPIOB (&fake_gpiob)
#define GPIOC (&fake_gpioc)
#define GPIO_PIN_0   0x0001U
#define GPIO_PIN_1   0x0002U
#define GPIO_PIN_2   0x0004U
#define GPIO_PIN_3   0x0008U
#define GPIO_PIN_4   0x0010U
#define GPIO_PIN_5   0x0020U
#define GPIO_PIN_6   0x0040U
#define GPIO_PIN_7   0x0080U
#define GPIO_PIN_8   0x0100U
#define GPIO_PIN_9   0x0200U
#define GPIO_PIN_10  0x0400U
#define GPIO_PIN_11  0x0800U
#define GPIO_PIN_12  0x1000U
#define GPIO_PIN_13  0x2000U
#define GPIO_PIN_14  0x4000U
#define GPIO_PIN_15  0x8000U

typedef struct { int dummy; } UART_HandleTypeDef;
typedef struct { int dummy; } SPI_HandleTypeDef;
typedef struct { int dummy; } ADC_HandleTypeDef;
typedef struct { int dummy; } TIM_HandleTypeDef;
typedef struct { int dummy; } RTC_HandleTypeDef;

#define SPI_BAUDRATEPRESCALER_8  0x10U
#define TIM_CHANNEL_1            0x00U
#define TIM_CHANNEL_3            0x08U
#define TIM_CHANNEL_4            0x0CU

uint32_t      HAL_GetTick(void);
void          HAL_Delay(uint32_t ms);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
uint32_t      HAL_RCC_GetSysClockFreq(void);

#endif
