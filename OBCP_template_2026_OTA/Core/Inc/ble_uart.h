/**
 ******************************************************************************
 * @file    ble_uart.h
 * @brief   Liaison Bluetooth Low Energy (BLE) « façon port série » avec un
 *          téléphone, par le module BlueNRG-M0 de l'ObCP (BLE1).
 *
 *  RÔLES. La carte est un « périphérique » : elle se signale (publicité,
 *  « advertising ») sous le nom OBCP-CROC ; le téléphone, « central »,
 *  la trouve et s'y connecte. Une fois connecté, il dialogue avec un
 *  « service » de la carte : un ensemble de « caractéristiques », des
 *  valeurs repérées par un identifiant unique de 128 bits (UUID).
 *
 *  SERVICE « UART » (le même qu'en 2024, à ne pas changer : l'application
 *  Android ObCP Control et les sujets de TP s'en servent) :
 *    service  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 *    TX       6E400002-...  notification + lecture : carte -> téléphone
 *    RX       6E400003-...  écriture : téléphone -> carte (les commandes)
 *  Ces UUID sont ceux du « Nordic UART Service » (NUS), mais avec les rôles
 *  de TX et RX inversés par rapport à la norme Nordic (chez Nordic, ...02
 *  reçoit et ...03 émet). Une application « UART BLE » générique peut donc
 *  demander de choisir les caractéristiques à la main (nRF Connect : écrire
 *  dans ...03, s'abonner à ...02).
 *
 *  ÉCHANGES. Le téléphone écrit une commande (20 octets au plus) dans RX :
 *  ce module la range et la boucle principale la lit avec
 *  ble_uart_read_line(). Dans l'autre sens, ble_uart_print() range le texte
 *  à envoyer ; il part ensuite en « notifications » de 20 octets au plus (la
 *  taille de base d'un paquet BLE). Un message commence toujours une
 *  nouvelle notification : l'application Android analyse chaque
 *  notification séparément (« X0.01Y0.02Z1.00 », « T = 25 »...).
 *
 *  MODULE. Le BlueNRG-M0 contient toute la pile Bluetooth ; le STM32 lui
 *  envoie des commandes « ACI » par le bus SPI2 et reçoit des évènements
 *  (connexion, écriture...) signalés par sa broche IRQ (PB12). La
 *  bibliothèque BlueNRG-MS de ST (dossier Middlewares, ajoutée par CubeMX
 *  avec le paquet X-CUBE-BLE1) fait ce dialogue ; hci_user_evt_proc()
 *  appelle la fonction de traitement des évènements de ce module.
 *
 *  Documentation : PM0237 (programmation BlueNRG-MS) et UM1865 (commandes
 *  ACI), dans le dossier Datasheets/ ou sur www.st.com.
 ******************************************************************************
 */
#ifndef BLE_UART_H
#define BLE_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "line_builder.h"

/** Résultat de ble_uart_init() : BLE_UART_OK ou l'étape qui a échoué. */
typedef enum
{
  BLE_UART_OK = 0,           /**< module prêt */
  BLE_UART_ERR_NO_MODULE,    /**< le module ne répond pas (absent, SPI, reset...) */
  BLE_UART_ERR_ADDRESS,      /**< adresse Bluetooth refusée */
  BLE_UART_ERR_GATT_INIT,    /**< initialisation GATT refusée */
  BLE_UART_ERR_GAP_INIT,     /**< initialisation GAP refusée */
  BLE_UART_ERR_DEVICE_NAME,  /**< nom de l'appareil refusé */
  BLE_UART_ERR_SECURITY,     /**< réglage de sécurité refusé */
  BLE_UART_ERR_SERVICE,      /**< création du service UART refusée */
  BLE_UART_ERR_TX_POWER      /**< réglage de la puissance d'émission refusé */
} ble_uart_status_t;

/** Évènement de connexion rendu par ble_uart_get_event(). */
typedef enum
{
  BLE_EVENT_NONE = 0,        /**< rien de nouveau */
  BLE_EVENT_CONNECTED,       /**< un téléphone vient de se connecter */
  BLE_EVENT_DISCONNECTED     /**< le téléphone vient de se déconnecter */
} ble_uart_event_t;

/**
 * @brief  Démarre le module Bluetooth : adresse, nom, service UART,
 *         puissance d'émission. La publicité démarre ensuite d'elle-même
 *         dans ble_uart_process(). Durée : environ 150 ms.
 * @retval BLE_UART_OK, ou l'étape qui a échoué (le reste du programme
 *         fonctionne sans Bluetooth)
 */
ble_uart_status_t ble_uart_init(void);

/**
 * @brief  À appeler dans la boucle principale : traite les évènements du
 *         module, relance la publicité après une déconnexion et envoie les
 *         notifications en attente. Ne bloque pas longtemps (quelques ms au
 *         plus).
 */
void ble_uart_process(void);

/**
 * @brief  Indique si le module a été initialisé avec succès.
 */
bool ble_uart_is_ready(void);

/**
 * @brief  Indique si un téléphone est connecté.
 */
bool ble_uart_is_connected(void);

/**
 * @brief  Rend la prochaine ligne de commande reçue du téléphone. Chaque
 *         écriture du téléphone compte comme une ligne complète, même sans
 *         fin de ligne ; si elle contient « \r » ou « \n », elle est
 *         découpée en plusieurs lignes.
 * @param  line  destination (CMD_LINE_MAX + 1 octets conseillés)
 * @param  size  taille de line
 * @retval LINE_READY (ligne dans line), LINE_TOO_LONG ou LINE_NONE
 */
line_status_t ble_uart_read_line(char *line, size_t size);

/**
 * @brief  Range un message à envoyer au téléphone (copié aussitôt). Il partira
 *         en notifications de 20 octets au plus.
 * @param  data  octets à envoyer
 * @param  len   nombre d'octets
 * @retval true si le message est en attente d'envoi ; false si aucun
 *         téléphone n'est connecté ou si le tampon est plein
 */
bool ble_uart_send(const uint8_t *data, uint16_t len);

/**
 * @brief  Envoie une chaîne de caractères (terminée par '\0') au téléphone.
 * @retval comme ble_uart_send()
 */
bool ble_uart_print(const char *text);

/**
 * @brief  Envoie un texte mis en forme comme avec printf (127 caractères au
 *         plus) au téléphone.
 * @retval comme ble_uart_send()
 */
bool ble_uart_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief  Rend le prochain évènement de connexion ou de déconnexion.
 * @param  peer  si non NULL et BLE_EVENT_CONNECTED : adresse du téléphone
 *               (6 octets, octet de poids faible en premier)
 * @retval BLE_EVENT_NONE s'il n'y a rien de nouveau
 */
ble_uart_event_t ble_uart_get_event(uint8_t peer[6]);

/**
 * @brief  Adresse Bluetooth de la carte (6 octets, poids faible en premier).
 */
void ble_uart_get_address(uint8_t address[6]);

/**
 * @brief  Indique s'il reste des messages à envoyer au téléphone (false si
 *         aucun téléphone n'est connecté). Sert à la commande « #ota »
 *         (ota_switch.c) : on attend que la réponse soit partie.
 */
bool ble_uart_tx_pending(void);

/**
 * @brief  Ferme la connexion avec le téléphone (sans effet s'il n'y en a
 *         pas). La fin de la connexion est confirmée un peu plus tard par le
 *         module : ble_uart_is_connected() devient false, et
 *         ble_uart_get_event() rend BLE_EVENT_DISCONNECTED.
 */
void ble_uart_disconnect(void);

#endif /* BLE_UART_H */
