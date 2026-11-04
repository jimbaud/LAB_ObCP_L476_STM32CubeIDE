/**
 ******************************************************************************
 * @file    ncp_wifi.h
 * @brief   Fonctions Wi-Fi du ST67W611M1 : scan, station (STA), point d'accès
 *          (SoftAP). Toutes les fonctions retournent 0 (AT_OK) en cas de
 *          succès, une valeur négative sinon (at_status_t ou WIFI_ERR_xxx).
 *
 *  Vocabulaire :
 *   - station (STA) : le module se connecte, comme un téléphone, au réseau
 *     d'une box ou d'un routeur ;
 *   - point d'accès (SoftAP) : le module crée son propre réseau Wi-Fi, que
 *     rejoint le téléphone (démo 3) ; les deux peuvent fonctionner ensemble ;
 *   - SSID : nom du réseau ; BSSID : adresse MAC du point d'accès ;
 *   - RSSI : puissance reçue en dBm (-40 : excellent, -80 : très faible).
 *
 *  L'état de la station est tenu à jour par les événements +CW: du module
 *  (wifi_urc()) dans la variable globale g_wifi, lue par l'application.
 ******************************************************************************
 */
#ifndef NCP_WIFI_H
#define NCP_WIFI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SCAN_MAX         40U   /* réseaux mémorisés par un balayage   */
#define WIFI_SSID_MAX         32U   /* longueur maximale d'un SSID          */
#define WIFI_PASS_MAX         64U   /* longueur maximale d'un mot de passe  */

#define WIFI_ERR_REASON      (-20)  /* échec signalé par +CW:ERROR,<raison>    */
#define WIFI_ERR_TIMEOUT     (-21)  /* pas d'événement de connexion            */
#define WIFI_ERR_NOIP        (-22)  /* associé mais pas d'adresse DHCP         */
#define WIFI_ERR_CHARS       (-23)  /* caractère '"' interdit dans SSID / clé  */
#define WIFI_ERR_NOTCONN     (-24)  /* station non connectée                   */

/* Sécurité (champ <ecn> de +CWLAP) */
enum
{
  WIFI_SEC_OPEN = 0, WIFI_SEC_WEP, WIFI_SEC_WPA, WIFI_SEC_WPA2, WIFI_SEC_WPA_WPA2,
  WIFI_SEC_WPA_ENT, WIFI_SEC_WPA3, WIFI_SEC_WPA2_WPA3, WIFI_SEC_UNKNOWN
};

/** Un réseau trouvé par wifi_scan() (une ligne +CWLAP:). */
typedef struct
{
  char    ssid[WIFI_SSID_MAX + 1U];
  int8_t  rssi;          /* dBm                                  */
  uint8_t channel;
  uint8_t security;      /* WIFI_SEC_xxx                         */
  uint8_t cipher;        /* 0 aucun, 1 WEP, 3 TKIP, 4 AES, 5 TKIP+AES */
  uint8_t proto;         /* masque : 1 b, 2 g, 4 n, 8 ax         */
  uint8_t wps;
  uint8_t bssid[6];
} wifi_ap_t;

/** États successifs de la station : IDLE -> CONNECTING -> CONNECTED -> GOT_IP. */
typedef enum
{
  WIFI_STA_OFF = 0,      /* Wi-Fi pas encore initialisé                  */
  WIFI_STA_IDLE,         /* non connectée                                */
  WIFI_STA_CONNECTING,   /* tentative en cours                           */
  WIFI_STA_CONNECTED,    /* associée au point d'accès, sans adresse IP   */
  WIFI_STA_GOT_IP        /* adresse IP obtenue : liaison utilisable      */
} wifi_sta_state_t;

/** Caractéristiques de la liaison en cours (réponse à AT+CWJAP?). */
typedef struct
{
  char    ssid[WIFI_SSID_MAX + 1U];
  uint8_t bssid[6];
  uint8_t channel;
  int16_t rssi;
} wifi_link_t;

/* État partagé, mis à jour par les événements +CW: du module. Les champs
 * evt_xxx sont des « drapeaux » : mis à vrai par wifi_urc() quand l'événement
 * arrive, remis à faux par le code qui l'a traité. « volatile » : ils changent
 * pendant les attentes (at_poll()), le compilateur doit les relire.     */
typedef struct
{
  volatile wifi_sta_state_t sta;
  volatile bool    evt_connected;
  volatile bool    evt_gotip;
  volatile bool    evt_disconnected;
  volatile bool    evt_reason;
  volatile bool    evt_scan_done;
  volatile int32_t last_reason;

  wifi_ap_t        scan[WIFI_SCAN_MAX];
  volatile uint8_t scan_count;
  volatile bool    scan_truncated;

  volatile bool    ap_running;
  volatile uint8_t ap_clients;
  volatile bool    evt_ap_join;
  volatile bool    evt_ap_leave;
  volatile bool    evt_ap_ip;
  uint8_t          ap_evt_mac[6];
  char             ap_evt_ip[16];
} wifi_ctx_t;

extern wifi_ctx_t g_wifi;      /* état Wi-Fi courant (défini dans ncp_wifi.c) */

/**
 * @brief  Traite un événement +CW: ou une ligne de scan +CWLAP: (appelée
 *         par urc_router() dans ncp.c).
 * @retval true si la ligne concerne le Wi-Fi (traitée), false sinon.
 */
bool        wifi_urc(const char *line);

/**
 * @brief  Initialise le Wi-Fi : mode station, pas de reconnexion par le module
 *         (c'est l'application qui décide), pays (canaux autorisés).
 * @param  country  "EU", "US", "JP", "CN" ou "00" (mondial).
 */
int         wifi_init(const char *country);

/**
 * @brief  Balayage des réseaux visibles ; résultat dans g_wifi.scan[]
 *         (g_wifi.scan_count réseaux).
 * @param  passive     true : écoute des balises seulement (plus lent, discret).
 * @param  channel     canal 1 à 13, ou 0 pour tous les canaux.
 * @param  timeout_ms  délai maximal.
 */
int         wifi_scan(bool passive, uint8_t channel, uint32_t timeout_ms);

/**
 * @brief  Lance une connexion de la station SANS attendre le résultat : la
 *         suite arrive par les événements (g_wifi.sta, evt_gotip, evt_reason).
 *         Utilisée pour la (re)connexion en tâche de fond (demo_net.c).
 * @param  ssid  nom du réseau (le caractère " est interdit).
 * @param  pass  mot de passe ("" ou NULL : réseau ouvert).
 */
int         wifi_connect_start(const char *ssid, const char *pass);

/**
 * @brief  Connexion de la station avec attente du résultat (bloquante).
 * @param  wait_ip     true : attend aussi l'adresse IP (DHCP).
 * @param  timeout_ms  délai maximal (0 : 25 s).
 * @retval AT_OK, WIFI_ERR_REASON (raison dans g_wifi.last_reason),
 *         WIFI_ERR_TIMEOUT, WIFI_ERR_NOIP...
 */
int         wifi_connect(const char *ssid, const char *pass, bool wait_ip, uint32_t timeout_ms);

/** @brief  Déconnecte la station du réseau (attend la confirmation, 5 s au plus). */
int         wifi_disconnect(void);

/**
 * @brief  Lit les caractéristiques de la liaison en cours (SSID, BSSID, canal, RSSI).
 * @retval AT_OK, ou WIFI_ERR_NOTCONN si la station n'est pas connectée.
 */
int         wifi_link_info(wifi_link_t *link);

/** @brief  Lit la puissance reçue (dBm) de la liaison en cours. */
int         wifi_rssi(int16_t *rssi);

/**
 * @brief  Sécurité et norme de la liaison en cours (AT+CWSTATE?).
 * @param  security   reçoit WIFI_SEC_xxx.
 * @param  proto_idx  reçoit 1 (b), 2 (g), 3 (n, Wi-Fi 4) ou 4 (ax, Wi-Fi 6).
 */
int         wifi_conn_details(uint8_t *security, uint8_t *proto_idx);

/**
 * @brief  Démarre le point d'accès (mode station + point d'accès ; une
 *         connexion de la station en cours est conservée).
 * @param  ssid      nom du réseau créé.
 * @param  pass      "" : réseau ouvert, sinon WPA2 (8 caractères au moins).
 * @param  channel   canal 1 à 13 (imposé par la box si la station est connectée).
 * @param  max_conn  nombre maximal d'appareils (1 à 4).
 */
int         wifi_ap_start(const char *ssid, const char *pass, uint8_t channel, uint8_t max_conn);

/** @brief  Arrête le point d'accès (retour au mode station seule). */
int         wifi_ap_stop(void);

/** @brief  Compte les appareils connectés au point d'accès (AT+CWLIF). */
int         wifi_ap_clients(uint8_t *count);

/** @brief  Vrai si la station est associée à un réseau (avec ou sans adresse IP). */
bool        wifi_is_connected(void);

/** @brief  Nom d'un type de sécurité WIFI_SEC_xxx (« WPA2 »...). */
const char *wifi_security_str(uint8_t sec);

/** @brief  Normes annoncées par un réseau (masque 1 b, 2 g, 4 n, 8 ax) : « b/g/n/ax »... */
const char *wifi_proto_str(uint8_t proto_mask);

/** @brief  Explication en français d'un code de raison d'échec (+CW:ERROR,<raison>). */
const char *wifi_reason_str(int32_t reason);

/** @brief  Texte en français d'un code WIFI_ERR_xxx (ou at_status_t). */
const char *wifi_err_str(int err);

#ifdef __cplusplus
}
#endif

#endif /* NCP_WIFI_H */
