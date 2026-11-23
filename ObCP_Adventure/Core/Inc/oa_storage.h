/** @file oa_storage.h @brief Journal EEPROM transactionnel à seize emplacements.
 * @details Une opération ne traverse jamais une page EEPROM de 32 octets.
 * SPDX-License-Identifier: MIT */
#ifndef OA_STORAGE_H
#define OA_STORAGE_H
#include "oa_engine.h"
#define OA_SAVE_BASE 4096u
#define OA_SAVE_SLOTS 16u
#define OA_SAVE_SIZE 256u
/** @brief Interface identique pour une EEPROM et un fichier simulé sur PC. */
typedef struct {
    void *context;
    bool (*read)(void *, uint16_t, uint8_t *, size_t);
    bool (*write_page)(void *, uint16_t, const uint8_t *, size_t);
    bool (*ready)(void *);
} OaNv;
typedef struct {
    OaNv nv;
    uint8_t record[OA_SAVE_SIZE];
    uint32_t sequence, deadline;
    uint8_t slot, page, state;
    bool writable, available, error, pending;
} OaStorage;
/** @brief CRC-32/ISO-HDLC, polynôme réfléchi 0xEDB88320. */
uint32_t oa_crc32(const uint8_t *data, size_t length);
/** @brief Recherche la dernière copie valide et restaure des valeurs validées. */
bool oa_storage_open(OaStorage *store, OaNv nv, OaGame *game);
/** @brief Autorise explicitement le premier enregistrement dans la région réservée. */
void oa_storage_allow_initialize(OaStorage *store);
/** @brief Fige une requête ; retourne false si une écriture est déjà en cours. */
bool oa_storage_request(OaStorage *store, const OaGame *game, uint32_t now_ms);
/** @brief Avance au plus une page ou une lecture de contrôle, sans boucle d'attente. */
void oa_storage_task(OaStorage *store, uint32_t now_ms);
#endif
