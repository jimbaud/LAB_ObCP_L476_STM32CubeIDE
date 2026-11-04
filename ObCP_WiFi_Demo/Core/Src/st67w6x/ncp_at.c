/**
 ******************************************************************************
 * @file    ncp_at.c
 * @brief   Moteur de commandes AT du ST67W611M1 (voir ncp_at.h).
 *
 *  Logique d'analyse reprise du pilote officiel ST67W6X_Network_Driver
 *  (modem_cmd_handler.c / w61_at_common.c, STMicroelectronics, BSD-3-Clause),
 *  réécrite pour une boucle principale sans RTOS.
 *
 *  Fonctionnement :
 *   - les trames SPI reçues (on_spi_frame) sont ajoutées à la fin d'un
 *     tampon, s_stream ; process_stream() en extrait les lignes complètes
 *     et les trames de données, puis les retire du début du tampon ;
 *   - une seule commande peut être en cours à la fois : son état est dans
 *     s_cmd (préfixe attendu, rappel, OK / ERROR reçus...) ;
 *   - dispatch_line() décide si une ligne est la réponse à la commande en
 *     cours ou un événement non sollicité (transmis à urc_router(), ncp.c) ;
 *   - at_cmd() envoie la commande puis appelle at_poll() en boucle jusqu'à
 *     la réponse finale ou la fin du délai.
 ******************************************************************************
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "ncp_at.h"
#include "ncp_spi.h"
#include "ncp_port.h"

#define PROMPT_TIMEOUT_MS   5000U   /* attente de l'invite '>' (at_cmd_data) */
#define POLL_MAX_FRAMES       16U   /* trames lues au plus par at_poll()     */

/* Débuts des deux trames qui transportent des données binaires */
static const char P_RECVDATA[] = "+CIPRECVDATA:";
static const char P_SUBRECV[]  = "+MQTT:SUBRECV:";

/* État de la commande AT en cours (une seule à la fois) */
typedef struct
{
  bool          active;
  bool          done;
  bool          ok;
  bool          err;
  bool          data_mode;     /* commande suivie de données : OK ne termine pas */
  bool          data_sent;     /* données transmises : confirmation attendue     */
  bool          sent;          /* « Recv n bytes » ou « SEND OK » reçu          */
  bool          send_fail;
  int32_t       recv_len;      /* n de « Recv n bytes », -1 si absent          */
  const char   *prefix;
  size_t        prefix_len;
  at_line_cb_t  cb;
  void         *ctx;
} cmd_state_t;

static uint8_t       s_stream[AT_STREAM_SIZE];   /* octets reçus, pas encore traités */
static uint16_t      s_len;                      /* nombre d'octets dans s_stream    */
static char          s_txbuf[AT_CMD_MAX + 2U];   /* commande + CR LF                 */
static at_urc_cb_t   s_urc;                      /* destinataire des événements      */
static at_mqtt_cb_t  s_mqtt;                     /* destinataire des messages MQTT   */
static bool          s_log;                      /* journal actif (« log on »)       */
static volatile bool s_ready;                    /* « ready » reçu                   */
static at_stats_t    s_stats;
static cmd_state_t   s_cmd;
static bool          s_expect_prompt;            /* invite '>' attendue              */
static bool          s_prompt;                   /* invite '>' reçue                 */

/* Destination de la prochaine trame +CIPRECVDATA */
static uint8_t      *s_sink;
static uint16_t      s_sink_max;
static uint16_t     *s_sink_got;

/* -------------------------------------------------------------------------- */
/* Version par défaut (vide) du journal : « weak » permet à l'application d'en
 * fournir une autre (app.c) sans modifier le pilote.                     */
__attribute__((weak)) void ncp_at_log_hook(const char *dir, const char *text, size_t len)
{
  (void)dir;
  (void)text;
  (void)len;
}

/* Transmet une ligne au journal s'il est actif */
static void log_txt(const char *dir, const char *text, size_t len)
{
  if (s_log)
  {
    ncp_at_log_hook(dir, text, len);
  }
}

/* Voir ncp_at.h */
bool at_starts(const char *s, const char *prefix)
{
  size_t n = strlen(prefix);
  return strncmp(s, prefix, n) == 0;
}

/* Retire les n premiers octets du tampon (les suivants sont décalés au début) */
static void consume(uint16_t n)
{
  if (n >= s_len)
  {
    s_len = 0U;
  }
  else
  {
    memmove(s_stream, &s_stream[n], (size_t)(s_len - n));
    s_len = (uint16_t)(s_len - n);
  }
}

/* Vrai si le flux commence par prefix (complet). */
static bool stream_starts(const char *prefix)
{
  size_t n = strlen(prefix);
  return (s_len >= n) && (memcmp(s_stream, prefix, n) == 0);
}

/* Lit un entier décimal à partir de *pos ; retourne 1 si un séparateur ','
 * le suit, 0 s'il manque des octets, -1 si le format est invalide.        */
static int read_uint(uint16_t *pos, uint32_t *val)
{
  uint32_t v = 0U;
  uint16_t i = *pos;
  uint16_t start = i;

  while ((i < s_len) && (s_stream[i] >= (uint8_t)'0') && (s_stream[i] <= (uint8_t)'9'))
  {
    v = (v * 10U) + (uint32_t)(s_stream[i] - (uint8_t)'0');
    i++;
  }
  if (i == s_len)
  {
    return 0;                       /* attendre la suite */
  }
  if ((i == start) || (s_stream[i] != (uint8_t)','))
  {
    return -1;
  }
  *val = v;
  *pos = (uint16_t)(i + 1U);        /* saute la virgule */
  return 1;
}

/* +CIPRECVDATA:<n>,<données>  — retourne 1 si traité, 0 si absent, -2 si incomplet */
static int direct_recvdata(void)
{
  uint16_t pos = (uint16_t)(sizeof(P_RECVDATA) - 1U);
  uint32_t n = 0U;
  int      r;

  if (!stream_starts(P_RECVDATA))
  {
    return 0;
  }
  r = read_uint(&pos, &n);
  if (r == 0)
  {
    return -2;
  }
  if (r < 0)
  {
    return 0;                       /* format inattendu : traité comme une ligne */
  }
  if (((uint32_t)pos + n) > AT_STREAM_SIZE)
  {
    s_stats.overflows++;            /* ne tiendra jamais dans le tampon */
    s_len = 0U;
    return 1;
  }
  if (s_len < (uint16_t)(pos + n))
  {
    return -2;
  }

  if (s_log)
  {
    char tmp[48];
    int  k = snprintf(tmp, sizeof(tmp), "+CIPRECVDATA:%lu,[%lu octets]", (unsigned long)n, (unsigned long)n);
    log_txt("<-", tmp, (size_t)k);
  }

  if ((s_sink != NULL) && (s_sink_got != NULL))
  {
    uint16_t cp = (n > s_sink_max) ? s_sink_max : (uint16_t)n;
    memcpy(s_sink, &s_stream[pos], cp);
    *s_sink_got = cp;
  }
  consume((uint16_t)(pos + n));
  return 1;
}

/* +MQTT:SUBRECV:<lien>,<lg topic>,<lg msg>,"<topic>",<message> */
static int direct_subrecv(void)
{
  uint16_t pos = (uint16_t)(sizeof(P_SUBRECV) - 1U);
  uint32_t link = 0U;
  uint32_t tlen = 0U;
  uint32_t mlen = 0U;
  uint32_t total;
  int      r;

  if (!stream_starts(P_SUBRECV))
  {
    return 0;
  }
  r = read_uint(&pos, &link);
  if (r <= 0)
  {
    return (r == 0) ? -2 : 0;
  }
  r = read_uint(&pos, &tlen);
  if (r <= 0)
  {
    return (r == 0) ? -2 : 0;
  }
  r = read_uint(&pos, &mlen);
  if (r <= 0)
  {
    return (r == 0) ? -2 : 0;
  }

  total = (uint32_t)pos + tlen + 2U + 1U + mlen;   /* guillemets + virgule */
  if (total > AT_STREAM_SIZE)
  {
    s_stats.overflows++;
    s_len = 0U;
    return 1;
  }
  if (s_len < (uint16_t)total)
  {
    return -2;
  }

  {
    char     topic[128];
    uint32_t tl = (tlen < (sizeof(topic) - 1U)) ? tlen : (uint32_t)(sizeof(topic) - 1U);
    memcpy(topic, &s_stream[pos + 1U], tl);       /* +1 : guillemet ouvrant */
    topic[tl] = '\0';

    if (s_log)
    {
      char tmp[200];
      int  k = snprintf(tmp, sizeof(tmp), "+MQTT:SUBRECV:%lu,\"%s\",[%lu octets]",
                        (unsigned long)link, topic, (unsigned long)mlen);
      log_txt("<-", tmp, (size_t)k);
    }
    if (s_mqtt != NULL)
    {
      s_mqtt(topic, &s_stream[pos + tlen + 3U], (uint16_t)mlen);
    }
  }
  consume((uint16_t)total);
  return 1;
}

/* Aiguille une ligne complète :
 *   1) « ready » : le module (re)démarre ;
 *   2) si une commande est en cours : OK / ERROR la terminent, les lignes
 *      commençant par le préfixe attendu vont au rappel de la commande ;
 *      pour un envoi de données, « Recv n bytes » / « SEND OK » le confirment ;
 *   3) sinon : événement non sollicité, transmis à urc_router() (ncp.c).   */
static void dispatch_line(const char *line)
{
  s_stats.lines++;
  log_txt("<-", line, strlen(line));

  if (strcmp(line, "ready") == 0)
  {
    s_ready = true;
  }

  if (s_cmd.active)
  {
    if (strcmp(line, "OK") == 0)
    {
      s_cmd.ok = true;
      if (!s_cmd.data_mode)
      {
        s_cmd.done = true;
      }
      return;
    }
    if ((strcmp(line, "ERROR") == 0) || (strcmp(line, "FAIL") == 0))
    {
      s_cmd.err  = true;
      s_cmd.done = true;
      return;
    }
    if (at_starts(line, "busy"))
    {
      return;                       /* « busy p... » : le module poursuit */
    }
    if (s_cmd.data_mode)
    {
      if (!s_cmd.data_sent &&
          (at_starts(line, "Recv ") || at_starts(line, "SEND ") || at_starts(line, "+MQTTPUB:")))
      {
        return;                     /* confirmation tardive d'un envoi précédent */
      }
      if (at_starts(line, "Recv "))
      {
        s_cmd.recv_len = atoi(&line[5]);
        s_cmd.sent = true;
        return;
      }
      if ((strcmp(line, "SEND OK") == 0) || at_starts(line, "+MQTTPUB:OK"))
      {
        s_cmd.sent = true;
        return;
      }
      if ((strcmp(line, "SEND FAIL") == 0) || (strcmp(line, "SEND CANCELLED") == 0) ||
          at_starts(line, "+MQTTPUB:FAIL"))
      {
        s_cmd.send_fail = true;
        return;
      }
    }
    if ((s_cmd.prefix != NULL) && (s_cmd.cb != NULL) && at_starts(line, s_cmd.prefix))
    {
      s_cmd.cb(&line[s_cmd.prefix_len], s_cmd.ctx);
      if (s_cmd.prefix_len > 0U)
      {
        return;                     /* ligne de réponse consommée */
      }
      /* préfixe vide (mode brut) : la ligne est aussi traitée comme URC */
    }
  }

  if ((strcmp(line, "SEND OK") == 0) || at_starts(line, "Recv "))
  {
    return;                         /* confirmations tardives : sans objet */
  }

  s_stats.urcs++;
  if (s_urc != NULL)
  {
    s_urc(line);
  }
}

/* Découpe le tampon en éléments complets, dans l'ordre d'arrivée :
 * invite '>', trames de données (+CIPRECVDATA, +MQTT:SUBRECV) ou lignes.
 * S'arrête dès qu'un élément est incomplet : la suite arrivera dans une
 * prochaine trame SPI.                                                   */
static void process_stream(void)
{
  for (;;)
  {
    uint16_t i = 0U;
    uint16_t e;
    int      r;
    char     line[AT_LINE_MAX];

    while ((i < s_len) && ((s_stream[i] == (uint8_t)'\r') || (s_stream[i] == (uint8_t)'\n')))
    {
      i++;
    }
    if (i > 0U)
    {
      consume(i);
    }
    if (s_len == 0U)
    {
      return;
    }

    if (s_expect_prompt && (s_stream[0] == (uint8_t)'>'))
    {
      consume(1U);
      s_prompt = true;
      log_txt("<-", ">", 1U);
      continue;
    }

    r = direct_recvdata();
    if (r == -2)
    {
      return;
    }
    if (r > 0)
    {
      continue;
    }
    r = direct_subrecv();
    if (r == -2)
    {
      return;
    }
    if (r > 0)
    {
      continue;
    }

    e = 0U;
    while ((e < s_len) && (s_stream[e] != (uint8_t)'\r') && (s_stream[e] != (uint8_t)'\n'))
    {
      e++;
    }
    if (e == s_len)
    {
      if (s_len >= AT_STREAM_SIZE)
      {
        s_stats.overflows++;        /* ligne démesurée : on vide le tampon */
        s_len = 0U;
      }
      return;                       /* ligne incomplète : attendre la suite */
    }

    {
      uint16_t n = (e < (AT_LINE_MAX - 1U)) ? e : (uint16_t)(AT_LINE_MAX - 1U);
      memcpy(line, s_stream, n);
      line[n] = '\0';
    }
    consume((uint16_t)(e + 1U));
    dispatch_line(line);
  }
}

/* Rappel de la couche SPI : chaque trame reçue est ajoutée au tampon puis
 * analysée.                                                             */
static void on_spi_frame(uint8_t type, const uint8_t *data, uint16_t len)
{
  if (type != NCP_SPI_TYPE_AT)
  {
    s_stats.other_frames++;
    return;
  }
  if (len > (uint16_t)(AT_STREAM_SIZE - s_len))
  {
    /* Débordement : on privilégie les données les plus récentes. */
    s_stats.overflows++;
    s_len = 0U;
    if (len > AT_STREAM_SIZE)
    {
      data = &data[len - AT_STREAM_SIZE];
      len = AT_STREAM_SIZE;
    }
  }
  memcpy(&s_stream[s_len], data, len);
  s_len = (uint16_t)(s_len + len);
  process_stream();
}

/* -------------------------------------------------------------------------- */
/* Voir ncp_at.h */
void at_init(at_urc_cb_t urc_cb)
{
  s_urc = urc_cb;
  s_len = 0U;
  s_ready = false;
  s_expect_prompt = false;
  s_prompt = false;
  memset(&s_cmd, 0, sizeof(s_cmd));
  memset(&s_stats, 0, sizeof(s_stats));
  ncp_spi_init(on_spi_frame);
}

void at_set_mqtt_cb(at_mqtt_cb_t cb)
{
  s_mqtt = cb;
}

void at_set_log(bool on)
{
  s_log = on;
}

bool at_get_log(void)
{
  return s_log;
}

bool at_busy(void)
{
  return s_cmd.active;
}

/* Voir ncp_at.h : lit des trames tant que le module en signale (RDY = 1) */
void at_poll(void)
{
  uint32_t n = 0U;
  while ((n++ < POLL_MAX_FRAMES) && (ncp_spi_poll() > 0))
  {
  }
}

void at_clear_ready(void)
{
  s_ready = false;
}

/* Voir ncp_at.h */
at_status_t at_wait_ready(uint32_t timeout_ms)
{
  uint32_t t0 = ncp_port_tick();
  while (!s_ready)
  {
    at_poll();
    if ((ncp_port_tick() - t0) >= timeout_ms)
    {
      return AT_TIMEOUT;
    }
  }
  return AT_OK;
}

void at_set_recv_sink(uint8_t *buf, uint16_t max, uint16_t *got)
{
  s_sink = buf;
  s_sink_max = max;
  s_sink_got = got;
}

/* Prépare l'état de la commande puis l'envoie (texte + CR LF) au module */
static at_status_t start_cmd(const char *cmd, const char *prefix, at_line_cb_t cb, void *ctx, bool data_mode)
{
  size_t n;

  if (s_cmd.active)
  {
    return AT_BUSY;
  }
  n = strlen(cmd);
  if ((n == 0U) || (n > AT_CMD_MAX))
  {
    return AT_PARAM;
  }
  memcpy(s_txbuf, cmd, n);
  s_txbuf[n++] = '\r';
  s_txbuf[n++] = '\n';

  memset(&s_cmd, 0, sizeof(s_cmd));
  s_cmd.active     = true;
  s_cmd.data_mode  = data_mode;
  s_cmd.recv_len   = -1;
  s_cmd.prefix     = prefix;
  s_cmd.prefix_len = (prefix != NULL) ? strlen(prefix) : 0U;
  s_cmd.cb         = cb;
  s_cmd.ctx        = ctx;

  log_txt("->", cmd, strlen(cmd));

  if (ncp_spi_send((const uint8_t *)s_txbuf, (uint16_t)n) != NCP_SPI_OK)
  {
    memset(&s_cmd, 0, sizeof(s_cmd));
    return AT_IO;
  }
  return AT_OK;
}

/* Fin de la commande : l'état est effacé, une autre peut être envoyée */
static void end_cmd(void)
{
  memset(&s_cmd, 0, sizeof(s_cmd));
  s_expect_prompt = false;
  s_prompt = false;
}

/* Voir ncp_at.h : envoi, puis attente active de OK / ERROR en lisant les
 * trames du module (les événements reçus entre-temps sont traités).     */
at_status_t at_cmd(const char *cmd, const char *prefix, at_line_cb_t cb, void *ctx, uint32_t timeout_ms)
{
  at_status_t st = start_cmd(cmd, prefix, cb, ctx, false);
  uint32_t    t0 = ncp_port_tick();

  if (st != AT_OK)
  {
    return st;
  }
  if (timeout_ms == 0U)
  {
    end_cmd();                      /* commande sans réponse attendue */
    return AT_OK;
  }
  while (!s_cmd.done)
  {
    at_poll();
    if ((ncp_port_tick() - t0) >= timeout_ms)
    {
      break;
    }
  }
  st = s_cmd.done ? (s_cmd.err ? AT_ERROR : AT_OK) : AT_TIMEOUT;
  end_cmd();
  return st;
}

/* Voir ncp_at.h : la commande est d'abord écrite dans buf (vsnprintf) */
at_status_t at_cmdf(uint32_t timeout_ms, const char *prefix, at_line_cb_t cb, void *ctx, const char *fmt, ...)
{
  char    buf[AT_CMD_MAX + 1U];
  va_list ap;
  int     n;

  va_start(ap, fmt);
  n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if ((n <= 0) || ((size_t)n > AT_CMD_MAX))
  {
    return AT_PARAM;
  }
  return at_cmd(buf, prefix, cb, ctx, timeout_ms);
}

/* Voir ncp_at.h : trois étapes (invite '>', données, confirmation) */
at_status_t at_cmd_data(const char *cmd, const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
  at_status_t st;
  uint32_t    t0;
  uint16_t    off = 0U;

  if ((data == NULL) || (len == 0U))
  {
    return AT_PARAM;
  }

  s_expect_prompt = true;
  s_prompt = false;
  st = start_cmd(cmd, NULL, NULL, NULL, true);
  if (st != AT_OK)
  {
    s_expect_prompt = false;
    return st;
  }

  /* 1) attente de l'invite '>' (le module répond d'abord OK) */
  t0 = ncp_port_tick();
  while (!s_prompt && !s_cmd.err)
  {
    at_poll();
    if ((ncp_port_tick() - t0) >= PROMPT_TIMEOUT_MS)
    {
      break;
    }
  }
  if (!s_prompt)
  {
    st = s_cmd.err ? AT_ERROR : AT_TIMEOUT;
    end_cmd();
    return st;
  }
  s_expect_prompt = false;

  /* 2) envoi des données (éventuellement en plusieurs trames) */
  if (s_log)
  {
    char tmp[32];
    int  k = snprintf(tmp, sizeof(tmp), "[%u octets de données]", (unsigned)len);
    log_txt("->", tmp, (size_t)k);
  }
  while (off < len)
  {
    uint16_t chunk = (uint16_t)(len - off);
    if (chunk > NCP_SPI_TX_MAX)
    {
      chunk = NCP_SPI_TX_MAX;
    }
    if (ncp_spi_send(&data[off], chunk) != NCP_SPI_OK)
    {
      end_cmd();
      return AT_IO;
    }
    off = (uint16_t)(off + chunk);
  }
  s_cmd.data_sent = true;

  /* 3) attente de la confirmation */
  t0 = ncp_port_tick();
  while (!s_cmd.sent && !s_cmd.send_fail && !s_cmd.err)
  {
    at_poll();
    if ((ncp_port_tick() - t0) >= timeout_ms)
    {
      break;
    }
  }
  if (s_cmd.sent && !s_cmd.send_fail && !s_cmd.err)
  {
    st = ((s_cmd.recv_len >= 0) && ((uint32_t)s_cmd.recv_len != len)) ? AT_SENDFAIL : AT_OK;
  }
  else if (s_cmd.send_fail || s_cmd.err)
  {
    st = AT_SENDFAIL;
  }
  else
  {
    st = AT_TIMEOUT;
  }
  end_cmd();
  return st;
}

const at_stats_t *at_stats(void)
{
  return &s_stats;
}

const char *at_status_str(at_status_t st)
{
  switch (st)
  {
    case AT_OK:       return "OK";
    case AT_ERROR:    return "ERROR (refusé par le module)";
    case AT_TIMEOUT:  return "délai dépassé";
    case AT_BUSY:     return "commande déjà en cours";
    case AT_IO:       return "erreur SPI";
    case AT_PARAM:    return "paramètre invalide";
    case AT_SENDFAIL: return "échec d'envoi des données";
    default:          return "?";
  }
}

/* ---- Outils d'analyse ------------------------------------------------------ */
/* Voir ncp_at.h. Exemple : « "Box",-47,6 » -> argv = { "Box", "-47", "6" }, 3 */
int at_split(char *s, char **argv, int max)
{
  int   n = 0;
  char *p = s;

  if ((s == NULL) || (argv == NULL) || (max <= 0))
  {
    return 0;
  }
  for (;;)
  {
    char *field;
    while (*p == ' ')
    {
      p++;
    }
    if (*p == '"')
    {
      field = ++p;
      while ((*p != '\0') && (*p != '"'))
      {
        p++;
      }
      if (*p == '"')
      {
        *p = '\0';
        p++;
      }
      while ((*p != '\0') && (*p != ','))
      {
        p++;
      }
    }
    else
    {
      field = p;
      while ((*p != '\0') && (*p != ','))
      {
        p++;
      }
    }
    if (n < max)
    {
      argv[n++] = field;
    }
    if (*p == ',')
    {
      *p = '\0';
      p++;
      continue;
    }
    break;
  }
  return n;
}

/* Voir ncp_at.h : 4 nombres de 0 à 255 séparés par des points */
bool at_parse_ip(const char *s, uint8_t ip[4])
{
  const char *p = s;
  int         i;

  if (*p == '"')
  {
    p++;
  }
  for (i = 0; i < 4; i++)
  {
    char         *end;
    unsigned long v = strtoul(p, &end, 10);
    if ((end == p) || (v > 255UL))
    {
      return false;
    }
    ip[i] = (uint8_t)v;
    p = end;
    if (i < 3)
    {
      if (*p != '.')
      {
        return false;
      }
      p++;
    }
  }
  return true;
}

/* Voir ncp_at.h : 6 nombres hexadécimaux séparés par ':' */
bool at_parse_mac(const char *s, uint8_t mac[6])
{
  const char *p = s;
  int         i;

  if (*p == '"')
  {
    p++;
  }
  for (i = 0; i < 6; i++)
  {
    char         *end;
    unsigned long v = strtoul(p, &end, 16);
    if ((end == p) || (v > 255UL))
    {
      return false;
    }
    mac[i] = (uint8_t)v;
    p = end;
    if (i < 5)
    {
      if (*p != ':')
      {
        return false;
      }
      p++;
    }
  }
  return true;
}
