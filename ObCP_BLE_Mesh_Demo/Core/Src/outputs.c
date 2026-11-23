/**
 ******************************************************************************
 * @file    outputs.c
 * @brief   Sorties PWM : LED tricolore et sortie moteur (voir outputs.h).
 ******************************************************************************
 */
#include "outputs.h"

#include "tim.h"            /* htim1, htim3 : minuteries configurées par CubeMX */

/* Minuterie et voie de chaque sortie, dans l'ordre de output_t. */
typedef struct
{
  TIM_HandleTypeDef *htim;      /* minuterie (« handle » de la HAL) */
  uint32_t           channel;   /* voie : TIM_CHANNEL_1... */
} pwm_channel_t;

static const pwm_channel_t s_channels[OUTPUT_COUNT] =
{
  [OUTPUT_RED]   = { &htim3, TIM_CHANNEL_1 },
  [OUTPUT_GREEN] = { &htim3, TIM_CHANNEL_3 },
  [OUTPUT_BLUE]  = { &htim3, TIM_CHANNEL_4 },
  [OUTPUT_MOTOR] = { &htim1, TIM_CHANNEL_3 },
};

static uint16_t s_values[OUTPUT_COUNT];   /* dernières valeurs réglées */

/* Voir outputs.h */
void outputs_init(void)
{
  outputs_all_off();                     /* rapport cyclique nul avant de démarrer */

  /* Les LED sont sur les sorties « normales » de TIM3 (CH1, CH3, CH4). */
  (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
  (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
  (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);

  /* La broche PB1 est la sortie « complémentaire » CH3N de TIM1 : elle se
   * démarre avec la fonction HAL_TIMEx_PWMN_Start (N comme « negated »).
   * Seule CH3N est utilisée, elle suit donc directement le rapport cyclique.
   * TIM1 est une minuterie « avancée » : la fonction active aussi sa sortie
   * principale (bit MOE), sans laquelle la broche resterait inactive.    */
  (void)HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
}

/* Voir outputs.h */
void outputs_set(output_t out, uint16_t value)
{
  if (out >= OUTPUT_COUNT)
  {
    return;
  }
  s_values[out] = value;
  /* Le registre de comparaison CCR fixe l'instant où la sortie repasse à 0 :
   * la sortie vaut 1 tant que le compteur (0 à 65535) est inférieur à CCR.
   * Avec la « précharge » (preload) activée dans CubeMX, la nouvelle valeur
   * n'est prise qu'au début de la période suivante : pas d'impulsion
   * tronquée au moment du changement.                                    */
  __HAL_TIM_SET_COMPARE(s_channels[out].htim, s_channels[out].channel, value);
}

/* Voir outputs.h */
uint16_t outputs_get(output_t out)
{
  return (out < OUTPUT_COUNT) ? s_values[out] : 0U;
}

/* Voir outputs.h */
void outputs_all_off(void)
{
  for (int i = 0; i < (int)OUTPUT_COUNT; i++)
  {
    outputs_set((output_t)i, 0U);
  }
}
