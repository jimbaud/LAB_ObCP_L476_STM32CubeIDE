/**
 ******************************************************************************
 * @file    ncp_net.h
 * @brief   Services réseau du ST67W611M1 en architecture T01 (pile TCP/IP
 *          dans le module) : configuration IP, DNS, ping, SNTP, serveur TCP,
 *          réception passive (+IPD / AT+CIPRECVDATA) et émission (AT+CIPSEND).
 *
 *  Vocabulaire :
 *   - DHCP : la box attribue automatiquement une adresse IP à la carte ;
 *   - DNS : traduit un nom (www.st.com) en adresse IP ;
 *   - SNTP : protocole de mise à l'heure par Internet ;
 *   - lien (link) : une connexion TCP gérée par le module, numérotée de 0 à 4.
 *
 *  Serveur web (démo 3) : le module accepte les connexions des navigateurs
 *  (AT+CIPSERVER) et annonce chaque connexion (+CIP:<lien>,CONNECTED) puis
 *  les données arrivées (+IPD:<lien>,<n>,...). L'application lit ces données
 *  avec net_recv(), répond avec net_send() et ferme avec net_close().
 ******************************************************************************
 */
#ifndef NCP_NET_H
#define NCP_NET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_LINKS          5U      /* connexions simultanées gérées par le module */
#define NET_RECV_MAX    1400U      /* < W61_MAX_SPI_XFER - 64 du pilote officiel  */
#define NET_SEND_CHUNK  1024U      /* taille des blocs envoyés par net_send()     */

/** État d'une connexion TCP (lien), mis à jour par les événements +CIP: et +IPD:. */
typedef struct
{
  volatile bool     connected;
  volatile uint32_t avail;         /* octets en attente dans le module (+IPD) */
  char              rip[16];       /* adresse IP distante                     */
  uint16_t          rport;
} net_link_t;

/** État réseau partagé (défini dans ncp_net.c). */
typedef struct
{
  net_link_t    link[NET_LINKS];
  volatile bool evt_link;          /* une connexion a été ouverte/fermée      */
} net_ctx_t;

extern net_ctx_t g_net;

/** Configuration IP de la station : adresse, passerelle (box), masque. */
typedef struct
{
  uint8_t ip[4];
  uint8_t gw[4];
  uint8_t mask[4];
} net_ipcfg_t;

/** Date et heure UTC données par le serveur de temps. */
typedef struct
{
  uint16_t year;
  uint8_t  mon;    /* 1..12             */
  uint8_t  day;    /* 1..31             */
  uint8_t  hour;
  uint8_t  min;
  uint8_t  sec;
  uint8_t  wday;   /* 1 = lundi ... 7 = dimanche */
} net_time_t;

/**
 * @brief  Rappel appelé pour chaque écho de net_ping().
 * @param  seq  numéro de l'écho (1, 2...).
 * @param  ms   temps aller-retour en ms, ou valeur négative si pas de réponse.
 */
typedef void (*net_ping_cb_t)(uint16_t seq, int32_t ms);

/**
 * @brief  Traite les événements réseau +CIP: (connexion ouverte / fermée),
 *         +IPD: (données arrivées) et +PING: (appelée par urc_router()).
 * @retval true si la ligne a été reconnue.
 */
bool net_urc(const char *line);

/**
 * @brief  Initialise le réseau : IPv4, multi-connexions, réception passive,
 *         DHCP (client de la station et serveur du point d'accès), nom
 *         d'hôte et adresse du point d'accès.
 * @param  hostname  nom présenté à la box (DHCP).
 * @param  ap_ip     adresse du point d'accès (seuls les 3 premiers octets
 *                   comptent : il prend toujours l'adresse x.y.z.1).
 */
int  net_init(const char *hostname, const uint8_t ap_ip[4]);

/** @brief  Lit l'adresse IP, la passerelle et le masque de la station. */
int  net_sta_ipcfg(net_ipcfg_t *cfg);

/** @brief  Lit l'adresse IP du point d'accès. */
int  net_ap_ip(uint8_t ip[4]);

/** @brief  Lit l'adresse MAC de la station. */
int  net_sta_mac(uint8_t mac[6]);

/** @brief  Lit les serveurs DNS utilisés (texte « a.b.c.d », "" si absent). */
int  net_dns_servers(char dns[3][16]);

/** @brief  Active (true) ou désactive (false) le client DHCP de la station. */
int  net_set_dhcp(bool on);

/**
 * @brief  Adresse IP fixe de la station. À appeler une fois la station
 *         associée (état CONNECTED), avec le client DHCP désactivé
 *         (net_set_dhcp(false)), comme le fait le pilote officiel.
 */
int  net_set_static(const uint8_t ip[4], const uint8_t gw[4], const uint8_t mask[4]);

/** @brief  Serveur DNS manuel (dns != NULL) ou automatique / DHCP (dns == NULL). */
int  net_set_dns(const uint8_t dns[4]);

/**
 * @brief  Résolution DNS : adresse IPv4 d'un nom d'hôte.
 * @param  host  nom à résoudre (www.st.com).
 * @param  ip    reçoit l'adresse en texte.
 * @param  len   taille de ip (8 au moins).
 */
int  net_resolve(const char *host, char *ip, size_t len);

/**
 * @brief  Ping : envoie count échos ICMP et attend les réponses.
 * @param  host         nom ou adresse.
 * @param  count        nombre d'échos (0 : 4).
 * @param  interval_ms  intervalle entre deux échos (100 à 3500 ms, sinon 1000).
 * @param  cb           appelée pour chaque écho (peut être NULL).
 * @param  replies      reçoit le nombre de réponses (peut être NULL).
 */
int  net_ping(const char *host, uint16_t count, uint16_t interval_ms, net_ping_cb_t cb, uint16_t *replies);

/**
 * @brief  Configure le client SNTP du module (mise à l'heure par Internet).
 * @param  enable    true : client actif.
 * @param  tz_hours  fuseau appliqué par le module (0 : l'application garde l'UTC).
 * @param  server    nom du serveur de temps (pool.ntp.org).
 */
int  net_sntp_config(bool enable, int tz_hours, const char *server);

/**
 * @brief  Lit l'heure SNTP du module.
 * @retval AT_OK si une heure valide a été lue, AT_ERROR sinon (pas encore
 *         synchronisé...).
 */
int  net_sntp_time(net_time_t *t);

/**
 * @brief  Décode une date au format « asctime » : "Thu Oct 23 08:45:12 2026".
 * @retval true si la date est valide.
 */
bool net_parse_asctime(const char *s, net_time_t *t);

/**
 * @brief  Démarre le serveur TCP du module (serveur web de la démo 3).
 * @param  port      port d'écoute (80 pour HTTP).
 * @param  max_conn  connexions simultanées acceptées.
 */
int  net_server_start(uint16_t port, uint8_t max_conn);

/** @brief  Arrête le serveur TCP et ferme ses connexions. */
int  net_server_stop(void);

/**
 * @brief  Lit des données reçues sur un lien (signalées par +IPD).
 * @param  link  numéro du lien (0 à NET_LINKS-1).
 * @param  buf   tampon de destination.
 * @param  max   taille du tampon (limitée à NET_RECV_MAX).
 * @param  got   reçoit le nombre d'octets lus.
 */
int  net_recv(uint8_t link, uint8_t *buf, uint16_t max, uint16_t *got);

/** @brief  Envoie len octets sur un lien (découpés en blocs de NET_SEND_CHUNK). */
int  net_send(uint8_t link, const uint8_t *data, uint32_t len);

/** @brief  Ferme la connexion d'un lien. */
int  net_close(uint8_t link);

#ifdef __cplusplus
}
#endif

#endif /* NCP_NET_H */
