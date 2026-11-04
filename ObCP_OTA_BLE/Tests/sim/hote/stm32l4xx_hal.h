/**
 * @file    stm32l4xx_hal.h  (imitation pour les tests sur PC)
 * @brief   Le strict nécessaire de la HAL STM32L4 pour compiler ble_ota.c et
 *          les en-têtes de la bibliothèque BlueNRG-MS sur un PC. Les fonctions
 *          sont écrites dans Tests/sim/test_ble_ota.c.
 */

#ifndef STM32L4XX_HAL_H
#define STM32L4XX_HAL_H

#include <stddef.h>
#include <stdint.h>

typedef enum { HAL_OK = 0x00U, HAL_ERROR = 0x01U, HAL_BUSY = 0x02U, HAL_TIMEOUT = 0x03U } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0U, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t unused; } GPIO_TypeDef;
typedef struct { uint32_t Line; void (*PendingCallback)(void); } EXTI_HandleTypeDef;
typedef int IRQn_Type;

#define EXTI15_10_IRQn  40

extern GPIO_TypeDef fake_gpioa, fake_gpiob, fake_gpioc;
#define GPIOA           (&fake_gpioa)
#define GPIOB           (&fake_gpiob)
#define GPIOC           (&fake_gpioc)

#define GPIO_PIN_2      ((uint16_t)0x0004U)
#define GPIO_PIN_3      ((uint16_t)0x0008U)
#define GPIO_PIN_6      ((uint16_t)0x0040U)
#define GPIO_PIN_8      ((uint16_t)0x0100U)
#define GPIO_PIN_9      ((uint16_t)0x0200U)
#define GPIO_PIN_12     ((uint16_t)0x1000U)
#define GPIO_PIN_13     ((uint16_t)0x2000U)

uint32_t      HAL_GetTick(void);
void          HAL_Delay(uint32_t delay_ms);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void          HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
void          HAL_NVIC_EnableIRQ(IRQn_Type irq);
void          HAL_NVIC_DisableIRQ(IRQn_Type irq);

#endif /* STM32L4XX_HAL_H */
