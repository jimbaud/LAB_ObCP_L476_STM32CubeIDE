/**
 ******************************************************************************
 * @file    Tests/sim_net.c
 * @brief   Simulation sur PC d'un réseau de cartes ObCP (voir sim_net.h).
 ******************************************************************************
 */
#include "sim_net.h"

#include <string.h>

/* ---- Générateur aléatoire (xorshift32) : rapide, reproductible ----------- */
uint32_t sim_rand(sim_net_t *s)
{
  uint32_t x = s->rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  s->rng = x;
  return x;
}

/* ---- Fonctions données à mesh.c (mesh_ops_t) ------------------------------ */

static bool sim_tx_ready(void *ctx)
{
  const sim_node_t *node = ctx;
  return !node->tx_active;
}

static bool sim_tx_start(void *ctx, const uint8_t *pdu, uint8_t len)
{
  sim_node_t *node = ctx;
  sim_net_t  *s    = node->net;

  if (node->radio_refuses || node->tx_active)
  {
    return false;
  }
  memcpy(node->pdu, pdu, len);
  node->len       = len;
  node->tx_active = true;
  node->tx_end    = s->now + RADIO_TX_TIME_MS;
  node->next_adv  = s->now + (sim_rand(s) % 3U);   /* premier évènement presque aussitôt */
  return true;
}

static void sim_deliver(void *ctx, const mesh_frame_t *f, int8_t rssi)
{
  sim_node_t *node = ctx;

  node->delivered++;
  node->last      = *f;
  node->last_rssi = rssi;
  node->last_time = node->net->now;
  if (node->first_time == 0U)
  {
    node->first_time = node->net->now;
  }
  if (node->on_deliver != NULL)
  {
    node->on_deliver(node, f, rssi);
  }
}

/* Verbes du journal : les mêmes que ceux du journal de la carte (report.c). */
static const char *const s_ev_names[] =
{
  "-> émise", "-> relayée", "<- reçue", "<- doublon", "<- écho", "xx filtrée",
  "-- relais annulé", "-- fin du TTL", "!! perdue (file pleine)", "!! refusée par la radio",
  "!! numéro en double", "!! numéro changé"
};

/* Une ligne du journal, au format de celui de la carte :
 *   « 1235 ms  nœud   3  <- reçue TEXTE 1>tous n°34 ttl 3 de 2 (1 relais, -59 dBm) » */
static void sim_trace(const sim_node_t *node, mesh_event_t ev, const mesh_frame_t *f, int8_t rssi)
{
  FILE       *out  = node->net->trace;
  const char *verb = ((unsigned)ev < (sizeof(s_ev_names) / sizeof(s_ev_names[0]))) ? s_ev_names[ev] : "?";
  char        dst[8];

  fprintf(out, "%6u ms  nœud %3u  %s", (unsigned)node->net->now, (unsigned)mesh_id(&node->mesh), verb);
  if (f == NULL)
  {
    fprintf(out, " : %u -> %u\n", (unsigned)mesh_previous_id(&node->mesh), (unsigned)mesh_id(&node->mesh));
    return;
  }
  (void)mesh_addr_format(f->dst, dst, sizeof(dst));
  fprintf(out, " %s %u>%s n°%u ttl %u", mesh_type_name(f->type), (unsigned)f->src, dst, (unsigned)f->seq,
          (unsigned)f->ttl);
  switch (ev)
  {
    case MESH_EV_RX:
    case MESH_EV_DUP:
      if (f->hops == 0U)
      {
        fprintf(out, " de %u (direct, %d dBm)", (unsigned)f->last, (int)rssi);
      }
      else
      {
        fprintf(out, " de %u (%u relais, %d dBm)", (unsigned)f->last, (unsigned)f->hops, (int)rssi);
      }
      break;
    case MESH_EV_ECHO:
      fprintf(out, " relayée par %u (%d dBm)", (unsigned)f->last, (int)rssi);
      break;
    case MESH_EV_RELAY:
      fprintf(out, " (%u relais avant)", (unsigned)(f->hops - 1U));
      break;
    default:
      break;
  }
  fprintf(out, "\n");
}

static void sim_event(void *ctx, mesh_event_t ev, const mesh_frame_t *f, int8_t rssi)
{
  sim_node_t *node = ctx;

  if ((unsigned)ev < SIM_EV_COUNT)
  {
    node->ev_count[ev]++;
  }
  if (node->net->trace != NULL)
  {
    sim_trace(node, ev, f, rssi);
  }
}

static uint32_t sim_random(void *ctx)
{
  sim_node_t *node = ctx;
  return sim_rand(node->net);
}

/* ---- Construction ---------------------------------------------------------- */

void sim_init(sim_net_t *s, int n, uint32_t seed)
{
  memset(s, 0, sizeof(*s));
  s->n   = n;
  s->rng = (seed != 0U) ? seed : 0x12345678U;
  s->now = 1000U;                                     /* éviter l'instant 0 */
  for (int a = 0; a < SIM_MAX_NODES; a++)
  {
    for (int b = 0; b < SIM_MAX_NODES; b++)
    {
      s->rssi[a][b] = SIM_NO_LINK;
    }
  }
  for (int i = 0; i < n; i++)
  {
    sim_node_t *node = &s->nodes[i];
    node->net          = s;
    node->index        = i;
    node->ops.tx_ready = sim_tx_ready;
    node->ops.tx_start = sim_tx_start;
    node->ops.deliver  = sim_deliver;
    node->ops.event    = sim_event;
    node->ops.random   = sim_random;
    node->ops.ctx      = node;
    mesh_init(&node->mesh, &node->ops, 0x1000U + (uint32_t)i, s->now);
    (void)mesh_set_id(&node->mesh, (uint8_t)(i + 1), true);
    mesh_set_hello_period(&node->mesh, 0U);
  }
}

void sim_link(sim_net_t *s, int a, int b, int8_t rssi)
{
  s->rssi[a][b] = rssi;
  s->rssi[b][a] = rssi;
}

void sim_topology_line(sim_net_t *s, int8_t rssi)
{
  for (int i = 0; (i + 1) < s->n; i++)
  {
    sim_link(s, i, i + 1, rssi);
  }
}

void sim_topology_full(sim_net_t *s, int8_t rssi)
{
  for (int a = 0; a < s->n; a++)
  {
    for (int b = a + 1; b < s->n; b++)
    {
      sim_link(s, a, b, rssi);
    }
  }
}

void sim_topology_grid(sim_net_t *s, int width, int8_t rssi)
{
  for (int i = 0; i < s->n; i++)
  {
    if (((i % width) + 1) < width && (i + 1) < s->n)
    {
      sim_link(s, i, i + 1, rssi);         /* voisin de droite */
    }
    if ((i + width) < s->n)
    {
      sim_link(s, i, i + width, rssi);     /* voisin du dessous */
    }
  }
}

/* ---- Déroulement ----------------------------------------------------------- */

void sim_run(sim_net_t *s, uint32_t ms)
{
  bool adv_now[SIM_MAX_NODES];

  for (uint32_t step = 0U; step < ms; step++)
  {
    s->now++;

    /* 1. chaque nœud fait tourner sa tâche de fond */
    for (int i = 0; i < s->n; i++)
    {
      mesh_process(&s->nodes[i].mesh, s->now);
    }

    /* 2. évènements de publicité de cette milliseconde */
    for (int i = 0; i < s->n; i++)
    {
      sim_node_t *node = &s->nodes[i];
      adv_now[i] = false;
      if (node->tx_active && ((int32_t)(s->now - node->next_adv) >= 0)
          && ((int32_t)(s->now - node->tx_end) < 0))
      {
        adv_now[i] = true;
        node->adv_events++;
        s->adv_events++;
        node->next_adv = s->now + 100U + (sim_rand(s) % 11U);
      }
    }

    /* 3. réceptions */
    for (int j = 0; j < s->n; j++)
    {
      int heard = 0;

      if (adv_now[j])
      {
        continue;                            /* il émet : il n'écoute pas */
      }
      for (int i = 0; i < s->n; i++)
      {
        if (adv_now[i] && (s->rssi[i][j] != SIM_NO_LINK))
        {
          heard++;
        }
      }
      if (heard == 0)
      {
        continue;
      }
      if (s->collisions && (heard >= 2))
      {
        s->collided += (uint32_t)heard;      /* trames brouillées */
        continue;
      }
      for (int i = 0; i < s->n; i++)
      {
        if (adv_now[i] && (s->rssi[i][j] != SIM_NO_LINK)
            && ((sim_rand(s) % 1000U) >= s->loss_permille))
        {
          int rssi = s->rssi[i][j] + (int)(sim_rand(s) % 7U) - 3;
          s->receptions++;
          mesh_on_pdu(&s->nodes[j].mesh, s->nodes[i].pdu, s->nodes[i].len, (int8_t)rssi, s->now);
        }
      }
    }

    /* 4. fin des émissions */
    for (int i = 0; i < s->n; i++)
    {
      sim_node_t *node = &s->nodes[i];
      if (node->tx_active && ((int32_t)(s->now - node->tx_end) >= 0))
      {
        node->tx_active = false;
      }
    }
  }
}

void sim_clear_counters(sim_net_t *s)
{
  for (int i = 0; i < s->n; i++)
  {
    sim_node_t *node = &s->nodes[i];
    node->delivered  = 0U;
    node->first_time = 0U;
    node->adv_events = 0U;
    memset(node->ev_count, 0, sizeof(node->ev_count));
    mesh_stats_clear(&node->mesh);
  }
  s->adv_events = 0U;
  s->receptions = 0U;
  s->collided   = 0U;
}

uint32_t sim_total_events(const sim_net_t *s, mesh_event_t ev)
{
  uint32_t total = 0U;
  for (int i = 0; i < s->n; i++)
  {
    total += s->nodes[i].ev_count[ev];
  }
  return total;
}

int sim_count_reached(const sim_net_t *s, int except)
{
  int count = 0;
  for (int i = 0; i < s->n; i++)
  {
    if ((i != except) && (s->nodes[i].delivered > 0U))
    {
      count++;
    }
  }
  return count;
}
