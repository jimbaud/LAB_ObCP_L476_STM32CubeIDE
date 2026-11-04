/**
 ******************************************************************************
 * @file    ncp_wifi.c
 * @brief   Fonctions Wi-Fi du ST67W611M1 (voir ncp_wifi.h).
 *
 *  Séquences de commandes reprises du pilote officiel (w61_at_wifi.c et
 *  w6x_wifi.c, X-CUBE-ST67W61 v1.3.0, firmware module 2.0.106).
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ncp_wifi.h"
#include "ncp_at.h"
#include "ncp_port.h"

#define WIFI_TIMEOUT_MS          6000U   /* commandes Wi-Fi ordinaires          */
#define WIFI_JOIN_CMD_TIMEOUT    3000U   /* réponse à AT+CWJAP (pas la connexion) */
#define WIFI_CONNECT_TIMEOUT    10000U   /* association au point d'accès        */
#define WIFI_GOTIP_TIMEOUT      15000U   /* obtention de l'adresse IP (DHCP)    */
#define WIFI_DISC_TIMEOUT        5000U   /* confirmation de la déconnexion      */

wifi_ctx_t g_wifi;

/* -------------------------------------------------------------------------- */
/* Décode une ligne de résultat de scan et l'ajoute à g_wifi.scan[] */
static void parse_scan(const char *args)
{
  char  buf[160];
  char *argv[10];
  char *p;
  int   argc;
  size_t n;

  if (g_wifi.scan_count >= WIFI_SCAN_MAX)
  {
    g_wifi.scan_truncated = true;
    return;
  }
  strncpy(buf, args, sizeof(buf) - 1U);
  buf[sizeof(buf) - 1U] = '\0';

  /* format : (<ecn>,"<ssid>",<rssi>,"<mac>",<canal>,<chiffrement>,<proto>,<wps>) */
  p = buf;
  if (*p == '(')
  {
    p++;
  }
  n = strlen(p);
  if ((n > 0U) && (p[n - 1U] == ')'))
  {
    p[n - 1U] = '\0';
  }

  argc = at_split(p, argv, 10);
  if (argc >= 8)
  {
    wifi_ap_t *ap = &g_wifi.scan[g_wifi.scan_count];
    memset(ap, 0, sizeof(*ap));
    ap->security = (uint8_t)atoi(argv[0]);
    strncpy(ap->ssid, argv[1], WIFI_SSID_MAX);
    ap->ssid[WIFI_SSID_MAX] = '\0';
    ap->rssi     = (int8_t)atoi(argv[2]);
    (void)at_parse_mac(argv[3], ap->bssid);
    ap->channel  = (uint8_t)atoi(argv[4]);
    ap->cipher   = (uint8_t)atoi(argv[5]);
    ap->proto    = (uint8_t)atoi(argv[6]);
    ap->wps      = (uint8_t)atoi(argv[7]);
    g_wifi.scan_count++;
  }
}

/* Voir ncp_wifi.h. Une ligne « +CW:<évènement>[,<arguments>] » met à jour
 * g_wifi : état de la station, drapeaux evt_xxx, clients du point d'accès. */
bool wifi_urc(const char *line)
{
  char  buf[96];
  char *ev;
  char *arg;

  if (at_starts(line, "+CWLAP:"))
  {
    parse_scan(&line[7]);
    return true;
  }
  if (!at_starts(line, "+CW:"))
  {
    return false;
  }

  strncpy(buf, &line[4], sizeof(buf) - 1U);
  buf[sizeof(buf) - 1U] = '\0';
  ev = buf;
  arg = strpbrk(buf, ", ");          /* évènement[,arguments] */
  if (arg != NULL)
  {
    *arg = '\0';
    arg++;
  }

  if (strcmp(ev, "SCAN_DONE") == 0)
  {
    g_wifi.evt_scan_done = true;
  }
  else if (strcmp(ev, "CONNECTING") == 0)
  {
    g_wifi.sta = WIFI_STA_CONNECTING;
  }
  else if (strcmp(ev, "CONNECTED") == 0)
  {
    g_wifi.sta = WIFI_STA_CONNECTED;
    g_wifi.evt_connected = true;
  }
  else if (strcmp(ev, "GOTIP") == 0)
  {
    g_wifi.sta = WIFI_STA_GOT_IP;
    g_wifi.evt_gotip = true;
  }
  else if (strcmp(ev, "DISCONNECTED") == 0)
  {
    if (g_wifi.sta != WIFI_STA_OFF)
    {
      g_wifi.sta = WIFI_STA_IDLE;
    }
    g_wifi.evt_disconnected = true;
  }
  else if (strcmp(ev, "ERROR") == 0)
  {
    g_wifi.last_reason = (arg != NULL) ? atoi(arg) : -1;
    g_wifi.evt_reason = true;
  }
  else if (strcmp(ev, "STA_CONNECTED") == 0)
  {
    if (arg != NULL)
    {
      char *argv[2];
      if (at_split(arg, argv, 2) >= 1)
      {
        (void)at_parse_mac(argv[0], g_wifi.ap_evt_mac);
      }
    }
    if (g_wifi.ap_clients < 255U)
    {
      g_wifi.ap_clients++;
    }
    g_wifi.evt_ap_join = true;
  }
  else if (strcmp(ev, "STA_DISCONNECTED") == 0)
  {
    if (arg != NULL)
    {
      char *argv[2];
      if (at_split(arg, argv, 2) >= 1)
      {
        (void)at_parse_mac(argv[0], g_wifi.ap_evt_mac);
      }
    }
    if (g_wifi.ap_clients > 0U)
    {
      g_wifi.ap_clients--;
    }
    g_wifi.evt_ap_leave = true;
  }
  else if (strcmp(ev, "DIST_STA_IP") == 0)
  {
    if (arg != NULL)
    {
      char *argv[3];
      if (at_split(arg, argv, 3) >= 2)
      {
        (void)at_parse_mac(argv[0], g_wifi.ap_evt_mac);
        strncpy(g_wifi.ap_evt_ip, argv[1], sizeof(g_wifi.ap_evt_ip) - 1U);
        g_wifi.ap_evt_ip[sizeof(g_wifi.ap_evt_ip) - 1U] = '\0';
      }
    }
    g_wifi.evt_ap_ip = true;
  }
  else
  {
    /* autre évènement +CW: non exploité */
  }
  return true;
}

/* -------------------------------------------------------------------------- */
/* Voir ncp_wifi.h */
int wifi_init(const char *country)
{
  int st;

  memset((void *)&g_wifi, 0, sizeof(g_wifi));
  g_wifi.sta = WIFI_STA_OFF;
  g_wifi.last_reason = -1;

  st = at_cmd("AT+CWMODE=1,0", NULL, NULL, NULL, WIFI_TIMEOUT_MS);   /* station seule */
  if (st != AT_OK)
  {
    return st;
  }
  g_wifi.sta = WIFI_STA_IDLE;

  st = at_cmd("AT+CWAUTOCONN=0", NULL, NULL, NULL, 2000U);        /* l'hôte décide */
  if (st != AT_OK)
  {
    return st;
  }
  if ((country != NULL) && (country[0] != '\0'))
  {
    st = at_cmdf(2000U, NULL, NULL, NULL, "AT+CWCOUNTRY=0,\"%s\"", country);
    if (st != AT_OK)
    {
      return st;
    }
  }
  (void)at_cmd("AT+SLWKDTIM=1", NULL, NULL, NULL, 2000U);         /* DTIM 1 */
  return AT_OK;
}

/* Voir ncp_wifi.h : les lignes +CWLAP: arrivent comme des événements
 * (parse_scan()), la fin du balayage est signalée par +CW:SCAN_DONE.  */
int wifi_scan(bool passive, uint8_t channel, uint32_t timeout_ms)
{
  int      st;
  uint32_t t0;

  g_wifi.scan_count = 0U;
  g_wifi.scan_truncated = false;
  g_wifi.evt_scan_done = false;

  st = at_cmdf(2000U, NULL, NULL, NULL, "AT+CWLAPOPT=1,1695,-100,255,%u", (unsigned)WIFI_SCAN_MAX);
  if (st != AT_OK)
  {
    return st;
  }
  st = at_cmdf(2000U, NULL, NULL, NULL, "AT+CWLAP=%u,,,%u", passive ? 1U : 0U, (unsigned)channel);
  if (st != AT_OK)
  {
    return st;
  }

  t0 = ncp_port_tick();
  while (!g_wifi.evt_scan_done)
  {
    at_poll();
    if ((ncp_port_tick() - t0) >= timeout_ms)
    {
      return AT_TIMEOUT;
    }
  }
  return AT_OK;
}

/* Un guillemet dans un SSID ou un mot de passe casserait la commande AT */
static bool has_quote(const char *s)
{
  return (s != NULL) && (strchr(s, '"') != NULL);
}

/* Voir ncp_wifi.h */
int wifi_connect_start(const char *ssid, const char *pass)
{
  int st;

  if ((ssid == NULL) || (ssid[0] == '\0') || has_quote(ssid) || has_quote(pass))
  {
    return WIFI_ERR_CHARS;
  }
  g_wifi.evt_connected = false;
  g_wifi.evt_gotip = false;
  g_wifi.evt_disconnected = false;
  g_wifi.evt_reason = false;
  g_wifi.last_reason = -1;

  (void)at_cmd("AT+SLWKDTIM=1", NULL, NULL, NULL, 2000U);
  /* L'état passe à CONNECTING avant l'envoi : les évènements +CW: peuvent
   * arriver dans la même rafale de trames que le OK de la commande.       */
  g_wifi.sta = WIFI_STA_CONNECTING;
  st = at_cmdf(WIFI_JOIN_CMD_TIMEOUT, NULL, NULL, NULL, "AT+CWJAP=\"%s\",\"%s\",,0",
               ssid, (pass != NULL) ? pass : "");
  if (st != AT_OK)
  {
    g_wifi.sta = WIFI_STA_IDLE;
    return st;
  }
  (void)at_cmd("AT+CWRECONNCFG=0,0", NULL, NULL, NULL, 2000U);   /* pas de reconnexion par le module */
  return AT_OK;
}

/* Voir ncp_wifi.h : wifi_connect_start() puis attente des événements */
int wifi_connect(const char *ssid, const char *pass, bool wait_ip, uint32_t timeout_ms)
{
  int      st = wifi_connect_start(ssid, pass);
  uint32_t t0 = ncp_port_tick();

  if (st != AT_OK)
  {
    return st;
  }
  if (timeout_ms == 0U)
  {
    timeout_ms = WIFI_CONNECT_TIMEOUT + WIFI_GOTIP_TIMEOUT;
  }
  for (;;)
  {
    at_poll();
    if (g_wifi.evt_gotip || (!wait_ip && g_wifi.evt_connected))
    {
      return AT_OK;
    }
    if (g_wifi.evt_reason)
    {
      return WIFI_ERR_REASON;
    }
    if (g_wifi.evt_disconnected && g_wifi.evt_connected)
    {
      return WIFI_ERR_REASON;     /* associé puis aussitôt déconnecté */
    }
    if ((ncp_port_tick() - t0) >= timeout_ms)
    {
      return g_wifi.evt_connected ? WIFI_ERR_NOIP : WIFI_ERR_TIMEOUT;
    }
  }
}

/* Voir ncp_wifi.h */
int wifi_disconnect(void)
{
  int      st;
  uint32_t t0;

  g_wifi.evt_disconnected = false;
  st = at_cmd("AT+CWQAP=1", NULL, NULL, NULL, WIFI_TIMEOUT_MS);
  if (st != AT_OK)
  {
    return st;
  }
  t0 = ncp_port_tick();
  while (!g_wifi.evt_disconnected && (g_wifi.sta != WIFI_STA_IDLE))
  {
    at_poll();
    if ((ncp_port_tick() - t0) >= WIFI_DISC_TIMEOUT)
    {
      break;
    }
  }
  g_wifi.sta = WIFI_STA_IDLE;
  return AT_OK;
}

/* Rappel de AT+CWJAP? : remplit la structure wifi_link_t passée dans ctx */
static void on_cwjap(const char *args, void *ctx)
{
  wifi_link_t *link = (wifi_link_t *)ctx;
  char         buf[160];
  char        *argv[8];
  int          argc;

  strncpy(buf, args, sizeof(buf) - 1U);
  buf[sizeof(buf) - 1U] = '\0';
  argc = at_split(buf, argv, 8);
  /* "<ssid>","<bssid>",<canal>,<rssi>,... */
  if (argc >= 4)
  {
    strncpy(link->ssid, argv[0], WIFI_SSID_MAX);
    link->ssid[WIFI_SSID_MAX] = '\0';
    (void)at_parse_mac(argv[1], link->bssid);
    link->channel = (uint8_t)atoi(argv[2]);
    link->rssi = (int16_t)atoi(argv[3]);
  }
  else if (argc == 3)
  {
    (void)at_parse_mac(argv[0], link->bssid);
    link->channel = (uint8_t)atoi(argv[1]);
    link->rssi = (int16_t)atoi(argv[2]);
  }
  else
  {
    link->rssi = 0;
  }
}

/* Voir ncp_wifi.h */
int wifi_link_info(wifi_link_t *link)
{
  int st;

  if (!wifi_is_connected())
  {
    return WIFI_ERR_NOTCONN;
  }
  memset(link, 0, sizeof(*link));
  st = at_cmd("AT+CWJAP?", "+CWJAP:", on_cwjap, link, WIFI_TIMEOUT_MS);
  if ((st == AT_OK) && (link->rssi == 0))
  {
    return WIFI_ERR_NOTCONN;
  }
  return st;
}

/* Voir ncp_wifi.h */
int wifi_rssi(int16_t *rssi)
{
  wifi_link_t link;
  int         st = wifi_link_info(&link);
  if (st == AT_OK)
  {
    *rssi = link.rssi;
  }
  return st;
}

/* Résultat de AT+CWSTATE? */
typedef struct
{
  uint8_t sec;
  uint8_t proto;
  bool    ok;
} cwstate_t;

/* Rappel de AT+CWSTATE? : « <état>,"<ssid>",<sécurité>,<norme> » */
static void on_cwstate(const char *args, void *ctx)
{
  cwstate_t *s = (cwstate_t *)ctx;
  char       buf[96];
  char      *argv[6];

  strncpy(buf, args, sizeof(buf) - 1U);
  buf[sizeof(buf) - 1U] = '\0';
  if (at_split(buf, argv, 6) >= 4)
  {
    s->sec = (uint8_t)atoi(argv[2]);
    s->proto = (uint8_t)atoi(argv[3]);
    s->ok = true;
  }
}

/* Voir ncp_wifi.h */
int wifi_conn_details(uint8_t *security, uint8_t *proto_idx)
{
  cwstate_t s = { WIFI_SEC_UNKNOWN, 0U, false };
  int       st = at_cmd("AT+CWSTATE?", "+CWSTATE:", on_cwstate, &s, WIFI_TIMEOUT_MS);
  if ((st == AT_OK) && s.ok)
  {
    *security = s.sec;
    *proto_idx = s.proto;
    return AT_OK;
  }
  return (st == AT_OK) ? AT_ERROR : st;
}

/* -------------------------------------------------------------------------- */
/* Voir ncp_wifi.h. AT+CWSAP="ssid","pass",canal,sécurité,max,0 (SSID visible) */
int wifi_ap_start(const char *ssid, const char *pass, uint8_t channel, uint8_t max_conn)
{
  int     st;
  uint8_t sec = WIFI_SEC_WPA2;

  if ((ssid == NULL) || (ssid[0] == '\0') || has_quote(ssid) || has_quote(pass))
  {
    return WIFI_ERR_CHARS;
  }
  if ((pass == NULL) || (pass[0] == '\0'))
  {
    sec = WIFI_SEC_OPEN;
    pass = "";
  }
  else if (strlen(pass) < 8U)
  {
    return AT_PARAM;              /* WPA2 : 8 caractères minimum */
  }
  if ((channel < 1U) || (channel > 13U))
  {
    channel = 1U;
  }
  if ((max_conn == 0U) || (max_conn > 4U))
  {
    max_conn = 4U;
  }

  /* mode double STA + SoftAP (le 2e paramètre maintient la connexion STA) */
  st = at_cmdf(WIFI_TIMEOUT_MS, NULL, NULL, NULL, "AT+CWMODE=3,%u", wifi_is_connected() ? 1U : 0U);
  if (st != AT_OK)
  {
    return st;
  }
  (void)at_cmd("AT+CWAPPROTO=15", NULL, NULL, NULL, 2000U);       /* b/g/n/ax */
  st = at_cmdf(WIFI_TIMEOUT_MS, NULL, NULL, NULL, "AT+CWSAP=\"%s\",\"%s\",%u,%u,%u,0",
               ssid, pass, (unsigned)channel, (unsigned)sec, (unsigned)max_conn);
  if (st != AT_OK)
  {
    (void)at_cmdf(WIFI_TIMEOUT_MS, NULL, NULL, NULL, "AT+CWMODE=1,%u", wifi_is_connected() ? 1U : 0U);
    return st;
  }
  g_wifi.ap_running = true;
  g_wifi.ap_clients = 0U;
  return AT_OK;
}

/* Voir ncp_wifi.h */
int wifi_ap_stop(void)
{
  int st = at_cmdf(WIFI_TIMEOUT_MS, NULL, NULL, NULL, "AT+CWMODE=1,%u", wifi_is_connected() ? 1U : 0U);
  if (st == AT_OK)
  {
    g_wifi.ap_running = false;
    g_wifi.ap_clients = 0U;
  }
  return st;
}

/* Rappel de AT+CWLIF : une ligne par appareil connecté, on les compte */
static void on_cwlif(const char *args, void *ctx)
{
  (void)args;
  (*(uint8_t *)ctx)++;
}

/* Voir ncp_wifi.h */
int wifi_ap_clients(uint8_t *count)
{
  uint8_t n = 0U;
  int     st = at_cmd("AT+CWLIF", "+CWLIF:", on_cwlif, &n, 2000U);
  if (st == AT_OK)
  {
    *count = n;
    g_wifi.ap_clients = n;
  }
  return st;
}

/* -------------------------------------------------------------------------- */
/* Voir ncp_wifi.h */
bool wifi_is_connected(void)
{
  return (g_wifi.sta == WIFI_STA_CONNECTED) || (g_wifi.sta == WIFI_STA_GOT_IP);
}

/* Voir ncp_wifi.h : tableau indexé par le code WIFI_SEC_xxx */
const char *wifi_security_str(uint8_t sec)
{
  static const char *const tab[] =
  {
    "ouvert", "WEP", "WPA", "WPA2", "WPA/WPA2", "WPA-Ent", "WPA3", "WPA2/WPA3", "?"
  };
  return (sec < (sizeof(tab) / sizeof(tab[0]))) ? tab[sec] : "?";
}

/* Voir ncp_wifi.h : la norme la plus récente annoncée l'emporte */
const char *wifi_proto_str(uint8_t proto_mask)
{
  if ((proto_mask & 8U) != 0U)
  {
    return "b/g/n/ax";
  }
  if ((proto_mask & 4U) != 0U)
  {
    return "b/g/n";
  }
  if ((proto_mask & 2U) != 0U)
  {
    return "b/g";
  }
  if ((proto_mask & 1U) != 0U)
  {
    return "b";
  }
  return "?";
}

/* Voir ncp_wifi.h : codes de raison du firmware du module, traduits */
const char *wifi_reason_str(int32_t reason)
{
  static const char *const tab[] =
  {
    /*  0 */ "succès",
    /*  1 */ "allocation de la trame d'authentification impossible",
    /*  2 */ "échec d'authentification",
    /*  3 */ "algorithme d'authentification refusé",
    /*  4 */ "allocation de la trame d'association impossible",
    /*  5 */ "échec d'association",
    /*  6 */ "désauthentifié par le point d'accès (avant connexion)",
    /*  7 */ "désauthentifié par le point d'accès",
    /*  8 */ "échec de la négociation 4 temps (mot de passe incorrect ?)",
    /*  9 */ "échec d'émission de la désauthentification",
    /* 10 */ "allocation de la désauthentification impossible",
    /* 11 */ "pas de réponse d'authentification / association",
    /* 12 */ "point d'accès introuvable (SSID ? bande 2,4 GHz ?)",
    /* 13 */ "contexte de canal impossible à créer",
    /* 14 */ "échec de connexion au réseau",
    /* 15 */ "ajout de la station impossible",
    /* 16 */ "perte des balises du point d'accès",
    /* 17 */ "type de sécurité incompatible",
    /* 18 */ "longueur de clé WEP incorrecte",
    /* 19 */ "déconnexion demandée",
    /* 20 */ "déconnexion demandée (sans désauthentification)",
    /* 21 */ "échec de trame nulle (économie d'énergie)",
    /* 22 */ "perte de trafic",
    /* 23 */ "changement de canal impossible",
    /* 24 */ "confirmation d'authentification / association manquante",
    /* 25 */ "réassociation en cours"
  };
  if ((reason >= 0) && ((uint32_t)reason < (sizeof(tab) / sizeof(tab[0]))))
  {
    return tab[reason];
  }
  return "raison inconnue";
}

/* Voir ncp_wifi.h */
const char *wifi_err_str(int err)
{
  switch (err)
  {
    case WIFI_ERR_REASON:  return wifi_reason_str(g_wifi.last_reason);
    case WIFI_ERR_TIMEOUT: return "pas de connexion dans le délai";
    case WIFI_ERR_NOIP:    return "associé mais pas d'adresse IP (DHCP)";
    case WIFI_ERR_CHARS:   return "SSID vide ou caractère \" interdit";
    case WIFI_ERR_NOTCONN: return "station non connectée";
    default:               return at_status_str((at_status_t)err);
  }
}
