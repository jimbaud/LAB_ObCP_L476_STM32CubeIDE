/**
 ******************************************************************************
 * @file    mesh.c
 * @brief   Réseau maillé par inondation contrôlée (voir mesh.h pour le
 *          principe et le schéma de la réception).
 *
 *  Organisation du fichier :
 *    1. outils (temps, hasard, évènements) ;
 *    2. mémoire des trames déjà vues (cache) ;
 *    3. tables des voisins et des nœuds connus ;
 *    4. numéro du nœud et conflits ;
 *    5. file d'émission et relais ;
 *    6. émission, réception, tâche de fond ;
 *    7. réglages et accesseurs.
 *
 *  Le temps est un compteur de millisecondes sur 32 bits qui repasse par 0
 *  au bout de 49 jours. Les comparaisons se font toujours par différence
 *  (maintenant - instant), qui reste juste au passage par 0.
 ******************************************************************************
 */
#include "mesh.h"

#include <string.h>

/* ========================================================================== */
/* 1. Outils                                                                  */
/* ========================================================================== */

/* Vrai si l'instant t est atteint ou dépassé. */
static bool time_reached(uint32_t now, uint32_t t)
{
  return (int32_t)(now - t) >= 0;
}

/* Nombre aléatoire fourni par l'utilisateur du module. */
static uint32_t rnd(const mesh_t *m)
{
  return m->ops->random(m->ops->ctx);
}

/* Nombre aléatoire entre lo et hi (compris). */
static uint32_t rnd_between(const mesh_t *m, uint32_t lo, uint32_t hi)
{
  return (hi > lo) ? (lo + (rnd(m) % (hi - lo + 1U))) : lo;
}

/* Signale un évènement à l'application (si elle veut les recevoir). */
static void notify(const mesh_t *m, mesh_event_t ev, const mesh_frame_t *f, int8_t rssi)
{
  if (m->ops->event != NULL)
  {
    m->ops->event(m->ops->ctx, ev, f, rssi);
  }
}

/* ========================================================================== */
/* 2. Mémoire des trames déjà vues                                            */
/* ========================================================================== */

/* Vrai si l'entrée est utilisée et encore valable. */
static bool cache_valid(const mesh_t *m, const mesh_cache_entry_t *e)
{
  return e->used && ((m->now - e->t_ms) < MESH_CACHE_LIFETIME_MS);
}

/* Trame (src, seq) déjà vue : rend son entrée, NULL si elle est nouvelle. */
static mesh_cache_entry_t *cache_find(mesh_t *m, uint8_t src, uint8_t seq)
{
  for (uint32_t i = 0U; i < MESH_CACHE_SIZE; i++)
  {
    mesh_cache_entry_t *e = &m->cache[i];
    if (cache_valid(m, e) && (e->src == src) && (e->seq == seq))
    {
      return e;
    }
  }
  return NULL;
}

/* Vrai si l'émetteur from a déjà été entendu pour cette trame. */
static bool cache_from_has(const mesh_cache_entry_t *e, uint8_t from)
{
  for (uint32_t i = 0U; i < e->from_count; i++)
  {
    if (e->from[i] == from)
    {
      return true;
    }
  }
  return false;
}

/* Retient l'émetteur from pour cette trame (au-delà de 4, il n'est plus
 * retenu : ses répétitions compteront alors comme des doublons, sans
 * gravité). */
static void cache_from_add(mesh_cache_entry_t *e, uint8_t from)
{
  if (e->from_count < MESH_CACHE_FROM_MAX)
  {
    e->from[e->from_count] = from;
    e->from_count++;
  }
}

/* Note la trame (src, seq), reçue de l'émetteur from. Prend une case libre
 * ou périmée, sinon la plus ancienne (la mémoire est pleine : on oublie la
 * trame vue il y a le plus longtemps). */
static void cache_add(mesh_t *m, uint8_t src, uint8_t seq, uint8_t from)
{
  mesh_cache_entry_t *victim = &m->cache[0];

  for (uint32_t i = 0U; i < MESH_CACHE_SIZE; i++)
  {
    mesh_cache_entry_t *e = &m->cache[i];
    if (!cache_valid(m, e))
    {
      victim = e;
      break;
    }
    if ((m->now - e->t_ms) > (m->now - victim->t_ms))
    {
      victim = e;
    }
  }
  memset(victim, 0, sizeof(*victim));
  victim->used = true;
  victim->src  = src;
  victim->seq  = seq;
  victim->t_ms = m->now;
  cache_from_add(victim, from);
}

/* ========================================================================== */
/* 3. Voisins et nœuds connus                                                 */
/* ========================================================================== */

/* Moyenne glissante de la puissance reçue : 3/4 de l'ancienne valeur, 1/4
 * de la nouvelle, arrondie au plus proche (le RSSI varie beaucoup d'une
 * trame à l'autre : on lisse). */
static int8_t rssi_average(int8_t old_dbm, int8_t new_dbm)
{
  int v = (3 * (int)old_dbm) + (int)new_dbm;
  v = (v >= 0) ? ((v + 2) / 4) : -((-v + 2) / 4);
  return (int8_t)v;
}

/* Âge d'une case de table : temps écoulé depuis sa dernière mise à jour,
 * ou le plus grand âge possible pour une case libre. La case à réutiliser
 * pour un nouveau venu est celle de plus grand âge : une case libre s'il y
 * en a, sinon la plus ancienne.                                           */
static uint32_t slot_age(const mesh_t *m, uint8_t id, uint32_t last_ms)
{
  return (id == 0U) ? UINT32_MAX : (m->now - last_ms);
}

/* Note qu'une trame a été entendue directement de l'émetteur id. La
 * puissance est moyennée sur chaque réception ; le compteur de trames
 * n'augmente que pour une nouvelle émission (count_frame), pas pour ses
 * répétitions. */
static void neighbor_update(mesh_t *m, uint8_t id, int8_t rssi, bool count_frame)
{
  mesh_neighbor_t *slot = NULL;
  uint32_t         slot_age_ms = 0U;

  for (uint32_t i = 0U; i < MESH_NEIGHBORS_MAX; i++)
  {
    mesh_neighbor_t *n = &m->neighbors[i];
    uint32_t         age;

    if (n->id == id)                      /* voisin déjà connu */
    {
      n->rssi    = rssi_average(n->rssi, rssi);
      if (count_frame && (n->frames < 0xFFFFU))
      {
        n->frames++;
      }
      n->last_ms = m->now;
      return;
    }
    age = slot_age(m, n->id, n->last_ms);
    if ((slot == NULL) || (age > slot_age_ms))
    {
      slot        = n;
      slot_age_ms = age;
    }
  }
  slot->id      = id;                     /* nouveau voisin */
  slot->rssi    = rssi;
  slot->frames  = 1U;
  slot->last_ms = m->now;
}

/* Vrai si le voisin est encore « actif » (entendu récemment). */
static bool neighbor_fresh(const mesh_t *m, const mesh_neighbor_t *n)
{
  return (n->id != 0U) && ((m->now - n->last_ms) < MESH_NEIGHBOR_TIMEOUT_MS);
}

/* Note qu'une trame de l'origine f->src a été reçue (première copie). */
static void node_update(mesh_t *m, const mesh_frame_t *f)
{
  mesh_node_t *slot = NULL;
  uint32_t     slot_age_ms = 0U;

  for (uint32_t i = 0U; i < MESH_NODES_MAX; i++)
  {
    mesh_node_t *n = &m->nodes[i];
    uint32_t     age;

    if (n->id == f->src)                  /* nœud déjà connu */
    {
      slot = n;
      break;
    }
    age = slot_age(m, n->id, n->last_ms);
    if ((slot == NULL) || (age > slot_age_ms))
    {
      slot        = n;
      slot_age_ms = age;
    }
  }
  if (slot->id != f->src)
  {
    memset(slot, 0, sizeof(*slot));       /* nouveau nœud (case libre ou réutilisée) */
    slot->id = f->src;
  }
  slot->hops    = f->hops;
  slot->last_ms = m->now;
  if ((f->type == MESH_T_HELLO) && (f->len >= MESH_HELLO_LEN(0U)))
  {
    slot->tag       = mesh_get_u32(f->payload);
    slot->tag_known = true;
    slot->flags     = f->payload[4];
  }
}

/* Vrai si le nœud est encore « actif ». */
static bool node_fresh(const mesh_t *m, const mesh_node_t *n)
{
  return (n->id != 0U) && ((m->now - n->last_ms) < MESH_NODE_TIMEOUT_MS);
}

/* ========================================================================== */
/* 4. Numéro du nœud et conflits                                              */
/* ========================================================================== */

/* Voir mesh.h. Le mélange (fonction de hachage « lowbias32 ») répartit les
 * étiquettes sur les 239 numéros possibles : deux cartes ont peu de chances
 * d'avoir le même numéro, mais cela arrive (un peu plus d'une chance sur
 * deux pour 18 cartes : « paradoxe des anniversaires »). */
uint8_t mesh_id_from_tag(uint32_t tag, uint8_t attempt)
{
  uint32_t h = tag ^ (0x9E3779B9U * ((uint32_t)attempt + 1U));

  h ^= h >> 16;
  h *= 0x7FEB352DU;
  h ^= h >> 15;
  h *= 0x846CA68BU;
  h ^= h >> 16;
  return (uint8_t)(MESH_ADDR_NODE_MIN + (h % MESH_ADDR_NODE_MAX));
}

/* Vrai si le numéro id est utilisé par un nœud entendu récemment. */
static bool id_in_use(const mesh_t *m, uint8_t id)
{
  for (uint32_t i = 0U; i < MESH_NODES_MAX; i++)
  {
    if ((m->nodes[i].id == id) && node_fresh(m, &m->nodes[i]))
    {
      return true;
    }
  }
  for (uint32_t i = 0U; i < MESH_NEIGHBORS_MAX; i++)
  {
    if ((m->neighbors[i].id == id) && neighbor_fresh(m, &m->neighbors[i]))
    {
      return true;
    }
  }
  return false;
}

/* Prend un autre numéro automatique, libre d'après les tables. */
static void change_id(mesh_t *m)
{
  uint8_t candidate = m->id;

  for (uint32_t tries = 0U; tries < 64U; tries++)
  {
    m->id_attempt++;
    candidate = mesh_id_from_tag(m->tag, m->id_attempt);
    if ((candidate != m->id) && !id_in_use(m, candidate))
    {
      break;
    }
  }
  m->prev_id = m->id;
  m->id      = candidate;
  notify(m, MESH_EV_ID_CHANGED, NULL, 0);
  mesh_hello_soon(m);                     /* annoncer le nouveau numéro */
}

/* HELLO reçu : un autre nœud utilise-t-il notre numéro ? On le sait par
 * l'étiquette (identité de la carte), différente de la nôtre. Qui change ?
 *   - un numéro fixé par l'utilisateur ne change jamais ;
 *   - sinon, si l'autre a un numéro fixé, c'est nous ;
 *   - sinon, la carte qui a la plus grande étiquette.
 * Chaque carte applique la même règle : une seule des deux change.       */
static void hello_received(mesh_t *m, const mesh_frame_t *f, int8_t rssi)
{
  uint32_t other_tag;
  bool     other_fixed;

  if ((f->len < MESH_HELLO_LEN(0U)) || (f->src != m->id))
  {
    return;
  }
  other_tag   = mesh_get_u32(f->payload);
  other_fixed = (f->payload[4] & MESH_HELLO_FIXED_ID) != 0U;
  if (other_tag == m->tag)
  {
    return;                               /* c'est bien nous (cas improbable) */
  }
  m->stats.id_conflicts++;
  notify(m, MESH_EV_ID_CONFLICT, f, rssi);
  if (!m->id_fixed && (other_fixed || (m->tag > other_tag)))
  {
    change_id(m);
  }
}

/* Données d'un HELLO : étiquette, drapeaux, puis les (au plus) 4 voisins
 * les mieux entendus. Rend la longueur des données. */
static uint8_t hello_build(const mesh_t *m, uint8_t *p)
{
  bool    chosen[MESH_NEIGHBORS_MAX] = { false };
  uint8_t n = 0U;

  mesh_put_u32(p, m->tag);
  p[4] = (uint8_t)((m->set.relay ? MESH_HELLO_RELAY : 0U) | (m->id_fixed ? MESH_HELLO_FIXED_ID : 0U));
  while (n < MESH_HELLO_NEIGHBORS)
  {
    int best = -1;
    for (uint32_t i = 0U; i < MESH_NEIGHBORS_MAX; i++)
    {
      const mesh_neighbor_t *nb = &m->neighbors[i];
      if (!chosen[i] && neighbor_fresh(m, nb) && (nb->id != m->id)
          && ((best < 0) || (nb->rssi > m->neighbors[best].rssi)))
      {
        best = (int)i;
      }
    }
    if (best < 0)
    {
      break;                              /* plus de voisin */
    }
    chosen[best] = true;
    p[6U + (2U * n)] = m->neighbors[best].id;
    p[7U + (2U * n)] = (uint8_t)m->neighbors[best].rssi;
    n++;
  }
  p[5] = n;
  return MESH_HELLO_LEN(n);
}

/* Délai jusqu'au prochain HELLO : la période, plus ou moins 20 %. Le
 * hasard évite que des cartes allumées ensemble émettent toujours ensemble. */
static uint32_t hello_delay(const mesh_t *m)
{
  uint32_t base   = (uint32_t)m->set.hello_period_s * 1000U;
  uint32_t jitter = base / 5U;

  return rnd_between(m, base - jitter, base + jitter);
}

/* ========================================================================== */
/* 5. File d'émission et relais                                               */
/* ========================================================================== */

/* Ajoute une trame en fin de file. Rend false si la file est pleine. */
static bool txq_push(mesh_t *m, const mesh_frame_t *f, bool relay)
{
  mesh_txq_item_t *item;

  if (m->txq_count >= MESH_TXQ_LEN)
  {
    return false;
  }
  item = &m->txq[(m->txq_head + m->txq_count) % MESH_TXQ_LEN];
  item->f     = *f;
  item->relay = relay;
  m->txq_count++;
  return true;
}

/* Retire la trame en tête de file. */
static void txq_pop(mesh_t *m)
{
  if (m->txq_count > 0U)
  {
    m->txq_head = (uint8_t)((m->txq_head + 1U) % MESH_TXQ_LEN);
    m->txq_count--;
  }
}

/* Une copie de (src, seq) vient d'être entendue d'un nouvel émetteur : si
 * un relais de cette trame attend son tour, c'est un relais de plus
 * entendu. */
static void pending_heard(mesh_t *m, uint8_t src, uint8_t seq)
{
  for (uint32_t i = 0U; i < MESH_PENDING_MAX; i++)
  {
    mesh_pending_t *p = &m->pending[i];
    if (p->used && (p->f.src == src) && (p->f.seq == seq) && (p->heard < 0xFFU))
    {
      p->heard++;
    }
  }
}

/* Programme le relais d'une trame reçue : TTL - 1, relais traversés + 1, émetteur =
 * ce nœud, émission après une attente aléatoire. Les trames de recherche
 * de route (TRACE) reçoivent en plus le numéro du relais à la fin de leurs
 * données (s'il reste de la place). */
static void relay_schedule(mesh_t *m, const mesh_frame_t *f, int8_t rssi)
{
  mesh_pending_t *p = NULL;

  for (uint32_t i = 0U; i < MESH_PENDING_MAX; i++)
  {
    if (!m->pending[i].used)
    {
      p = &m->pending[i];
      break;
    }
  }
  if (p == NULL)
  {
    m->stats.dropped++;                   /* trop de relais en attente */
    notify(m, MESH_EV_DROPPED, f, rssi);
    return;
  }
  p->f      = *f;
  p->f.ttl  = (uint8_t)(f->ttl - 1U);
  p->f.hops = (f->hops < 0xFFU) ? (uint8_t)(f->hops + 1U) : f->hops;
  p->f.last = m->id;
  if (((f->type == MESH_T_TRACE) || (f->type == MESH_T_TRACE_BACK)) && (p->f.len < MESH_PAYLOAD_MAX))
  {
    p->f.payload[p->f.len] = m->id;       /* « record route » */
    p->f.len++;
  }
  p->heard  = 0U;
  p->due_ms = m->now + rnd_between(m, MESH_RELAY_DELAY_MIN_MS, MESH_RELAY_DELAY_MAX_MS);
  p->used   = true;
}

/* Vrai si une trame adressée à dst concerne ce nœud. */
static bool is_for_me(const mesh_t *m, uint8_t dst)
{
  if ((dst == m->id) || (dst == MESH_ADDR_ALL))
  {
    return true;
  }
  return mesh_addr_is_group(dst) && mesh_group_has(m, mesh_addr_group(dst));
}

/* ========================================================================== */
/* 6. Émission, réception, tâche de fond                                      */
/* ========================================================================== */

/* Voir mesh.h */
void mesh_init(mesh_t *m, const mesh_ops_t *ops, uint32_t tag, uint32_t now)
{
  memset(m, 0, sizeof(*m));
  m->ops                = ops;
  m->tag                = tag;
  m->id                 = mesh_id_from_tag(tag, 0U);
  m->prev_id            = m->id;
  m->net                = MESH_NET_DEFAULT;
  m->set.ttl            = MESH_TTL_DEFAULT;
  m->set.relay          = true;
  m->set.suppress       = MESH_SUPPRESS_DEFAULT;
  m->set.rssi_min       = MESH_RSSI_MIN_DEFAULT;
  m->set.hello_period_s = MESH_HELLO_PERIOD_S;
  m->now                = now;
  m->tx_retry_ms        = now;
  /* Premier numéro d'ordre au hasard : après un redémarrage, les voisins se
   * souviennent encore des trames d'avant (pendant 30 s) ; repartir de 0
   * ferait prendre les premières trames pour des doublons.               */
  m->seq                = (uint8_t)rnd(m);
  /* Premier HELLO peu après le démarrage (0,5 à 2,5 s) : les voisins
   * apprennent vite le numéro et l'étiquette de la carte.                */
  m->next_hello_ms      = now + rnd_between(m, 500U, 2500U);
}

/* Voir mesh.h */
mesh_result_t mesh_send(mesh_t *m, uint8_t dst, uint8_t type, const uint8_t *data,
                        uint8_t len, uint8_t ttl, uint8_t *seq_out)
{
  mesh_frame_t f;

  if ((dst == MESH_ADDR_NONE) || (dst == m->id))
  {
    return MESH_ERR_DEST;
  }
  if (len > MESH_PAYLOAD_MAX)
  {
    return MESH_ERR_TOO_LONG;
  }
  if (m->txq_count >= MESH_TXQ_LEN)
  {
    return MESH_ERR_QUEUE_FULL;
  }
  memset(&f, 0, sizeof(f));
  m->seq++;                               /* 255 + 1 = 0 : le compteur fait le tour */
  f.net  = m->net;
  f.src  = m->id;
  f.dst  = dst;
  f.seq  = m->seq;
  f.ttl  = (ttl == MESH_TTL_USE_DEFAULT) ? m->set.ttl : ((ttl > MESH_TTL_MAX) ? (uint8_t)MESH_TTL_MAX : ttl);
  f.hops = 0U;
  f.last = m->id;
  f.type = type;
  f.len  = len;
  if (len > 0U)
  {
    memcpy(f.payload, data, len);
  }
  cache_add(m, f.src, f.seq, m->id);      /* ses propres trames ne reviennent pas */
  (void)txq_push(m, &f, false);
  if (seq_out != NULL)
  {
    *seq_out = f.seq;
  }
  return MESH_OK;
}

/* Voir mesh.h et le schéma de la réception en tête de mesh.h. */
void mesh_on_pdu(mesh_t *m, const uint8_t *pdu, uint8_t len, int8_t rssi, uint32_t now)
{
  mesh_frame_t        f;
  mesh_cache_entry_t *entry;
  bool                repeat;

  m->now = now;
  m->stats.rx_frames++;
  if (!mesh_pdu_decode(pdu, len, &f))
  {
    m->stats.rx_invalid++;
    return;
  }
  if (f.net != m->net)
  {
    m->stats.rx_other_net++;
    return;
  }
  /* Filtre : trame trop faible, ou émetteur bloqué → comme si elle n'avait
   * pas été entendue (sert à fabriquer une topologie sur une table).     */
  if ((rssi < m->set.rssi_min) || mesh_is_blocked(m, f.last))
  {
    m->stats.rx_filtered++;
    notify(m, MESH_EV_FILTERED, &f, rssi);
    return;
  }
  entry  = cache_find(m, f.src, f.seq);
  repeat = (entry != NULL) && cache_from_has(entry, f.last);
  if (f.last != m->id)
  {
    neighbor_update(m, f.last, rssi, !repeat);  /* « last » est à portée directe */
  }

  /* Déjà vue ? Du même émetteur : répétition de la même émission. D'un
   * autre émetteur : copie arrivée par un autre chemin (doublon).        */
  if (entry != NULL)
  {
    if (repeat)
    {
      m->stats.rx_repeat++;
      return;
    }
    cache_from_add(entry, f.last);
    if (f.src == m->id)
    {
      m->stats.rx_echo++;                 /* notre trame, relayée par un voisin */
      notify(m, MESH_EV_ECHO, &f, rssi);
    }
    else
    {
      m->stats.rx_dup++;
      notify(m, MESH_EV_DUP, &f, rssi);
    }
    pending_heard(m, f.src, f.seq);
    return;
  }
  cache_add(m, f.src, f.seq, f.last);
  m->stats.rx_first++;
  if (f.src != m->id)
  {
    node_update(m, &f);
  }
  if (f.type == MESH_T_HELLO)
  {
    hello_received(m, &f, rssi);          /* conflit de numéro ? */
  }
  notify(m, MESH_EV_RX, &f, rssi);

  /* Pour nous ? */
  if (is_for_me(m, f.dst))
  {
    m->stats.delivered++;
    m->ops->deliver(m->ops->ctx, &f, rssi);
  }
  if (f.dst == m->id)
  {
    return;                               /* adressée à nous seuls : pas de relais */
  }

  /* Relais */
  if (!m->set.relay)
  {
    return;
  }
  if (f.ttl == 0U)
  {
    m->stats.relay_ttl_end++;
    notify(m, MESH_EV_TTL_END, &f, rssi);
    return;
  }
  relay_schedule(m, &f, rssi);
}

/* Voir mesh.h */
void mesh_process(mesh_t *m, uint32_t now)
{
  m->now = now;

  /* 1. Relais arrivés à échéance : annulés si assez d'autres relais ont
   *    été entendus pendant l'attente, sinon mis dans la file d'émission. */
  for (uint32_t i = 0U; i < MESH_PENDING_MAX; i++)
  {
    mesh_pending_t *p = &m->pending[i];
    if (!p->used || !time_reached(now, p->due_ms))
    {
      continue;
    }
    p->used = false;
    if ((m->set.suppress > 0U) && (p->heard >= m->set.suppress))
    {
      m->stats.relay_suppressed++;
      notify(m, MESH_EV_SUPPRESSED, &p->f, 0);
    }
    else if (!txq_push(m, &p->f, true))
    {
      m->stats.dropped++;
      notify(m, MESH_EV_DROPPED, &p->f, 0);
    }
  }

  /* 2. HELLO périodique (si la file est pleine, nouvel essai dans 1 s). */
  if ((m->set.hello_period_s > 0U) && time_reached(now, m->next_hello_ms))
  {
    uint8_t payload[MESH_PAYLOAD_MAX];
    uint8_t len = hello_build(m, payload);

    if (mesh_send(m, MESH_ADDR_ALL, MESH_T_HELLO, payload, len, MESH_TTL_USE_DEFAULT, NULL) == MESH_OK)
    {
      m->next_hello_ms = now + hello_delay(m);
    }
    else
    {
      m->next_hello_ms = now + 1000U;
    }
  }

  /* 3. Émission de la trame suivante, dès que la radio est libre. */
  if ((m->txq_count > 0U) && time_reached(now, m->tx_retry_ms) && m->ops->tx_ready(m->ops->ctx))
  {
    mesh_txq_item_t *item = &m->txq[m->txq_head];
    mesh_frame_t     sent = item->f;
    bool             relay = item->relay;
    uint8_t          pdu[MESH_PDU_MAX];
    uint8_t          len = mesh_pdu_encode(&sent, pdu, sizeof(pdu));

    if ((len > 0U) && m->ops->tx_start(m->ops->ctx, pdu, len))
    {
      txq_pop(m);
      m->tx_attempts = 0U;
      if (relay)
      {
        m->stats.tx_relay++;
        notify(m, MESH_EV_RELAY, &sent, 0);
      }
      else
      {
        m->stats.tx_origin++;
        notify(m, MESH_EV_TX, &sent, 0);
      }
    }
    else
    {
      m->tx_attempts++;
      if ((len == 0U) || (m->tx_attempts >= MESH_TX_ATTEMPTS))
      {
        txq_pop(m);                       /* abandon de la trame */
        m->tx_attempts = 0U;
        m->stats.tx_fail++;
        notify(m, MESH_EV_TX_FAIL, &sent, 0);
      }
      else
      {
        m->tx_retry_ms = now + MESH_TX_RETRY_MS;
      }
    }
  }
}

/* ========================================================================== */
/* 7. Réglages et accesseurs                                                  */
/* ========================================================================== */

/* Voir mesh.h */
uint8_t mesh_id(const mesh_t *m)
{
  return m->id;
}

/* Voir mesh.h */
uint8_t mesh_previous_id(const mesh_t *m)
{
  return m->prev_id;
}

/* Voir mesh.h */
bool mesh_id_is_fixed(const mesh_t *m)
{
  return m->id_fixed;
}

/* Voir mesh.h */
uint32_t mesh_tag(const mesh_t *m)
{
  return m->tag;
}

/* Voir mesh.h */
bool mesh_set_id(mesh_t *m, uint8_t id, bool fixed)
{
  uint8_t new_id = id;

  if (id == 0U)
  {
    m->id_attempt = 0U;
    new_id = mesh_id_from_tag(m->tag, 0U);
    fixed  = false;
  }
  else if (!mesh_addr_is_node(id))
  {
    return false;
  }
  if (new_id != m->id)
  {
    m->prev_id = m->id;
    m->id      = new_id;
  }
  m->id_fixed = fixed;
  mesh_hello_soon(m);
  return true;
}

/* Voir mesh.h */
uint8_t mesh_net(const mesh_t *m)
{
  return m->net;
}

/* Voir mesh.h */
void mesh_set_net(mesh_t *m, uint8_t net)
{
  m->net = net;
  memset(m->cache, 0, sizeof(m->cache));
  memset(m->pending, 0, sizeof(m->pending));
  memset(m->neighbors, 0, sizeof(m->neighbors));
  memset(m->nodes, 0, sizeof(m->nodes));
  m->txq_head    = 0U;
  m->txq_count   = 0U;
  m->tx_attempts = 0U;
  mesh_hello_soon(m);
}

/* Voir mesh.h */
const mesh_settings_t *mesh_settings(const mesh_t *m)
{
  return &m->set;
}

/* Voir mesh.h */
void mesh_set_ttl(mesh_t *m, uint8_t ttl)
{
  m->set.ttl = (ttl > MESH_TTL_MAX) ? (uint8_t)MESH_TTL_MAX : ttl;
}

/* Voir mesh.h */
void mesh_set_relay(mesh_t *m, bool on)
{
  m->set.relay = on;
  if (!on)
  {
    memset(m->pending, 0, sizeof(m->pending));   /* relais en attente : annulés */
  }
}

/* Voir mesh.h */
void mesh_set_suppress(mesh_t *m, uint8_t n)
{
  m->set.suppress = n;
}

/* Voir mesh.h */
void mesh_set_rssi_min(mesh_t *m, int8_t dbm)
{
  m->set.rssi_min = dbm;
}

/* Voir mesh.h */
void mesh_set_hello_period(mesh_t *m, uint8_t seconds)
{
  m->set.hello_period_s = seconds;
  if (seconds > 0U)
  {
    mesh_hello_soon(m);
  }
}

/* Voir mesh.h */
void mesh_hello_soon(mesh_t *m)
{
  m->next_hello_ms = m->now + rnd_between(m, 200U, 1200U);
}

/* Voir mesh.h */
bool mesh_group_set(mesh_t *m, uint8_t g, bool join)
{
  if ((g < 1U) || (g > MESH_GROUP_COUNT))
  {
    return false;
  }
  if (join)
  {
    m->groups = (uint16_t)(m->groups | (1U << (g - 1U)));
  }
  else
  {
    m->groups = (uint16_t)(m->groups & ~(1U << (g - 1U)));
  }
  return true;
}

/* Voir mesh.h */
bool mesh_group_has(const mesh_t *m, uint8_t g)
{
  return (g >= 1U) && (g <= MESH_GROUP_COUNT) && ((m->groups & (1U << (g - 1U))) != 0U);
}

/* Voir mesh.h */
void mesh_block_set(mesh_t *m, uint8_t id, bool blocked)
{
  uint8_t mask = (uint8_t)(1U << (id & 7U));

  if (blocked)
  {
    m->blocked[id >> 3] = (uint8_t)(m->blocked[id >> 3] | mask);
  }
  else
  {
    m->blocked[id >> 3] = (uint8_t)(m->blocked[id >> 3] & (uint8_t)~mask);
  }
}

/* Voir mesh.h */
bool mesh_is_blocked(const mesh_t *m, uint8_t id)
{
  return (m->blocked[id >> 3] & (1U << (id & 7U))) != 0U;
}

/* Voir mesh.h */
void mesh_block_clear(mesh_t *m)
{
  memset(m->blocked, 0, sizeof(m->blocked));
}

/* Voir mesh.h */
bool mesh_neighbor_at(const mesh_t *m, uint8_t index, mesh_neighbor_t *out)
{
  uint8_t k = 0U;

  for (uint32_t i = 0U; i < MESH_NEIGHBORS_MAX; i++)
  {
    if (neighbor_fresh(m, &m->neighbors[i]))
    {
      if (k == index)
      {
        *out = m->neighbors[i];
        return true;
      }
      k++;
    }
  }
  return false;
}

/* Voir mesh.h */
bool mesh_node_at(const mesh_t *m, uint8_t index, mesh_node_t *out)
{
  uint8_t k = 0U;

  for (uint32_t i = 0U; i < MESH_NODES_MAX; i++)
  {
    if (node_fresh(m, &m->nodes[i]))
    {
      if (k == index)
      {
        *out = m->nodes[i];
        return true;
      }
      k++;
    }
  }
  return false;
}

/* Voir mesh.h */
const mesh_stats_t *mesh_stats(const mesh_t *m)
{
  return &m->stats;
}

/* Voir mesh.h */
void mesh_stats_clear(mesh_t *m)
{
  memset(&m->stats, 0, sizeof(m->stats));
}

/* Voir mesh.h */
uint8_t mesh_tx_queue_count(const mesh_t *m)
{
  return m->txq_count;
}
