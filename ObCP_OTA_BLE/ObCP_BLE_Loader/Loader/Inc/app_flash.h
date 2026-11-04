/**
 ******************************************************************************
 * @file    app_flash.h
 * @brief   Effacement, écriture et lecture de la zone APPLICATION de la flash
 *          (banque 2, de 0x08080000 à 0x080FFFFF).
 ******************************************************************************
 *
 *  RAPPEL SUR LA MÉMOIRE FLASH DU STM32L4
 *  --------------------------------------
 *   - On ne peut écrire qu'à un endroit EFFACÉ (tous les bits à 1, octets 0xFF).
 *   - On efface par PAGE de 2 Ko (2048 octets), jamais octet par octet.
 *   - On écrit par DOUBLE MOT de 8 octets alignés (adresse multiple de 8).
 *   - Avant toute écriture il faut DÉVERROUILLER la flash, et la reverrouiller
 *     ensuite pour éviter une écriture accidentelle.
 *
 *  Toutes les fonctions prennent une position RELATIVE (« offset ») depuis le
 *  début de la zone application : offset 0 correspond à l'adresse 0x08080000.
 *  Elles refusent toute adresse hors de la zone : le chargeur ne peut donc
 *  jamais s'effacer lui-même.
 ******************************************************************************
 */

#ifndef APP_FLASH_H
#define APP_FLASH_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  Déverrouille la flash et efface les pages qui contiendront
 *         'size' octets à partir du début de la zone application.
 * @param  size  Taille du futur programme, en octets (1 à 524288).
 * @return true si l'effacement a réussi.
 * @note   Durée : environ 22 ms par page de 2 Ko (0,7 s pour 64 Ko).
 *         Les pages situées au-delà ne sont pas touchées.
 */
bool app_flash_erase(uint32_t size);

/**
 * @brief  Écrit un double mot (8 octets) dans la zone application.
 * @param  offset  Position relative, multiple de 8.
 * @param  data    Les 8 octets à écrire (dans l'ordre de la mémoire).
 * @return true si l'écriture a réussi.
 * @note   La flash doit avoir été déverrouillée par app_flash_erase().
 */
bool app_flash_program(uint32_t offset, const uint8_t data[8]);

/**
 * @brief  Donne l'adresse en mémoire d'un octet de la zone application.
 * @param  offset  Position relative.
 * @return Pointeur en lecture seule. La flash se lit comme de la RAM.
 */
const uint8_t *app_flash_read(uint32_t offset);

/**
 * @brief  Reverrouille la flash (à appeler à la fin ou en cas d'erreur).
 */
void app_flash_lock(void);

/**
 * @brief  Vide les mémoires cache de la flash.
 * @note   Le STM32 garde en cache des morceaux de flash déjà lus. Après une
 *         écriture, il faut vider ce cache pour être sûr de relire la valeur
 *         neuve. HAL_FLASH_Program() et HAL_FLASHEx_Erase() le font déjà
 *         après chaque opération ; cet appel, à la fin du transfert, est une
 *         précaution supplémentaire qui ne coûte presque rien.
 */
void app_flash_flush_caches(void);

/**
 * @brief  Code d'erreur de la dernière opération ratée (HAL_FLASH_GetError()).
 * @return 0 si aucune erreur ; sinon une combinaison de HAL_FLASH_ERROR_xxx.
 */
uint32_t app_flash_last_error(void);

#endif /* APP_FLASH_H */
