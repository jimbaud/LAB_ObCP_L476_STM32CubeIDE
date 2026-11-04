/**
 ******************************************************************************
 * @file    Tests/fakes/obcp_ota_request.h
 * @brief   Remplaçant PC de Core/Inc/obcp_ota_request.h.
 ******************************************************************************
 *  Sur la carte, obcp_ota_enter_loader() écrit le « mot magique » en SRAM2
 *  puis redémarre le microcontrôleur : impossible sur un PC. Ici, c'est une
 *  vraie fonction (fakes/fake_hal.c) qui compte ses appels
 *  (fake_ota_enter_count, déclaré dans fake_board.h) et revient.
 *
 *  Ce fichier est trouvé avant celui de Core/Inc parce que le dossier fakes/
 *  est cité en premier dans les options -I du Makefile.
 ******************************************************************************
 */
#ifndef FAKE_OBCP_OTA_REQUEST_H
#define FAKE_OBCP_OTA_REQUEST_H

void obcp_ota_init_vectors(void);
void obcp_ota_enter_loader(void);    /* sur la carte : ne revient jamais */

#endif /* FAKE_OBCP_OTA_REQUEST_H */
