/**
 ******************************************************************************
 * @file    demo_mqtt.c
 * @brief   Démo 4 : télémétrie MQTT avec le client MQTT intégré au module.
 *
 *  Topics (racine = mqtt.topic, par défaut obcp/<id>) :
 *    <racine>/status     online / offline (message retenu + « testament »)
 *    <racine>/telemetry  état de la carte en JSON, toutes les mqtt.period s
 *    <racine>/event      appuis sur les boutons User1 (ObCP) et SW1 (X-NUCLEO)
 *    <racine>/cmd        commandes reçues : led=N, color=..., pwm=N, period=N,
 *                        report, ping, msg=...
 *    <racine>/ack        réponses aux commandes
 *
 *  Déroulement : « mqtt start » (ou mqtt.auto = 1 et connexion à la box)
 *  met s_want à vrai ; mqttapp_poll(), à chaque tour de boucle, connecte le
 *  client s'il ne l'est pas (nouvel essai toutes les 15 s), publie la
 *  télémétrie à l'échéance et traite les messages reçus.
 *
 *  Les messages reçus arrivent pendant l'analyse du flux AT, alors qu'une
 *  commande AT peut être en cours : mqttapp_on_message() les range dans une
 *  petite file (4 messages) ; ils sont traités plus tard, hors de cette
 *  analyse, par process_queue(). Règle du pilote : ne jamais envoyer de
 *  commande AT depuis un rappel d'événement.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "app.h"
#include "board.h"
#include "config.h"
#include "console.h"
#include "demos.h"
#include "io.h"
#include "color.h"
#include "util.h"

#define MQTT_QUEUE          4U     /* messages reçus en attente de traitement */
#define MQTT_TOPIC_MAX     96U
#define MQTT_MSG_MAX      200U
#define MQTT_RETRY_MS   15000U     /* délai avant un nouvel essai de connexion */
#define MQTT_CONNECT_MS 15000U     /* attente de la connexion au broker        */
#define MQTT_KEEPALIVE_S   60U     /* « signe de vie » envoyé au broker        */

/* Un message reçu, en attente dans la file */
typedef struct
{
  char     topic[MQTT_TOPIC_MAX];
  char     msg[MQTT_MSG_MAX];
  uint16_t len;
} mqtt_rx_t;

static bool              s_want;          /* client demandé (mqtt start ou mqtt.auto) */
static bool              s_conn;          /* connecté au broker                       */
static uint32_t          s_next_pub;      /* échéance de la prochaine télémétrie      */
static uint32_t          s_retry_at;      /* prochain essai de connexion              */
static uint32_t          s_pub;           /* compteurs de « mqtt status »             */
static uint32_t          s_telemetry;
static bool              s_trace = true;  /* affiche chaque publication de télémétrie */
static uint32_t          s_rx;
static uint32_t          s_err;
static int               s_last_err;
static uint32_t          s_since;         /* début de la connexion en cours           */
static char              s_root[64];      /* racine des topics (obcp/<id>)            */
/* File circulaire des messages reçus : remplie par mqttapp_on_message(),
 * vidée par process_queue()                                             */
static mqtt_rx_t         s_q[MQTT_QUEUE];
static volatile uint8_t  s_q_head;
static volatile uint8_t  s_q_tail;
static volatile uint32_t s_q_drop;        /* messages perdus, file pleine             */

/* Voir demos.h */
bool mqttapp_connected(void)
{
  return s_conn && g_mqtt.connected;
}

/* Appelé depuis le flux AT (+MQTT:SUBRECV) : copie seulement */
void mqttapp_on_message(const char *topic, const uint8_t *msg, uint16_t len)
{
  uint8_t    next = (uint8_t)((s_q_head + 1U) % MQTT_QUEUE);
  mqtt_rx_t *e;
  uint16_t   n;

  if (next == s_q_tail)
  {
    s_q_drop++;
    return;
  }
  e = &s_q[s_q_head];
  (void)util_strlcpy(e->topic, topic, sizeof(e->topic));
  n = (len < (MQTT_MSG_MAX - 1U)) ? len : (uint16_t)(MQTT_MSG_MAX - 1U);
  memcpy(e->msg, msg, n);
  e->msg[n] = '\0';
  e->len = len;
  s_q_head = next;
}

/* ---- Publication ------------------------------------------------------------ */
/* Topic complet : « telemetry » -> « obcp/A1B2C3/telemetry » ; un topic
 * commençant par '/' est absolu (« /maison/test » -> « maison/test »)  */
static void full_topic(char *out, size_t len, const char *sub)
{
  if (sub[0] == '/')
  {
    (void)util_strlcpy(out, &sub[1], len);    /* topic absolu */
  }
  else
  {
    (void)snprintf(out, len, "%s/%s", s_root, sub);
  }
}

/* Publie un texte sur <racine>/<sub> et tient les compteurs à jour */
static int publish(const char *sub, const char *payload, uint8_t qos, bool retain)
{
  char topic[MQTT_TOPIC_MAX];
  int  st;

  full_topic(topic, sizeof(topic), sub);
  st = mqtt_publish(topic, (const uint8_t *)payload, (uint16_t)strlen(payload), qos, retain);
  if (st == AT_OK)
  {
    s_pub++;
  }
  else
  {
    s_err++;
    s_last_err = st;
  }
  return st;
}

/* Publie l'état de la carte (JSON de app_status_json()) sur <racine>/telemetry */
static void publish_telemetry(bool show)
{
  static char json[512];
  size_t      n = app_status_json(json, sizeof(json), true);

  if (n == 0U)
  {
    return;
  }
  if (publish("telemetry", json, 0U, false) == AT_OK)
  {
    s_telemetry++;
    if (show && s_trace)
    {
      io_sensors_t s;
      char         t[12];
      char         z[12];
      io_read(&s);
      util_fmt_fixed(t, sizeof(t), s.temp_x10, 1U);
      util_fmt_fixed(z, sizeof(z), s.acc_mg[2], 3U);
      console_event("%s[MQTT] télémétrie n°%lu publiée (%u octets) : %s °C, az %s g, LED %u %%, PWM %u %%%s", C_DIM(),
                    (unsigned long)s_telemetry, (unsigned)n, t, z, (unsigned)io_led_get(), (unsigned)io_pwm_get(),
                    C_RST());
    }
  }
}

/* Réponse à une commande sur <racine>/ack : {"cmd":"led","ok":true,"led":75} */
static void ack(const char *cmd, bool ok, const char *extra_key, long extra_val)
{
  char   buf[160];
  jbuf_t j;

  jb_init(&j, buf, sizeof(buf));
  jb_open(&j, '{');
  jb_kv_str(&j, "cmd", cmd);
  jb_kv_bool(&j, "ok", ok);
  if (extra_key != NULL)
  {
    jb_kv_int(&j, extra_key, extra_val);
  }
  jb_close(&j, '}');
  (void)publish("ack", buf, 0U, false);
}

/* ---- Commandes reçues ---------------------------------------------------------- */
/* Remplace les caractères de contrôle d'un message reçu par des espaces
 * (un message malveillant ne doit pas piloter le terminal)              */
static void sanitize(char *s)
{
  for (; *s != '\0'; s++)
  {
    if (((unsigned char)*s < 0x20U) || (*s == 0x7F))
    {
      *s = ' ';
    }
  }
}

/* Accepte « led=50 », « led 50 », « led:50 » ou {"led":50} */
static void handle_cmd(const char *msg)
{
  char  buf[MQTT_MSG_MAX];
  char *key;
  char *val = NULL;
  char *p;
  char *w = buf;
  long  v;

  const char *q;

  for (q = msg; *q != '\0'; q++)
  {
    if ((*q != '{') && (*q != '}') && (*q != '"') && ((size_t)(w - buf) < (sizeof(buf) - 1U)))
    {
      *w++ = *q;
    }
  }
  *w = '\0';
  key = buf;
  while (*key == ' ')
  {
    key++;
  }
  for (p = key; *p != '\0'; p++)
  {
    if ((*p == '=') || (*p == ':') || (*p == ' '))
    {
      *p = '\0';
      val = p + 1;
      while (*val == ' ')
      {
        val++;
      }
      break;
    }
    *p = (char)tolower((unsigned char)*p);
  }

  if (strcmp(key, "led") == 0)
  {
    if ((val != NULL) && util_parse_long(val, &v) && (v >= 0) && (v <= 100))
    {
      io_led_set((uint8_t)v);
      console_event("%s[MQTT] commande : LED à %ld %%%s", C_INFO(), v, C_RST());
      ack("led", true, "led", v);
    }
    else
    {
      ack("led", false, NULL, 0);
    }
  }
  else if ((strcmp(key, "color") == 0) || (strcmp(key, "couleur") == 0) || (strcmp(key, "rgb") == 0))
  {
    uint8_t rgb[3];
    if ((val != NULL) && color_parse(val, rgb))
    {
      char hex[8];
      io_led_color_set(rgb);
      if (io_led_get() == 0U)
      {
        io_led_toggle();
      }
      color_hex(rgb, hex);
      console_event("%s[MQTT] commande : LED en %s%s", C_INFO(), hex, C_RST());
      ack("color", true, "led", io_led_get());
    }
    else
    {
      ack("color", false, NULL, 0);
    }
  }
  else if (strcmp(key, "pwm") == 0)
  {
    if ((val != NULL) && util_parse_long(val, &v) && (v >= 0) && (v <= 100))
    {
      io_pwm_set((uint8_t)v);
      console_event("%s[MQTT] commande : sortie PWM à %ld %%%s", C_INFO(), v, C_RST());
      ack("pwm", true, "pwm", v);
    }
    else
    {
      ack("pwm", false, NULL, 0);
    }
  }
  else if (strcmp(key, "period") == 0)
  {
    if ((val != NULL) && util_parse_long(val, &v) && (v >= 2) && (v <= 3600))
    {
      g_cfg.mqtt_period = (uint16_t)v;
      g_app.cfg_dirty = true;
      s_next_pub = HAL_GetTick() + ((uint32_t)v * 1000U);
      console_event("%s[MQTT] commande : période de télémétrie %ld s%s", C_INFO(), v, C_RST());
      ack("period", true, "period", v);
    }
    else
    {
      ack("period", false, NULL, 0);
    }
  }
  else if ((strcmp(key, "report") == 0) || (strcmp(key, "telemetry") == 0))
  {
    publish_telemetry(true);
    ack("report", true, NULL, 0);
  }
  else if (strcmp(key, "ping") == 0)
  {
    ack("ping", true, "uptime", (long)(HAL_GetTick() / 1000U));
  }
  else if ((strcmp(key, "msg") == 0) || (strcmp(key, "print") == 0))
  {
    console_event("%s[MQTT] message : %s%s", C_BOLD(), (val != NULL) ? val : "", C_RST());
    ack("msg", true, NULL, 0);
  }
  else
  {
    console_event("%s[MQTT] commande inconnue : « %s »%s", C_WARN(), key, C_RST());
    ack(key, false, NULL, 0);
  }
}

/* Traite les messages de la file : commandes sur <racine>/cmd, simple
 * affichage pour les autres topics souscrits                          */
static void process_queue(void)
{
  char cmd_topic[MQTT_TOPIC_MAX];

  full_topic(cmd_topic, sizeof(cmd_topic), "cmd");
  while (s_q_tail != s_q_head)
  {
    mqtt_rx_t *e = &s_q[s_q_tail];
    s_rx++;
    sanitize(e->msg);
    if (strcmp(e->topic, cmd_topic) == 0)
    {
      handle_cmd(e->msg);
    }
    else
    {
      console_event("%s[MQTT] %s : %s%s%s", C_INFO(), e->topic, e->msg,
                    (e->len >= MQTT_MSG_MAX) ? " [...]" : "", C_RST());
    }
    s_q_tail = (uint8_t)((s_q_tail + 1U) % MQTT_QUEUE);
  }
}

/* ---- Connexion ------------------------------------------------------------------- */
/* Connexion au broker : configuration (identifiant, testament « offline »),
 * connexion, publication de « online » (retenu), abonnement à <racine>/cmd */
static int mqtt_start(bool verbose)
{
  char lwt[MQTT_TOPIC_MAX];
  char topic[MQTT_TOPIC_MAX];
  int  st;

  if (!netapp_sta_ready())
  {
    return APP_ERR_NOSTA;
  }
  (void)util_strlcpy(s_root, config_mqtt_root(), sizeof(s_root));
  full_topic(lwt, sizeof(lwt), "status");
  if (verbose)
  {
    console_printf("Connexion au broker %s:%u (client « %s »%s)...\n", g_cfg.mqtt_host, (unsigned)g_cfg.mqtt_port,
                   config_mqtt_client_id(), (g_cfg.mqtt_user[0] != '\0') ? ", avec identifiant" : "");
  }
  (void)mqtt_disconnect();                /* AT+MQTTCLEAN : repart d'un état propre */
  st = mqtt_configure((g_cfg.mqtt_user[0] != '\0') ? 1U : 0U, config_mqtt_client_id(), g_cfg.mqtt_user,
                      g_cfg.mqtt_pass, MQTT_KEEPALIVE_S, lwt, "offline", true);
  if (st == AT_OK)
  {
    st = mqtt_connect(g_cfg.mqtt_host, g_cfg.mqtt_port, MQTT_CONNECT_MS);
  }
  if (st != AT_OK)
  {
    s_conn = false;
    s_last_err = st;
    return st;
  }
  s_conn = true;
  s_since = HAL_GetTick();
  g_mqtt.evt_disconnected = false;
  (void)publish("status", "online", 1U, true);
  full_topic(topic, sizeof(topic), "cmd");
  if (mqtt_subscribe(topic, 1U) != AT_OK)
  {
    console_event("%s[MQTT] abonnement à %s impossible%s", C_WARN(), topic, C_RST());
  }
  s_next_pub = HAL_GetTick();             /* première télémétrie immédiate */

  if (verbose)
  {
    console_printf("%s\xE2\x9C\x93 Connecté au broker MQTT%s\n", C_OK(), C_RST());
    console_printf("  Télémétrie : %s/telemetry  (JSON toutes les %u s)\n", s_root, (unsigned)g_cfg.mqtt_period);
    console_printf("  Évènements : %s/event      (boutons User1 et SW1)\n", s_root);
    console_printf("  État       : %s/status     (online / offline, retenu)\n", s_root);
    console_printf("  Commandes  : %s/cmd        (led=50, color=bleu, pwm=30, period=5, report,\n"
                   "                                     ping, msg=Bonjour)\n", s_root);
    console_printf("  Réponses   : %s/ack\n", s_root);
    console_puts("  Depuis un PC (paquet mosquitto-clients, ou MQTTX / MQTT Explorer) :\n");
    console_printf("    mosquitto_sub -h %s -p %u -t \"%s/#\" -v\n", g_cfg.mqtt_host, (unsigned)g_cfg.mqtt_port, s_root);
    console_printf("    mosquitto_pub -h %s -p %u -t \"%s/cmd\" -m \"led=80\"\n", g_cfg.mqtt_host,
                   (unsigned)g_cfg.mqtt_port, s_root);
    if (util_strieq(g_cfg.mqtt_host, "broker.emqx.io") || util_strieq(g_cfg.mqtt_host, "test.mosquitto.org") ||
        util_strieq(g_cfg.mqtt_host, "broker.hivemq.com"))
    {
      console_printf("%s  Broker public : les messages sont visibles de tous, ne rien publier de sensible.%s\n",
                     C_WARN(), C_RST());
    }
  }
  return AT_OK;
}

/* Déconnexion propre : « offline » publié avant de fermer */
static void mqtt_stop(void)
{
  if (s_conn && g_mqtt.connected)
  {
    (void)publish("status", "offline", 1U, true);
  }
  (void)mqtt_disconnect();
  s_conn = false;
}

/* Voir demos.h */
void mqttapp_poll(void)
{
  uint32_t now = HAL_GetTick();

  process_queue();

  if (s_conn && !g_mqtt.connected)
  {
    s_conn = false;
    s_retry_at = now + MQTT_RETRY_MS;
    console_event("%sMQTT : connexion au broker perdue%s%s", C_WARN(),
                  s_want ? " - nouvel essai dans 15 s" : "", C_RST());
  }
  if (s_want && !s_conn && netapp_sta_ready() && ((int32_t)(now - s_retry_at) >= 0))
  {
    int st = mqtt_start(false);
    if (st == AT_OK)
    {
      console_event("%s\xE2\x9C\x93 MQTT : connecté à %s, télémétrie sur %s/telemetry%s", C_OK(), g_cfg.mqtt_host,
                    s_root, C_RST());
    }
    else
    {
      s_retry_at = HAL_GetTick() + MQTT_RETRY_MS;
      console_event("%sMQTT : échec de connexion à %s (%s), nouvel essai dans 15 s%s", C_WARN(), g_cfg.mqtt_host,
                    app_err_str(st), C_RST());
    }
  }
  if (s_conn && ((int32_t)(now - s_next_pub) >= 0))
  {
    s_next_pub = now + ((uint32_t)g_cfg.mqtt_period * 1000U);
    publish_telemetry(true);
  }
}

/* Voir demos.h */
void mqttapp_button(uint8_t btn)
{
  char   buf[128];
  jbuf_t j;

  if (!mqttapp_connected())
  {
    return;
  }
  jb_init(&j, buf, sizeof(buf));
  jb_open(&j, '{');
  jb_kv_str(&j, "event", "button");
  jb_kv_str(&j, "name", (btn == IO_BTN_USER) ? "User1" : "SW1");
  jb_kv_int(&j, "count", (long)io_button_count(btn));
  jb_kv_int(&j, "led", io_led_get());
  jb_close(&j, '}');
  if (publish("event", buf, 1U, false) == AT_OK)
  {
    console_event("%s[MQTT] évènement publié sur %s/event%s", C_DIM(), s_root, C_RST());
  }
}

/* Voir demos.h : connexion 1 s après l'obtention de l'adresse IP */
void mqttapp_sta_up(void)
{
  if (g_cfg.mqtt_auto != 0U)
  {
    s_want = true;
  }
  if (s_want)
  {
    s_retry_at = HAL_GetTick() + 1000U;
  }
}

/* Voir demos.h */
void mqttapp_sta_down(void)
{
  s_conn = false;
  s_retry_at = HAL_GetTick() + 2000U;
}

/* Voir demos.h */
void mqttapp_module_restarted(void)
{
  s_conn = false;
  s_retry_at = HAL_GetTick() + 3000U;
}

/* ---- Commande ---------------------------------------------------------------------- */
/* Recolle les mots argv[from..] séparés par des espaces (message de « mqtt pub ») */
static int join(char *out, size_t len, int argc, char **argv, int from)
{
  size_t pos = 0U;
  int    i;

  out[0] = '\0';
  for (i = from; i < argc; i++)
  {
    int n = snprintf(&out[pos], len - pos, "%s%s", (i > from) ? " " : "", argv[i]);
    if ((n < 0) || ((size_t)n >= (len - pos)))
    {
      return -1;
    }
    pos += (size_t)n;
  }
  return (int)pos;
}

/* Voir demos.h */
int cmd_mqtt(int argc, char **argv)
{
  char topic[MQTT_TOPIC_MAX];
  char msg[MQTT_MSG_MAX];
  int  st;

  if ((argc >= 2) && (util_strieq(argv[1], "start") || util_strieq(argv[1], "connect")))
  {
    if (!netapp_sta_ready())
    {
      return app_report(APP_ERR_NOSTA, "MQTT");
    }
    s_want = true;
    st = mqtt_start(true);
    if (st != AT_OK)
    {
      s_want = false;
      return app_report(st, "MQTT");
    }
    if (g_cfg.mqtt_auto == 0U)
    {
      console_printf("%s(set mqtt.auto 1 + save : démarrage automatique après chaque connexion Wi-Fi)%s\n",
                     C_DIM(), C_RST());
    }
    return 0;
  }
  if ((argc >= 2) && (util_strieq(argv[1], "stop") || util_strieq(argv[1], "disconnect")))
  {
    s_want = false;
    mqtt_stop();
    console_puts("Client MQTT arrêté (état « offline » publié).\n");
    return 0;
  }
  if ((argc >= 4) && util_strieq(argv[1], "pub"))
  {
    if (!mqttapp_connected())
    {
      console_puts("Client MQTT non connecté (mqtt start).\n");
      return MQTT_ERR_NOTCONN;
    }
    if (join(msg, sizeof(msg), argc, argv, 3) <= 0)
    {
      console_puts("Message vide ou trop long.\n");
      return APP_ERR_USAGE;
    }
    full_topic(topic, sizeof(topic), argv[2]);
    st = publish(argv[2], msg, 0U, false);
    if (st != AT_OK)
    {
      return app_report(st, "publication");
    }
    console_printf("Publié sur %s : %s\n", topic, msg);
    return 0;
  }
  if ((argc >= 3) && util_strieq(argv[1], "sub"))
  {
    if (!mqttapp_connected())
    {
      console_puts("Client MQTT non connecté (mqtt start).\n");
      return MQTT_ERR_NOTCONN;
    }
    full_topic(topic, sizeof(topic), argv[2]);
    st = mqtt_subscribe(topic, 0U);
    if (st != AT_OK)
    {
      return app_report(st, "abonnement");
    }
    console_printf("Abonné à %s (les messages reçus s'affichent ici).\n", topic);
    return 0;
  }
  if ((argc >= 2) && util_strieq(argv[1], "trace"))
  {
    bool on;
    if ((argc >= 3) && util_parse_bool(argv[2], &on))
    {
      s_trace = on;
    }
    console_printf("Affichage des publications de télémétrie : %s\n", s_trace ? "activé" : "désactivé");
    return 0;
  }
  if ((argc < 2) || util_strieq(argv[1], "status"))
  {
    if (mqttapp_connected())
    {
      char up[24];
      util_fmt_uptime(up, sizeof(up), (HAL_GetTick() - s_since) / 1000U);
      console_printf("MQTT : %sconnecté%s à %s:%u depuis %s (client « %s »)\n", C_OK(), C_RST(), g_cfg.mqtt_host,
                     (unsigned)g_cfg.mqtt_port, up, config_mqtt_client_id());
      console_printf("  Racine des topics : %s, télémétrie toutes les %u s\n", s_root, (unsigned)g_cfg.mqtt_period);
    }
    else
    {
      console_printf("MQTT : %sdéconnecté%s%s\n", C_WARN(), C_RST(),
                     s_want ? " (reconnexion automatique en attente du Wi-Fi ou du broker)" : " (mqtt start)");
    }
    console_printf("  Messages : %lu publié(s) dont %lu de télémétrie, %lu reçu(s), %lu erreur(s)",
                   (unsigned long)s_pub, (unsigned long)s_telemetry, (unsigned long)s_rx, (unsigned long)s_err);
    if (s_q_drop > 0U)
    {
      console_printf(", %lu perdu(s) (file pleine)", (unsigned long)s_q_drop);
    }
    if (s_last_err != 0)
    {
      console_printf(" ; dernière erreur : %s", app_err_str(s_last_err));
    }
    console_puts("\n");
    return 0;
  }
  console_puts("Usage : mqtt start | stop | status | pub <topic> <message> | sub <topic> | trace on|off\n");
  return APP_ERR_USAGE;
}
