/**
 ******************************************************************************
 * @file    timer_1s.c
 * @brief   Minuterie d'une seconde, TIM7 (voir timer_1s.h).
 ******************************************************************************
 */
#include "timer_1s.h"

#include <stdint.h>
#include "tim.h"            /* htim7 : minuterie configurée par CubeMX */

/* Compteur de secondes écrit par l'interruption, et dernière valeur vue par
 * la boucle principale. Chaque variable n'a qu'un seul « écrivain » : pas de
 * conflit possible entre l'interruption et la boucle.
 * volatile : le compilateur doit relire la variable en mémoire à chaque
 * fois, car elle peut changer à tout moment (dans l'interruption).        */
static volatile uint32_t s_ticks;
static uint32_t          s_ticks_seen;
static bool              s_running;

/* Voir timer_1s.h */
void timer_1s_start(void)
{
  if (s_running)
  {
    return;                       /* déjà lancée : rien à faire (comme en 2024) */
  }
  /* Compteur remis à 0 et drapeau d'interruption effacé : le premier tic
   * arrive une seconde pleine après la commande (la version 2024 reprenait
   * le comptage là où il s'était arrêté).                                */
  __HAL_TIM_SET_COUNTER(&htim7, 0U);
  __HAL_TIM_CLEAR_FLAG(&htim7, TIM_FLAG_UPDATE);
  s_ticks_seen = s_ticks;         /* oublier un tic resté en attente */
  if (HAL_TIM_Base_Start_IT(&htim7) == HAL_OK)
  {
    s_running = true;
  }
}

/* Voir timer_1s.h */
void timer_1s_stop(void)
{
  (void)HAL_TIM_Base_Stop_IT(&htim7);
  s_running = false;
  s_ticks_seen = s_ticks;
}

/* Voir timer_1s.h */
bool timer_1s_is_running(void)
{
  return s_running;
}

/* Voir timer_1s.h : appelée dans l'interruption, doit rester très courte. */
void timer_1s_on_interrupt(void)
{
  s_ticks++;
}

/* Voir timer_1s.h */
bool timer_1s_take_tick(void)
{
  uint32_t now = s_ticks;         /* une seule lecture de la variable partagée */

  if (now == s_ticks_seen)
  {
    return false;
  }
  s_ticks_seen = now;             /* plusieurs tics en retard comptent pour un */
  return s_running;
}
