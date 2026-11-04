/**
 ******************************************************************************
 * @file    crc32.h
 * @brief   Calcul du CRC-32 (le même que zlib, ZIP, Ethernet et Python).
 ******************************************************************************
 *
 *  Un CRC (« contrôle de redondance cyclique ») est une empreinte de 32 bits
 *  calculée sur une suite d'octets. Si un seul bit change, le CRC change.
 *  Le PC calcule le CRC du programme avant l'envoi (zlib.crc32 en Python),
 *  l'ObCP le recalcule sur ce qu'il a réellement écrit en flash : si les deux
 *  valeurs sont égales, le transfert et l'écriture sont corrects.
 ******************************************************************************
 */

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>

/**
 * @brief  Poursuit le calcul d'un CRC-32 avec de nouveaux octets.
 * @param  crc   CRC obtenu jusqu'ici (0 pour commencer un nouveau calcul).
 * @param  data  Adresse des octets à ajouter.
 * @param  len   Nombre d'octets.
 * @return CRC-32 de toutes les données vues depuis le départ.
 *
 * Exemple : crc32_update(crc32_update(0, a, na), b, nb) donne le même
 * résultat que le CRC de a puis b mis bout à bout. C'est exactement le
 * comportement de zlib.crc32(b, zlib.crc32(a)) en Python.
 */
uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len);

#endif /* CRC32_H */
