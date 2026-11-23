/**
 ******************************************************************************
 * @file    settings.h
 * @brief   Liste des réglages de la carte modifiables pendant le
 *          fonctionnement : nom tapé dans le terminal, limites, lecture de
 *          la valeur.
 *
 *  Un même réglage se change de trois façons :
 *    - sur la carte, par le terminal :       ttl 3
 *    - à distance, par une trame REGLAGE :   regle tous ttl 3
 *    - au démarrage, depuis l'EEPROM :       sauve (puis redémarrage)
 *  Ce module ne fait que décrire les réglages (fonctions « pures », testées
 *  sur PC) ; node.c les applique (node_setting_set).
 *
 *    nom        valeurs                    effet
 *    ttl        0 à 15                     TTL des trames émises
 *    relais     on / off                   relaie les trames des autres
 *    suppr      0 à 10                     annule un relais après N autres relais entendus (0 : jamais)
 *    rssimin    -100 à -20 dBm, ou off     ignore les trames plus faibles
 *    puissance  -18 à 8 dBm                puissance d'émission (niveau le plus proche)
 *    hello      0 à 255 s                  période des HELLO (0 : aucun)
 *    auto       0 à 255 s                  envoi automatique des mesures à tous (0 : aucun)
 *    flash      on / off                   éclairs de la LED à chaque trame relayée ou reçue
 ******************************************************************************
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

/** Réglages. Leur numéro est aussi celui transmis dans une trame REGLAGE :
 *  ne pas changer l'ordre (ajouter les nouveaux à la fin). */
typedef enum
{
  SETTING_TTL = 1,
  SETTING_RELAY,
  SETTING_SUPPRESS,
  SETTING_RSSI_MIN,
  SETTING_TX_POWER,
  SETTING_HELLO,
  SETTING_AUTO,
  SETTING_FLASH,
  SETTING_END            /**< fin de la liste (pas un réglage) */
} setting_t;

/** Valeur de rssimin qui signifie « pas de seuil » (MESH_RSSI_MIN_OFF). */
#define SETTING_RSSI_OFF   (-127)

/** Nom du réglage tel qu'il se tape dans le terminal ("ttl", "relais"...),
 *  ou "?" pour un numéro inconnu. */
const char *settings_name(setting_t key);

/** Retrouve un réglage par son nom. */
bool settings_from_name(const char *name, setting_t *key);

/** Vrai pour un réglage « on / off ». */
bool settings_is_boolean(setting_t key);

/**
 * @brief  Lit la valeur d'un réglage tapée dans le terminal et vérifie ses
 *         limites. Accepte on / off (et oui / non, 1 / 0) pour les réglages
 *         on / off, « off » pour rssimin, un entier décimal (éventuellement
 *         négatif) pour les autres.
 * @retval true si la valeur est acceptable (rangée dans *value)
 */
bool settings_parse_value(setting_t key, const char *text, int32_t *value);

/** Vérifie les limites d'une valeur (reçue d'une trame REGLAGE, par exemple). */
bool settings_value_ok(setting_t key, int32_t value);

/** Écrit une valeur lisible : « on », « off », « -60 dBm », « 20 s »... */
int settings_format_value(setting_t key, int32_t value, char *text, unsigned size);

/** Texte d'aide d'un réglage (valeurs permises). */
const char *settings_help(setting_t key);

#endif /* SETTINGS_H */
