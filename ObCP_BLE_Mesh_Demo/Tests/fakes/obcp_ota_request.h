/**
 ******************************************************************************
 * @file    Tests/fakes/obcp_ota_request.h
 * @brief   Remplaçant PC de Core/Inc/obcp_ota_request.h : le vrai écrit à une
 *          adresse fixe de la SRAM2 puis redémarre la carte, impossible sur
 *          PC. Ici, on compte seulement les demandes.
 ******************************************************************************
 */
#ifndef OBCP_OTA_REQUEST_H
#define OBCP_OTA_REQUEST_H
#include "fake_board.h"
/* Table des vecteurs : sur la carte, dans startup_stm32l476rgtx.s ; ici,
 * dans fake_eeprom.c. Son adresse sur PC n'est jamais dans la zone OTA :
 * app_is_ota() répond « version USB ». */
extern const uint32_t g_pfnVectors[];
static inline void obcp_ota_init_vectors(void) { }
static inline void obcp_ota_enter_loader(void) { fake_ota_enter_count++; }
#endif
