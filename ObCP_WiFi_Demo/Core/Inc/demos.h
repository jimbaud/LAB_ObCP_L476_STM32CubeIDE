/**
 ******************************************************************************
 * @file    demos.h
 * @brief   Commandes du terminal et services des quatre démonstrations.
 *
 *   Démo 1 (demo_scan.c) : scanner Wi-Fi, occupation des canaux, suivi RSSI
 *   Démo 2 (demo_net.c)  : connexion, IP (DHCP / fixe), DNS, ping, SNTP -> RTC
 *   Démo 3 (demo_web.c)  : point d'accès (SoftAP) + serveur web
 *   Démo 4 (demo_mqtt.c) : télémétrie et commandes MQTT
 ******************************************************************************
 */
#ifndef DEMOS_H
#define DEMOS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "timeutil.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Type d'une fonction de commande du terminal. Comme main() en C :
 *         argc = nombre de mots de la ligne tapée, argv = les mots
 *         (« ping 8.8.8.8 4 » : argc = 3, argv[0] = "ping", argv[1] = "8.8.8.8").
 * @retval 0 si la commande a réussi, sinon un code d'erreur négatif.
 */
typedef int (*cmd_fn_t)(int argc, char **argv);

/* ---- Commandes générales (cli.c) ------------------------------------------ */
/**
 * @brief  Exécute une ligne tapée dans le terminal : découpe en mots, recherche
 *         de la commande dans la table s_cmds[] (cli.c), appel de sa fonction.
 * @param  line  ligne tapée (modifiée par le découpage).
 * @retval code de retour de la commande.
 */
int  cli_exec(char *line);

/* ---- Démo 1 : scanner (demo_scan.c) --------------------------------------------- */
/** @brief  Commande « scan » : réseaux visibles (signal, canal, sécurité, norme). */
int  cmd_scan(int argc, char **argv);
/** @brief  Commande « channels » : occupation des canaux 1 à 13 et canal conseillé. */
int  cmd_channels(int argc, char **argv);
/** @brief  Commande « track » : suivi du niveau de signal en direct (LED RVB, CSV). */
int  cmd_track(int argc, char **argv);
/** @brief  Âge du dernier balayage en secondes, UINT32_MAX s'il n'y en a pas eu. */
uint32_t scan_age_s(void);
/** @brief  Balayage complet sans affichage détaillé (page web) ; quiet : sans message. */
int  scan_run(bool quiet);
/** @brief  Norme la plus récente annoncée par un réseau : « Wi-Fi 6 », « Wi-Fi 4 »... */
const char *scan_std_label(uint8_t proto);

/* ---- Démo 2 : réseau (demo_net.c) ------------------------------------------------ */
/** @brief  Commande « connect [ssid [mot de passe]] » : connexion à un réseau Wi-Fi. */
int  cmd_connect(int argc, char **argv);
/** @brief  Commande « disconnect » : déconnexion (reconnexion automatique suspendue). */
int  cmd_disconnect(int argc, char **argv);
/** @brief  Commande « status » : état de la liaison Wi-Fi et configuration IP. */
int  cmd_status(int argc, char **argv);
/** @brief  Commande « ip » : adressage DHCP ou adresse fixe. */
int  cmd_ip(int argc, char **argv);
/** @brief  Commande « dns <nom> » : résolution d'un nom en adresse IP. */
int  cmd_dns(int argc, char **argv);
/** @brief  Commande « ping <hôte> [nombre] [intervalle] » : test d'accessibilité. */
int  cmd_ping(int argc, char **argv);
/** @brief  Commande « ntp [serveur] » : mise à l'heure de la RTC par SNTP. */
int  cmd_ntp(int argc, char **argv);
/** @brief  Commande « time » : heure de la RTC (UTC et locale). */
int  cmd_time(int argc, char **argv);

/** @brief  Au démarrage : programme la connexion automatique à la box enregistrée (wifi.auto). */
void netapp_boot(void);
/** @brief  Tâche de fond : événements Wi-Fi, (re)connexion automatique, resynchronisation NTP. */
void netapp_poll(void);
/** @brief  Le module a redémarré : la liaison voulue sera rétablie. */
void netapp_module_restarted(void);
/** @brief  Vrai si la station est connectée et a une adresse IP. */
bool netapp_sta_ready(void);
/** @brief  Vrai pendant une tentative de connexion de la station en tâche de fond. */
bool netapp_connecting(void);
/** @brief  Adresse IP de la station en texte ("" si non connectée). */
const char *netapp_ip(void);
/**
 * @brief  Connexion bloquante avec le SSID et le mot de passe de la configuration.
 * @param  verbose  true : affiche le détail de la liaison obtenue.
 */
int  netapp_connect(bool verbose);
/** @brief  Connexion différée (après la réponse HTTP de la page de configuration). */
void netapp_request_connect(void);
/** @brief  Déconnexion volontaire (plus de reconnexion automatique). */
void netapp_disconnect(void);
/* Heure */
/** @brief  Vrai si l'heure de la RTC a été réglée par NTP. */
bool netapp_time_synced(void);
/**
 * @brief  Heure locale (fuseau time.tz et heure d'été time.dst appliqués).
 * @param  local     reçoit la date et l'heure locales.
 * @param  offset_s  reçoit le décalage par rapport à l'UTC en secondes (peut être NULL).
 */
bool netapp_local_time(dt_t *local, int32_t *offset_s);
/** @brief  Heure UTC au format ISO 8601 (« 2026-10-23T08:15:00Z »), "" si non synchronisée. */
void netapp_iso_utc(char *out, size_t len);
/** @brief  Affiche la ligne « Heure » des commandes info et time. */
void netapp_print_time(void);

/* ---- Démo 3 : serveur web (demo_web.c) ----------------------------------------- */
/** @brief  Commande « web start | stop | status » : point d'accès et serveur web. */
int  cmd_web(int argc, char **argv);
/** @brief  Au démarrage : programme le lancement automatique du serveur web (web.auto). */
void web_boot(void);
/** @brief  Tâche de fond : connexions et requêtes HTTP, événements du point d'accès. */
void web_poll(void);
/** @brief  Vrai si le serveur web est actif. */
bool web_running(void);
/** @brief  Le module a redémarré : serveur et point d'accès relancés s'ils étaient actifs. */
void web_module_restarted(void);

/* ---- Démo 4 : MQTT (demo_mqtt.c) ----------------------------------------------- */
/** @brief  Commande « mqtt start | stop | status | pub | sub | trace ». */
int  cmd_mqtt(int argc, char **argv);
/** @brief  Tâche de fond : (re)connexion au broker, télémétrie périodique, commandes reçues. */
void mqttapp_poll(void);
/**
 * @brief  Rappel du pilote pour chaque message reçu : le message est seulement
 *         copié dans une file, traité ensuite par mqttapp_poll().
 */
void mqttapp_on_message(const char *topic, const uint8_t *msg, uint16_t len);
/** @brief  Publie l'appui d'un bouton (IO_BTN_xxx) sur <racine>/event si MQTT est connecté. */
void mqttapp_button(uint8_t btn);
/** @brief  La station vient d'obtenir son adresse IP : démarrage si mqtt.auto = 1. */
void mqttapp_sta_up(void);
/** @brief  La station a perdu sa liaison : reconnexion au broker programmée. */
void mqttapp_sta_down(void);
/** @brief  Le module a redémarré : reconnexion au broker programmée. */
void mqttapp_module_restarted(void);
/** @brief  Vrai si le client MQTT est connecté au broker. */
bool mqttapp_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* DEMOS_H */
