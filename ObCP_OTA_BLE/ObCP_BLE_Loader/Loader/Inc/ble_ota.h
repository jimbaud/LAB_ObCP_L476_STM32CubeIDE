/**
 ******************************************************************************
 * @file    ble_ota.h
 * @brief   Partie Bluetooth du chargeur : module BlueNRG-MS, service GATT
 *          « ObCP OTA », annonces et événements.
 ******************************************************************************
 *
 *  Le module BLE de l'ObCP (BlueNRG-M0 ou SPBTLE-RF, puce BlueNRG-MS) est un
 *  « coprocesseur réseau » : il gère seul la radio et la pile Bluetooth. Le
 *  STM32 lui parle par une liaison SPI en lui envoyant des commandes
 *  (fonctions aci_xxx et hci_xxx de la bibliothèque ST X-CUBE-BLE1) et
 *  reçoit en retour des événements (connexion, écriture d'une
 *  caractéristique...). Quand le module a un événement à transmettre, il
 *  met sa broche IRQ à 1 ; l'interruption EXTI12 lit alors l'événement et le
 *  range dans une file, et hci_user_evt_proc() (appelée dans la boucle
 *  principale) le passe à notre fonction de traitement.
 *
 *  Ce module fait le lien entre le Bluetooth et le cœur du transfert (ota.c).
 ******************************************************************************
 */

#ifndef BLE_OTA_H
#define BLE_OTA_H

#include <stdbool.h>

/**
 * @brief  Initialise le module BLE et crée le service « ObCP OTA ».
 * @return true si le module a répondu à toutes les commandes.
 * @note   Prépare aussi le cœur du transfert (ota_init). Durée : ~0,2 s.
 */
bool ble_ota_init(void);

/**
 * @brief  Traite les événements BLE et relance les annonces si besoin.
 * @note   À appeler le plus souvent possible dans la boucle principale.
 *         Si le module BLE a redémarré tout seul, cette fonction refait
 *         toute l'initialisation (la carte redevient visible).
 */
void ble_ota_process(void);

/** @brief Indique si le module BLE est initialisé et répond. */
bool ble_ota_is_ready(void);

/** @brief Indique si un PC est connecté. */
bool ble_ota_is_connected(void);

/**
 * @brief  Termine proprement la connexion en cours, avant un redémarrage.
 * @note   Laisse d'abord au PC le temps de se déconnecter lui-même (au plus
 *         1 s), puis coupe la liaison s'il est encore connecté et attend la
 *         confirmation du module (au plus 0,5 s).
 */
void ble_ota_disconnect(void);

/** @brief Nom annoncé par la carte, par exemple « ObCP-OTA-A1B2 ». */
const char *ble_ota_get_name(void);

#endif /* BLE_OTA_H */
