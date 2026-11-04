/**
 ******************************************************************************
 * @file    timer_1s.h
 * @brief   Minuterie d'une seconde (TIM7) des commandes « start timer » et
 *          « stop timer » : chaque seconde, l'application envoie
 *          l'accélération et la température au téléphone.
 *
 *  Réglage de TIM7 dans STM32CubeMX : horloge 64 MHz, Prescaler = 6399
 *  (division par 6399 + 1 = 6400 : 10 kHz), Period = 9999 (10 000 pas) :
 *  une interruption toutes les 10 000 / 10 kHz = 1 s exactement. La version
 *  2024 avait Prescaler = 6400, soit une période de 1,0001 s.
 *
 *  Partage du travail :
 *    - l'interruption de TIM7 (toutes les secondes) appelle
 *      timer_1s_on_interrupt() qui lève seulement un drapeau ;
 *    - la boucle principale appelle timer_1s_take_tick() et fait la mesure
 *      et l'envoi, qui prennent du temps et n'ont rien à faire dans une
 *      interruption.
 ******************************************************************************
 */
#ifndef TIMER_1S_H
#define TIMER_1S_H

#include <stdbool.h>

/**
 * @brief  Démarre la minuterie : premier « tic » une seconde plus tard.
 */
void timer_1s_start(void);

/**
 * @brief  Arrête la minuterie (un tic en attente est oublié).
 */
void timer_1s_stop(void);

/**
 * @brief  Indique si la minuterie tourne.
 */
bool timer_1s_is_running(void);

/**
 * @brief  À appeler par l'interruption de TIM7 (HAL_TIM_PeriodElapsedCallback,
 *         dans hal_callbacks.c) : note qu'une seconde s'est écoulée.
 */
void timer_1s_on_interrupt(void);

/**
 * @brief  Boucle principale : rend true une fois par seconde écoulée.
 */
bool timer_1s_take_tick(void);

#endif /* TIMER_1S_H */
