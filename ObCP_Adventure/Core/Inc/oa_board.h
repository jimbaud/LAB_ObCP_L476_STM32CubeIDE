/**
 ******************************************************************************
 * @file    oa_board.h
 * @brief   Interface matérielle de l'ObCP pour le jeu (BSP).
 *
 *  oa_board.c est le SEUL fichier du jeu qui appelle la HAL de ST : bus SPI1,
 *  SPI2, SPI3 et I2C1, sélections (CS), DMA de l'écran, base de temps TIM2,
 *  LED, bouton USER et liaison série USART2. Il utilise les noms de broches
 *  définis dans CubeMX (main.h : LCD_CS, LCD_DC, TOUCH_CS, ACC_CS...).
 *
 *  Cet en-tête n'inclut aucun fichier de ST : le moteur, les pilotes et
 *  l'ordonnanceur (oa_app.c) se compilent donc aussi sur PC, où les tests
 *  (Tests/test_app.c) fournissent leur propre version de ces fonctions.
 *
 *  SPDX-License-Identifier: MIT
 ******************************************************************************
 */
#ifndef OA_BOARD_H
#define OA_BOARD_H

#include "oa_devices.h"

/* ---- Bus des pilotes ------------------------------------------------------ */

/** @brief Adaptateur des transactions courtes et bloquantes (SPI, I2C, délai)
 *         utilisé par les pilotes du tactile, de l'accéléromètre et de
 *         l'EEPROM (voir OaIo dans oa_devices.h). */
OaIo oa_board_io(void);

/* ---- Temps ---------------------------------------------------------------- */

/** @brief Démarre TIM2 (1 MHz) : CubeMX le configure sans le lancer. */
void oa_board_start_timebase(void);
/** @brief Temps monotone en microsecondes, modulo 2^32 (compteur de TIM2). */
uint32_t oa_board_us(void);
/** @brief Millisecondes depuis le démarrage (HAL_GetTick). */
uint32_t oa_board_ms(void);
/** @brief Attente active ; réservée au démarrage (séquence de l'écran, LED). */
void oa_board_delay_ms(uint32_t ms);

/* ---- Écran ILI9341 sur SPI1 ----------------------------------------------- */

/** @brief Vrai tant qu'un envoi DMA vers l'écran est en cours. Un envoi bloqué
 *         plus de 200 ms est abandonné et signalé par oa_board_display_error(). */
bool oa_board_display_busy(void);
/** @brief Consomme le drapeau de panne DMA (le rendu redessine alors tout). */
bool oa_board_display_error(void);
/** @brief Envoie une commande de l'ILI9341 et ses paramètres (SPI1, 10 MHz). */
bool oa_board_lcd_command(uint8_t command, const uint8_t *data, size_t n);
/** @brief Lance l'envoi par DMA d'un rectangle RGB565 (octet de poids fort
 *         d'abord). Le tampon ne doit pas être modifié avant la fin. */
bool oa_board_lcd_dma(unsigned x, unsigned y, unsigned w, unsigned h, const uint8_t *bytes);
/** @brief Profil SPI de l'écran : 10 MHz (défaut) ou 40 MHz expérimental. */
void oa_board_lcd_fast(bool fast);
/** @brief Séquence d'initialisation de l'ILI9341, paysage 320 x 240
 *         (Core/Src/devices/oa_lcd.c). */
bool oa_lcd_init(void);

/* ---- Bouton, LED, liaison série ------------------------------------------- */

/** @brief Bouton USER (SW1) avec anti-rebond de 25 ms ; *pressed signale un
 *         nouvel appui. Retourne l'état stable (vrai = appuyé). */
bool oa_board_button(bool *pressed);
/** @brief État brut du bouton USER, sans anti-rebond (lecture au démarrage). */
bool oa_board_button_raw(void);
/** @brief Allume ou éteint les trois couleurs de la LED RVB (sorties GPIO). */
void oa_board_led(bool red, bool green, bool blue);
/** @brief Caractère reçu sur USART2 (D0/D1, 115200 bauds), ou -1. */
int oa_board_uart_getchar(void);
/** @brief Erreur grave : interruptions coupées, LED rouge fixe, arrêt.
 *         Appelée par Error_Handler() et les fautes du processeur. */
void oa_board_fatal(void);

#endif
