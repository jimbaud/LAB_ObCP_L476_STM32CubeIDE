/**
 ******************************************************************************
 * @file    config_store.h
 * @brief   Enregistrement des réglages de la carte dans l'EEPROM M95640 de
 *          l'ObCP (commandes « sauve » et « oublie »), relus au démarrage.
 *
 *  L'ObCP porte une mémoire EEPROM SPI de 8 ko (U7, M95640) sur le même bus
 *  SPI que le module Bluetooth (SPI2 : PB13, PB14, PB15), avec sa propre
 *  broche de sélection EEPROM_CS (PC3). Elle garde son contenu sans
 *  alimentation, et aussi quand on recharge un programme (même avec
 *  « Full chip erase », qui n'efface que la flash du STM32). On y range un
 *  bloc de 32 octets, à l'adresse CONFIG_EEPROM_ADDRESS (app_config.h) :
 *
 *    octets  contenu
 *    0..3    « MESH » : reconnaît un bloc de ce programme
 *    4       version du bloc (1)
 *    5       nombre d'octets de réglages qui suivent (12)
 *    6..17   réglages (voir stored_config_t, un octet par champ, groupes
 *            sur 2 octets poids faible en premier)
 *    18..29  libres (0)
 *    30..31  CRC-16 (CCITT) des octets 0 à 29 : détecte un bloc abîmé
 *
 *  PARTAGE DU BUS SPI : le module Bluetooth lit ses paquets dans une
 *  interruption (broche IRQ, PB12). Pendant chaque échange avec l'EEPROM,
 *  cette interruption est masquée, pour que les deux ne se mélangent pas
 *  sur le bus ; les paquets du module attendent quelques microsecondes.
 *
 *  Documentation du circuit : https://www.st.com/resource/en/datasheet/m95640-r.pdf
 ******************************************************************************
 */
#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdbool.h>
#include <stdint.h>

/** Réglages enregistrés. */
typedef struct
{
  uint8_t  id;            /**< numéro fixé (1 à 239), 0 : numéro automatique */
  uint8_t  net;           /**< numéro de réseau */
  uint16_t groups;        /**< groupes suivis (bit g-1 pour le groupe g) */
  uint8_t  ttl;
  bool     relay;
  uint8_t  suppress;
  int8_t   rssi_min;      /**< dBm, -127 : aucun seuil */
  int8_t   tx_dbm;
  uint8_t  hello_s;
  uint8_t  auto_s;        /**< envoi automatique des mesures, s (0 : aucun) */
  bool     flash;         /**< éclairs d'activité de la LED */
} stored_config_t;

/** Résultat d'une opération. */
typedef enum
{
  STORE_OK = 0,
  STORE_EMPTY,            /**< rien d'enregistré (ou bloc abîmé) : réglages par défaut */
  STORE_NO_EEPROM,        /**< l'EEPROM ne répond pas (toujours occupée) */
  STORE_VERIFY_FAILED     /**< relecture différente de l'écriture (EEPROM absente ?) */
} store_status_t;

/** Lit les réglages enregistrés. */
store_status_t config_store_load(stored_config_t *c);

/** Enregistre les réglages (environ 5 ms), puis vérifie par relecture. */
store_status_t config_store_save(const stored_config_t *c);

/** Efface le bloc de réglages (retour aux valeurs par défaut au démarrage). */
store_status_t config_store_erase(void);

/** Texte court qui décrit un résultat. */
const char *config_store_status_text(store_status_t s);

/** CRC-16 CCITT (polynôme 0x1021, départ 0xFFFF) : fonction pure, testée sur PC. */
uint16_t config_store_crc16(const uint8_t *data, uint32_t len);

#endif /* CONFIG_STORE_H */
