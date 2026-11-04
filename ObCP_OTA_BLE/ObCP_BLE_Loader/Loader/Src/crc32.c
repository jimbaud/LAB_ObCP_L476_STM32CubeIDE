/**
 ******************************************************************************
 * @file    crc32.c
 * @brief   CRC-32 « bit à bit », sans table, compatible avec zlib.
 ******************************************************************************
 *
 *  Il existe des versions plus rapides (avec une table de 256 valeurs), mais
 *  celle-ci tient en quelques lignes et suffit largement : vérifier 64 Ko de
 *  programme ne prend qu'une fraction de seconde à 80 MHz, et ce calcul n'a
 *  lieu qu'une fois, à la fin du transfert.
 *
 *  Principe : on traite les octets un par un, et chaque octet bit par bit.
 *  Le polynôme 0xEDB88320 est la forme « réfléchie » du polynôme standard
 *  0x04C11DB7. Le registre est inversé (~) au début et à la fin, comme le
 *  demande la norme : c'est ce qui rend le calcul identique à zlib.
 ******************************************************************************
 */

#include "crc32.h"

#define CRC32_POLY_REFLECTED   0xEDB88320UL

uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    crc = ~crc;                                  /* inversion de départ */

    while (len > 0U)
    {
        crc ^= *data;                            /* on « injecte » l'octet suivant */
        data++;
        len--;

        for (int bit = 0; bit < 8; bit++)
        {
            /* Si le bit de poids faible vaut 1, on décale et on applique le
             * polynôme ; sinon on décale simplement. */
            if ((crc & 1UL) != 0UL)
            {
                crc = (crc >> 1) ^ CRC32_POLY_REFLECTED;
            }
            else
            {
                crc = crc >> 1;
            }
        }
    }

    return ~crc;                                 /* inversion finale */
}
