/**
 * @file    sim_api.h
 * @brief   Fonctions de pilotage de la flash simulée (tests sur PC seulement).
 */

#ifndef SIM_API_H
#define SIM_API_H

#include <stdbool.h>
#include <stdint.h>

void           sim_flash_reset(uint8_t fill);              /* remplit toute la zone */
void           sim_flash_write_raw(uint32_t offset, const uint8_t *data, uint32_t len);
const uint8_t *sim_flash_data(void);                       /* contenu (512 Ko)      */
void           sim_flash_fail_next_erase(void);            /* panne d'effacement    */
void           sim_flash_fail_program_at(long offset);     /* panne d'écriture      */
bool           sim_flash_is_unlocked(void);
uint32_t       sim_flash_erase_count(void);
uint32_t       sim_flash_program_count(void);
long           sim_flash_first_dword_write_rank(void);     /* -1 si jamais écrit    */

#endif /* SIM_API_H */
