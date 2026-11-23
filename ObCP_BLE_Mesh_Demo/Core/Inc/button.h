/**
 ******************************************************************************
 * @file    button.h
 * @brief   Bouton utilisateur SW1 (« User Push Button » du schéma), sur PC13.
 *
 *  Câblage : résistance de rappel R1 (4,7 kΩ) vers 3,3 V, le bouton relie
 *  PC13 à la masse. Bouton relâché : PC13 = 1 ; appuyé : PC13 = 0. Le front
 *  descendant (passage de 1 à 0) déclenche une interruption EXTI13.
 *
 *  Anti-rebond : un contact mécanique « rebondit » (plusieurs passages 1/0
 *  pendant quelques ms). L'interruption ne fait que signaler l'activité ;
 *  la boucle principale vérifie ensuite que le bouton est resté appuyé
 *  BUTTON_DEBOUNCE_MS (30 ms) avant de compter un appui, puis attend qu'il
 *  soit relâché aussi longtemps avant d'accepter l'appui suivant. Tout cela
 *  sans attente bloquante : la version 2024 bloquait la boucle principale
 *  tant que le bouton restait enfoncé (USB et Bluetooth non servis).
 *
 *      relâché ──appui──> confirmation ──30 ms appuyé──> APPUI SIGNALÉ
 *         ^                (30 ms)                            │
 *         └──── 30 ms relâché ──── attente du relâchement <───┘
 *
 *  Pour aller plus loin : wiki ST « Getting started with EXTI »,
 *  https://wiki.st.com/stm32mcu/wiki/Getting_started_with_EXTI
 ******************************************************************************
 */
#ifndef BUTTON_H
#define BUTTON_H

#include <stdbool.h>

/**
 * @brief  Prépare la détection (à appeler une fois au démarrage).
 */
void button_init(void);

/**
 * @brief  À appeler par l'interruption EXTI du bouton
 *         (HAL_GPIO_EXTI_Callback, dans hal_callbacks.c).
 */
void button_on_interrupt(void);

/**
 * @brief  Boucle principale : rend true une fois par appui (anti-rebond fait).
 */
bool button_poll_pressed(void);

/**
 * @brief  État instantané du bouton (true = enfoncé), sans anti-rebond.
 */
bool button_is_down(void);

#endif /* BUTTON_H */
