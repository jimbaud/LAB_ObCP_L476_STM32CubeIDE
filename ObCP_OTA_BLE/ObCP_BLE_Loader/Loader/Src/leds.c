/**
 ******************************************************************************
 * @file    leds.c
 * @brief   Clignotements non bloquants de la LED RVB (voir leds.h).
 ******************************************************************************
 *
 *  Sur l'ObCP, chaque couleur de la LED est commandée par un transistor :
 *  broche à 1 = couleur allumée. Le chargeur utilise les broches en simples
 *  sorties numériques (PC6 rouge, PC8 vert, PC9 bleu), sans PWM.
 ******************************************************************************
 */

#include <stdbool.h>

#include "main.h"     /* HAL_GPIO_WritePin, HAL_GetTick, LED_xxx_Pin */
#include "leds.h"

#define FLASH_ON_MS        100U    /* durée de l'éclair en attente      */
#define FLASH_PERIOD_MS    1000U   /* période de l'éclair en attente    */
#define FAULT_PERIOD_MS    200U    /* période des clignotements rapides */

static leds_mode_t mode = LEDS_OFF;
static uint32_t    mode_start = 0U;     /* instant du changement de mode */
static bool        green_state = false; /* état de la LED verte en transfert */

/* Allume ou éteint chacune des trois couleurs. */
static void leds_write(bool red, bool green, bool blue)
{
    HAL_GPIO_WritePin(LED_RED_GPIO_Port,   LED_RED_Pin,   red   ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, green ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port,  LED_BLUE_Pin,  blue  ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void leds_set_mode(leds_mode_t new_mode)
{
    if (new_mode != mode)
    {
        mode = new_mode;
        mode_start = HAL_GetTick();
        green_state = true;            /* le transfert commence vert allumé */
        leds_process();                /* affichage immédiat */
    }
}

void leds_pulse(void)
{
    green_state = !green_state;
}

void leds_process(void)
{
    /* Temps écoulé depuis le changement de mode, ramené à une période. */
    uint32_t elapsed = HAL_GetTick() - mode_start;
    bool flash = (elapsed % FLASH_PERIOD_MS) < FLASH_ON_MS;
    bool fault = (elapsed % FAULT_PERIOD_MS) < (FAULT_PERIOD_MS / 2U);

    switch (mode)
    {
    case LEDS_WAITING:        leds_write(false, false, flash);       break;
    case LEDS_WAITING_NO_APP: leds_write(flash, false, flash);       break;
    case LEDS_CONNECTED:      leds_write(false, false, true);        break;
    case LEDS_TRANSFER:       leds_write(false, green_state, false); break;
    case LEDS_SUCCESS:        leds_write(false, true, false);        break;
    case LEDS_ERROR:          leds_write(true, false, false);        break;
    case LEDS_FAULT:          leds_write(fault, false, false);       break;
    case LEDS_IDENTIFY:       leds_write(fault, fault, fault);       break;
    case LEDS_OFF:
    default:                  leds_write(false, false, false);       break;
    }
}
