/**
 ******************************************************************************
 * @file    node.c
 * @brief   Le nœud de démonstration (voir node.h) : identité de la carte,
 *          lien entre le réseau maillé et la radio, réactions aux messages,
 *          pings et traces, réglages.
 *
 *  Organisation du fichier :
 *    1. état et outils (hasard, identité de la carte) ;
 *    2. fonctions données au réseau maillé (mesh_ops_t) ;
 *    3. réponses différées et réglages à distance ;
 *    4. réactions aux messages reçus ;
 *    5. démarrage et tâche de fond ;
 *    6. actions (commandes du terminal) ;
 *    7. réglages et enregistrement.
 ******************************************************************************
 */
#include "node.h"

#include <string.h>

#include "accelerometer.h"
#include "app_config.h"
#include "main.h"                 /* HAL_GetTick, identifiant unique du STM32 */
#include "report.h"

/* ========================================================================== */
/* 1. État et outils                                                          */
/* ========================================================================== */

static mesh_t         s_mesh;               /* le réseau maillé de la carte */
static mesh_ops_t     s_ops;
static radio_status_t s_radio_status = RADIO_ERR_NO_MODULE;
static store_status_t s_load_status  = STORE_EMPTY;  /* lecture de l'EEPROM au démarrage */
static store_status_t s_store_status = STORE_EMPTY;  /* contenu actuel : lu, puis sauve / oublie */
static bool           s_store_saved;                 /* « sauve » réussi depuis le démarrage */
static uint32_t       s_rng = 1U;           /* état du générateur aléatoire */
static uint8_t        s_auto_s = NODE_MEASURE_PERIOD_S;
static uint8_t        s_auto_count;
static uint8_t        s_last_ping_id;
static uint8_t        s_last_trace_id;

/* Ping ou trace en attente de réponse. */
typedef struct
{
  bool     used;
  bool     trace;          /* trace (true) ou ping (false) */
  uint8_t  id;             /* numéro du ping ou de la trace */
  uint8_t  dst;
  uint8_t  replies;        /* réponses reçues */
  uint32_t sent_ms;
} wait_slot_t;
static wait_slot_t s_waits[NODE_PING_SLOTS];

/* Réponse à envoyer plus tard (demande adressée à un groupe ou à tous). */
typedef struct
{
  bool     used;
  uint32_t due_ms;
  uint32_t deadline_ms;    /* abandon si la file reste pleine jusque-là */
  uint8_t  dst;
  uint8_t  type;
  uint8_t  len;
  uint8_t  data[MESH_PAYLOAD_MAX];
} deferred_t;
static deferred_t s_deferred[NODE_DEFERRED_SLOTS];

/* Réglage reçu à distance, appliqué un peu plus tard (voir apply_later). */
typedef struct
{
  bool      used;
  uint32_t  due_ms;
  uint8_t   by;
  setting_t key;
  int32_t   value;
} remote_setting_t;
static remote_setting_t s_remote[2];

/* Délai avant d'appliquer un réglage reçu à distance : le temps que la
 * trame REGLAGE soit relayée par cette carte (attente aléatoire puis
 * émission). Sinon « regle tous relais off » couperait le relais de la
 * trame même qui le demande, et les cartes plus loin ne la recevraient
 * jamais. */
#define REMOTE_SETTING_DELAY_MS  (MESH_RELAY_DELAY_MAX_MS + RADIO_TX_TIME_MS + 100U)

/* Couleurs des éclairs d'activité (faibles, pour ne pas masquer le fond). */
static const led_color_t k_flash_relay = { 0U, 70U, 70U };     /* cyan : relais */
static const led_color_t k_flash_tx    = { 70U, 0U, 70U };     /* magenta : émission */
static const led_color_t k_flash_rx    = { 0U, 0U, 140U };     /* bleu : message reçu */
static const led_color_t k_alert       = { 255U, 0U, 0U };     /* rouge : conflit */

static bool time_reached(uint32_t now, uint32_t t)
{
  return (int32_t)(now - t) >= 0;
}

/* Générateur aléatoire « xorshift32 » : rapide, assez bon pour tirer des
 * délais. Il part d'un vrai nombre aléatoire fourni par le module
 * Bluetooth, mélangé à l'étiquette de la carte. */
static uint32_t rng_next(void)
{
  uint32_t x = s_rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  s_rng = x;
  return x;
}

/* Étiquette de la carte : hachage FNV-1a des 96 bits de l'identifiant
 * unique du STM32 (gravé en usine, différent d'une puce à l'autre). */
static uint32_t board_tag(void)
{
  const uint32_t uid[3] = { HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2() };
  uint32_t       h      = 2166136261U;

  for (int i = 0; i < 12; i++)
  {
    h = (h ^ (uint8_t)(uid[i / 4] >> (8 * (i % 4)))) * 16777619U;
  }
  return h;
}

/* ========================================================================== */
/* 2. Fonctions données au réseau maillé                                      */
/* ========================================================================== */

static void on_message(const mesh_frame_t *f, int8_t rssi);

/* Le réseau maillé demande si la radio peut émettre. */
static bool op_tx_ready(void *ctx)
{
  (void)ctx;
  return mesh_radio_tx_ready();
}

/* Le réseau maillé confie une trame à la radio. */
static bool op_tx_start(void *ctx, const uint8_t *pdu, uint8_t len)
{
  (void)ctx;
  return mesh_radio_send(pdu, len);
}

/* Le réseau maillé remet une trame destinée à cette carte. */
static void op_deliver(void *ctx, const mesh_frame_t *f, int8_t rssi)
{
  (void)ctx;
  on_message(f, rssi);
}

/* Le réseau maillé signale un évènement (journal, éclairs de la LED). */
static void op_event(void *ctx, mesh_event_t ev, const mesh_frame_t *f, int8_t rssi)
{
  (void)ctx;
  report_mesh_event(&s_mesh, ev, f, rssi);
  switch (ev)
  {
    case MESH_EV_RELAY:
      led_fx_flash(k_flash_relay, LED_FLASH_MS);
      break;
    case MESH_EV_TX:
      if ((f != NULL) && (f->type != MESH_T_HELLO))
      {
        led_fx_flash(k_flash_tx, LED_FLASH_MS);
      }
      break;
    case MESH_EV_ID_CONFLICT:
    case MESH_EV_ID_CHANGED:
      led_fx_blink(k_alert, 2000U, 200U);
      break;
    default:
      break;
  }
}

/* Le réseau maillé demande un nombre aléatoire (attente avant relais). */
static uint32_t op_random(void *ctx)
{
  (void)ctx;
  return rng_next();
}

/* ========================================================================== */
/* 3. Réponses différées et réglages à distance                               */
/* ========================================================================== */

/* Envoie une réponse à la trame f : tout de suite si f nous était adressée
 * seule, sinon après un délai aléatoire (on n'est pas le seul à répondre).
 * En mode alterné, jamais avant NODE_REPLY_MIN_ALT_MS (voir app_config.h).
 * Rend le délai en ms, ou -1 si la réponse n'a pas pu être prévue. */
static int32_t reply(const mesh_frame_t *f, uint8_t type, const uint8_t *data, uint8_t len)
{
  uint32_t delay = (mesh_radio_mode() == RADIO_MODE_ALTERNATE) ? NODE_REPLY_MIN_ALT_MS : 0U;

  if (f->dst != mesh_id(&s_mesh))
  {
    delay += rng_next() % (NODE_REPLY_SPREAD_MS + 1U);
  }
  if ((delay == 0U) && (mesh_send(&s_mesh, f->src, type, data, len, MESH_TTL_USE_DEFAULT, NULL) == MESH_OK))
  {
    return 0;
  }
  for (uint32_t i = 0U; i < NODE_DEFERRED_SLOTS; i++)
  {
    deferred_t *d = &s_deferred[i];
    if (!d->used)
    {
      d->used        = true;
      d->due_ms      = HAL_GetTick() + delay;
      d->deadline_ms = d->due_ms + NODE_REPLY_TIMEOUT_MS;
      d->dst         = f->src;
      d->type        = type;
      d->len         = len;
      memcpy(d->data, data, len);
      return (int32_t)delay;
    }
  }
  return -1;                              /* trop de réponses en attente */
}

/* Envoie les réponses différées arrivées à échéance (nouvel essai 100 ms
 * plus tard si la file d'émission est pleine). */
static void process_deferred(uint32_t now)
{
  for (uint32_t i = 0U; i < NODE_DEFERRED_SLOTS; i++)
  {
    deferred_t *d = &s_deferred[i];
    if (!d->used || !time_reached(now, d->due_ms))
    {
      continue;
    }
    if (mesh_send(&s_mesh, d->dst, d->type, d->data, d->len, MESH_TTL_USE_DEFAULT, NULL) == MESH_OK)
    {
      d->used = false;
    }
    else if (time_reached(now, d->deadline_ms))
    {
      d->used = false;                    /* abandon */
    }
    else
    {
      d->due_ms = now + 100U;
    }
  }
}

/* Note un réglage reçu à distance, appliqué après REMOTE_SETTING_DELAY_MS. */
static void apply_later(uint8_t by, setting_t key, int32_t value)
{
  for (uint32_t i = 0U; i < (sizeof(s_remote) / sizeof(s_remote[0])); i++)
  {
    if (!s_remote[i].used)
    {
      s_remote[i].used   = true;
      s_remote[i].due_ms = HAL_GetTick() + REMOTE_SETTING_DELAY_MS;
      s_remote[i].by     = by;
      s_remote[i].key    = key;
      s_remote[i].value  = value;
      return;
    }
  }
  report_setting_changed(by, key, value, false);   /* trop de réglages à la fois */
}

/* Applique les réglages reçus à distance dont le délai est écoulé. */
static void process_remote_settings(uint32_t now)
{
  for (uint32_t i = 0U; i < (sizeof(s_remote) / sizeof(s_remote[0])); i++)
  {
    remote_setting_t *r = &s_remote[i];
    if (r->used && time_reached(now, r->due_ms))
    {
      int32_t applied = r->value;
      bool    ok      = node_setting_set(r->key, r->value, &applied);
      r->used = false;
      report_setting_changed(r->by, r->key, applied, ok);
    }
  }
}

/* ========================================================================== */
/* 4. Réactions aux messages reçus                                            */
/* ========================================================================== */

/* Données d'une trame MESURE. */
static uint8_t build_measure(uint8_t *p)
{
  node_measure_t m;

  node_measure(&m);
  p[0] = (uint8_t)((m.accel_ok ? MESH_MEASURE_ACCEL_OK : 0U) | (m.temp_ok ? MESH_MEASURE_TEMP_OK : 0U));
  p[1] = (uint8_t)m.temp_c;
  mesh_put_u16(&p[2], (uint16_t)m.ax_mg);
  mesh_put_u16(&p[4], (uint16_t)m.ay_mg);
  mesh_put_u16(&p[6], (uint16_t)m.az_mg);
  return MESH_MEASURE_LEN;
}

/* Réponse à un ping : on cherche le ping en attente qui lui correspond. */
static void on_pong(const mesh_frame_t *f, int8_t rssi)
{
  uint32_t now = HAL_GetTick();

  if (f->len < 3U)
  {
    return;
  }
  for (uint32_t i = 0U; i < NODE_PING_SLOTS; i++)
  {
    wait_slot_t *w = &s_waits[i];
    if (w->used && !w->trace && (w->id == f->payload[0]) && ((w->dst == f->src) || !mesh_addr_is_node(w->dst)))
    {
      report_pong(f, rssi, now - w->sent_ms);
      if (w->replies < 0xFFU)
      {
        w->replies++;
      }
      if (mesh_addr_is_node(w->dst))
      {
        w->used = false;                  /* un seul destinataire : terminé */
      }
      return;
    }
  }
}

/* Retour d'une trace. */
static void on_trace_back(const mesh_frame_t *f)
{
  for (uint32_t i = 0U; i < NODE_PING_SLOTS; i++)
  {
    wait_slot_t *w = &s_waits[i];
    if (w->used && w->trace && (f->len >= 2U) && (w->id == f->payload[0]) && (w->dst == f->src))
    {
      report_trace(f, HAL_GetTick() - w->sent_ms);
      w->used = false;
      return;
    }
  }
}

/* Une trame qui nous est destinée (appelée par mesh.c). */
static void on_message(const mesh_frame_t *f, int8_t rssi)
{
  const uint8_t *p = f->payload;
  uint8_t        data[MESH_PAYLOAD_MAX];
  int32_t        reply_ms = -1;

  switch (f->type)
  {
    case MESH_T_PING:
      if (f->len >= 1U)
      {
        data[0]  = p[0];                  /* numéro du ping */
        data[1]  = f->hops;               /* relais traversés à l'aller */
        data[2]  = (uint8_t)rssi;         /* puissance reçue à l'arrivée */
        reply_ms = reply(f, MESH_T_PONG, data, 3U);
        report_message(f, rssi, reply_ms);
      }
      break;

    case MESH_T_PONG:
      on_pong(f, rssi);
      break;

    case MESH_T_TRACE:
      /* Route de l'aller : les relais ont ajouté leur numéro après p[0]. */
      if ((f->len >= 1U) && (f->dst == mesh_id(&s_mesh)))
      {
        uint8_t k = (uint8_t)(f->len - 1U);
        data[0] = p[0];
        data[1] = k;
        memcpy(&data[2], &p[1], k);
        reply_ms = reply(f, MESH_T_TRACE_BACK, data, (uint8_t)(2U + k));
        report_message(f, rssi, reply_ms);
      }
      break;

    case MESH_T_TRACE_BACK:
      on_trace_back(f);
      break;

    case MESH_T_TEXT:
      report_message(f, rssi, -1);
      led_fx_flash(k_flash_rx, 4U * LED_FLASH_MS);
      break;

    case MESH_T_LED:
      if (f->len >= 3U)
      {
        led_color_t c = { p[0], p[1], p[2] };
        uint8_t     seconds = (f->len >= 4U) ? p[3] : 0U;
        led_fx_set_color(c, (uint32_t)seconds * 1000U);
        report_message(f, rssi, -1);
      }
      break;

    case MESH_T_WAVE:
      led_fx_show(led_fx_hops_color(f->hops), LED_WAVE_MS);
      report_message(f, rssi, -1);
      break;

    case MESH_T_IDENT:
    {
      uint8_t seconds = ((f->len >= 1U) && (p[0] > 0U)) ? p[0] : (uint8_t)NODE_IDENT_DEFAULT_S;
      led_fx_blink(LED_WHITE, (uint32_t)seconds * 1000U, 250U);
      report_message(f, rssi, -1);
      break;
    }

    case MESH_T_MEASURE_REQ:
      reply_ms = reply(f, MESH_T_MEASURE, data, build_measure(data));
      report_message(f, rssi, reply_ms);
      break;

    case MESH_T_MEASURE:
      report_message(f, rssi, -1);
      break;

    case MESH_T_CONFIG:
      if (f->len >= 3U)
      {
        setting_t key   = (setting_t)p[0];
        int32_t   value = (int16_t)mesh_get_u16(&p[1]);
        if (settings_value_ok(key, value))
        {
          apply_later(f->src, key, value);
        }
        else
        {
          report_setting_changed(f->src, key, value, false);
        }
      }
      break;

    default:                              /* HELLO : rien à faire ici */
      break;
  }
}

/* ========================================================================== */
/* 5. Démarrage et tâche de fond                                              */
/* ========================================================================== */

/* Applique les réglages lus dans l'EEPROM (chacun vérifié). */
static void apply_stored(const stored_config_t *c)
{
  if (mesh_addr_is_node(c->id))
  {
    (void)mesh_set_id(&s_mesh, c->id, true);
  }
  mesh_set_net(&s_mesh, c->net);
  for (uint8_t g = 1U; g <= MESH_GROUP_COUNT; g++)
  {
    (void)mesh_group_set(&s_mesh, g, (c->groups & (1U << (g - 1U))) != 0U);
  }
  (void)node_setting_set(SETTING_TTL, c->ttl, NULL);
  (void)node_setting_set(SETTING_RELAY, c->relay ? 1 : 0, NULL);
  (void)node_setting_set(SETTING_SUPPRESS, c->suppress, NULL);
  (void)node_setting_set(SETTING_RSSI_MIN, c->rssi_min, NULL);
  (void)node_setting_set(SETTING_TX_POWER, c->tx_dbm, NULL);
  (void)node_setting_set(SETTING_HELLO, c->hello_s, NULL);
  (void)node_setting_set(SETTING_AUTO, c->auto_s, NULL);
  (void)node_setting_set(SETTING_FLASH, c->flash ? 1 : 0, NULL);
}

/* Voir node.h */
void node_init(radio_status_t radio_status)
{
  stored_config_t stored;
  uint32_t        tag = board_tag();

  s_radio_status = radio_status;
  s_rng = tag ^ mesh_radio_random_seed() ^ (HAL_GetTick() * 2654435761U) ^ 0xA5A5A5A5U;
  if (s_rng == 0U)
  {
    s_rng = 1U;                           /* xorshift ne doit jamais valoir 0 */
  }
  s_ops.tx_ready = op_tx_ready;
  s_ops.tx_start = op_tx_start;
  s_ops.deliver  = op_deliver;
  s_ops.event    = op_event;
  s_ops.random   = op_random;
  s_ops.ctx      = NULL;
  mesh_init(&s_mesh, &s_ops, tag, HAL_GetTick());
  memset(s_waits, 0, sizeof(s_waits));
  memset(s_deferred, 0, sizeof(s_deferred));
  memset(s_remote, 0, sizeof(s_remote));
  s_last_ping_id  = (uint8_t)rng_next();
  s_last_trace_id = (uint8_t)rng_next();

  s_load_status  = config_store_load(&stored);
  s_store_status = s_load_status;
  s_store_saved  = false;
  if (s_load_status == STORE_OK)
  {
    apply_stored(&stored);
  }
}

/* Délais d'attente des pings et des traces. */
static void process_waits(uint32_t now)
{
  for (uint32_t i = 0U; i < NODE_PING_SLOTS; i++)
  {
    wait_slot_t *w = &s_waits[i];
    uint32_t     timeout = NODE_REPLY_TIMEOUT_MS + (mesh_addr_is_node(w->dst) ? 0U : NODE_REPLY_SPREAD_MS);

    if (!w->used || ((now - w->sent_ms) < timeout))
    {
      continue;
    }
    w->used = false;
    if (w->trace)
    {
      report_trace_timeout(w->dst, w->id);
    }
    else
    {
      report_ping_end(w->dst, w->id, w->replies);
    }
  }
}

/* Voir node.h */
void node_process(void)
{
  radio_frame_t rx;
  uint32_t      now = HAL_GetTick();
  uint8_t       code;

  /* Trames reçues par la radio, une par une, au réseau maillé. */
  while (mesh_radio_receive(&rx))
  {
    s_rng ^= (uint32_t)(uint8_t)rx.rssi;  /* un peu de hasard en plus */
    mesh_on_pdu(&s_mesh, rx.pdu, rx.len, rx.rssi, now);
  }
  mesh_process(&s_mesh, now);
  if (mesh_radio_take_fallback(&code))
  {
    report_radio_fallback(code);          /* le module a refusé d'émettre pendant l'écoute */
  }
  process_deferred(now);
  process_remote_settings(now);
  process_waits(now);
}

/* Voir node.h */
void node_on_second(void)
{
  uint8_t data[MESH_PAYLOAD_MAX];

  if (s_auto_s == 0U)
  {
    return;
  }
  s_auto_count++;
  if (s_auto_count >= s_auto_s)
  {
    s_auto_count = 0U;
    (void)mesh_send(&s_mesh, MESH_ADDR_ALL, MESH_T_MEASURE, data, build_measure(data),
                    MESH_TTL_USE_DEFAULT, NULL);
  }
}

/* Voir node.h */
void node_on_button(void)
{
  if (node_send_wave() == MESH_OK)
  {
    report_printf("Vague lancée (bouton SW1)\r\n");
  }
}

/* Voir node.h */
mesh_t *node_mesh(void)
{
  return &s_mesh;
}

/* Voir node.h */
radio_status_t node_radio_status(void)
{
  return s_radio_status;
}

/* Voir node.h */
store_status_t node_load_status(void)
{
  return s_load_status;
}

/* Voir node.h */
const char *node_store_text(void)
{
  if (s_store_status == STORE_OK)
  {
    return s_store_saved ? "réglages enregistrés par « sauve »" : "réglages enregistrés, lus au démarrage";
  }
  return config_store_status_text(s_store_status);
}

/* ========================================================================== */
/* 6. Actions                                                                 */
/* ========================================================================== */

/* Voir node.h. Le texte est coupé à 14 octets, sans couper un caractère
 * accentué en deux (en UTF-8, un « é » occupe 2 octets : 0xC3 0xA9 ; les
 * octets de suite s'écrivent 10xxxxxx en binaire). */
mesh_result_t node_send_text(uint8_t dst, const char *text)
{
  size_t len = strlen(text);

  if (len > MESH_PAYLOAD_MAX)
  {
    len = MESH_PAYLOAD_MAX;
    while ((len > 0U) && (((uint8_t)text[len] & 0xC0U) == 0x80U))
    {
      len--;                              /* recule au début du caractère */
    }
  }
  return mesh_send(&s_mesh, dst, MESH_T_TEXT, (const uint8_t *)text, (uint8_t)len,
                   MESH_TTL_USE_DEFAULT, NULL);
}

/* Voir node.h */
mesh_result_t node_send_led(uint8_t dst, led_color_t c, uint8_t seconds)
{
  const uint8_t data[4] = { c.r, c.g, c.b, seconds };
  return mesh_send(&s_mesh, dst, MESH_T_LED, data, 4U, MESH_TTL_USE_DEFAULT, NULL);
}

/* Voir node.h */
mesh_result_t node_send_wave(void)
{
  mesh_result_t r = mesh_send(&s_mesh, MESH_ADDR_ALL, MESH_T_WAVE, NULL, 0U, MESH_TTL_USE_DEFAULT, NULL);

  if (r == MESH_OK)
  {
    led_fx_show(LED_WHITE, LED_WAVE_MS);  /* l'origine de la vague s'allume en blanc */
  }
  return r;
}

/* Voir node.h */
mesh_result_t node_send_ident(uint8_t dst, uint8_t seconds)
{
  return mesh_send(&s_mesh, dst, MESH_T_IDENT, &seconds, 1U, MESH_TTL_USE_DEFAULT, NULL);
}

/* Prend une case d'attente (ping ou trace) : une libre, sinon la plus
 * ancienne (son attente est abandonnée). */
static wait_slot_t *take_wait_slot(void)
{
  wait_slot_t *oldest = &s_waits[0];
  uint32_t     now    = HAL_GetTick();

  for (uint32_t i = 0U; i < NODE_PING_SLOTS; i++)
  {
    if (!s_waits[i].used)
    {
      return &s_waits[i];
    }
    if ((now - s_waits[i].sent_ms) > (now - oldest->sent_ms))
    {
      oldest = &s_waits[i];
    }
  }
  return oldest;
}

/* Voir node.h */
mesh_result_t node_ping(uint8_t dst, uint8_t *ping_id)
{
  uint8_t       id = (uint8_t)(s_last_ping_id + 1U);
  mesh_result_t r  = mesh_send(&s_mesh, dst, MESH_T_PING, &id, 1U, MESH_TTL_USE_DEFAULT, NULL);

  if (r == MESH_OK)
  {
    wait_slot_t *w = take_wait_slot();
    s_last_ping_id = id;
    w->used    = true;
    w->trace   = false;
    w->id      = id;
    w->dst     = dst;
    w->replies = 0U;
    w->sent_ms = HAL_GetTick();
    *ping_id   = id;
  }
  return r;
}

/* Voir node.h */
mesh_result_t node_trace(uint8_t dst, uint8_t *trace_id)
{
  uint8_t       id = (uint8_t)(s_last_trace_id + 1U);
  mesh_result_t r;

  if (!mesh_addr_is_node(dst))
  {
    return MESH_ERR_DEST;                 /* une trace vise un seul nœud */
  }
  r = mesh_send(&s_mesh, dst, MESH_T_TRACE, &id, 1U, MESH_TTL_USE_DEFAULT, NULL);
  if (r == MESH_OK)
  {
    wait_slot_t *w = take_wait_slot();
    s_last_trace_id = id;
    w->used    = true;
    w->trace   = true;
    w->id      = id;
    w->dst     = dst;
    w->replies = 0U;
    w->sent_ms = HAL_GetTick();
    *trace_id  = id;
  }
  return r;
}

/* Voir node.h */
mesh_result_t node_request_measure(uint8_t dst)
{
  return mesh_send(&s_mesh, dst, MESH_T_MEASURE_REQ, NULL, 0U, MESH_TTL_USE_DEFAULT, NULL);
}

/* Voir node.h */
mesh_result_t node_send_setting(uint8_t dst, setting_t key, int32_t value)
{
  uint8_t data[3];

  data[0] = (uint8_t)key;
  mesh_put_u16(&data[1], (uint16_t)(int16_t)value);
  return mesh_send(&s_mesh, dst, MESH_T_CONFIG, data, 3U, MESH_TTL_USE_DEFAULT, NULL);
}

/* Voir node.h. Valeurs bornées pour tenir sur 16 bits signés (mg). */
void node_measure(node_measure_t *m)
{
  accel_g_t g;
  int       celsius;

  memset(m, 0, sizeof(*m));
  if (accel_read(&g))
  {
    float v[3] = { g.x * 1000.0f, g.y * 1000.0f, g.z * 1000.0f };
    int16_t *out[3] = { &m->ax_mg, &m->ay_mg, &m->az_mg };
    for (int i = 0; i < 3; i++)
    {
      float c = (v[i] > 32000.0f) ? 32000.0f : ((v[i] < -32000.0f) ? -32000.0f : v[i]);
      *out[i] = (int16_t)((c >= 0.0f) ? (c + 0.5f) : (c - 0.5f));
    }
    m->accel_ok = true;
  }
  if (accel_read_temperature(&celsius))
  {
    m->temp_c  = (int8_t)((celsius > 127) ? 127 : ((celsius < -128) ? -128 : celsius));
    m->temp_ok = true;
  }
}

/* ========================================================================== */
/* 7. Réglages et enregistrement                                              */
/* ========================================================================== */

/* Voir node.h */
bool node_setting_set(setting_t key, int32_t value, int32_t *applied)
{
  int32_t out = value;

  if (!settings_value_ok(key, value))
  {
    return false;
  }
  switch (key)
  {
    case SETTING_TTL:
      mesh_set_ttl(&s_mesh, (uint8_t)value);
      break;
    case SETTING_RELAY:
      mesh_set_relay(&s_mesh, value != 0);
      break;
    case SETTING_SUPPRESS:
      mesh_set_suppress(&s_mesh, (uint8_t)value);
      break;
    case SETTING_RSSI_MIN:
      mesh_set_rssi_min(&s_mesh, (int8_t)value);
      break;
    case SETTING_TX_POWER:
    {
      int8_t dbm = 0;
      if (!mesh_radio_set_tx_power((int8_t)value, &dbm))
      {
        return false;
      }
      out = dbm;
      break;
    }
    case SETTING_HELLO:
      mesh_set_hello_period(&s_mesh, (uint8_t)value);
      break;
    case SETTING_AUTO:
      s_auto_s     = (uint8_t)value;
      s_auto_count = 0U;
      break;
    case SETTING_FLASH:
      led_fx_set_activity(value != 0);
      break;
    default:
      return false;
  }
  if (applied != NULL)
  {
    *applied = out;
  }
  return true;
}

/* Voir node.h */
int32_t node_setting_get(setting_t key)
{
  const mesh_settings_t *set = mesh_settings(&s_mesh);

  switch (key)
  {
    case SETTING_TTL:      return set->ttl;
    case SETTING_RELAY:    return set->relay ? 1 : 0;
    case SETTING_SUPPRESS: return set->suppress;
    case SETTING_RSSI_MIN: return set->rssi_min;
    case SETTING_TX_POWER: return mesh_radio_tx_power();
    case SETTING_HELLO:    return set->hello_period_s;
    case SETTING_AUTO:     return s_auto_s;
    case SETTING_FLASH:    return led_fx_activity() ? 1 : 0;
    default:               return 0;
  }
}

/* Voir node.h */
bool node_set_id(uint8_t id)
{
  return mesh_set_id(&s_mesh, id, id != 0U);
}

/* Voir node.h */
void node_set_net(uint8_t net)
{
  mesh_set_net(&s_mesh, net);
  memset(s_waits, 0, sizeof(s_waits));
  memset(s_deferred, 0, sizeof(s_deferred));
}

/* Voir node.h */
store_status_t node_save(void)
{
  stored_config_t c;

  memset(&c, 0, sizeof(c));
  c.id       = mesh_id_is_fixed(&s_mesh) ? mesh_id(&s_mesh) : 0U;
  c.net      = mesh_net(&s_mesh);
  for (uint8_t g = 1U; g <= MESH_GROUP_COUNT; g++)
  {
    if (mesh_group_has(&s_mesh, g))
    {
      c.groups = (uint16_t)(c.groups | (1U << (g - 1U)));
    }
  }
  c.ttl      = (uint8_t)node_setting_get(SETTING_TTL);
  c.relay    = node_setting_get(SETTING_RELAY) != 0;
  c.suppress = (uint8_t)node_setting_get(SETTING_SUPPRESS);
  c.rssi_min = (int8_t)node_setting_get(SETTING_RSSI_MIN);
  c.tx_dbm   = (int8_t)node_setting_get(SETTING_TX_POWER);
  c.hello_s  = (uint8_t)node_setting_get(SETTING_HELLO);
  c.auto_s   = (uint8_t)node_setting_get(SETTING_AUTO);
  c.flash    = node_setting_get(SETTING_FLASH) != 0;
  s_store_status = config_store_save(&c);
  s_store_saved  = (s_store_status == STORE_OK);
  return s_store_status;
}

/* Voir node.h */
store_status_t node_forget(void)
{
  store_status_t s = config_store_erase();

  s_store_status = (s == STORE_OK) ? STORE_EMPTY : s;
  s_store_saved  = false;
  return s;
}
