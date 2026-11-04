/**
 ******************************************************************************
 * @file    outputs.h
 * @brief   Sorties PWM de l'ObCP : LED tricolore (rouge, verte, bleue) et
 *          sortie de puissance du bornier J13 (« moteur »).
 *
 *  PWM (« Pulse Width Modulation », modulation de largeur d'impulsion) : la
 *  broche passe à 1 pendant une fraction de chaque période, puis à 0. Le
 *  rapport cyclique (la fraction à 1) règle la luminosité de la LED ou la
 *  puissance moyenne envoyée au moteur. Ici la période dure 1,02 ms
 *  (environ 977 Hz), trop rapide pour que l'œil voie le clignotement.
 *
 *  Matériel (schéma de l'ObCP V2.3) :
 *    sortie         broche   minuterie (timer)    commande   transistor
 *    LED rouge      PC6      TIM3 voie 1          r<n>       Q1 (NPN)
 *    LED verte      PC8      TIM3 voie 3          v<n>       Q2 (NPN)
 *    LED bleue      PC9      TIM3 voie 4          b<n>       Q3 (NPN)
 *    bornier J13    PB1      TIM1 voie 3N         m<n>       M1 (MOSFET), 5 V
 *
 *  Valeur n de 0 à 65535 : rapport cyclique = n / 65536 (0 = éteint,
 *  65535 = toujours à 1 ou presque). Les deux minuteries comptent de 0 à
 *  65535 à 64 MHz (réglages de STM32CubeMX : Prescaler = 0, Period = 65535),
 *  et le registre de comparaison CCR reçoit directement n.
 *
 *  Pour aller plus loin : wiki ST « Getting started with TIM » (minuteries
 *  et PWM), https://wiki.st.com/stm32mcu/wiki/Getting_started_with_TIM
 ******************************************************************************
 */
#ifndef OUTPUTS_H
#define OUTPUTS_H

#include <stdint.h>

/** Les quatre sorties PWM. */
typedef enum
{
  OUTPUT_RED = 0,    /**< LED rouge (TIM3, voie 1) */
  OUTPUT_GREEN,      /**< LED verte (TIM3, voie 3) */
  OUTPUT_BLUE,       /**< LED bleue (TIM3, voie 4) */
  OUTPUT_MOTOR,      /**< sortie du bornier J13 (TIM1, voie 3 complémentaire) */
  OUTPUT_COUNT       /**< nombre de sorties (pas une sortie) */
} output_t;

/**
 * @brief  Démarre les quatre PWM avec un rapport cyclique nul (tout éteint).
 *         À appeler une fois, après MX_TIM1_Init() et MX_TIM3_Init().
 */
void outputs_init(void);

/**
 * @brief  Règle une sortie.
 * @param  out    sortie (OUTPUT_RED...)
 * @param  value  0 (éteint) à 65535 (pleine puissance)
 */
void outputs_set(output_t out, uint16_t value);

/**
 * @brief  Dernière valeur réglée pour une sortie.
 */
uint16_t outputs_get(output_t out);

/**
 * @brief  Éteint les quatre sorties (commande « #rst » et déconnexion BLE).
 */
void outputs_all_off(void);

#endif /* OUTPUTS_H */
