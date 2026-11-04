/**
 ******************************************************************************
 * @file    leds.h
 * @brief   Signalisation de l'état du chargeur avec la LED RVB de l'ObCP.
 ******************************************************************************
 *
 *   Couleur / rythme                 Signification
 *   -------------------------------  ---------------------------------------
 *   bleu, bref éclair chaque seconde  attente d'une connexion
 *   violet, bref éclair chaque sec.   attente, et aucune application valide
 *   bleu fixe                         PC connecté
 *   vert qui change à chaque Ko       transfert en cours
 *   vert fixe                         programme reçu, vérifié et validé
 *   rouge fixe                        erreur de transfert (voir le PC)
 *   rouge clignotant rapide           le module BLE ne répond pas
 *   blanc clignotant (3 s)            identification demandée par le PC
 *
 *  Les fonctions ne bloquent jamais : leds_process() regarde l'heure
 *  (HAL_GetTick, en ms) et allume ou éteint les LED au bon moment. La boucle
 *  principale peut ainsi continuer à traiter le Bluetooth.
 ******************************************************************************
 */

#ifndef LEDS_H
#define LEDS_H

/** Modes d'affichage. */
typedef enum
{
    LEDS_OFF = 0,
    LEDS_WAITING,          /**< bleu, éclair chaque seconde      */
    LEDS_WAITING_NO_APP,   /**< violet, éclair chaque seconde    */
    LEDS_CONNECTED,        /**< bleu fixe                        */
    LEDS_TRANSFER,         /**< vert, change à chaque leds_pulse */
    LEDS_SUCCESS,          /**< vert fixe                        */
    LEDS_ERROR,            /**< rouge fixe                       */
    LEDS_FAULT,            /**< rouge clignotant rapide          */
    LEDS_IDENTIFY          /**< blanc clignotant                 */
} leds_mode_t;

/** @brief Choisit le mode d'affichage (sans effet s'il est déjà actif). */
void leds_set_mode(leds_mode_t mode);

/** @brief Inverse la LED verte (appelé à chaque bloc reçu). */
void leds_pulse(void);

/** @brief Met à jour les LED ; à appeler dans la boucle principale. */
void leds_process(void);

#endif /* LEDS_H */
