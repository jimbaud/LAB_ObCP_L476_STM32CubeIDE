/**
 ******************************************************************************
 * @file    fault.c
 * @brief   Arrêt sur erreur grave, signalé par la LED rouge (voir fault.h).
 ******************************************************************************
 */
#include "fault.h"

#include <stdint.h>
#include "main.h"           /* HAL, étiquettes LED_xxx et PWM_MOTOR */

/* Attente de ms millisecondes, interruptions masquées.
 * La fonction HAL_Delay() ne marche plus ici : elle compte les interruptions
 * du SysTick, qui sont masquées. Mais le compteur du SysTick, lui, continue
 * de tourner : son drapeau COUNTFLAG passe à 1 à chaque fin de comptage
 * (toutes les millisecondes) et se remet à 0 quand on lit le registre.   */
static void wait_ms(uint32_t ms)
{
  if ((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) != 0U)
  {
    while (ms > 0U)
    {
      if ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) != 0U)
      {
        ms--;
      }
    }
  }
  else
  {
    /* SysTick pas encore lancé (erreur très tôt) : boucle d'attente
     * approximative (environ 1 ms par tour à 4 MHz, l'horloge de départ). */
    for (volatile uint32_t i = 0U; i < (ms * 1000U); i++)
    {
    }
  }
}

/* Voir fault.h
 * Registres du « System Control Block » du Cortex-M4 (PM0214, § 4.4) :
 *   SHCSR : bits d'activation des gestionnaires MemManage, BusFault et
 *           UsageFault (sinon ces fautes deviennent des HardFault) ;
 *   CCR   : bit DIV_0_TRP, faute UsageFault sur une division entière par
 *           zéro (le langage C ne définit pas son résultat).              */
void fault_init(void)
{
  SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
  SCB->CCR   |= SCB_CCR_DIV_0_TRP_Msk;
}

/* Voir fault.h */
void fault_signal(fault_t cause)
{
  GPIO_InitTypeDef gpio = {0};

  __disable_irq();                 /* plus aucune interruption : le programme est arrêté */

  /* Sécurité : moteur et LED à 0. Leurs broches, pilotées par TIM1 et TIM3
   * (fonction « alternative »), redeviennent des sorties ordinaires.      */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  HAL_GPIO_WritePin(PWM_MOTOR_GPIO_Port, PWM_MOTOR_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOC, LED_RED_Pin | LED_GREEN_Pin | LED_BLUE_Pin, GPIO_PIN_RESET);
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Pin   = PWM_MOTOR_Pin;
  HAL_GPIO_Init(PWM_MOTOR_GPIO_Port, &gpio);
  gpio.Pin   = LED_RED_Pin | LED_GREEN_Pin | LED_BLUE_Pin;
  HAL_GPIO_Init(GPIOC, &gpio);

  /* Clignotement sans fin : « cause » éclats, puis une pause. */
  for (;;)
  {
    for (int i = 0; i < (int)cause; i++)
    {
      HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
      wait_ms(150U);
      HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET);
      wait_ms(250U);
    }
    wait_ms(1200U);
  }
}
