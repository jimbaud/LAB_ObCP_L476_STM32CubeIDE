/**
 ******************************************************************************
 * @file    ota_switch.h
 * @brief   Passage au chargeur Bluetooth (commande « #ota »), sans bloquer
 *          la boucle principale.
 ******************************************************************************
 *
 *  LE CHARGEUR BLUETOOTH
 *  ---------------------
 *  Depuis la version 2.1.0, le programme modèle est rangé dans la seconde
 *  moitié de la mémoire flash (à partir de 0x08080000). La première moitié
 *  accueille le chargeur Bluetooth « ObCP-OTA » (projet ObCP_OTA_BLE),
 *  installé une fois pour toutes par le câble USB. À chaque démarrage, le
 *  chargeur lance le programme modèle ; il reste actif, prêt à recevoir un
 *  nouveau programme par Bluetooth, si on le lui demande.
 *
 *  La commande « #ota » (port COM virtuel ou Bluetooth) est cette demande :
 *  la carte redémarre dans le chargeur, sans toucher à ses boutons.
 *  L'application PC obcp_ota.py (version 1.2 et suivantes) l'envoie
 *  d'ailleurs elle-même à une carte qu'elle connaît, quand celle-ci
 *  fonctionne.
 *
 *  POURQUOI PAS UN REDÉMARRAGE IMMÉDIAT ?
 *  ---------------------------------------
 *  La réponse « OTA loader... » doit d'abord partir vers le PC et vers le
 *  téléphone, et la connexion Bluetooth doit être fermée proprement (sinon
 *  le téléphone ne s'aperçoit de la coupure qu'au bout de plusieurs
 *  secondes). Le passage se fait donc en trois étapes, une par tour de
 *  boucle au moins, sans jamais attendre sur place :
 *
 *    1. ENVOI        attendre que les réponses soient parties
 *                    (au plus OTA_REPLY_TIMEOUT_MS, voir app_config.h) ;
 *    2. DÉCONNEXION  fermer la connexion Bluetooth et attendre que le module
 *                    le confirme, et que le message « BLE disconnected »
 *                    soit parti vers le PC (au plus OTA_DISCONNECT_TIMEOUT_MS) ;
 *    3. REDÉMARRAGE  obcp_ota_enter_loader() (voir obcp_ota_request.h) : la
 *                    carte redémarre dans le chargeur (LED bleue qui émet un
 *                    bref éclair chaque seconde).
 *
 *  Au redémarrage, le port COM virtuel disparaît du PC (le chargeur
 *  n'utilise pas l'USB) : un terminal ouvert signale une erreur, c'est normal.
 *  Sans connexion pendant 2 minutes, le chargeur relance le programme modèle
 *  (le bouton RESET le relance aussi tout de suite).
 ******************************************************************************
 */
#ifndef OTA_SWITCH_H
#define OTA_SWITCH_H

#include <stdbool.h>

/**
 * @brief  Demande le passage au chargeur Bluetooth (commande « #ota »).
 *         Le passage lui-même est fait par ota_switch_process(), dans la
 *         boucle principale. Une seconde demande pendant le passage est
 *         sans effet.
 */
void ota_switch_request(void);

/**
 * @brief  À appeler à chaque tour de la boucle principale : fait avancer le
 *         passage au chargeur s'il a été demandé (rien sinon). Ne bloque pas.
 */
void ota_switch_process(void);

/**
 * @brief  Indique si un passage au chargeur est en cours.
 */
bool ota_switch_is_pending(void);

#endif /* OTA_SWITCH_H */
