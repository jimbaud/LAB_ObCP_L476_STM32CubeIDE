/**
 ******************************************************************************
 * @file    boot.h
 * @brief   Décision de démarrage : lancer l'application de la banque 2, ou
 *          rester dans le chargeur BLE ?
 ******************************************************************************
 *
 *  boot_check_and_jump() est appelée en TOUT PREMIER dans main(), avant
 *  HAL_Init() : à ce moment, aucun périphérique n'est configuré et
 *  l'horloge est celle du démarrage (oscillateur MSI à 4 MHz). C'est l'état
 *  idéal pour passer la main à l'application, qui trouvera le processeur
 *  comme après un reset.
 *
 *  Le chargeur RESTE actif dans trois cas :
 *   1. aucune application valide n'est installée en banque 2 ;
 *   2. le bouton USER est maintenu enfoncé pendant le reset ;
 *   3. l'application a demandé le chargeur (mot magique dans la SRAM2,
 *      voir obcp_ota_request.h côté application).
 *  Sinon, la fonction ne revient pas : l'application démarre.
 ******************************************************************************
 */

#ifndef BOOT_H
#define BOOT_H

#include <stdint.h>

/** Raison pour laquelle le chargeur est resté actif (lue dans INFO[15]). */
typedef enum
{
    BOOT_REASON_NONE    = 0,   /**< (ne devrait pas arriver)               */
    BOOT_REASON_NO_APP  = 1,   /**< pas d'application valide en banque 2   */
    BOOT_REASON_BUTTON  = 2,   /**< bouton USER maintenu pendant le reset  */
    BOOT_REASON_REQUEST = 3    /**< demande de l'application (« #ota »...) */
} boot_reason_t;

/**
 * @brief  Lance l'application si rien ne s'y oppose ; sinon revient.
 * @note   À appeler au début de main(), dans USER CODE BEGIN 1.
 */
void boot_check_and_jump(void);

/** @brief Raison du maintien dans le chargeur (après boot_check_and_jump). */
boot_reason_t boot_get_reason(void);

#endif /* BOOT_H */
