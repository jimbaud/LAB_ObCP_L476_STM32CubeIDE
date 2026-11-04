/**
 ******************************************************************************
 * @file    demo_web.c
 * @brief   Démo 3 : point d'accès Wi-Fi (SoftAP) et serveur web embarqué.
 *
 *  Le serveur TCP tourne dans le module (AT+CIPSERVER) ; à chaque connexion
 *  le module signale +CIP:<lien>,CONNECTED puis +IPD:<lien>,<n>,... quand
 *  des données arrivent. L'application lit la requête (AT+CIPRECVDATA),
 *  l'analyse, répond (AT+CIPSEND) puis ferme la connexion (AT+CIPCLOSE).
 *
 *  Pages : /               tableau de bord (HTML + JavaScript)
 *          /api/status     état de la carte (JSON)
 *          /api/led?v=N&c=RRGGBB   luminosité et couleur de la LED RVB
 *          /api/pwm?v=N    sortie PWM du bornier (0..100 %)
 *          /api/scan       réseaux voisins (?refresh=1 : nouveau balayage)
 *          /api/wifi       POST ssid, pass, save : connexion de la station
 *          /api/auto?web=0|1&mqtt=0|1   démarrage automatique, enregistré
 *
 *  Démarrage automatique (web.auto = 1, utile sur batterie) : web_boot()
 *  programme le lancement, web_poll() l'exécute dès que la tentative de
 *  connexion à la box est terminée. Après un redémarrage du module, le point
 *  d'accès et le serveur sont relancés de la même façon s'ils étaient actifs.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "board.h"
#include "config.h"
#include "console.h"
#include "demos.h"
#include "http_parse.h"
#include "io.h"
#include "color.h"
#include "qr_term.h"
#include "util.h"
#include "web_page.h"

/* Chaque connexion d'un navigateur occupe un « lien » du module (0 à 4) ;
 * s_conn[lien] accumule les octets reçus jusqu'à ce que la requête soit
 * complète, puis handle() y répond et la connexion est fermée.         */
#define WEB_BUF          1536U        /* requête la plus longue acceptée          */
#define WEB_RESP_MAX     5120U        /* réponse JSON (en-tête compris)           */
#define WEB_HDR_ROOM      256U        /* place réservée pour l'en-tête HTTP       */
#define WEB_IDLE_MS      8000U        /* connexion sans requête complète : fermée */
#define WEB_MAX_CONN        4U
#define WEB_AUTO_MIN_MS   1500U       /* délai avant un lancement automatique      */
#define WEB_AUTO_WAIT_MS 35000U       /* attente maximale de la connexion à la box */

/* Une connexion en cours */
typedef struct
{
  bool     active;                  /* connexion ouverte par un navigateur   */
  uint16_t len;                     /* octets de la requête déjà reçus       */
  uint32_t t0;                      /* ouverture (fermeture au bout de 8 s)  */
  char     buf[WEB_BUF];            /* requête reçue                         */
} web_conn_t;

static bool       s_running;
static bool       s_pending;          /* lancement automatique programmé       */
static uint32_t   s_pending_t0;
static web_conn_t s_conn[NET_LINKS];
static char       s_resp[WEB_RESP_MAX];  /* réponse en préparation (en-tête + corps) */
static uint32_t   s_requests;            /* compteurs affichés par « web status »    */
static uint32_t   s_errors;
static uint32_t   s_bytes;
static char       s_last_client[16];     /* adresse du dernier navigateur servi      */

/* Voir demos.h */
bool web_running(void)
{
  return s_running;
}

/* ---- Réponses ---------------------------------------------------------------- */
/* En-tête de réponse HTTP : code (200, 404...), type du contenu, longueur.
 * « Connection: close » : une requête par connexion, plus simple à gérer. */
static int header(char *out, size_t len, int code, const char *ctype, size_t blen)
{
  return snprintf(out, len,
                  "HTTP/1.1 %d %s\r\n"
                  "Content-Type: %s\r\n"
                  "Content-Length: %u\r\n"
                  "Cache-Control: no-store\r\n"
                  "Connection: close\r\n\r\n",
                  code, http_status_text(code), ctype, (unsigned)blen);
}

/* Corps déjà écrit dans s_resp + WEB_HDR_ROOM : l'en-tête est placé juste
 * devant pour tout envoyer en une seule fois.                              */
static int send_buffered(uint8_t link, int code, const char *ctype, size_t blen, bool head_only)
{
  char hdr[WEB_HDR_ROOM];
  int  n = header(hdr, sizeof(hdr), code, ctype, blen);
  int  st;
  char *start;

  if ((n <= 0) || ((size_t)n >= sizeof(hdr)))
  {
    return AT_PARAM;
  }
  start = &s_resp[WEB_HDR_ROOM - (size_t)n];
  memcpy(start, hdr, (size_t)n);
  st = net_send(link, (const uint8_t *)start, (uint32_t)n + (head_only ? 0U : (uint32_t)blen));
  if (st == AT_OK)
  {
    s_bytes += (uint32_t)n + (head_only ? 0U : (uint32_t)blen);
  }
  return st;
}

/* Réponse courte (texte d'erreur...) : copiée après la place de l'en-tête */
static int send_text(uint8_t link, int code, const char *ctype, const char *text, bool head_only)
{
  size_t n = strlen(text);
  if (n > (WEB_RESP_MAX - WEB_HDR_ROOM - 1U))
  {
    n = WEB_RESP_MAX - WEB_HDR_ROOM - 1U;
  }
  memcpy(&s_resp[WEB_HDR_ROOM], text, n);
  return send_buffered(link, code, ctype, n, head_only);
}

/* Page HTML (10 Ko, en flash) : en-tête puis page, envoyés directement */
static int send_page(uint8_t link, bool head_only)
{
  char hdr[WEB_HDR_ROOM];
  int  n = header(hdr, sizeof(hdr), 200, "text/html; charset=utf-8", WEB_PAGE_INDEX_LEN);
  int  st = net_send(link, (const uint8_t *)hdr, (uint32_t)n);

  if ((st == AT_OK) && !head_only)
  {
    st = net_send(link, (const uint8_t *)WEB_PAGE_INDEX, (uint32_t)WEB_PAGE_INDEX_LEN);
  }
  if (st == AT_OK)
  {
    s_bytes += (uint32_t)n + (head_only ? 0U : (uint32_t)WEB_PAGE_INDEX_LEN);
  }
  return st;
}

/* ---- API --------------------------------------------------------------------- */
/* /api/scan : réseaux du dernier balayage en JSON */
static size_t scan_json(char *out, size_t len)
{
  jbuf_t   j;
  uint8_t  i;
  uint32_t age = scan_age_s();

  jb_init(&j, out, len);
  jb_open(&j, '{');
  jb_kv_int(&j, "age", (age == UINT32_MAX) ? -1L : (long)age);
  jb_key(&j, "aps");
  jb_open(&j, '[');
  for (i = 0U; (age != UINT32_MAX) && (i < g_wifi.scan_count); i++)
  {
    const wifi_ap_t *ap = &g_wifi.scan[i];
    jb_item(&j);
    jb_open(&j, '{');
    jb_kv_str(&j, "ssid", ap->ssid);
    jb_kv_int(&j, "ch", ap->channel);
    jb_kv_int(&j, "rssi", ap->rssi);
    jb_kv_str(&j, "sec", wifi_security_str(ap->security));
    jb_kv_str(&j, "std", scan_std_label(ap->proto));
    jb_close(&j, '}');
  }
  jb_close(&j, ']');
  jb_close(&j, '}');
  return j.overflow ? 0U : j.pos;
}

/* /api/auto?web=0|1&mqtt=0|1 : démarrage automatique, enregistré aussitôt en
 * flash (sur batterie, la page web est souvent la seule interface).        */
static size_t auto_get(const http_req_t *req, char *out, size_t len)
{
  char   v[4];
  bool   changed = false;
  bool   saved = false;
  jbuf_t j;

  if (http_get_param(req->query, "web", v, sizeof(v)) && ((v[0] == '0') || (v[0] == '1')))
  {
    g_cfg.web_auto = (v[0] == '1') ? 1U : 0U;
    changed = true;
  }
  if (http_get_param(req->query, "mqtt", v, sizeof(v)) && ((v[0] == '0') || (v[0] == '1')))
  {
    g_cfg.mqtt_auto = (v[0] == '1') ? 1U : 0U;
    changed = true;
  }
  if (changed)
  {
    g_app.cfg_dirty = true;
    if (config_save() == 0)
    {
      g_app.cfg_dirty = false;
      saved = true;
    }
    console_event("Page web : démarrage automatique %s serveur web, %s MQTT%s",
                  (g_cfg.web_auto != 0U) ? "avec" : "sans", (g_cfg.mqtt_auto != 0U) ? "avec" : "sans",
                  saved ? " (enregistré en flash)" : " (échec d'enregistrement)");
  }
  jb_init(&j, out, len);
  jb_open(&j, '{');
  jb_kv_bool(&j, "web", g_cfg.web_auto != 0U);
  jb_kv_bool(&j, "mqtt", g_cfg.mqtt_auto != 0U);
  jb_kv_bool(&j, "saved", saved || (!changed && !g_app.cfg_dirty && config_stored()));
  jb_close(&j, '}');
  return j.overflow ? 0U : j.pos;
}

/* POST /api/wifi (formulaire « Connecter la carte à une box ») : SSID et mot
 * de passe enregistrés dans la configuration (et en flash si save=1), puis
 * connexion différée pour laisser partir la réponse au navigateur.      */
static size_t wifi_post(const http_req_t *req, char *out, size_t len)
{
  char   ssid[40];
  char   pass[72];
  char   save[4];
  char   msg[120];
  bool   do_save;
  int    st;
  jbuf_t j;

  jb_init(&j, out, len);
  jb_open(&j, '{');
  if (!http_get_param(req->query, "ssid", ssid, sizeof(ssid)) || (ssid[0] == '\0'))
  {
    jb_kv_bool(&j, "ok", false);
    jb_kv_str(&j, "msg", "SSID manquant");
    jb_close(&j, '}');
    return j.pos;
  }
  if (!http_get_param(req->query, "pass", pass, sizeof(pass)))
  {
    pass[0] = '\0';
  }
  do_save = http_get_param(req->query, "save", save, sizeof(save)) && (save[0] == '1');

  st = cfg_set(&g_cfg, "wifi.ssid", ssid);
  if (st == CFG_OK)
  {
    st = cfg_set(&g_cfg, "wifi.pass", pass);
  }
  if (st != CFG_OK)
  {
    jb_kv_bool(&j, "ok", false);
    jb_kv_str(&j, "msg", cfg_err_str(st));
    jb_close(&j, '}');
    return j.pos;
  }
  g_app.cfg_dirty = true;
  if (do_save && (config_save() == 0))
  {
    g_app.cfg_dirty = false;
  }
  console_event("Page web : connexion demandée au réseau « %s »%s", ssid,
                do_save ? (g_app.cfg_dirty ? " (échec d'enregistrement)" : " (enregistré en flash)") : "");
  netapp_request_connect();
  (void)snprintf(msg, sizeof(msg), "Connexion à « %s » en cours… (suivi dans le terminal)", ssid);
  jb_kv_bool(&j, "ok", true);
  jb_kv_str(&j, "msg", msg);
  jb_close(&j, '}');
  return j.overflow ? 0U : j.pos;
}

/* Aiguillage d'une requête complète selon son chemin : page, API ou 404 */
static int handle(uint8_t link, const http_req_t *req)
{
  char  *body = &s_resp[WEB_HDR_ROOM];
  size_t cap = WEB_RESP_MAX - WEB_HDR_ROOM;
  bool   head = (req->method == HTTP_HEAD);
  size_t n;

  if (req->method == HTTP_OTHER)
  {
    return send_text(link, 405, "text/plain; charset=utf-8", "Méthode non prise en charge\n", false);
  }
  if ((strcmp(req->path, "/") == 0) || (strcmp(req->path, "/index.html") == 0))
  {
    return send_page(link, head);
  }
  if (strcmp(req->path, "/api/status") == 0)
  {
    n = app_status_json(body, cap, true);
    return (n > 0U) ? send_buffered(link, 200, "application/json", n, head)
                    : send_text(link, 500, "text/plain", "Erreur interne\n", head);
  }
  if (strcmp(req->path, "/api/led") == 0)
  {
    char    v[12];
    char    hex[8];
    uint8_t rgb[3];
    long    val;
    if (http_get_param(req->query, "c", v, sizeof(v)) && color_parse(v, rgb))
    {
      io_led_color_set(rgb);
    }
    if (http_get_param(req->query, "v", v, sizeof(v)) && util_parse_long(v, &val) && (val >= 0) && (val <= 100))
    {
      io_led_set((uint8_t)val);
    }
    io_led_color_get(rgb);
    color_hex(rgb, hex);
    n = (size_t)snprintf(body, cap, "{\"led\":%u,\"rgb\":\"%s\"}", (unsigned)io_led_get(), hex);
    return send_buffered(link, 200, "application/json", n, head);
  }
  if (strcmp(req->path, "/api/pwm") == 0)
  {
    char v[8];
    long val;
    if (http_get_param(req->query, "v", v, sizeof(v)) && util_parse_long(v, &val) && (val >= 0) && (val <= 100))
    {
      io_pwm_set((uint8_t)val);
    }
    n = (size_t)snprintf(body, cap, "{\"pwm\":%u}", (unsigned)io_pwm_get());
    return send_buffered(link, 200, "application/json", n, head);
  }
  if (strcmp(req->path, "/api/scan") == 0)
  {
    char r[4];
    if ((http_get_param(req->query, "refresh", r, sizeof(r)) && (r[0] == '1')) || (scan_age_s() == UINT32_MAX))
    {
      (void)scan_run(true);
    }
    n = scan_json(body, cap);
    return (n > 0U) ? send_buffered(link, 200, "application/json", n, head)
                    : send_text(link, 500, "text/plain", "Erreur interne\n", head);
  }
  if (strcmp(req->path, "/api/auto") == 0)
  {
    n = auto_get(req, body, cap);
    return (n > 0U) ? send_buffered(link, 200, "application/json", n, head)
                    : send_text(link, 500, "text/plain", "Erreur interne\n", head);
  }
  if ((strcmp(req->path, "/api/wifi") == 0) && (req->method == HTTP_POST))
  {
    n = wifi_post(req, body, cap);
    return (n > 0U) ? send_buffered(link, 200, "application/json", n, false)
                    : send_text(link, 500, "text/plain", "Erreur interne\n", false);
  }
  if (strcmp(req->path, "/favicon.ico") == 0)
  {
    return send_buffered(link, 204, "image/x-icon", 0U, true);
  }
  return send_text(link, 404, "text/plain; charset=utf-8", "Page introuvable\n", head);
}

/* ---- Connexions ---------------------------------------------------------------- */
/* Ferme la connexion du lien i et libère son tampon */
static void conn_close(uint8_t i)
{
  (void)net_close(i);
  s_conn[i].active = false;
  s_conn[i].len = 0U;
}

/* Oublie toutes les connexions (serveur relancé, module redémarré) */
static void conn_reset(void)
{
  uint8_t i;
  for (i = 0U; i < NET_LINKS; i++)
  {
    s_conn[i].active = false;
    s_conn[i].len = 0U;
  }
}

static int web_start(bool interactive);

/* Lancement automatique programmé (web_boot() ou redémarrage du module) */
static void pending_poll(uint32_t now)
{
  if (s_running)
  {
    s_pending = false;                   /* déjà lancé par « web start » */
    return;
  }
  /* Laisser d'abord partir la (re)connexion à la box, puis attendre son
   * issue pour ne pas envoyer de commandes au module pendant l'association ;
   * le point d'accès démarre ensuite directement sur le canal de la box.   */
  if (((now - s_pending_t0) < WEB_AUTO_MIN_MS) ||
      (netapp_connecting() && ((now - s_pending_t0) < WEB_AUTO_WAIT_MS)))
  {
    return;
  }
  s_pending = false;
  console_async_begin();
  (void)web_start(false);
  console_async_end();
}

/* Événements du point d'accès : appareil connecté, adresse attribuée, départ */
static void ap_events(void)
{
  char mac[20];

  if (g_wifi.evt_ap_join)
  {
    g_wifi.evt_ap_join = false;
    util_fmt_mac(mac, sizeof(mac), g_wifi.ap_evt_mac);
    console_event("Point d'accès : appareil %s connecté (%u au total)", mac, (unsigned)g_wifi.ap_clients);
  }
  if (g_wifi.evt_ap_ip)
  {
    uint8_t ip[4];
    char    ips[16];
    g_wifi.evt_ap_ip = false;
    util_fmt_mac(mac, sizeof(mac), g_wifi.ap_evt_mac);
    memcpy(ip, g_cfg.ap_ip, 4);
    ip[3] = 1U;
    util_fmt_ip(ips, sizeof(ips), ip);
    console_event("Point d'accès : %s a reçu l'adresse %s%s%s%s", mac, g_wifi.ap_evt_ip,
                  s_running ? " -> ouvrir http://" : "", s_running ? ips : "", s_running ? "/" : "");
  }
  if (g_wifi.evt_ap_leave)
  {
    g_wifi.evt_ap_leave = false;
    util_fmt_mac(mac, sizeof(mac), g_wifi.ap_evt_mac);
    console_event("Point d'accès : appareil %s déconnecté (%u restant(s))", mac, (unsigned)g_wifi.ap_clients);
  }
}

/* Voir demos.h. Pour chaque lien : nouvelle connexion -> tampon réservé ;
 * données signalées (+IPD) -> lecture, analyse ; requête complète ->
 * réponse puis fermeture ; connexion muette depuis 8 s -> fermeture.   */
void web_poll(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t  i;

  ap_events();
  if (s_pending)
  {
    pending_poll(now);
  }
  if (!s_running)
  {
    return;
  }
  for (i = 0U; i < NET_LINKS; i++)
  {
    web_conn_t *c = &s_conn[i];
    net_link_t *l = &g_net.link[i];

    if (!c->active)
    {
      if (!l->connected && (l->avail == 0U))
      {
        continue;
      }
      c->active = true;
      c->len = 0U;
      c->t0 = now;
    }
    if (!l->connected && (l->avail == 0U))
    {
      c->active = false;                 /* fermée par le client */
      continue;
    }
    if (l->avail > 0U)
    {
      http_req_t req;
      uint16_t   got = 0U;
      uint16_t   room = (uint16_t)(WEB_BUF - c->len);
      int        r;

      if (room == 0U)
      {
        r = HTTP_PARSE_TOO_BIG;
      }
      else if (net_recv(i, (uint8_t *)&c->buf[c->len], room, &got) != AT_OK)
      {
        s_errors++;
        conn_close(i);
        continue;
      }
      else
      {
        c->len = (uint16_t)(c->len + got);
        r = http_parse_request(c->buf, c->len, WEB_BUF, &req);
      }

      if (r == HTTP_PARSE_OK)
      {
        s_requests++;
        (void)util_strlcpy(s_last_client, l->rip, sizeof(s_last_client));
        if (handle(i, &req) != AT_OK)
        {
          s_errors++;
        }
        conn_close(i);
      }
      else if (r == HTTP_PARSE_BAD)
      {
        s_errors++;
        (void)send_text(i, 400, "text/plain", "Requête invalide\n", false);
        conn_close(i);
      }
      else if (r == HTTP_PARSE_TOO_BIG)
      {
        s_errors++;
        (void)send_text(i, 413, "text/plain", "Requête trop longue\n", false);
        conn_close(i);
      }
      else
      {
        /* requête incomplète : attendre la suite */
      }
    }
    else if ((now - c->t0) >= WEB_IDLE_MS)
    {
      conn_close(i);                     /* connexion ouverte sans requête */
    }
    else
    {
      /* rien à faire */
    }
  }
}

/* Voir demos.h */
void web_module_restarted(void)
{
  /* Le module a perdu son point d'accès et son serveur : s'ils étaient
   * actifs (ou attendus), ils seront relancés par web_poll().          */
  if (s_running || s_pending)
  {
    s_pending = true;
    s_pending_t0 = HAL_GetTick();
  }
  s_running = false;
  conn_reset();
}

/* Voir demos.h */
void web_boot(void)
{
  if (g_cfg.web_auto == 0U)
  {
    return;
  }
  s_pending = true;
  s_pending_t0 = HAL_GetTick();
  console_printf("Serveur web : démarrage automatique (web.auto = 1)%s.\n",
                 ((g_cfg.sta_auto != 0U) && (g_cfg.sta_ssid[0] != '\0')) ? " après la connexion à la box" : "");
}

/* ---- Commande ----------------------------------------------------------------- */
/* Comment rejoindre le serveur : réseau, mot de passe, adresses, QR code */
static void print_access(bool with_qr)
{
  uint8_t ip[4];
  char    ips[16];
  char    port[8] = "";
  char    qr[160];

  if ((net_ap_ip(ip) != AT_OK) || (ip[0] == 0U))
  {
    memcpy(ip, g_cfg.ap_ip, 4);
    ip[3] = 1U;
  }
  util_fmt_ip(ips, sizeof(ips), ip);
  if (g_cfg.web_port != 80U)
  {
    (void)snprintf(port, sizeof(port), ":%u", (unsigned)g_cfg.web_port);
  }
  console_printf("  Réseau Wi-Fi  : %s%s%s\n", C_BOLD(), config_ap_ssid(), C_RST());
  console_printf("  Mot de passe  : %s%s%s\n", C_BOLD(), (g_cfg.ap_pass[0] != '\0') ? g_cfg.ap_pass : "(aucun, réseau ouvert)",
                 C_RST());
  console_printf("  Adresse       : %shttp://%s%s/%s\n", C_BOLD(), ips, port, C_RST());
  if (netapp_sta_ready())
  {
    console_printf("  Réseau local  : http://%s%s/ (via la box)\n", netapp_ip(), port);
  }
  if (with_qr && (util_wifi_qr(qr, sizeof(qr), config_ap_ssid(), g_cfg.ap_pass) > 0U))
  {
    console_puts("  QR code de connexion au point d'accès (appareil photo du téléphone) :\n");
    if (!qr_print(qr))
    {
      console_puts("  (QR code affiché seulement si term.color = 1)\n");
    }
  }
}

/* interactive : commande « web start » (QR code de connexion affiché) ;
 * sinon lancement automatique, plus bref (le QR code remplirait le tampon
 * de la console alors qu'aucun terminal n'est peut-être ouvert).          */
static int web_start(bool interactive)
{
  int st;

  if (s_running)
  {
    console_puts("Le serveur web est déjà actif :\n");
    print_access(true);
    return 0;
  }
  console_printf("Démarrage du point d'accès « %s » (canal %u, %s)...\n", config_ap_ssid(), (unsigned)g_cfg.ap_channel,
                 (g_cfg.ap_pass[0] != '\0') ? "WPA2" : "ouvert");
  if (wifi_is_connected())
  {
    console_printf("%s(station connectée : le point d'accès prend le canal de la box)%s\n", C_DIM(), C_RST());
  }
  st = wifi_ap_start(config_ap_ssid(), g_cfg.ap_pass, g_cfg.ap_channel, WEB_MAX_CONN);
  if (st != AT_OK)
  {
    return app_report(st, "point d'accès");
  }
  st = net_server_start(g_cfg.web_port, WEB_MAX_CONN);
  if (st != AT_OK)
  {
    (void)wifi_ap_stop();
    return app_report(st, "serveur web");
  }
  conn_reset();
  s_running = true;
  s_pending = false;
  console_printf("%s\xE2\x9C\x93 Point d'accès et serveur web actifs%s%s\n", C_OK(),
                 interactive ? "" : " (démarrage automatique)", C_RST());
  print_access(interactive);
  if (!interactive)
  {
    console_printf("%s  (« web start » affiche le QR code de connexion)%s\n", C_DIM(), C_RST());
  }
  else if (g_cfg.web_auto == 0U)
  {
    console_printf("%s(set web.auto 1 + save : démarrage automatique à la mise sous tension)%s\n", C_DIM(),
                   C_RST());
  }
  else
  {
    /* déjà en démarrage automatique */
  }
  return 0;
}

/* « web stop » : ferme les connexions, arrête le serveur et le point d'accès */
static int web_stop(void)
{
  uint8_t i;

  s_pending = false;                       /* annule un lancement automatique */
  if (!s_running && !g_wifi.ap_running)
  {
    console_puts("Le serveur web est déjà arrêté.\n");
    return 0;
  }
  for (i = 0U; i < NET_LINKS; i++)
  {
    if (s_conn[i].active)
    {
      conn_close(i);
    }
  }
  (void)net_server_stop();
  s_running = false;
  if (g_wifi.ap_running)
  {
    (void)wifi_ap_stop();
  }
  console_puts("Serveur web et point d'accès arrêtés.\n");
  return 0;
}

/* « web status » : état, compteurs, adresses */
static int web_status(void)
{
  uint8_t n = 0U;

  if (!s_running)
  {
    console_printf("Serveur web : arrêté%s.\n", s_pending ? " (démarrage automatique en attente de la connexion à la box)"
                                                           : " (« web start » pour le lancer)");
    return 0;
  }
  (void)wifi_ap_clients(&n);
  console_printf("Serveur web : %sactif%s sur le port %u, point d'accès « %s », %u appareil(s) connecté(s)\n",
                 C_OK(), C_RST(), (unsigned)g_cfg.web_port, config_ap_ssid(), (unsigned)n);
  console_printf("  Requêtes : %lu (%lu erreur(s)), %lu Ko envoyés%s%s\n", (unsigned long)s_requests,
                 (unsigned long)s_errors, (unsigned long)(s_bytes / 1024U),
                 (s_last_client[0] != '\0') ? ", dernier client " : "", s_last_client);
  print_access(false);
  return 0;
}

/* Voir demos.h */
int cmd_web(int argc, char **argv)
{
  if ((argc >= 2) && (util_strieq(argv[1], "start") || util_strieq(argv[1], "on")))
  {
    return web_start(true);
  }
  if ((argc >= 2) && (util_strieq(argv[1], "stop") || util_strieq(argv[1], "off")))
  {
    return web_stop();
  }
  if ((argc < 2) || util_strieq(argv[1], "status"))
  {
    return web_status();
  }
  console_puts("Usage : web start | stop | status\n");
  return APP_ERR_USAGE;
}
