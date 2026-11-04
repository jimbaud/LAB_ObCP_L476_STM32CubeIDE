/**
 * @file    stm32l4xx_nucleo_bus.h  (imitation pour les tests sur PC)
 * @brief   Seule la fonction BSP_GetTick est utilisée par ble_ota.c.
 */

#ifndef STM32L4XX_NUCLEO_BUS_H
#define STM32L4XX_NUCLEO_BUS_H

#include "stm32l4xx_hal.h"

int32_t BSP_GetTick(void);

#endif /* STM32L4XX_NUCLEO_BUS_H */
