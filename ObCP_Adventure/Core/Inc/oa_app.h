/**
 ******************************************************************************
 * @file    oa_app.h
 * @brief   Ordonnanceur du jeu sur la carte : entrées, physique, rendu.
 *
 *  main.c (code généré) n'appelle que ces deux fonctions, depuis ses
 *  sections USER CODE 2 et 3 :
 *    - oa_app_init() une fois, après les initialisations de CubeMX ;
 *    - oa_app_task() à chaque tour de la super-boucle, sans jamais attendre.
 *
 *  SPDX-License-Identifier: MIT
 ******************************************************************************
 */
#ifndef OA_APP_H
#define OA_APP_H
/** @brief Démarrage : base de temps, sauvegarde, tactile, accéléromètre,
 *         écran ; appelée dans USER CODE 2 de main.c. */
void oa_app_init(void);
/** @brief Un tour court de la super-boucle : console, capteurs (100 Hz),
 *         pas de physique (60 Hz), sauvegarde, rendu par bandes et DMA ;
 *         appelée dans USER CODE 3 de main.c. */
void oa_app_task(void);
#ifdef OA_APP_TEST
#include "oa_engine.h"
/** @brief Observation en lecture seule pour les tests PC, absente du firmware. */
const OaGame *oa_app_test_game(void);
#endif
#endif
