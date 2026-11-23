/**
 ******************************************************************************
 * @file    report.h
 * @brief   Tout ce que la carte écrit dans le terminal (port COM virtuel) :
 *          messages reçus, résultats des pings et des traces, journal des
 *          trames, et lignes JSON pour l'outil PC.
 *
 *  Deux présentations :
 *    - TEXTE (par défaut) : des phrases, précédées de l'heure de la carte en
 *      secondes depuis le démarrage, par exemple
 *        [    12.345] Message de 5 (2 relais, -61 dBm) : Bonjour
 *      Avec « journal on », chaque trame émise, relayée, reçue, filtrée,
 *      annulée... a aussi sa ligne : on suit l'inondation trame par trame.
 *    - JSON (« pc on ») : un objet JSON par ligne et par évènement, pour
 *      l'outil PC (Tools/obcp_mesh_monitor.py). Exemple :
 *        {"ev":"rx","t":12345,"src":5,"dst":255,"seq":42,"ttl":3,"hops":1,
 *         "last":9,"type":"TEXTE","rssi":-61,"text":"Bonjour",
 *         "data":"426F6E6A6F7572"}
 *      Les réponses aux commandes restent en texte : une ligne qui ne
 *      commence pas par « { » est du texte.
 *  Format complet des lignes JSON : documentation du programme
 *  (Doc/Documentation_Programme_BLE_Mesh_ObCP.pdf), « Lignes JSON pour l'outil PC ».
 ******************************************************************************
 */
#ifndef REPORT_H
#define REPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "mesh.h"
#include "node.h"           /* node_measure_t */
#include "settings.h"

/** Présentation JSON (true) ou texte (false). */
void report_set_json(bool on);
bool report_is_json(void);

/** Journal des trames (présentation texte). */
void report_set_journal(bool on);
bool report_is_journal(void);

/** Écrit une ligne de texte telle quelle (réponse à une commande). */
void report_print(const char *text);

/** Comme printf (255 caractères au plus) : réponse à une commande. */
void report_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));

/** Évènement du réseau (appelé pour chaque mesh_event_t). */
void report_mesh_event(const mesh_t *m, mesh_event_t ev, const mesh_frame_t *f, int8_t rssi);

/**
 * @brief  Message reçu par la carte (présentation texte seulement : en JSON,
 *         l'évènement « rx » contient déjà tout).
 * @param  f         trame
 * @param  rssi      puissance de réception
 * @param  reply_ms  pour une demande (PING, DEM_MESURE) : délai avant la
 *                   réponse, en ms ; -1 si le message n'appelle pas de réponse
 */
void report_message(const mesh_frame_t *f, int8_t rssi, int32_t reply_ms);

/** Réponse à un ping. */
void report_pong(const mesh_frame_t *f, int8_t rssi, uint32_t rtt_ms);

/** Fin d'un ping : délai écoulé, replies réponses reçues. */
void report_ping_end(uint8_t dst, uint8_t ping_id, uint8_t replies);

/** Résultat d'une trace (trame TRACE_RET reçue). */
void report_trace(const mesh_frame_t *f, uint32_t rtt_ms);

/** Trace sans réponse. */
void report_trace_timeout(uint8_t dst, uint8_t trace_id);

/** Un réglage de la carte a été changé à distance par le nœud by. */
void report_setting_changed(uint8_t by, setting_t key, int32_t value, bool accepted);

/** Retour automatique au mode alterné : le module a refusé d'émettre pendant
 *  l'écoute (code du refus, voir mesh_radio.h). Toujours affiché. */
void report_radio_fallback(uint8_t code);

/** Mesures de cette carte (commande « mesure »), dans les mêmes unités que
 *  les mesures reçues : « Mesures de cette carte : 24 °C, X = 0.01 g... ». */
void report_measure_local(const node_measure_t *m);

/** Présentation au branchement du câble USB (nom, version, numéro). */
void report_banner(const mesh_t *m);

/** Identité et réglages en JSON (au passage en mode JSON et pour « info »). */
void report_info_json(const mesh_t *m);

#endif /* REPORT_H */
