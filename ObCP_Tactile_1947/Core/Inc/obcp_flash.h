/**
 * @file obcp_flash.h
 * @brief Adaptation du stockage aux banques et à l’ECC STM32L476.
 * @ingroup obcp_storage
 *
 * Les adresses logiques sont des offsets dans la réserve du linker. La programmation porte
 * sur huit octets alignés. Les opérations physiques sont synchrones et exécutées dans la
 * boucle principale. Le seul chemin d’interruption est le signal ECCD en NMI.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
#ifndef OBCP_FLASH_H
#define OBCP_FLASH_H

#include "obcp_storage.h"

/**
 * @brief Vérifier la géométrie réelle avant de modifier la flash.
 *
 * Contrôle capacité 1 Mio, absence de permutation des banques, pages de 2 Kio et bornes de
 * la réserve. Ces contrôles relient les hypothèses du linker au composant lu.
 * @return true si la réserve peut être utilisée avec cette géométrie.
 */
bool obcp_flash_ready(void);
/**
 * @brief Construire le port utilisé par storage.c.
 *
 * Le port contient les trois callbacks synchrones. Un port nul est renvoyé si les
 * hypothèses de placement ne correspondent pas au composant.
 * @return Interface Storage prête, ou structure nulle si la géométrie est incompatible.
 */
Storage obcp_flash_storage(void);

#endif
