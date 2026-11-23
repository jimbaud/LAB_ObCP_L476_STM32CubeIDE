/**
 * @file obcp_board.h
 * @brief Ordonnancement coopératif et liaison avec la carte réelle.
 * @ingroup obcp_board
 *
 * Ce module possède App, Renderer et les états d’entrée. Il cadence les tâches sans RTOS,
 * arbitre l’affichage, prépare les demandes flash et expose obcp_live au débogueur. Les
 * callbacks d’interruption se limitent à leurs pilotes.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
#ifndef OBCP_BOARD_H
#define OBCP_BOARD_H

#include <stdbool.h>

/* Seuls ces deux appels relient la boucle principale au code généré. */
/**
 * @brief Initialiser l'application après les périphériques CubeMX.
 *
 * Charge les objets persistants, initialise TFT et capteur, sonde le tactile, prépare
 * l'accueil et les bases de temps. À appeler une fois depuis le bloc USER CODE 2 de
 * main().
 */
void obcp_start(void);
/**
 * @brief Servir une itération de toutes les tâches de l'application.
 *
 * Priorise l'audio, les entrées et au plus quatre pas de simulation de rattrapage. Sert le
 * rendu puis un travail flash lorsque son message a pu être affiché. À appeler
 * continuellement depuis USER CODE 3.
 */
void obcp_task(void);
/**
 * @brief Reconnaître une NMI provoquée par une erreur ECC double.
 *
 * Le bit ECCD est acquitté et un indicateur volatile mémorise l'erreur pour la lecture en
 * cours. Les autres causes de NMI restent à traiter par le gestionnaire généré.
 * @return true si la cause ECCD a été prise en charge.
 * @note Appel depuis NMI_Handler(). L’indicateur est consulté par la lecture flash.
 */
bool obcp_flash_ecc_nmi(void);

#endif
