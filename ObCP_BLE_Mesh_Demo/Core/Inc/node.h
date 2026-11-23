/**
 ******************************************************************************
 * @file    node.h
 * @brief   Le nœud de démonstration : relie le réseau maillé (mesh.c) à la
 *          radio (mesh_radio.c), à la LED, à l'accéléromètre et au terminal,
 *          et réagit aux messages reçus.
 *
 *  Ce que fait la carte quand elle reçoit un message qui lui est destiné :
 *
 *    type        réaction
 *    TEXTE       affiche le texte dans le terminal, éclair bleu
 *    LED         couleur de la LED (pendant la durée demandée)
 *    VAGUE       LED de la couleur du nombre de relais (rouge, orange, jaune...)
 *    IDENT       LED blanche clignotante (trouver la carte n° N dans la salle)
 *    PING        répond PONG (RSSI et relais de l'aller)
 *    TRACE       répond TRACE_RET avec la route de l'aller
 *    DEM_MESURE  répond MESURE (accélération et température)
 *    REGLAGE     applique le réglage (ttl, relais, suppr...)
 *    HELLO       rien (le réseau s'en sert ; l'outil PC dessine la carte)
 *
 *  À une demande adressée à un groupe ou à tous, chaque carte répond après
 *  un délai aléatoire (jusqu'à NODE_REPLY_SPREAD_MS), sinon toutes les
 *  réponses partiraient en même temps.
 *
 *  Le bouton SW1 lance une vague. Si le réglage « auto » est non nul, la
 *  carte envoie ses mesures à tous toutes les « auto » secondes.
 ******************************************************************************
 */
#ifndef NODE_H
#define NODE_H

#include <stdbool.h>
#include <stdint.h>

#include "config_store.h"
#include "led_fx.h"
#include "mesh.h"
#include "mesh_radio.h"
#include "settings.h"

/** Mesures de la carte (accéléromètre LIS3DH). */
typedef struct
{
  bool    accel_ok;      /**< accélération valable */
  bool    temp_ok;       /**< température valable */
  int16_t ax_mg;         /**< accélération en mg (1 g = 1000 mg) */
  int16_t ay_mg;
  int16_t az_mg;
  int8_t  temp_c;        /**< température en °C (ordre de grandeur) */
} node_measure_t;

/**
 * @brief  Prépare le nœud : étiquette tirée de l'identifiant unique du
 *         STM32, générateur aléatoire, réseau maillé, réglages enregistrés
 *         dans l'EEPROM (s'il y en a).
 * @param  radio_status  résultat de mesh_radio_init()
 */
void node_init(radio_status_t radio_status);

/** À chaque tour de boucle : trames reçues, réseau, réponses différées,
 *  délais d'attente des pings et des traces. */
void node_process(void);

/** Chaque seconde (TIM7) : envoi automatique des mesures. */
void node_on_second(void);

/** Appui sur le bouton SW1 : lance une vague. */
void node_on_button(void);

/** Le réseau maillé de la carte (lecture des tables et compteurs). */
mesh_t *node_mesh(void);

/** Résultat du démarrage de la radio. */
radio_status_t node_radio_status(void);

/** Résultat de la lecture des réglages enregistrés, au démarrage. */
store_status_t node_load_status(void);

/** État des réglages enregistrés, pour « info » : lus au démarrage,
 *  enregistrés par « sauve », effacés par « oublie », EEPROM muette... */
const char *node_store_text(void);

/* ---- Actions (commandes du terminal) -------------------------------------- */

/** Envoie un texte (14 octets au plus ; au-delà, il est coupé). */
mesh_result_t node_send_text(uint8_t dst, const char *text);
/** Envoie une couleur pour la LED, pendant seconds (0 : sans limite). */
mesh_result_t node_send_led(uint8_t dst, led_color_t c, uint8_t seconds);
/** Lance une vague (à tous). */
mesh_result_t node_send_wave(void);
/** Demande l'identification (LED blanche clignotante) pendant seconds. */
mesh_result_t node_send_ident(uint8_t dst, uint8_t seconds);
/** Envoie un ping ; *ping_id reçoit son numéro. */
mesh_result_t node_ping(uint8_t dst, uint8_t *ping_id);
/** Cherche la route vers un nœud ; *trace_id reçoit le numéro de la trace. */
mesh_result_t node_trace(uint8_t dst, uint8_t *trace_id);
/** Demande les mesures. */
mesh_result_t node_request_measure(uint8_t dst);
/** Change un réglage à distance. */
mesh_result_t node_send_setting(uint8_t dst, setting_t key, int32_t value);

/** Mesures de cette carte. */
void node_measure(node_measure_t *m);

/* ---- Réglages -------------------------------------------------------------- */

/**
 * @brief  Applique un réglage sur cette carte.
 * @param  key      réglage
 * @param  value    valeur (limites vérifiées)
 * @param  applied  reçoit la valeur retenue (puissance : niveau le plus
 *                  proche) ; peut être NULL
 * @retval false si la valeur est hors limites ou refusée par la radio
 */
bool node_setting_set(setting_t key, int32_t value, int32_t *applied);

/** Valeur actuelle d'un réglage. */
int32_t node_setting_get(setting_t key);

/** Fixe le numéro de la carte (1 à 239), ou 0 pour le numéro automatique. */
bool node_set_id(uint8_t id);

/** Change de réseau (0 à 255). */
void node_set_net(uint8_t net);

/* ---- Enregistrement dans l'EEPROM ----------------------------------------- */

/** Enregistre le numéro (s'il est fixé), le réseau, les groupes et les réglages. */
store_status_t node_save(void);

/** Efface les réglages enregistrés (valeurs par défaut au prochain démarrage). */
store_status_t node_forget(void);

#endif /* NODE_H */
