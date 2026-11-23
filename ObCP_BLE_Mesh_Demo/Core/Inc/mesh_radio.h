/**
 ******************************************************************************
 * @file    mesh_radio.h
 * @brief   Radio du réseau maillé : le module Bluetooth de l'ObCP utilisé en
 *          « diffuseur » (broadcaster) et en « observateur » (observer), sans
 *          aucune connexion.
 *
 *  DEUX MODULES, UN SEUL PROGRAMME. Les ObCP d'avant 2024 portent un
 *  SPBTLE-RF, celles de 2024 et après un BlueNRG-M0. Les deux contiennent la
 *  même puce BlueNRG-MS et ont les mêmes 11 broches : même câblage, mêmes
 *  commandes. Ce qui peut changer d'une carte à l'autre :
 *    - la version de la pile Bluetooth gravée dans le module (7.1e sur les
 *      SPBTLE-RF d'après leur fiche technique, 7.2 ou plus récente sur les
 *      BlueNRG-M0) : lue au démarrage, elle dit si le « mode 4 » existe
 *      (ci-dessous) ;
 *    - la puissance rayonnée, pour un même réglage (antenne et circuit du
 *      module) : +4 dBm annoncés pour le SPBTLE-RF, +6 dBm rayonnés (+8 dBm
 *      conduits) pour le BlueNRG-M0 au réglage maximal. À distance égale, le
 *      RSSI mesuré diffère donc un peu selon le module.
 *  La commande « info » affiche la version de la pile et le mode d'écoute.
 *
 *  ÉMETTRE une trame = la diffuser en publicité « non connectable »
 *  (ADV_NONCONN_IND) pendant RADIO_TX_TIME_MS (320 ms), à raison d'un
 *  évènement de publicité toutes les 100 ms, puis arrêter. Commandes du
 *  module (UM1865) : aci_gap_set_broadcast_mode() pour lancer,
 *  aci_gap_set_non_discoverable() pour arrêter.
 *
 *  ÉCOUTER = faire tourner sans fin la « procédure d'observation »
 *  (aci_gap_start_observation_procedure), en écoute passive, sans filtre
 *  des doublons : le module rend chaque publicité entendue dans un
 *  évènement « LE Advertising Report », avec sa puissance de réception
 *  (RSSI). Seules les publicités qui contiennent une trame du réseau
 *  maillé (voir mesh_adv_find) sont gardées.
 *
 *  ÉCOUTER ET ÉMETTRE EN MÊME TEMPS ?
 *  Par défaut, non : la carte écoute sans interruption, sauf pendant ses
 *  propres émissions (mode « alterné »). C'est la méthode qui marche avec
 *  toutes les piles.
 *  Le BlueNRG-MS sait aussi émettre pendant l'écoute dans son « mode 4 »
 *  (« simultaneous advertising and scanning », UM1865 table 285), qui existe
 *  à partir de la pile 7.1b et doit être choisi juste après le redémarrage
 *  du module, avant toute autre commande (aci_hal_write_config_data, offset
 *  CONFIG_DATA_MODE_OFFSET). Il loge alors écoute et publicité dans une même
 *  base de temps (PM0237, § 4.2), avec deux règles : l'intervalle de
 *  publicité plus 5 ms doit être un multiple de la période d'écoute, et
 *  l'écoute doit laisser libre un créneau de 14,6 ms pour chaque évènement
 *  de publicité. La commande « radio simultane » essaie ce mode, avec des
 *  temps qui respectent ces règles (app_config.h). Si le module refuse
 *  quand même d'émettre pendant l'écoute, quel que soit le code d'erreur,
 *  le programme revient de lui-même en mode alterné et le signale.
 *  Pourquoi pas par défaut ? Dans cette base de temps, les évènements de
 *  publicité ne sont plus décalés au hasard (retard fixe de 5 ms) : deux
 *  cartes réglées de la même façon peuvent rester calées de sorte que l'une
 *  émette toujours pendant que l'autre n'écoute pas.
 *
 *  Les évènements du module sont traités dans la boucle principale (par
 *  mesh_radio_process), jamais en interruption : l'interruption de la
 *  broche IRQ (PB12) ne fait que lire les paquets du module (bibliothèque
 *  X-CUBE-BLE1, hci_tl_interface.c).
 *
 *  Pour aller plus loin : PM0237 (programmation du BlueNRG-MS), UM1865
 *  (commandes ACI), dans le dossier Datasheets du programme modèle.
 ******************************************************************************
 */
#ifndef MESH_RADIO_H
#define MESH_RADIO_H

#include <stdbool.h>
#include <stdint.h>

#include "mesh_pdu.h"

/** Résultat du démarrage du module (mesh_radio_init). */
typedef enum
{
  RADIO_OK = 0,            /**< prêt : l'écoute démarre */
  RADIO_ERR_NO_MODULE,     /**< le module ne répond pas (absent, mal soudé...) */
  RADIO_ERR_ADDRESS,       /**< écriture de l'adresse refusée */
  RADIO_ERR_GATT_INIT,     /**< aci_gatt_init refusé */
  RADIO_ERR_GAP_INIT,      /**< aci_gap_init refusé */
  RADIO_ERR_TX_POWER       /**< réglage de la puissance refusé */
} radio_status_t;

/** Écoute pendant les émissions. */
typedef enum
{
  RADIO_MODE_SIMULTANEOUS = 0, /**< l'écoute continue pendant l'émission */
  RADIO_MODE_ALTERNATE         /**< écoute arrêtée pendant chaque émission */
} radio_mode_t;

/** Mode de la pile du module, choisi au démarrage (voir l'en-tête). */
typedef enum
{
  RADIO_STACK_MODE4 = 0,       /**< mode 4 : émission possible pendant l'écoute */
  RADIO_STACK_TOO_OLD,         /**< pile antérieure à 7.1b : mode du module gardé */
  RADIO_STACK_MODE4_REFUSED    /**< mode 4 refusé par le module : mode du module gardé */
} radio_stack_t;

/** Une trame reçue. */
typedef struct
{
  uint8_t len;                 /**< longueur de la trame */
  uint8_t pdu[MESH_PDU_MAX];   /**< trame (sans l'emballage de la publicité) */
  int8_t  rssi;                /**< puissance de réception, en dBm */
} radio_frame_t;

/** Compteurs de la radio (commande « stats »). */
typedef struct
{
  uint32_t adv_reports;        /**< publicités entendues (toutes, téléphones compris) */
  uint32_t mesh_frames;        /**< dont trames du réseau maillé */
  uint32_t rx_overflow;        /**< trames perdues : file de réception pleine */
  uint32_t tx_started;         /**< émissions lancées */
  uint32_t tx_refused;         /**< émissions refusées par le module (code : mesh_radio_last_refusal) */
  uint32_t tx_stop_errors;     /**< arrêts d'émission refusés (réessayés) */
  uint32_t scan_starts;        /**< lancements de l'écoute */
  uint32_t scan_errors;        /**< lancements refusés (réessayés) */
  uint32_t events_lost;        /**< évènements perdus par le module (non lus à temps) */
  uint32_t module_restarts;    /**< redémarrages inattendus du module, remis en service */
} radio_stats_t;

/**
 * @brief  Démarre le module : liaison SPI, version de la pile, mode 4 si la
 *         pile le permet, adresse, rôles « diffuseur » et « observateur »,
 *         puissance, nombre aléatoire, puis lance l'écoute. Environ 150 ms
 *         (le double si le module refuse le mode 4 : second essai sans).
 * @retval RADIO_OK, ou la cause de l'échec
 */
radio_status_t mesh_radio_init(void);

/** Vrai si le module a démarré (mesh_radio_init a rendu RADIO_OK). */
bool mesh_radio_is_ready(void);

/**
 * @brief  À appeler à chaque tour de boucle : évènements du module (trames
 *         reçues), fin des émissions, relance de l'écoute, remise en
 *         service après un redémarrage inattendu du module.
 */
void mesh_radio_process(void);

/** Vrai si une émission peut commencer (module prêt, pas d'émission en cours). */
bool mesh_radio_tx_ready(void);

/**
 * @brief  Diffuse une trame pendant RADIO_TX_TIME_MS.
 * @param  pdu  trame codée (mesh_pdu_encode)
 * @param  len  longueur
 * @retval true si l'émission a commencé
 */
bool mesh_radio_send(const uint8_t *pdu, uint8_t len);

/**
 * @brief  Rend la plus ancienne trame reçue en attente.
 * @retval false s'il n'y en a pas
 */
bool mesh_radio_receive(radio_frame_t *out);

/**
 * @brief  Règle la puissance d'émission : le niveau le plus proche de dbm
 *         parmi -18, -15, -11, -8, -5, -2, 2, 5 et 8 dBm (puissance conduite,
 *         UM1865 table 292 ; mêmes réglages pour les deux modules).
 * @param  dbm      puissance souhaitée
 * @param  applied  reçoit la puissance retenue (peut être NULL)
 * @retval false si le module a refusé (la puissance précédente reste)
 */
bool mesh_radio_set_tx_power(int8_t dbm, int8_t *applied);

/** Puissance d'émission actuelle, en dBm. */
int8_t mesh_radio_tx_power(void);

/** Mode d'écoute pendant les émissions. */
radio_mode_t mesh_radio_mode(void);

/**
 * @brief  Choix du mode : forced = false rend le choix automatique (mode
 *         alterné) ; forced = true impose le mode donné. L'écoute en cours
 *         est relancée avec les temps du nouveau mode.
 */
void mesh_radio_set_mode(radio_mode_t mode, bool forced);

/** Vrai si le mode est imposé par l'utilisateur. */
bool mesh_radio_mode_forced(void);

/**
 * @brief  Code du dernier refus d'émission par le module (0x12 : paramètres
 *         refusés, 0x0C : commande refusée...), 0 si aucun refus.
 */
uint8_t mesh_radio_last_refusal(void);

/**
 * @brief  Code du refus qui a fait revenir le programme du mode simultané au
 *         mode alterné ; 0 si cela n'est pas arrivé depuis le dernier choix
 *         de mode.
 */
uint8_t mesh_radio_fallback_code(void);

/**
 * @brief  Rend vrai une seule fois après un retour automatique au mode
 *         alterné, avec le code du refus, pour le signaler dans le terminal.
 */
bool mesh_radio_take_fallback(uint8_t *code);

/**
 * @brief  Base de temps du module (commande Aci_Hal_Get_Anchor_Period) :
 *         période et plus grand créneau encore libre, en pas de 0,625 ms.
 *         Utile pour comprendre le mode simultané (voir l'en-tête).
 * @retval false si le module n'a pas répondu à la commande
 */
bool mesh_radio_time_base(uint32_t *period, uint32_t *free_slot);

/** Mode de la pile retenu au démarrage. */
radio_stack_t mesh_radio_stack(void);

/** Version de la pile Bluetooth du module, au format 0xJJMN de
 *  getBlueNRGVersion (0x0715 = 7.1e, 0x0723 = 7.2c) ; 0 si inconnue. */
uint16_t mesh_radio_fw_version(void);

/** Version du circuit BlueNRG-MS (0x31 = révision 3.1) ; 0 si inconnue. */
uint8_t mesh_radio_hw_version(void);

/**
 * @brief  Écrit une version de pile sous la forme habituelle de ST :
 *         0x0715 -> « 7.1e », 0x0723 -> « 7.2c » (la lettre compte à partir
 *         de a = 1, note DT0074), 0x0730 -> « 7.3 ».
 * @retval nombre de caractères écrits (comme snprintf)
 */
int mesh_radio_version_text(uint16_t fw_version, char *text, unsigned size);

/** Vrai si l'écoute tourne en ce moment. */
bool mesh_radio_is_scanning(void);

/** Adresse Bluetooth de la carte (octet de poids faible en premier). */
void mesh_radio_get_address(uint8_t address[6]);

/** Nombre aléatoire tiré par le module au démarrage (commande HCI LE Rand). */
uint32_t mesh_radio_random_seed(void);

/** Compteurs. */
const radio_stats_t *mesh_radio_stats(void);

/** Remet les compteurs à zéro. */
void mesh_radio_stats_clear(void);

/** Texte court qui décrit un résultat de mesh_radio_init (pour le terminal). */
const char *mesh_radio_status_text(radio_status_t status);

#endif /* MESH_RADIO_H */
