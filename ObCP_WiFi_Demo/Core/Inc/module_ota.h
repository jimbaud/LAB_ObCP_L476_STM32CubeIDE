/**
 ******************************************************************************
 * @file    module_ota.h
 * @brief   Commande « module ota » : mise à jour du firmware du ST67W611M1
 *          avec un fichier .ota envoyé depuis le PC par le terminal (YMODEM)
 *          et transmis au module par le lien SPI (AT+OTASTART / AT+OTASEND /
 *          AT+OTAFIN). Aucune liaison matérielle supplémentaire n'est
 *          nécessaire, mais le module doit déjà exécuter un firmware
 *          « mission T01 » 2.0.x.
 ******************************************************************************
 */
#ifndef MODULE_OTA_H
#define MODULE_OTA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Attente du début du transfert (l'utilisateur ouvre le menu d'envoi) */
#define OTA_START_TIMEOUT_MS   120000U
/* Attente du redémarrage du module après AT+OTAFIN */
#define OTA_READY_TIMEOUT_MS    40000U

/**
 * @brief  Commande « module ota » : attend un fichier .ota envoyé en YMODEM
 *         par le terminal, le contrôle (nom, en-tête, taille), le transmet
 *         au module au fil de la réception puis demande son installation.
 * @retval 0 si la mise à jour a réussi, sinon un code d'erreur négatif
 *         (APP_ERR_CANCELED, APP_ERR_TRANSFER, APP_ERR_BADFILE...).
 */
int module_ota_run(void);

#ifdef __cplusplus
}
#endif

#endif /* MODULE_OTA_H */
