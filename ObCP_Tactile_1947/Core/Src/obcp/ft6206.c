/**
 * @file ft6206.c
 * @brief Lecture du premier contact capacitif FT62xx sur I²C1.
 * @ingroup obcp_drivers
 *
 * Le capteur est sondé à l’adresse 7 bits 0x38. La HAL reçoit cette adresse décalée d’un
 * bit. Ce pilote livre les coordonnées natives. Le passage en paysage est réalisé par
 * input.c.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
/** @def FT_ADDRESS
 * @brief Adresse 0x38 sur sept bits, décalée d'un bit pour l'API HAL I2C.
 */

#include "touch_drivers.h"
#include "i2c.h"

/* HAL attend l'adresse décalée d'un bit. Sur le bus, le numéro est 0x38. */
#define FT_ADDRESS (0x38U << 1)

/**
 * @brief Lire une plage de registres FT62xx.
 *
 * Le pilote évite de démarrer si le bus est marqué occupé. La HAL utilise une adresse de
 * registre sur huit bits et un délai limité à quatre millisecondes.
 * @param[in] first Adresse du premier registre FT62xx à lire.
 * @param[out] data Tampon recevant au moins len octets consécutifs de registres.
 * @param[in] size Longueur du bloc en octets.
 * @return true si tous les registres sont reçus.
 */
static bool read_regs(uint8_t first, uint8_t *data, uint16_t size)
{
    if (__HAL_I2C_GET_FLAG(&hi2c1, I2C_FLAG_BUSY)) {
        return false;
    }
    return HAL_I2C_Mem_Read(&hi2c1, FT_ADDRESS, first, I2C_MEMADD_SIZE_8BIT,
                            data, size, 4U) == HAL_OK;
}

/*
 * Vérifier l'identifiant de famille du tactile capacitif.
 *
 * Le registre 0xA3 est lu. Les identifiants 0x06, 0x36 et 0x64 sont admis. Le résultat de
 * sortie commence à zéro pour éviter une identité résiduelle.
 */
bool ft6206_probe(uint8_t *chip)
{
    *chip = 0;
    return read_regs(0xA3, chip, 1U) && (*chip == 0x06U || *chip == 0x36U || *chip == 0x64U);
}

/*
 * Lire l'état et les coordonnées du premier doigt.
 *
 * Une rafale contient TD_STATUS et les deux coordonnées sur 12 bits. L'événement natif de
 * relâchement ne doit pas devenir un nouvel appui. Les coordonnées sont écrites seulement
 * si le doigt est présent.
 */
bool ft6206_sample(bool *pressed, uint16_t *raw_x, uint16_t *raw_y)
{
    uint8_t regs[5];
    if (!read_regs(0x02, regs, sizeof(regs))) {
        return false;
    }

    /* TD_STATUS, P1_XH, P1_XL, P1_YH, P1_YL. Les bits 7:6 de XH
     * donnent l'événement natif. Le firmware exploite le premier doigt. */
    *pressed = (regs[0] & 0x0FU) != 0U && (regs[1] >> 6) != 1U;
    if (*pressed) {
        *raw_x = (uint16_t)((uint16_t)(regs[1] & 15U) << 8 | regs[2]);
        *raw_y = (uint16_t)((uint16_t)(regs[3] & 15U) << 8 | regs[4]);
    }
    return true;
}
