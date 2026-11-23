/**
 ******************************************************************************
 * @file    hal_callbacks.c
 * @brief   Fonctions de rappel (« callbacks ») des interruptions de la HAL,
 *          rassemblées en un seul endroit.
 *
 *  Quand une interruption arrive, la HAL fait le travail commun (lire et
 *  effacer les drapeaux du périphérique) puis appelle une fonction de
 *  rappel. La HAL en fournit une version vide, déclarée « __weak » (faible) :
 *  écrire une fonction du même nom ici la remplace, sans toucher au code
 *  de ST ni au code généré par CubeMX.
 *
 *  Chaque rappel est partagé par tous les périphériques du même type
 *  (toutes les broches EXTI, toutes les minuteries) : on regarde donc
 *  d'abord QUI a déclenché l'interruption, puis on transmet au module
 *  concerné. Les rappels s'exécutent en mode interruption : ils doivent être
 *  très courts (lever un drapeau, compter) et ne jamais attendre.
 *
 *  Remarque : l'interruption du module Bluetooth (broche PB12, même ligne
 *  EXTI15_10 que le bouton) ne passe pas par HAL_GPIO_EXTI_Callback : la
 *  bibliothèque BlueNRG-MS enregistre sa propre fonction
 *  (hci_tl_lowlevel_isr, dans BlueNRG-MS/Target/hci_tl_interface.c).
 ******************************************************************************
 */
#include "main.h"
#include "button.h"
#include "timer_1s.h"

/**
 * @brief  Rappel des interruptions EXTI (fronts sur une broche).
 * @param  GPIO_Pin  broche qui a déclenché l'interruption
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == USER_BUTTON_Pin)
  {
    button_on_interrupt();
  }
}

/**
 * @brief  Rappel de fin de période d'une minuterie (débordement du compteur).
 * @param  htim  minuterie concernée
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM7)
  {
    timer_1s_on_interrupt();
  }
}
