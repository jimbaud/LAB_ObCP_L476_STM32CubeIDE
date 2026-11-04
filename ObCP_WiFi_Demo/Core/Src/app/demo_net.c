/**
 ******************************************************************************
 * @file    demo_net.c
 * @brief   Démo 2 : connexion Wi-Fi et services réseau du module (pile
 *          TCP/IP intégrée, firmware T01) : DHCP ou IP fixe, DNS, ping,
 *          SNTP vers la RTC du STM32. Gère aussi, en tâche de fond, la
 *          connexion automatique, la reconnexion et la resynchronisation NTP.
 *
 *  Connexion en tâche de fond (netapp_poll(), appelée à chaque tour de la
 *  boucle principale) : petite machine à états à deux états.
 *    RC_IDLE : rien en cours. Si la liaison est voulue (s_want) et absente,
 *              et que l'heure du prochain essai (s_retry_at) est passée,
 *              wifi_connect_start() lance une tentative -> RC_WAIT.
 *    RC_WAIT : attente des événements du module : adresse IP obtenue
 *              -> link_up() ; échec ou 30 s écoulées -> nouvel essai plus
 *              tard, avec un délai doublé à chaque échec (10, 20, 40, 60 s).
 *  link_up() / link_down() préviennent la démo MQTT (mqttapp_sta_up/down)
 *  et lancent la synchronisation NTP.
 *
 *  La commande « connect », elle, se connecte en attendant le résultat
 *  (netapp_connect(), bloquante) pour afficher le détail de la liaison.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "app_config.h"
#include "board.h"
#include "config.h"
#include "console.h"
#include "demos.h"
#include "util.h"

#define CONNECT_TIMEOUT_MS   30000U   /* durée maximale d'une tentative      */
#define RETRY_MAX_MS         60000U   /* délai maximal entre deux tentatives */

/* État de la connexion en tâche de fond */
typedef enum
{
  RC_IDLE = 0,                      /* aucune tentative en cours           */
  RC_WAIT                           /* tentative lancée, résultat attendu  */
} rc_state_t;

static bool       s_want;           /* la station doit être connectée          */
static bool       s_force;          /* une tentative même si wifi.auto = 0     */
static rc_state_t s_rc;
static uint32_t   s_rc_t0;
static uint32_t   s_retry_at;
static uint32_t   s_retry_delay = WIFI_RECONNECT_DELAY_MS;
static bool       s_up;             /* liaison prête (IP) signalée             */
static char       s_ip[16];
static uint32_t   s_up_since;
static uint32_t   s_disconnects;

/* Synchronisation NTP en tâche de fond */
static bool       s_ntp_busy;       /* demande envoyée, réponse attendue        */
static uint32_t   s_ntp_t0;         /* début de la demande                      */
static uint32_t   s_ntp_next;       /* prochaine lecture de l'heure du module   */
static uint32_t   s_ntp_last;       /* tick de la dernière synchro (0 = jamais) */
static bool       s_ntp_ok;         /* au moins une synchronisation réussie     */

/* Noms des normes, indexés par la valeur rendue par wifi_conn_details() */
static const char *const s_proto[] = { "?", "802.11b", "802.11g", "Wi-Fi 4 (802.11n)", "Wi-Fi 6 (802.11ax)" };

/* -------------------------------------------------------------------------- */
/* Heure                                                                       */
/* -------------------------------------------------------------------------- */
/* Voir demos.h */
bool netapp_time_synced(void)
{
  return board_rtc_synced();
}

/* Voir demos.h : UTC de la RTC -> époque -> + décalage -> date locale */
bool netapp_local_time(dt_t *local, int32_t *offset_s)
{
  dt_t    utc;
  int64_t t;
  int32_t off;

  if (!board_rtc_get(&utc))
  {
    return false;
  }
  t = dt_to_epoch(&utc);
  off = local_offset_s(t, g_cfg.tz_min, g_cfg.dst_eu != 0U);
  epoch_to_dt(t + off, local);
  if (offset_s != NULL)
  {
    *offset_s = off;
  }
  return true;
}

/* Voir demos.h */
void netapp_iso_utc(char *out, size_t len)
{
  dt_t d;
  if (!board_rtc_synced() || !board_rtc_get(&d))
  {
    if (len > 0U)
    {
      out[0] = '\0';
    }
    return;
  }
  (void)snprintf(out, len, "%04u-%02u-%02uT%02u:%02u:%02uZ", d.year, d.mon, d.day, d.hour, d.min, d.sec);
}

/* Décalage horaire en texte : 7200 -> « UTC+2 », 19800 -> « UTC+5:30 » */
static void fmt_offset(char *out, size_t len, int32_t off_s)
{
  int32_t a = (off_s < 0) ? -off_s : off_s;
  if ((a % 3600) == 0)
  {
    (void)snprintf(out, len, "UTC%c%ld", (off_s < 0) ? '-' : '+', (long)(a / 3600));
  }
  else
  {
    (void)snprintf(out, len, "UTC%c%ld:%02ld", (off_s < 0) ? '-' : '+', (long)(a / 3600), (long)((a % 3600) / 60));
  }
}

/* Voir demos.h */
void netapp_print_time(void)
{
  dt_t    loc;
  int32_t off = 0;
  char    s[24];
  char    tz[16];

  if (!netapp_local_time(&loc, &off))
  {
    console_puts("  Heure          : RTC illisible\n");
    return;
  }
  dt_format(&loc, s, sizeof(s));
  fmt_offset(tz, sizeof(tz), off);
  console_printf("  Heure          : %s (%s) - %s\n", s, tz,
                 board_rtc_synced() ? "réglée par NTP" : "non synchronisée (commande ntp)");
}

/* Règle la RTC (UTC) avec l'heure SNTP lue dans le module */
static bool set_rtc_from(const net_time_t *t)
{
  dt_t d;
  d.year = t->year;
  d.mon  = t->mon;
  d.day  = t->day;
  d.hour = t->hour;
  d.min  = t->min;
  d.sec  = t->sec;
  d.wday = dt_weekday(d.year, d.mon, d.day);
  return board_rtc_set(&d);
}

/* Lance une synchronisation NTP en tâche de fond (le module interroge le
 * serveur ; ntp_poll() lit ensuite son heure toutes les secondes)      */
static void ntp_start(void)
{
  if (net_sntp_config(true, 0, g_cfg.ntp_server) == AT_OK)   /* module en UTC */
  {
    s_ntp_busy = true;
    s_ntp_t0 = HAL_GetTick();
    s_ntp_next = s_ntp_t0 + 500U;
  }
}

/* Tâche de fond NTP : resynchronisation toutes les 6 h ; pendant une
 * synchronisation, lecture de l'heure du module jusqu'à ce qu'elle soit
 * valide (année >= 2024) ou que 20 s se soient écoulées.               */
static void ntp_poll(uint32_t now)
{
  net_time_t t;

  if (!s_ntp_busy)
  {
    if (s_up && (g_cfg.ntp_auto != 0U) && (s_ntp_last != 0U) && ((now - s_ntp_last) >= NTP_RESYNC_PERIOD_MS))
    {
      ntp_start();                       /* resynchronisation périodique */
    }
    return;
  }
  if ((int32_t)(now - s_ntp_next) < 0)
  {
    return;
  }
  s_ntp_next = now + 1000U;
  if ((net_sntp_time(&t) == AT_OK) && (t.year >= 2024U) && set_rtc_from(&t))
  {
    dt_t loc;
    char s[24];
    s_ntp_busy = false;
    s_ntp_last = now;
    s_ntp_ok = true;
    if (netapp_local_time(&loc, NULL))
    {
      dt_format(&loc, s, sizeof(s));
      console_event("%s\xE2\x9C\x93 Heure : RTC réglée par NTP (%s) : %s, heure locale%s", C_OK(),
                    g_cfg.ntp_server, s, C_RST());
    }
  }
  else if ((now - s_ntp_t0) >= NTP_SYNC_TIMEOUT_MS)
  {
    s_ntp_busy = false;
    s_ntp_last = (now - NTP_RESYNC_PERIOD_MS) + 300000UL;   /* nouvel essai dans 5 min */
    console_event("%sHeure : pas de réponse du serveur NTP %s%s", C_WARN(), g_cfg.ntp_server, C_RST());
  }
  else
  {
    /* en attente de la première réponse du serveur */
  }
}

/* -------------------------------------------------------------------------- */
/* Liaison                                                                     */
/* -------------------------------------------------------------------------- */
/* Voir demos.h */
bool netapp_sta_ready(void)
{
  return s_up && wifi_is_connected();
}

/* Voir demos.h */
bool netapp_connecting(void)
{
  return s_rc == RC_WAIT;
}

/* Voir demos.h */
const char *netapp_ip(void)
{
  return netapp_sta_ready() ? s_ip : "";
}

/* Avant la connexion : client DHCP actif (adresse donnée par la box) ou coupé */
static void apply_ip_mode_before(void)
{
  if (g_cfg.dhcp != 0U)
  {
    (void)net_set_dhcp(true);
    (void)net_set_dns(NULL);
  }
  else
  {
    (void)net_set_dhcp(false);
  }
}

/* Après l'association, en IP fixe : adresse, passerelle, masque et DNS */
static int apply_static_after(void)
{
  static const uint8_t zero[4] = { 0U, 0U, 0U, 0U };
  int st;

  if (g_cfg.dhcp != 0U)
  {
    return AT_OK;
  }
  st = net_set_static(g_cfg.ip, g_cfg.gw, g_cfg.mask);
  if (st == AT_OK)
  {
    (void)net_set_dns((memcmp(g_cfg.dns, zero, 4) != 0) ? g_cfg.dns : g_cfg.gw);
  }
  return st;
}

/* Détail de la liaison : réseau, canal, signal, sécurité, adresses */
static void print_link_details(void)
{
  wifi_link_t link;
  net_ipcfg_t ip;
  char        dns[3][16];
  char        a[20];
  char        b[20];
  uint8_t     sec = WIFI_SEC_UNKNOWN;
  uint8_t     proto = 0U;

  if (wifi_link_info(&link) == AT_OK)
  {
    char bar[64];
    int  q = util_rssi_quality(link.rssi);
    util_fmt_mac(a, sizeof(a), link.bssid);
    util_bar(bar, sizeof(bar), q, 0, 100, 10);
    console_printf("  Réseau         : « %s »\n", link.ssid);
    console_printf("  Point d'accès  : %s, canal %u (%u MHz)\n", a, (unsigned)link.channel,
                   (unsigned)util_channel_mhz(link.channel));
    console_printf("  Signal         : %d dBm  %s %d %% (%s)\n", (int)link.rssi, bar, q, util_rssi_label(link.rssi));
  }
  if (wifi_conn_details(&sec, &proto) == AT_OK)
  {
    console_printf("  Sécurité       : %s, norme %s\n", wifi_security_str(sec),
                   s_proto[(proto < 5U) ? proto : 0U]);
  }
  if (net_sta_ipcfg(&ip) == AT_OK)
  {
    util_fmt_ip(a, sizeof(a), ip.ip);
    util_fmt_ip(b, sizeof(b), ip.mask);
    console_printf("  Adresse IP     : %s / %s (%s)\n", a, b, (g_cfg.dhcp != 0U) ? "DHCP" : "fixe");
    util_fmt_ip(a, sizeof(a), ip.gw);
    console_printf("  Passerelle     : %s\n", a);
  }
  if (net_dns_servers(dns) == AT_OK)
  {
    console_printf("  DNS            : %s%s%s\n", dns[0], (dns[1][0] != '\0') ? ", " : "", dns[1]);
  }
  util_fmt_mac(a, sizeof(a), g_app.sta_mac);
  console_printf("  Adresse MAC    : %s, nom d'hôte « %s »\n", a, config_hostname());
}

/* La liaison vient d'être établie (adresse IP obtenue) : mémorise l'adresse,
 * prévient MQTT, lance la synchronisation NTP                          */
static void link_up(bool verbose)
{
  net_ipcfg_t ip;

  s_up = true;
  s_force = false;
  s_up_since = HAL_GetTick();
  s_retry_delay = WIFI_RECONNECT_DELAY_MS;
  s_ip[0] = '\0';
  if (net_sta_ipcfg(&ip) == AT_OK)
  {
    util_fmt_ip(s_ip, sizeof(s_ip), ip.ip);
  }
  if (verbose)
  {
    print_link_details();
  }
  else
  {
    console_event("%s\xE2\x9C\x93 Wi-Fi : connecté à « %s », adresse %s%s", C_OK(), g_cfg.sta_ssid, s_ip, C_RST());
  }
  if (g_cfg.ntp_auto != 0U)
  {
    ntp_start();
  }
  mqttapp_sta_up();
}

/* La liaison est perdue ou coupée */
static void link_down(void)
{
  if (s_up)
  {
    mqttapp_sta_down();
  }
  s_up = false;
  s_ip[0] = '\0';
  s_ntp_busy = false;
}

/* Conseil adapté à la raison de l'échec de connexion signalée par le module */
static void print_hint(int err)
{
  int32_t r = g_wifi.last_reason;

  if ((err == WIFI_ERR_REASON) && ((r == 2) || (r == 8) || (r == 15)))
  {
    console_printf("%s  -> Mot de passe incorrect ? (respecter majuscules / minuscules)%s\n", C_WARN(), C_RST());
  }
  else if ((err == WIFI_ERR_TIMEOUT) || ((err == WIFI_ERR_REASON) && ((r == 11) || (r == 12))))
  {
    console_printf("%s  -> Réseau introuvable : vérifier le SSID (« scan »). Le module ne fonctionne\n"
                   "     qu'en 2,4 GHz : activer cette bande sur la box si besoin.%s\n", C_WARN(), C_RST());
  }
  else if ((err == WIFI_ERR_REASON) && (r == 17))
  {
    console_printf("%s  -> Sécurité non prise en charge par ce réseau (WEP ? WPA-Entreprise ?)%s\n", C_WARN(), C_RST());
  }
  else if (err == WIFI_ERR_NOIP)
  {
    console_printf("%s  -> Pas de réponse DHCP : essayer une adresse fixe (help ip).%s\n", C_WARN(), C_RST());
  }
  else
  {
    /* pas de conseil particulier */
  }
}

/* Voir demos.h */
int netapp_connect(bool verbose)
{
  uint32_t t0 = HAL_GetTick();
  int      st;

  if (g_cfg.sta_ssid[0] == '\0')
  {
    console_puts("Aucun réseau configuré : connect <ssid> [mot de passe]\n");
    return APP_ERR_USAGE;
  }
  s_rc = RC_IDLE;                         /* annule une tentative en tâche de fond */
  if (wifi_is_connected() || (g_wifi.sta == WIFI_STA_CONNECTING))
  {
    link_down();
    (void)wifi_disconnect();
  }
  apply_ip_mode_before();
  if (verbose)
  {
    console_printf("Connexion à « %s »%s...\n", g_cfg.sta_ssid, (g_cfg.dhcp != 0U) ? "" : " (IP fixe)");
  }
  st = wifi_connect(g_cfg.sta_ssid, g_cfg.sta_pass, g_cfg.dhcp != 0U, CONNECT_TIMEOUT_MS);
  if (st == AT_OK)
  {
    st = apply_static_after();
  }
  if (st != AT_OK)
  {
    s_want = false;
    if (wifi_is_connected() || (g_wifi.sta == WIFI_STA_CONNECTING))
    {
      (void)wifi_disconnect();
    }
    (void)app_report(st, "connexion");
    print_hint(st);
    return st;
  }
  s_want = true;
  if (verbose)
  {
    char t[12];
    util_fmt_fixed(t, sizeof(t), (int32_t)((HAL_GetTick() - t0) / 100U), 1U);
    console_printf("%s\xE2\x9C\x93 Connecté en %s s%s\n", C_OK(), t, C_RST());
  }
  link_up(verbose);
  return AT_OK;
}

/* Voir demos.h : la connexion sera faite par netapp_poll() 1,5 s plus tard */
void netapp_request_connect(void)
{
  s_want = true;
  s_force = true;
  s_rc = RC_IDLE;
  s_retry_delay = WIFI_RECONNECT_DELAY_MS;
  s_retry_at = HAL_GetTick() + 1500U;     /* laisse partir la réponse HTTP */
}

/* Voir demos.h */
void netapp_disconnect(void)
{
  s_want = false;
  s_force = false;
  s_rc = RC_IDLE;
  link_down();
  (void)wifi_disconnect();
}

/* Voir demos.h : la première tentative partira au premier netapp_poll() */
void netapp_boot(void)
{
  if ((g_cfg.sta_auto != 0U) && (g_cfg.sta_ssid[0] != '\0'))
  {
    s_want = true;
    s_retry_at = HAL_GetTick();
    console_printf("Connexion automatique à « %s » en tâche de fond (wifi.auto = 1)...\n", g_cfg.sta_ssid);
  }
  else if (g_cfg.sta_ssid[0] == '\0')
  {
    console_printf("%sAucun réseau Wi-Fi configuré : « connect <ssid> <mot de passe> » puis « save ».%s\n",
                   C_DIM(), C_RST());
  }
  else
  {
    /* wifi.auto = 0 : connexion manuelle */
  }
}

/* Voir demos.h */
void netapp_module_restarted(void)
{
  s_up = false;
  s_ip[0] = '\0';
  s_rc = RC_IDLE;
  s_ntp_busy = false;
  if (s_want)
  {
    s_force = true;                       /* rétablit la liaison voulue par l'utilisateur */
    s_retry_at = HAL_GetTick() + 500U;
  }
}

/* Voir demos.h et la machine à états décrite en tête de fichier */
void netapp_poll(void)
{
  uint32_t now = HAL_GetTick();
  bool     connected = wifi_is_connected();

  /* 1) tentative de connexion en tâche de fond */
  if (s_rc == RC_WAIT)
  {
    bool ready = (g_cfg.dhcp != 0U) ? (g_wifi.sta == WIFI_STA_GOT_IP) : connected;
    if (ready)
    {
      s_rc = RC_IDLE;
      if (apply_static_after() == AT_OK)
      {
        link_up(false);
      }
      else
      {
        (void)wifi_disconnect();
        s_retry_at = now + s_retry_delay;
      }
    }
    else if (g_wifi.evt_reason || (g_wifi.evt_disconnected && g_wifi.evt_connected) ||
             ((now - s_rc_t0) >= CONNECT_TIMEOUT_MS))
    {
      const char *why = g_wifi.evt_reason ? wifi_reason_str(g_wifi.last_reason) :
                        (g_wifi.evt_connected ? "pas d'adresse IP" : "pas de réponse");
      s_rc = RC_IDLE;
      if (g_wifi.sta == WIFI_STA_CONNECTING)
      {
        (void)wifi_disconnect();
      }
      s_retry_at = now + s_retry_delay;
      console_event("%sWi-Fi : échec de connexion à « %s » (%s), nouvel essai dans %lu s%s", C_WARN(),
                    g_cfg.sta_ssid, why, (unsigned long)(s_retry_delay / 1000U), C_RST());
      s_retry_delay = (s_retry_delay * 2U > RETRY_MAX_MS) ? RETRY_MAX_MS : (s_retry_delay * 2U);
    }
    else
    {
      /* attente des évènements +CW: */
    }
  }
  else
  {
    /* 2) perte de liaison */
    if (s_up && !connected)
    {
      s_disconnects++;
      link_down();
      console_event("%sWi-Fi : liaison perdue (%s)%s%s", C_WARN(),
                    g_wifi.evt_reason ? wifi_reason_str(g_wifi.last_reason) : "point d'accès hors de portée ?",
                    (s_want && (g_cfg.sta_auto != 0U)) ? " - reconnexion automatique" : "", C_RST());
      s_retry_at = now + 2000U;
      s_retry_delay = WIFI_RECONNECT_DELAY_MS;
    }
    /* liaison établie hors application (commande AT brute...) */
    else if (!s_up && (g_wifi.sta == WIFI_STA_GOT_IP))
    {
      link_up(false);
    }
    else
    {
      /* rien de nouveau */
    }

    /* 3) (re)connexion automatique ; s_force : connexion demandée par la page
     *    web, éventuellement vers un autre réseau que celui en cours        */
    if (s_force && wifi_is_connected() && ((int32_t)(now - s_retry_at) >= 0))
    {
      link_down();
      (void)wifi_disconnect();
    }
    if (s_want && ((g_cfg.sta_auto != 0U) || s_force) && !wifi_is_connected() &&
        (g_cfg.sta_ssid[0] != '\0') && ((int32_t)(now - s_retry_at) >= 0))
    {
      int st;
      apply_ip_mode_before();
      st = wifi_connect_start(g_cfg.sta_ssid, g_cfg.sta_pass);
      if (st == AT_OK)
      {
        s_rc = RC_WAIT;
        s_rc_t0 = now;
      }
      else
      {
        s_retry_at = now + s_retry_delay;
        console_event("%sWi-Fi : connexion impossible (%s)%s", C_WARN(), app_err_str(st), C_RST());
      }
      s_force = false;
    }
  }

  /* 4) heure */
  ntp_poll(now);
}

/* -------------------------------------------------------------------------- */
/* Commandes                                                                   */
/* -------------------------------------------------------------------------- */
/* Voir demos.h : avec arguments, le SSID et le mot de passe sont d'abord
 * copiés dans la configuration (en RAM : « save » pour les garder)      */
int cmd_connect(int argc, char **argv)
{
  int st;

  if (argc >= 2)
  {
    st = cfg_set(&g_cfg, "wifi.ssid", argv[1]);
    if (st == CFG_OK)
    {
      st = cfg_set(&g_cfg, "wifi.pass", (argc >= 3) ? argv[2] : "");
    }
    if (st != CFG_OK)
    {
      console_printf("%s%s%s\n", C_ERR(), cfg_err_str(st), C_RST());
      return APP_ERR_USAGE;
    }
    g_app.cfg_dirty = true;
    if (argc > 3)
    {
      console_printf("%sAttention : arguments en trop ignorés (SSID avec espaces : utiliser des guillemets).%s\n",
                     C_WARN(), C_RST());
    }
  }
  st = netapp_connect(true);
  if ((st == AT_OK) && g_app.cfg_dirty)
  {
    console_printf("%s« save » pour mémoriser ce réseau (connexion automatique au démarrage).%s\n", C_DIM(), C_RST());
  }
  return st;
}

/* Voir demos.h */
int cmd_disconnect(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  if (!wifi_is_connected() && !s_want)
  {
    console_puts("Station déjà déconnectée.\n");
    return 0;
  }
  netapp_disconnect();
  console_puts("Station déconnectée (reconnexion automatique suspendue jusqu'au prochain « connect »).\n");
  return 0;
}

/* Voir demos.h */
int cmd_status(int argc, char **argv)
{
  char up[24];

  (void)argc;
  (void)argv;
  if (netapp_sta_ready())
  {
    util_fmt_uptime(up, sizeof(up), (HAL_GetTick() - s_up_since) / 1000U);
    console_printf("Station Wi-Fi : %sconnectée%s depuis %s", C_OK(), C_RST(), up);
    if (s_disconnects > 0U)
    {
      console_printf(" (%lu coupure(s) depuis le démarrage)", (unsigned long)s_disconnects);
    }
    console_puts("\n");
    print_link_details();
  }
  else if (s_rc == RC_WAIT)
  {
    console_printf("Station Wi-Fi : connexion à « %s » en cours...\n", g_cfg.sta_ssid);
  }
  else
  {
    console_printf("Station Wi-Fi : %snon connectée%s", C_WARN(), C_RST());
    if (s_want && (g_cfg.sta_auto != 0U))
    {
      int32_t left = (int32_t)(s_retry_at - HAL_GetTick());
      console_printf(" (nouvel essai dans %ld s)", (long)((left > 0) ? (left / 1000) : 0));
    }
    console_puts("\n");
  }
  console_printf("Point d'accès : %s", g_wifi.ap_running ? "actif" : "arrêté");
  if (g_wifi.ap_running)
  {
    console_printf(" « %s », %u client(s)", config_ap_ssid(), (unsigned)g_wifi.ap_clients);
  }
  console_puts("\n");
  return 0;
}

/* Voir demos.h : sans argument affiche le mode ; « dhcp » ou adresse fixe
 * (la passerelle doit être dans le même sous-réseau : (ip & masque) ==
 * (passerelle & masque)) ; reconnexion si la carte est connectée.      */
int cmd_ip(int argc, char **argv)
{
  char a[16];
  char b[16];
  char c[16];
  char d[16];

  if (argc == 1)
  {
    if (g_cfg.dhcp != 0U)
    {
      console_puts("Adressage : DHCP (adresse fournie par la box / le routeur)\n");
    }
    else
    {
      util_fmt_ip(a, sizeof(a), g_cfg.ip);
      util_fmt_ip(b, sizeof(b), g_cfg.mask);
      util_fmt_ip(c, sizeof(c), g_cfg.gw);
      util_fmt_ip(d, sizeof(d), g_cfg.dns);
      console_printf("Adressage : IP fixe %s, masque %s, passerelle %s, DNS %s\n", a, b, c,
                     ((g_cfg.dns[0] | g_cfg.dns[1] | g_cfg.dns[2] | g_cfg.dns[3]) != 0U) ? d : "= passerelle");
    }
    if (netapp_sta_ready())
    {
      console_printf("Adresse actuelle : %s\n", s_ip);
    }
    return 0;
  }

  if (util_strieq(argv[1], "dhcp"))
  {
    g_cfg.dhcp = 1U;
  }
  else if ((argc == 4) || (argc == 5))
  {
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gw[4];
    uint8_t dns[4] = { 0U, 0U, 0U, 0U };
    int     k;

    if (!util_parse_ip(argv[1], ip) || !util_parse_ip(argv[2], mask) || !util_parse_ip(argv[3], gw) ||
        ((argc == 5) && !util_parse_ip(argv[4], dns)))
    {
      console_puts("Adresse invalide (format a.b.c.d).\n");
      return APP_ERR_USAGE;
    }
    for (k = 0; k < 4; k++)
    {
      if ((ip[k] & mask[k]) != (gw[k] & mask[k]))
      {
        console_printf("%sAttention : la passerelle n'est pas dans le sous-réseau de l'adresse.%s\n",
                       C_WARN(), C_RST());
        break;
      }
    }
    g_cfg.dhcp = 0U;
    memcpy(g_cfg.ip, ip, 4);
    memcpy(g_cfg.mask, mask, 4);
    memcpy(g_cfg.gw, gw, 4);
    memcpy(g_cfg.dns, dns, 4);
  }
  else
  {
    console_puts("Usage : ip [dhcp | <ip> <masque> <passerelle> [dns]]\n");
    return APP_ERR_USAGE;
  }
  g_app.cfg_dirty = true;
  console_printf("Adressage %s enregistré en RAM (« save » pour le garder).\n", (g_cfg.dhcp != 0U) ? "DHCP" : "fixe");
  if (wifi_is_connected())
  {
    console_puts("Reconnexion pour appliquer le nouveau réglage...\n");
    return netapp_connect(true);
  }
  return 0;
}

/* Voir demos.h */
int cmd_dns(int argc, char **argv)
{
  char     ip[16];
  uint32_t t0;
  int      st;

  if (argc < 2)
  {
    console_puts("Usage : dns <nom>   (ex. dns www.st.com)\n");
    return APP_ERR_USAGE;
  }
  if (!netapp_sta_ready())
  {
    return app_report(APP_ERR_NOSTA, "dns");
  }
  t0 = HAL_GetTick();
  st = net_resolve(argv[1], ip, sizeof(ip));
  if (st != AT_OK)
  {
    return app_report(st, "résolution DNS");
  }
  console_printf("%s -> %s%s%s (%lu ms)\n", argv[1], C_BOLD(), ip, C_RST(), (unsigned long)(HAL_GetTick() - t0));
  return 0;
}

/* Statistiques du ping en cours (réponses, temps min / max / somme) */
static uint16_t s_p_ok;
static int32_t  s_p_min;
static int32_t  s_p_max;
static int32_t  s_p_sum;

/* Rappel de net_ping() pour chaque écho : affichage et statistiques */
static void on_ping(uint16_t seq, int32_t ms)
{
  if (ms >= 0)
  {
    if ((s_p_ok == 0U) || (ms < s_p_min))
    {
      s_p_min = ms;
    }
    if ((s_p_ok == 0U) || (ms > s_p_max))
    {
      s_p_max = ms;
    }
    s_p_ok++;
    s_p_sum += ms;
    console_printf("  Réponse %2u : temps = %s%ld ms%s\n", (unsigned)seq, C_OK(), (long)ms, C_RST());
  }
  else
  {
    console_printf("  Réponse %2u : %sdélai dépassé%s\n", (unsigned)seq, C_ERR(), C_RST());
  }
}

/* Voir demos.h : un nom est d'abord traduit en adresse (DNS) */
int cmd_ping(int argc, char **argv)
{
  long        count = 4;
  long        interval = 1000;
  char        ip[16];
  const char *target;
  uint16_t    replies = 0U;
  uint8_t     tmp[4];
  int         st;

  if ((argc < 2) || ((argc >= 3) && (!util_parse_long(argv[2], &count) || (count < 1) || (count > 50))) ||
      ((argc >= 4) && (!util_parse_long(argv[3], &interval) || (interval < 100) || (interval > 3500))))
  {
    console_puts("Usage : ping <hôte> [nombre 1-50] [intervalle 100-3500 ms]\n");
    return APP_ERR_USAGE;
  }
  if (!netapp_sta_ready())
  {
    return app_report(APP_ERR_NOSTA, "ping");
  }
  target = argv[1];
  if (!util_parse_ip(argv[1], tmp))
  {
    st = net_resolve(argv[1], ip, sizeof(ip));
    if (st != AT_OK)
    {
      return app_report(st, "résolution DNS");
    }
    target = ip;
    console_printf("PING %s (%s) : %ld requête(s), intervalle %ld ms\n", argv[1], ip, count, interval);
  }
  else
  {
    console_printf("PING %s : %ld requête(s), intervalle %ld ms\n", argv[1], count, interval);
  }
  s_p_ok = 0U;
  s_p_min = 0;
  s_p_max = 0;
  s_p_sum = 0;
  st = net_ping(target, (uint16_t)count, (uint16_t)interval, on_ping, &replies);
  if ((st != AT_OK) && (replies == 0U))
  {
    return app_report(st, "ping");
  }
  console_printf("Bilan : %ld envoyée(s), %u reçue(s), %ld %% de perte", count, (unsigned)s_p_ok,
                 ((count - (long)s_p_ok) * 100L) / count);
  if (s_p_ok > 0U)
  {
    console_printf(" ; temps min/moy/max = %ld/%ld/%ld ms", (long)s_p_min, (long)(s_p_sum / (int32_t)s_p_ok),
                   (long)s_p_max);
  }
  console_puts("\n");
  return 0;
}

/* Voir demos.h : synchronisation immédiate (bloquante, un point par
 * demi-seconde d'attente) puis affichage de la correction de la RTC    */
int cmd_ntp(int argc, char **argv)
{
  const char *server = (argc >= 2) ? argv[1] : g_cfg.ntp_server;
  bool        was_synced = board_rtc_synced();
  dt_t        before;
  bool        have_before;
  uint32_t    t_before;
  uint32_t    t0;
  int         st;

  if (!netapp_sta_ready())
  {
    return app_report(APP_ERR_NOSTA, "ntp");
  }
  have_before = board_rtc_get(&before);
  t_before = HAL_GetTick();
  console_printf("Synchronisation SNTP avec %s ", server);
  st = net_sntp_config(true, 0, server);
  if (st != AT_OK)
  {
    console_puts("\n");
    return app_report(st, "configuration SNTP");
  }
  s_ntp_busy = false;
  t0 = HAL_GetTick();
  for (;;)
  {
    net_time_t t;
    uint32_t   tw = HAL_GetTick();

    if ((net_sntp_time(&t) == AT_OK) && (t.year >= 2024U))
    {
      dt_t    loc;
      int32_t off = 0;
      char    s[24];
      char    tz[16];

      if (!set_rtc_from(&t))
      {
        console_puts("\n");
        return app_report(AT_ERROR, "réglage de la RTC");
      }
      s_ntp_last = HAL_GetTick();
      s_ntp_ok = true;
      console_printf("%s\xE2\x9C\x93%s\n", C_OK(), C_RST());
      (void)netapp_local_time(&loc, &off);
      dt_format(&loc, s, sizeof(s));
      fmt_offset(tz, sizeof(tz), off);
      console_printf("  Heure locale : %s%s%s, %s (%s%s)\n", C_BOLD(), s, C_RST(), dt_wday_fr(loc.wday), tz,
                     ((g_cfg.dst_eu != 0U) && (off != (int32_t)g_cfg.tz_min * 60)) ? ", heure d'été" : "");
      if (was_synced && have_before)
      {
        dt_t    now;
        int64_t diff;
        (void)board_rtc_get(&now);
        /* écart entre l'heure NTP et l'heure que la RTC aurait indiquée, temps
         * écoulé pendant la synchronisation déduit (résolution : 1 s)        */
        diff = (dt_to_epoch(&now) - dt_to_epoch(&before)) - (int64_t)((HAL_GetTick() - t_before + 500U) / 1000U);
        console_printf("  Correction de la RTC : %+ld s depuis la synchronisation précédente (à 1 s près)\n",
                       (long)diff);
      }
      return 0;
    }
    if ((HAL_GetTick() - t0) >= NTP_SYNC_TIMEOUT_MS)
    {
      console_puts("\n");
      return app_report(AT_TIMEOUT, "réponse du serveur NTP");
    }
    if (console_abort())
    {
      console_puts("\nInterrompu.\n");
      return 0;
    }
    console_puts(".");
    while ((HAL_GetTick() - tw) < 500U)
    {
      app_idle();
    }
  }
}

/* Voir demos.h */
int cmd_time(int argc, char **argv)
{
  dt_t    utc;
  dt_t    loc;
  int32_t off = 0;
  char    s[24];
  char    tz[16];

  (void)argc;
  (void)argv;
  if (!board_rtc_get(&utc) || !netapp_local_time(&loc, &off))
  {
    console_puts("RTC illisible.\n");
    return AT_ERROR;
  }
  dt_format(&utc, s, sizeof(s));
  console_printf("Heure UTC    : %s\n", s);
  dt_format(&loc, s, sizeof(s));
  fmt_offset(tz, sizeof(tz), off);
  console_printf("Heure locale : %s%s%s, %s (%s%s)\n", C_BOLD(), s, C_RST(), dt_wday_fr(loc.wday), tz,
                 ((g_cfg.dst_eu != 0U) && (off != (int32_t)g_cfg.tz_min * 60)) ? ", heure d'été" : "");
  if (board_rtc_synced())
  {
    if (s_ntp_ok)
    {
      console_printf("Source       : RTC (%s) réglée par NTP il y a %lu min\n", board_lse_ok() ? "quartz LSE" : "LSI",
                     (unsigned long)((HAL_GetTick() - s_ntp_last) / 60000U));
    }
    else
    {
      console_printf("Source       : RTC (%s) réglée par NTP avant le dernier reset\n",
                     board_lse_ok() ? "quartz LSE" : "LSI");
    }
  }
  else
  {
    console_printf("%sSource       : RTC non synchronisée (« ntp » une fois connecté)%s\n", C_WARN(), C_RST());
  }
  return 0;
}
