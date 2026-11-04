/**
 ******************************************************************************
 * @file    ota.h
 * @brief   Cœur du transfert OTA : interprète les commandes et les données
 *          reçues, écrit le programme en flash et le vérifie.
 ******************************************************************************
 *
 *  Ce module ne connaît rien au Bluetooth : il reçoit des tableaux d'octets
 *  (ota_on_control, ota_on_data) et répond par une fonction « notify » qu'on
 *  lui donne à l'initialisation. Grâce à cette séparation, le même fichier
 *  ota.c est compilé dans le chargeur ET dans les tests sur PC (dossier
 *  Tests/), où le Bluetooth et la flash sont simulés.
 *
 *  MACHINE À ÉTATS
 *  ---------------
 *
 *            START ok           dernier octet          END : CRC et
 *   +------+ -------> +-----------+ -------> +----------+ image ok  +------+
 *   | IDLE |          | RECEIVING |          | COMPLETE | --------> | DONE |
 *   +------+          +-----------+          +----------+           +------+
 *      ^                    |                      |
 *      |                    | paquet perdu,        | CRC ou image
 *      |                    | trop de données...   | incorrects
 *      |                    v                      v
 *      |   ABORT ou    +------------------------------+
 *      +---------------|            ERROR             |
 *        déconnexion   +------------------------------+
 *
 *   Depuis n'importe quel état : START recommence un transfert complet
 *   (nouvel effacement), ABORT ou une déconnexion ramènent à IDLE, et RUN
 *   redémarre la carte.
 ******************************************************************************
 */

#ifndef OTA_H
#define OTA_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_protocol.h"

/** États du transfert. */
typedef enum
{
    OTA_STATE_IDLE = 0,     /**< rien en cours, attente de START           */
    OTA_STATE_RECEIVING,    /**< réception des paquets de données          */
    OTA_STATE_COMPLETE,     /**< tout est reçu, attente de END             */
    OTA_STATE_DONE,         /**< programme vérifié et validé               */
    OTA_STATE_ERROR         /**< erreur signalée, attente de START ou ABORT */
} ota_state_t;

/**
 * Fonction appelée par le module pour répondre au PC.
 * @param code   OTA_RSP_xxx ou OTA_ERR_xxx (voir ota_protocol.h).
 * @param value  Information associée (taille, nombre d'octets, CRC...).
 */
typedef void (*ota_notify_fn)(uint8_t code, uint32_t value);

/**
 * @brief  Prépare le module (à appeler une fois au démarrage).
 * @param  notify  Fonction qui transmettra les réponses au PC.
 */
void ota_init(ota_notify_fn notify);

/**
 * @brief  Traite une commande écrite par le PC dans la caractéristique CTRL.
 * @param  data  Octets reçus (data[0] = code de la commande).
 * @param  len   Nombre d'octets.
 */
void ota_on_control(const uint8_t *data, uint16_t len);

/**
 * @brief  Traite un paquet écrit par le PC dans la caractéristique DATA.
 * @param  data  data[0] = numéro de séquence, puis 1 à 19 octets du programme.
 * @param  len   Nombre d'octets (2 à 20).
 */
void ota_on_data(const uint8_t *data, uint16_t len);

/**
 * @brief  À appeler quand le PC se déconnecte : un transfert inachevé est
 *         abandonné (l'application reste invalide tant qu'un nouveau
 *         transfert n'a pas réussi).
 */
void ota_on_disconnect(void);

/** @brief Renvoie l'état courant du transfert. */
ota_state_t ota_get_state(void);

/** @brief Nombre d'octets du programme reçus depuis START. */
uint32_t ota_get_received(void);

/** @brief Taille annoncée par START (0 si aucun transfert). */
uint32_t ota_get_size(void);

/**
 * @brief  Vrai une seule fois après réception de la commande RUN.
 * @note   « take » : la demande est effacée en même temps qu'elle est lue.
 */
bool ota_take_run_request(void);

/**
 * @brief  Vrai une seule fois après réception de la commande IDENTIFY.
 */
bool ota_take_identify_request(void);

/**
 * @brief  Vrai une seule fois après chaque accusé de réception envoyé
 *         (sert à faire clignoter la LED verte au rythme du transfert).
 */
bool ota_take_activity(void);

/**
 * @brief  Remplit la caractéristique INFO (OTA_INFO_LEN octets, voir
 *         ota_protocol.h). Les octets de diagnostic 16 et 17 sont laissés
 *         à 0 : ble_ota.c les complète.
 * @param  info         Tableau de OTA_INFO_LEN octets à remplir.
 * @param  boot_reason  Raison du démarrage dans le chargeur (voir boot.h).
 */
void ota_build_info(uint8_t info[OTA_INFO_LEN], uint8_t boot_reason);

#endif /* OTA_H */
