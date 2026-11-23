/** @file oa_devices.h @brief Pilotes testables : seules les transactions dépendent de HAL.
 * SPDX-License-Identifier: MIT */
#ifndef OA_DEVICES_H
#define OA_DEVICES_H
#include "oa_input.h"
#include "oa_storage.h"
typedef enum { OA_DEV_LCD, OA_DEV_TOUCH, OA_DEV_ACCEL, OA_DEV_EEPROM } OaDevice;
/** @brief Une transaction SPI garde CS actif sur toute sa longueur.
 * @param hz Fréquence plafond ; le BSP choisit un diviseur sans la dépasser.
 * @param mode Combinaison CPOL/CPHA, 0 à 3. Les pointeurs restent valides
 * pendant l'appel seulement : cette interface sert aux petites transactions.
 */
typedef struct {
    void *context;
    bool (*spi)(void *, OaDevice, const uint8_t *, uint8_t *, size_t, uint32_t, unsigned);
    bool (*i2c_write)(void *, uint8_t, const uint8_t *, size_t);
    bool (*i2c_read)(void *, uint8_t, uint8_t *, size_t);
    void (*delay_us)(void *, uint32_t);
} OaIo;
typedef enum { OA_PROOF_NONE, OA_PROOF_ELECTRICAL, OA_PROOF_FUNCTIONAL, OA_PROOF_IDENTITY } OaProof;
typedef enum { OA_TOUCH_AUTO, OA_TOUCH_FT, OA_TOUCH_TSC, OA_TOUCH_STMPE } OaTouchKind;
/** @brief Diagnostic séparé pour chacun des trois candidats. */
typedef struct {
    OaIo io;
    OaTouchKind kind;
    OaProof proof[4];
    uint16_t identity;
    uint8_t candidates, phase, failures, stm_mode;
    uint32_t deadline, last_sample;
    uint16_t conversions[7];
    OaTouchSample sample;
    bool ambiguous;
} OaTouch;
/** @brief Sonde seulement les trois profils connus ; retourne le profil sélectionné. */
OaTouchKind oa_touch_begin(OaTouch *touch, OaIo io, OaTouchKind requested);
/** @brief Acquisition périodique ; le TSC2007 avance une conversion à la fois. */
void oa_touch_poll(OaTouch *touch, const OaSettings *settings, uint32_t now_us);
/** @brief Lecture de WHO_AM_I, configuration 100 Hz, échelle ±2 g et BDU. */
uint8_t oa_accel_begin(OaIo io);
/** @brief Lit un échantillon frais, retourne false si absent, ancien ou en erreur. */
bool oa_accel_read(OaIo io, uint8_t identity, int16_t mg[3]);
/** @brief Adapte la M95640 au journal, sans écrire lors de l'ouverture. */
OaNv oa_eeprom_interface(OaIo *io);
#endif
