/**
 ******************************************************************************
 * @file    report.c
 * @brief   Textes et lignes JSON envoyés au terminal (voir report.h).
 *
 *  Toutes les phrases affichées par la carte sont ici : pour changer un
 *  texte (ou traduire le programme), c'est le seul fichier à modifier.
 *
 *  Une ligne est préparée dans un tampon (line_t) par petits morceaux
 *  (line_add, comme printf), puis envoyée d'un bloc : deux évènements ne
 *  se mélangent jamais au milieu d'une ligne.
 ******************************************************************************
 */
#include "report.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app.h"                  /* app_is_ota */
#include "app_config.h"
#include "led_fx.h"
#include "main.h"                 /* HAL_GetTick */
#include "mesh_radio.h"
#include "node.h"
#include "usb_console.h"

static bool s_json;               /* présentation JSON (« pc on ») */
static bool s_journal;            /* journal des trames (« journal on ») */

/* ========================================================================== */
/* Préparation d'une ligne                                                    */
/* ========================================================================== */

typedef struct
{
  char   text[384];
  size_t len;
} line_t;

/* Vide la ligne en préparation. */
static void line_init(line_t *l)
{
  l->len     = 0U;
  l->text[0] = '\0';
}

/* Ajoute du texte mis en forme (tronqué si le tampon est plein). */
static void line_add(line_t *l, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void line_add(line_t *l, const char *format, ...)
{
  va_list args;
  int     n;
  size_t  room = sizeof(l->text) - l->len;

  if (room <= 1U)
  {
    return;
  }
  va_start(args, format);
  n = vsnprintf(&l->text[l->len], room, format, args);
  va_end(args);
  if (n > 0)
  {
    l->len += ((size_t)n < room) ? (size_t)n : (room - 1U);
  }
}

/* Termine la ligne (\r\n) et l'envoie. Les 2 derniers octets du tampon
 * sont gardés pour la fin de ligne. */
static void line_send(line_t *l)
{
  if (l->len > (sizeof(l->text) - 3U))
  {
    l->len = sizeof(l->text) - 3U;
  }
  l->text[l->len]      = '\r';
  l->text[l->len + 1U] = '\n';
  l->text[l->len + 2U] = '\0';
  (void)usb_console_print(l->text);
}

/* Heure de la carte : « [    12.345] ». */
static void line_time(line_t *l)
{
  uint32_t t = HAL_GetTick();
  line_add(l, "[%6lu.%03lu] ", (unsigned long)(t / 1000U), (unsigned long)(t % 1000U));
}

/* Adresse lisible : 17, g3, tous. */
static void line_addr(line_t *l, uint8_t addr)
{
  char text[8];
  (void)mesh_addr_format(addr, text, sizeof(text));
  line_add(l, "%s", text);
}

/* Chemin d'une trame reçue : « (direct, -61 dBm) » ou « (2 relais, -61 dBm) ». */
static void line_via(line_t *l, uint8_t hops, int8_t rssi)
{
  if (hops == 0U)
  {
    line_add(l, "(direct, %d dBm)", (int)rssi);
  }
  else
  {
    line_add(l, "(%u relais, %d dBm)", (unsigned)hops, (int)rssi);
  }
}

/* Nombre de relais d'une route : « (direct) » ou « (2 relais) ». */
static void line_hops(line_t *l, uint8_t hops)
{
  if (hops == 0U)
  {
    line_add(l, " (direct)");
  }
  else
  {
    line_add(l, " (%u relais)", (unsigned)hops);
  }
}

/* Résumé d'une trame : « TEXTE 1>tous n°12 ttl 3 ». */
static void line_frame(line_t *l, const mesh_frame_t *f)
{
  line_add(l, "%s %u>", mesh_type_name(f->type), (unsigned)f->src);
  line_addr(l, f->dst);
  line_add(l, " n°%u ttl %u", (unsigned)f->seq, (unsigned)f->ttl);
}

/* Origine d'un message reçu, et sa destination si ce n'est pas nous seuls :
 * « de 5 », « de 5 à tous », « de 5 au groupe 2 ». */
static void line_from(line_t *l, const mesh_frame_t *f)
{
  line_add(l, "de %u", (unsigned)f->src);
  if (f->dst == MESH_ADDR_ALL)
  {
    line_add(l, " à tous");
  }
  else if (mesh_addr_is_group(f->dst))
  {
    line_add(l, " au groupe %u", (unsigned)mesh_addr_group(f->dst));
  }
}

/* Texte reçu : les caractères de contrôle sont remplacés par « ? ». */
static void line_text(line_t *l, const uint8_t *s, uint8_t n)
{
  char clean[MESH_PAYLOAD_MAX + 1U];

  for (uint8_t i = 0U; i < n; i++)
  {
    clean[i] = (s[i] < 0x20U || s[i] == 0x7FU) ? '?' : (char)s[i];
  }
  clean[n] = '\0';
  line_add(l, "%s", clean);
}

/* Accélération en g, à partir de mg : « -0.02 ». */
static void line_g(line_t *l, int16_t mg)
{
  int  v   = mg;
  bool neg = (v < 0);
  int  c   = ((neg ? -v : v) + 5) / 10;   /* centièmes de g, arrondis */

  line_add(l, "%s%d.%02d", (neg && (c > 0)) ? "-" : "", c / 100, c % 100);   /* pas de « -0.00 » */
}

/* « 24 °C, X = 0.01 g, Y = -0.01 g, Z = 1.00 g » (mêmes unités pour les
 * mesures de cette carte et celles reçues). */
static void line_measure(line_t *l, bool temp_ok, int temp_c, bool accel_ok, int16_t x, int16_t y, int16_t z)
{
  if (temp_ok)
  {
    line_add(l, " %d °C,", temp_c);
  }
  if (accel_ok)
  {
    line_add(l, " X = ");
    line_g(l, x);
    line_add(l, " g, Y = ");
    line_g(l, y);
    line_add(l, " g, Z = ");
    line_g(l, z);
    line_add(l, " g");
  }
  else
  {
    line_add(l, " accéléromètre absent");
  }
}

/* ========================================================================== */
/* Lignes JSON                                                                */
/* ========================================================================== */

/* Commence une ligne JSON : nature de l'évènement et heure de la carte. */
static void json_begin(line_t *l, const char *ev)
{
  line_init(l);
  line_add(l, "{\"ev\":\"%s\",\"t\":%lu", ev, (unsigned long)HAL_GetTick());
}

/* Termine la ligne JSON et l'envoie. */
static void json_end(line_t *l)
{
  line_add(l, "}");
  line_send(l);
}

/* Champs de l'en-tête d'une trame. */
static void json_frame(line_t *l, const mesh_frame_t *f)
{
  line_add(l, ",\"src\":%u,\"dst\":%u,\"seq\":%u,\"ttl\":%u,\"hops\":%u,\"last\":%u,\"type\":\"%s\"",
           (unsigned)f->src, (unsigned)f->dst, (unsigned)f->seq, (unsigned)f->ttl, (unsigned)f->hops,
           (unsigned)f->last, mesh_type_name(f->type));
}

/* Chaîne JSON : guillemets, barre oblique inverse et caractères de
 * contrôle échappés. */
static void json_string(line_t *l, const char *key, const uint8_t *s, uint8_t n)
{
  line_add(l, ",\"%s\":\"", key);
  for (uint8_t i = 0U; i < n; i++)
  {
    uint8_t c = s[i];
    if ((c == '"') || (c == '\\'))
    {
      line_add(l, "\\%c", (char)c);
    }
    else if (c < 0x20U)
    {
      line_add(l, "\\u%04x", (unsigned)c);
    }
    else
    {
      line_add(l, "%c", (char)c);
    }
  }
  line_add(l, "\"");
}

/* Liste JSON de numéros : [5,9,12]. */
static void json_list(line_t *l, const char *key, const uint8_t *ids, uint8_t n)
{
  line_add(l, ",\"%s\":[", key);
  for (uint8_t i = 0U; i < n; i++)
  {
    line_add(l, "%s%u", (i > 0U) ? "," : "", (unsigned)ids[i]);
  }
  line_add(l, "]");
}

/* Données décodées selon le type (longueurs vérifiées). */
static void json_payload(line_t *l, const mesh_frame_t *f)
{
  const uint8_t *p = f->payload;

  switch (f->type)
  {
    case MESH_T_HELLO:
      if (f->len >= MESH_HELLO_LEN(0U))
      {
        uint8_t n = p[5];
        if (n > MESH_HELLO_NEIGHBORS || f->len < MESH_HELLO_LEN(n))
        {
          n = 0U;
        }
        line_add(l, ",\"tag\":\"%08lX\",\"relay\":%u,\"fixed\":%u,\"nb\":[", (unsigned long)mesh_get_u32(p),
                 (p[4] & MESH_HELLO_RELAY) ? 1U : 0U, (p[4] & MESH_HELLO_FIXED_ID) ? 1U : 0U);
        for (uint8_t i = 0U; i < n; i++)
        {
          line_add(l, "%s[%u,%d]", (i > 0U) ? "," : "", (unsigned)p[6U + (2U * i)],
                   (int)(int8_t)p[7U + (2U * i)]);
        }
        line_add(l, "]");
      }
      break;
    case MESH_T_PING:
      if (f->len >= 1U)
      {
        line_add(l, ",\"ping\":%u", (unsigned)p[0]);
      }
      break;
    case MESH_T_PONG:
      if (f->len >= 3U)
      {
        line_add(l, ",\"ping\":%u,\"hops_out\":%u,\"rssi_out\":%d", (unsigned)p[0], (unsigned)p[1],
                 (int)(int8_t)p[2]);
      }
      break;
    case MESH_T_TRACE:
      if (f->len >= 1U)
      {
        line_add(l, ",\"trace\":%u", (unsigned)p[0]);
        json_list(l, "route", &p[1], (uint8_t)(f->len - 1U));
      }
      break;
    case MESH_T_TRACE_BACK:
      if ((f->len >= 2U) && (p[1] <= (uint8_t)(f->len - 2U)))
      {
        line_add(l, ",\"trace\":%u", (unsigned)p[0]);
        json_list(l, "fwd", &p[2], p[1]);
        json_list(l, "back", &p[2U + p[1]], (uint8_t)(f->len - 2U - p[1]));
      }
      break;
    case MESH_T_TEXT:
      json_string(l, "text", p, f->len);
      break;
    case MESH_T_LED:
      if (f->len >= 3U)
      {
        line_add(l, ",\"r\":%u,\"g\":%u,\"b\":%u,\"dur\":%u", (unsigned)p[0], (unsigned)p[1],
                 (unsigned)p[2], (f->len >= 4U) ? (unsigned)p[3] : 0U);
      }
      break;
    case MESH_T_IDENT:
      if (f->len >= 1U)
      {
        line_add(l, ",\"dur\":%u", (unsigned)p[0]);
      }
      break;
    case MESH_T_MEASURE:
      if (f->len >= MESH_MEASURE_LEN)
      {
        line_add(l, ",\"acc_ok\":%u,\"temp_ok\":%u,\"temp\":%d,\"ax\":%d,\"ay\":%d,\"az\":%d",
                 (p[0] & MESH_MEASURE_ACCEL_OK) ? 1U : 0U, (p[0] & MESH_MEASURE_TEMP_OK) ? 1U : 0U,
                 (int)(int8_t)p[1], (int)(int16_t)mesh_get_u16(&p[2]), (int)(int16_t)mesh_get_u16(&p[4]),
                 (int)(int16_t)mesh_get_u16(&p[6]));
      }
      break;
    case MESH_T_CONFIG:
      if (f->len >= 3U)
      {
        line_add(l, ",\"key\":\"%s\",\"value\":%d", settings_name((setting_t)p[0]),
                 (int)(int16_t)mesh_get_u16(&p[1]));
      }
      break;
    default:
      break;
  }
  if (f->len > 0U)
  {
    line_add(l, ",\"data\":\"");
    for (uint8_t i = 0U; i < f->len; i++)
    {
      line_add(l, "%02X", (unsigned)p[i]);
    }
    line_add(l, "\"");
  }
}

/* ========================================================================== */
/* Fonctions publiques                                                        */
/* ========================================================================== */

/* Voir report.h */
void report_set_json(bool on)
{
  s_json = on;
}

/* Voir report.h */
bool report_is_json(void)
{
  return s_json;
}

/* Voir report.h */
void report_set_journal(bool on)
{
  s_journal = on;
}

/* Voir report.h */
bool report_is_journal(void)
{
  return s_journal;
}

/* Voir report.h */
void report_print(const char *text)
{
  (void)usb_console_print(text);
}

/* Voir report.h */
void report_printf(const char *format, ...)
{
  char    text[256];
  va_list args;

  va_start(args, format);
  (void)vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  (void)usb_console_print(text);
}

/* Nom JSON de chaque évènement du réseau, dans l'ordre de mesh_event_t. */
static const char *const k_event_json[] =
{
  "tx", "relay", "rx", "dup", "echo", "filtered", "suppressed", "ttl_end",
  "dropped", "tx_fail", "conflict", "id_changed"
};

/* Verbe du journal (présentation texte), dans l'ordre de mesh_event_t. */
static const char *const k_event_text[] =
{
  "-> émise", "-> relayée", "<- reçue", "<- doublon", "<- écho", "xx filtrée",
  "-- relais annulé", "-- fin du TTL", "!! perdue (file pleine)", "!! refusée par la radio",
  "", ""
};

/* Voir report.h */
void report_mesh_event(const mesh_t *m, mesh_event_t ev, const mesh_frame_t *f, int8_t rssi)
{
  line_t l;

  if ((unsigned)ev >= (sizeof(k_event_json) / sizeof(k_event_json[0])))
  {
    return;
  }

  /* ---- Présentation JSON : chaque évènement ---- */
  if (s_json)
  {
    json_begin(&l, k_event_json[ev]);
    if (f != NULL)
    {
      json_frame(&l, f);
      if ((ev == MESH_EV_RX) || (ev == MESH_EV_DUP) || (ev == MESH_EV_ECHO)
          || (ev == MESH_EV_FILTERED) || (ev == MESH_EV_ID_CONFLICT))
      {
        line_add(&l, ",\"rssi\":%d", (int)rssi);
      }
      if ((ev == MESH_EV_RX) || (ev == MESH_EV_TX) || (ev == MESH_EV_RELAY))
      {
        json_payload(&l, f);
      }
    }
    if (ev == MESH_EV_ID_CHANGED)
    {
      line_add(&l, ",\"old\":%u,\"new\":%u", (unsigned)mesh_previous_id(m), (unsigned)mesh_id(m));
    }
    if (ev == MESH_EV_TX_FAIL)
    {
      line_add(&l, ",\"code\":%u", (unsigned)mesh_radio_last_refusal());
    }
    json_end(&l);
    return;
  }

  /* ---- Présentation texte ---- */
  line_init(&l);
  line_time(&l);
  switch (ev)
  {
    case MESH_EV_ID_CONFLICT:           /* toujours affiché */
      line_add(&l, "!! Numéro %u en double : la carte d'étiquette %08lX l'utilise aussi",
               (unsigned)f->src, (unsigned long)mesh_get_u32(f->payload));
      if (mesh_id_is_fixed(m) && (mesh_id(m) == f->src))
      {
        line_add(&l, " (numéro fixé ici : changez-le avec « id N »)");
      }
      line_send(&l);
      return;

    case MESH_EV_ID_CHANGED:            /* toujours affiché */
      line_add(&l, "!! Numéro changé : %u -> %u (il était aussi utilisé par une autre carte)",
               (unsigned)mesh_previous_id(m), (unsigned)mesh_id(m));
      line_send(&l);
      return;

    case MESH_EV_TX_FAIL:               /* toujours affiché : problème de radio */
      break;

    default:
      if (!s_journal)
      {
        return;
      }
      break;
  }
  if (f == NULL)
  {
    return;
  }
  line_add(&l, "%s ", k_event_text[ev]);
  line_frame(&l, f);
  switch (ev)
  {
    case MESH_EV_RX:
    case MESH_EV_DUP:
      line_add(&l, " de %u ", (unsigned)f->last);
      line_via(&l, f->hops, rssi);
      break;
    case MESH_EV_ECHO:
      line_add(&l, " relayée par %u (%d dBm)", (unsigned)f->last, (int)rssi);
      break;
    case MESH_EV_FILTERED:
      if (mesh_is_blocked(m, f->last))
      {
        line_add(&l, " de %u (émetteur bloqué)", (unsigned)f->last);
      }
      else
      {
        line_add(&l, " de %u (%d dBm, seuil %d)", (unsigned)f->last, (int)rssi,
                 (int)mesh_settings(m)->rssi_min);
      }
      break;
    case MESH_EV_SUPPRESSED:
      line_add(&l, " (assez d'autres relais entendus)");
      break;
    case MESH_EV_RELAY:
      line_add(&l, " (%u relais avant)", (unsigned)(f->hops - 1U));
      break;
    case MESH_EV_TX_FAIL:
      if (mesh_radio_last_refusal() != 0U)
      {
        line_add(&l, " (code 0x%02X du module)", (unsigned)mesh_radio_last_refusal());
      }
      break;
    default:
      break;
  }
  line_send(&l);
}

/* Voir report.h */
void report_radio_fallback(uint8_t code)
{
  line_t l;

  if (s_json)
  {
    json_begin(&l, "radio");
    line_add(&l, ",\"mode\":\"alterne\",\"code\":%u", (unsigned)code);
    json_end(&l);
    return;
  }
  line_init(&l);
  line_time(&l);
  line_add(&l, "!! Le module refuse d'émettre pendant l'écoute (code 0x%02X) : écoute arrêtée pendant "
           "chaque émission", (unsigned)code);
  line_send(&l);
}

/* Voir report.h */
void report_message(const mesh_frame_t *f, int8_t rssi, int32_t reply_ms)
{
  line_t         l;
  const uint8_t *p = f->payload;

  if (s_json)
  {
    return;
  }
  line_init(&l);
  line_time(&l);
  switch (f->type)
  {
    case MESH_T_TEXT:
      line_add(&l, "Message ");
      line_from(&l, f);
      line_add(&l, " ");
      line_via(&l, f->hops, rssi);
      line_add(&l, " : ");
      line_text(&l, p, f->len);
      break;
    case MESH_T_LED:
      if (f->len < 3U)
      {
        return;
      }
      line_add(&l, "Couleur ");
      line_from(&l, f);
      line_add(&l, " ");
      line_via(&l, f->hops, rssi);
      if ((p[0] | p[1] | p[2]) == 0U)
      {
        line_add(&l, " : LED éteinte");
      }
      else
      {
        line_add(&l, " : rouge %u, vert %u, bleu %u", (unsigned)p[0], (unsigned)p[1], (unsigned)p[2]);
        if ((f->len >= 4U) && (p[3] > 0U))
        {
          line_add(&l, ", pendant %u s", (unsigned)p[3]);
        }
      }
      break;
    case MESH_T_WAVE:
      line_add(&l, "Vague lancée par %u : reçue ", (unsigned)f->src);
      if (f->hops == 0U)
      {
        line_add(&l, "en direct");
      }
      else
      {
        line_add(&l, "après %u relais", (unsigned)f->hops);
      }
      line_add(&l, ", LED %s", led_fx_hops_color_name(f->hops));
      break;
    case MESH_T_IDENT:
      line_add(&l, "Identification demandée ");
      line_from(&l, f);
      line_add(&l, " ");
      line_via(&l, f->hops, rssi);
      line_add(&l, " : LED blanche clignotante pendant %u s",
               (f->len >= 1U && p[0] > 0U) ? (unsigned)p[0] : (unsigned)NODE_IDENT_DEFAULT_S);
      break;
    case MESH_T_PING:
      line_add(&l, "Ping n°%u ", (f->len >= 1U) ? (unsigned)p[0] : 0U);
      line_from(&l, f);
      line_add(&l, " ");
      line_via(&l, f->hops, rssi);
      break;
    case MESH_T_TRACE:
      line_add(&l, "Recherche de route ");
      line_from(&l, f);
      line_add(&l, " ");
      line_via(&l, f->hops, rssi);
      break;
    case MESH_T_MEASURE_REQ:
      line_add(&l, "Demande de mesures ");
      line_from(&l, f);
      line_add(&l, " ");
      line_via(&l, f->hops, rssi);
      break;
    case MESH_T_MEASURE:
      if (f->len < MESH_MEASURE_LEN)
      {
        return;
      }
      line_add(&l, "Mesures de %u ", (unsigned)f->src);
      line_via(&l, f->hops, rssi);
      line_add(&l, " :");
      line_measure(&l, (p[0] & MESH_MEASURE_TEMP_OK) != 0U, (int)(int8_t)p[1],
                   (p[0] & MESH_MEASURE_ACCEL_OK) != 0U, (int16_t)mesh_get_u16(&p[2]),
                   (int16_t)mesh_get_u16(&p[4]), (int16_t)mesh_get_u16(&p[6]));
      break;
    default:
      return;                             /* HELLO, PONG, TRACE_RET, REGLAGE : ailleurs */
  }
  if (reply_ms == 0)
  {
    line_add(&l, " -> réponse envoyée");
  }
  else if (reply_ms > 0)
  {
    line_add(&l, " -> réponse dans %ld ms", (long)reply_ms);
  }
  line_send(&l);
}

/* Voir report.h */
void report_pong(const mesh_frame_t *f, int8_t rssi, uint32_t rtt_ms)
{
  line_t l;

  if (f->len < 3U)
  {
    return;
  }
  if (s_json)
  {
    json_begin(&l, "pong");
    line_add(&l, ",\"from\":%u,\"ping\":%u,\"rtt\":%lu,\"hops_out\":%u,\"rssi_out\":%d,\"hops_back\":%u,\"rssi_back\":%d",
             (unsigned)f->src, (unsigned)f->payload[0], (unsigned long)rtt_ms, (unsigned)f->payload[1],
             (int)(int8_t)f->payload[2], (unsigned)f->hops, (int)rssi);
    json_end(&l);
    return;
  }
  line_init(&l);
  line_time(&l);
  line_add(&l, "Réponse de %u au ping n°%u en %lu ms", (unsigned)f->src, (unsigned)f->payload[0],
           (unsigned long)rtt_ms);
  line_send(&l);
  line_init(&l);                          /* deuxième ligne : moins de 100 colonnes */
  line_add(&l, "             aller ");
  line_via(&l, f->payload[1], (int8_t)f->payload[2]);
  line_add(&l, ", retour ");
  line_via(&l, f->hops, rssi);
  line_send(&l);
}

/* Voir report.h */
void report_ping_end(uint8_t dst, uint8_t ping_id, uint8_t replies)
{
  line_t l;

  if (s_json)
  {
    json_begin(&l, "ping_end");
    line_add(&l, ",\"to\":%u,\"ping\":%u,\"replies\":%u", (unsigned)dst, (unsigned)ping_id, (unsigned)replies);
    json_end(&l);
    return;
  }
  if (mesh_addr_is_node(dst) && (replies > 0U))
  {
    return;                               /* réponse déjà affichée */
  }
  line_init(&l);
  line_time(&l);
  if (replies == 0U)
  {
    line_add(&l, "Pas de réponse au ping n°%u vers ", (unsigned)ping_id);
    line_addr(&l, dst);
    line_add(&l, " (délai écoulé)");
  }
  else
  {
    line_add(&l, "Fin du ping n°%u vers ", (unsigned)ping_id);
    line_addr(&l, dst);
    line_add(&l, " : %u réponse%s", (unsigned)replies, (replies > 1U) ? "s" : "");
  }
  line_send(&l);
}

/* Écrit une route : départ > relais... > arrivée. */
static void line_route(line_t *l, uint8_t from, const uint8_t *relays, uint8_t n, uint8_t to)
{
  line_add(l, "%u", (unsigned)from);
  for (uint8_t i = 0U; i < n; i++)
  {
    line_add(l, " > %u", (unsigned)relays[i]);
  }
  line_add(l, " > %u", (unsigned)to);
}

/* Voir report.h */
void report_trace(const mesh_frame_t *f, uint32_t rtt_ms)
{
  line_t         l;
  const uint8_t *p = f->payload;
  uint8_t        k;
  uint8_t        n_back;
  uint8_t        me = mesh_id(node_mesh());

  if ((f->len < 2U) || (p[1] > (uint8_t)(f->len - 2U)))
  {
    return;
  }
  k      = p[1];
  n_back = (uint8_t)(f->len - 2U - k);
  if (s_json)
  {
    json_begin(&l, "trace");
    line_add(&l, ",\"to\":%u,\"trace\":%u,\"rtt\":%lu", (unsigned)f->src, (unsigned)p[0], (unsigned long)rtt_ms);
    json_list(&l, "fwd", &p[2], k);
    json_list(&l, "back", &p[2U + k], n_back);
    json_end(&l);
    return;
  }
  line_init(&l);
  line_time(&l);
  line_add(&l, "Route vers %u (trace n°%u, %lu ms)", (unsigned)f->src, (unsigned)p[0], (unsigned long)rtt_ms);
  line_send(&l);
  line_init(&l);
  line_add(&l, "             aller  : ");
  line_route(&l, me, &p[2], k, f->src);
  line_hops(&l, k);
  line_send(&l);
  line_init(&l);
  line_add(&l, "             retour : ");
  line_route(&l, f->src, &p[2U + k], n_back, me);
  line_hops(&l, f->hops);
  if (n_back < f->hops)
  {
    line_add(&l, " (liste tronquée : 14 octets de données au plus)");
  }
  line_send(&l);
}

/* Voir report.h */
void report_trace_timeout(uint8_t dst, uint8_t trace_id)
{
  line_t l;

  if (s_json)
  {
    json_begin(&l, "trace_timeout");
    line_add(&l, ",\"to\":%u,\"trace\":%u", (unsigned)dst, (unsigned)trace_id);
    json_end(&l);
    return;
  }
  line_init(&l);
  line_time(&l);
  line_add(&l, "Pas de réponse à la trace n°%u vers %u (délai écoulé)", (unsigned)trace_id, (unsigned)dst);
  line_send(&l);
}

/* Voir report.h */
void report_measure_local(const node_measure_t *m)
{
  line_t l;

  line_init(&l);
  line_add(&l, "Mesures de cette carte :");
  line_measure(&l, m->temp_ok, (int)m->temp_c, m->accel_ok, m->ax_mg, m->ay_mg, m->az_mg);
  line_send(&l);
}

/* Voir report.h */
void report_setting_changed(uint8_t by, setting_t key, int32_t value, bool accepted)
{
  line_t l;
  char   text[24];

  if (s_json)
  {
    json_begin(&l, "setting");
    line_add(&l, ",\"by\":%u,\"key\":\"%s\",\"value\":%ld,\"ok\":%u", (unsigned)by, settings_name(key),
             (long)value, accepted ? 1U : 0U);
    json_end(&l);
    return;
  }
  (void)settings_format_value(key, value, text, sizeof(text));
  line_init(&l);
  line_time(&l);
  if (accepted)
  {
    line_add(&l, "Réglage changé à distance par %u : %s = %s", (unsigned)by, settings_name(key), text);
  }
  else
  {
    line_add(&l, "Réglage refusé (demandé par %u) : %s = %ld, hors limites", (unsigned)by,
             settings_name(key), (long)value);
  }
  line_send(&l);
}

/* Voir report.h */
void report_banner(const mesh_t *m)
{
  line_t l;

  if (s_json)
  {
    report_info_json(m);
    return;
  }
  line_init(&l);
  line_add(&l, "\r\n%s %s (version %s) - nœud %u (%s), réseau %u", APP_NAME, APP_VERSION,
           app_is_ota() ? "OTA" : "USB",
           (unsigned)mesh_id(m), mesh_id_is_fixed(m) ? "fixé" : "automatique", (unsigned)mesh_net(m));
  line_send(&l);
  if (node_radio_status() != RADIO_OK)
  {
    report_printf("!! Radio : %s. Le réseau ne fonctionne pas.\r\n", mesh_radio_status_text(node_radio_status()));
  }
  report_print("Tapez « aide » pour la liste des commandes.\r\n");
}

/* Voir report.h */
void report_info_json(const mesh_t *m)
{
  line_t  l;
  uint8_t a[6];
  char    fw[12];
  bool    first = true;

  mesh_radio_get_address(a);
  json_begin(&l, "info");
  line_add(&l, ",\"name\":\"%s\",\"version\":\"%s\",\"ota\":%u,\"id\":%u,\"fixed\":%u,\"tag\":\"%08lX\",\"net\":%u",
           APP_NAME, APP_VERSION, app_is_ota() ? 1U : 0U,
           (unsigned)mesh_id(m), mesh_id_is_fixed(m) ? 1U : 0U,
           (unsigned long)mesh_tag(m), (unsigned)mesh_net(m));
  line_add(&l, ",\"addr\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"radio\":%u,\"mode\":\"%s\"",
           a[5], a[4], a[3], a[2], a[1], a[0], (node_radio_status() == RADIO_OK) ? 1U : 0U,
           (mesh_radio_mode() == RADIO_MODE_SIMULTANEOUS) ? "simultane" : "alterne");
  (void)mesh_radio_version_text(mesh_radio_fw_version(), fw, sizeof(fw));
  line_add(&l, ",\"fw\":\"%s\",\"stack_mode4\":%u", fw, (mesh_radio_stack() == RADIO_STACK_MODE4) ? 1U : 0U);
  for (int k = SETTING_TTL; k < SETTING_END; k++)
  {
    line_add(&l, ",\"%s\":%ld", settings_name((setting_t)k), (long)node_setting_get((setting_t)k));
  }
  line_add(&l, ",\"groups\":[");
  for (uint8_t g = 1U; g <= MESH_GROUP_COUNT; g++)
  {
    if (mesh_group_has(m, g))
    {
      line_add(&l, "%s%u", first ? "" : ",", (unsigned)g);
      first = false;
    }
  }
  line_add(&l, "]");
  json_end(&l);
}
