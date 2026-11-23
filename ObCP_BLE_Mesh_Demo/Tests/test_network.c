/**
 ******************************************************************************
 * @file    Tests/test_network.c
 * @brief   Tests du réseau maillé (Core/Src/mesh.c) sur un réseau simulé de
 *          plusieurs cartes : propagation, TTL, doublons, adresses, groupes,
 *          réseaux séparés, filtre RSSI, suppression des relais, conflits de
 *          numéro, recherche de route, pertes et collisions.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "sim_net.h"

static sim_net_t s_net;                 /* grosse structure : hors de la pile */

/* Envoie un texte depuis le nœud d'index i. */
static mesh_result_t send_text(sim_net_t *s, int i, uint8_t dst, uint8_t ttl, const char *text)
{
  return mesh_send(&s->nodes[i].mesh, dst, MESH_T_TEXT, (const uint8_t *)text,
                   (uint8_t)strlen(text), ttl, NULL);
}

/* ---- Réactions de l'application simulée ----------------------------------- */

/* Répond aux PING par un PONG et aux TRACE par un TRACE_RET, comme node.c. */
static void app_reply(sim_node_t *node, const mesh_frame_t *f, int8_t rssi)
{
  uint8_t data[MESH_PAYLOAD_MAX];

  if ((f->type == MESH_T_PING) && (f->dst == mesh_id(&node->mesh)))
  {
    data[0] = f->payload[0];
    data[1] = f->hops;
    data[2] = (uint8_t)rssi;
    (void)mesh_send(&node->mesh, f->src, MESH_T_PONG, data, 3U, MESH_TTL_USE_DEFAULT, NULL);
  }
  else if ((f->type == MESH_T_TRACE) && (f->dst == mesh_id(&node->mesh)))
  {
    uint8_t k = (uint8_t)(f->len - 1U);            /* relais enregistrés à l'aller */
    data[0] = f->payload[0];
    data[1] = k;
    memcpy(&data[2], &f->payload[1], k);
    (void)mesh_send(&node->mesh, f->src, MESH_T_TRACE_BACK, data, (uint8_t)(2U + k),
                    MESH_TTL_USE_DEFAULT, NULL);
  }
}

/* ========================================================================== */

static void test_line_broadcast(void)
{
  printf("Ligne de 6 nœuds, diffusion à tous (TTL 5)\n");
  sim_init(&s_net, 6, 1U);
  sim_topology_line(&s_net, -60);
  CHECK(send_text(&s_net, 0, MESH_ADDR_ALL, 5U, "bonjour") == MESH_OK);
  sim_run(&s_net, 5000U);
  for (int i = 1; i < 6; i++)
  {
    CHECK(s_net.nodes[i].delivered == 1U);           /* reçue une fois, pas plus */
    CHECK(s_net.nodes[i].last.hops == (uint8_t)(i - 1));
    CHECK(s_net.nodes[i].last.src == 1U);
    CHECK(memcmp(s_net.nodes[i].last.payload, "bonjour", 7) == 0);
  }
  CHECK(s_net.nodes[0].delivered == 0U);             /* l'origine ne se la remet pas */
  CHECK(s_net.nodes[0].ev_count[MESH_EV_ECHO] >= 1U); /* mais entend son relais */
  CHECK(s_net.nodes[5].last.ttl == 1U);              /* 5 - 4 relais */
  /* la trame arrive de plus en plus tard le long de la ligne */
  for (int i = 2; i < 6; i++)
  {
    CHECK(s_net.nodes[i].first_time > s_net.nodes[i - 1].first_time);
  }
  CHECK(sim_total_events(&s_net, MESH_EV_TX) == 1U);
}

/* Un type de message ajouté plus tard (documentation du programme, recette
 * « Ajouter un type de message ») : les cartes qui ne le
 * connaissent pas le relaient quand même, car la couche réseau ne regarde
 * pas le type (sauf HELLO et TRACE). */
static void test_unknown_type_relayed(void)
{
  const uint8_t buzz[2] = { 0xB0U, 0x0BU };

  printf("Ligne de 4 nœuds, type de message inconnu (0x0D) : relayé jusqu'au bout\n");
  sim_init(&s_net, 4, 7U);
  sim_topology_line(&s_net, -60);
  CHECK(mesh_send(&s_net.nodes[0].mesh, MESH_ADDR_ALL, 0x0DU, buzz, 2U, 4U, NULL) == MESH_OK);
  sim_run(&s_net, 5000U);
  for (int i = 1; i < 4; i++)
  {
    CHECK(s_net.nodes[i].delivered == 1U);
    CHECK(s_net.nodes[i].last.type == 0x0DU && s_net.nodes[i].last.len == 2U);
    CHECK(s_net.nodes[i].last.hops == (uint8_t)(i - 1));
  }
  CHECK(sim_total_events(&s_net, MESH_EV_RELAY) == 3U);   /* nœuds 1, 2 et 3 */
}

static void test_ttl_limit(void)
{
  printf("Ligne de 6 nœuds, TTL 2 : la trame va à 3 sauts, pas plus\n");
  sim_init(&s_net, 6, 2U);
  sim_topology_line(&s_net, -60);
  CHECK(send_text(&s_net, 0, MESH_ADDR_ALL, 2U, "ttl") == MESH_OK);
  sim_run(&s_net, 5000U);
  CHECK(s_net.nodes[1].delivered == 1U);
  CHECK(s_net.nodes[2].delivered == 1U);
  CHECK(s_net.nodes[3].delivered == 1U);
  CHECK(s_net.nodes[4].delivered == 0U);
  CHECK(s_net.nodes[5].delivered == 0U);
  CHECK(s_net.nodes[3].ev_count[MESH_EV_TTL_END] == 1U);
  CHECK(s_net.nodes[3].last.ttl == 0U);

  printf("TTL 0 : voisins directs seulement\n");
  sim_init(&s_net, 4, 3U);
  sim_topology_line(&s_net, -60);
  CHECK(send_text(&s_net, 1, MESH_ADDR_ALL, 0U, "zero") == MESH_OK);
  sim_run(&s_net, 3000U);
  CHECK(s_net.nodes[0].delivered == 1U);
  CHECK(s_net.nodes[2].delivered == 1U);
  CHECK(s_net.nodes[3].delivered == 0U);
  CHECK(sim_total_events(&s_net, MESH_EV_RELAY) == 0U);
}

static void test_unicast_ping_trace(void)
{
  printf("Ligne de 6 nœuds : ping et trace de 1 vers 6\n");
  sim_init(&s_net, 6, 4U);
  sim_topology_line(&s_net, -60);
  for (int i = 0; i < 6; i++)
  {
    s_net.nodes[i].on_deliver = app_reply;
  }
  {
    uint8_t ping_id = 7U;
    CHECK(mesh_send(&s_net.nodes[0].mesh, 6U, MESH_T_PING, &ping_id, 1U, 5U, NULL) == MESH_OK);
  }
  sim_run(&s_net, 8000U);
  CHECK(s_net.nodes[5].delivered == 1U);                   /* le ping */
  CHECK(s_net.nodes[0].delivered == 1U);                   /* le pong */
  CHECK(s_net.nodes[0].last.type == MESH_T_PONG);
  CHECK(s_net.nodes[0].last.payload[0] == 7U);
  CHECK(s_net.nodes[0].last.payload[1] == 4U);             /* 4 relais à l'aller */
  CHECK(s_net.nodes[0].last.hops == 4U);                   /* 4 relais au retour */
  for (int i = 1; i < 5; i++)
  {
    CHECK(s_net.nodes[i].delivered == 0U);                 /* les relais ne la gardent pas */
  }
  CHECK(s_net.nodes[5].ev_count[MESH_EV_RELAY] == 0U);     /* le destinataire ne relaie pas */

  sim_clear_counters(&s_net);
  {
    uint8_t trace_id = 3U;
    CHECK(mesh_send(&s_net.nodes[0].mesh, 6U, MESH_T_TRACE, &trace_id, 1U, 5U, NULL) == MESH_OK);
  }
  sim_run(&s_net, 8000U);
  CHECK(s_net.nodes[0].last.type == MESH_T_TRACE_BACK);
  {
    const uint8_t expected[] = { 3U, 4U, 2U, 3U, 4U, 5U, 5U, 4U, 3U, 2U };
    CHECK(s_net.nodes[0].last.len == sizeof(expected));
    CHECK(memcmp(s_net.nodes[0].last.payload, expected, sizeof(expected)) == 0);
  }
}

static void test_flooding_cost_and_suppression(void)
{
  uint32_t relays_full;
  uint32_t relays_suppr;

  printf("10 nœuds tous à portée : coût de l'inondation, effet de « suppr »\n");
  sim_init(&s_net, 10, 5U);
  sim_topology_full(&s_net, -55);
  for (int i = 0; i < 10; i++)
  {
    mesh_set_suppress(&s_net.nodes[i].mesh, 0U);
  }
  CHECK(send_text(&s_net, 0, MESH_ADDR_ALL, 4U, "inonde") == MESH_OK);
  sim_run(&s_net, 5000U);
  CHECK(sim_count_reached(&s_net, 0) == 9);
  relays_full = sim_total_events(&s_net, MESH_EV_RELAY);
  CHECK(relays_full == 9U);                       /* chaque nœud relaie une fois */
  CHECK(sim_total_events(&s_net, MESH_EV_DUP) > 0U);

  sim_init(&s_net, 10, 5U);
  sim_topology_full(&s_net, -55);
  for (int i = 0; i < 10; i++)
  {
    mesh_set_suppress(&s_net.nodes[i].mesh, 2U);
  }
  CHECK(send_text(&s_net, 0, MESH_ADDR_ALL, 4U, "inonde") == MESH_OK);
  sim_run(&s_net, 5000U);
  CHECK(sim_count_reached(&s_net, 0) == 9);     /* tout le monde la reçoit quand même */
  relays_suppr = sim_total_events(&s_net, MESH_EV_RELAY);
  CHECK(relays_suppr < relays_full);
  CHECK(sim_total_events(&s_net, MESH_EV_SUPPRESSED) > 0U);
  printf("  relais sans suppression : %u, avec suppr 2 : %u\n", (unsigned)relays_full,
         (unsigned)relays_suppr);

  /* Sur une ligne, un nœud n'entend que ses deux voisins : la suppression
   * ne doit jamais couper la propagation. Les répétitions de la même
   * émission (3 ou 4 évènements de publicité) ne comptent pas comme
   * d'autres relais : vérifié avec 30 tirages différents, suppr = 1.     */
  printf("Ligne de 8 nœuds, suppr 1, 30 tirages : la suppression ne coupe jamais la ligne\n");
  {
    int all_reached = 0;
    for (uint32_t seed = 100U; seed < 130U; seed++)
    {
      sim_init(&s_net, 8, seed);
      sim_topology_line(&s_net, -60);
      for (int i = 0; i < 8; i++)
      {
        mesh_set_suppress(&s_net.nodes[i].mesh, 1U);
      }
      (void)send_text(&s_net, 0, MESH_ADDR_ALL, 7U, "ligne");
      sim_run(&s_net, 8000U);
      all_reached += (sim_count_reached(&s_net, 0) == 7) ? 1 : 0;
    }
    CHECK(all_reached == 30);
  }
}

static void test_groups_and_networks(void)
{
  printf("Groupes : seuls les membres du groupe 2 gardent la trame\n");
  sim_init(&s_net, 6, 7U);
  sim_topology_line(&s_net, -60);
  CHECK(mesh_group_set(&s_net.nodes[2].mesh, 2U, true));
  CHECK(mesh_group_set(&s_net.nodes[4].mesh, 2U, true));
  CHECK(mesh_group_set(&s_net.nodes[4].mesh, 3U, true));
  CHECK(!mesh_group_set(&s_net.nodes[4].mesh, 16U, true));
  {
    const uint8_t rgb[4] = { 255U, 0U, 0U, 0U };
    CHECK(mesh_send(&s_net.nodes[0].mesh, MESH_GROUP_ADDR(2U), MESH_T_LED, rgb, 4U, 5U, NULL) == MESH_OK);
  }
  sim_run(&s_net, 5000U);
  CHECK(s_net.nodes[1].delivered == 0U);
  CHECK(s_net.nodes[2].delivered == 1U);
  CHECK(s_net.nodes[3].delivered == 0U);
  CHECK(s_net.nodes[4].delivered == 1U);
  CHECK(s_net.nodes[5].delivered == 0U);
  CHECK(s_net.nodes[3].ev_count[MESH_EV_RELAY] == 1U); /* non membre : relaie quand même */

  printf("Réseaux séparés : le nœud 4 (réseau 7) ignore et coupe la ligne\n");
  sim_init(&s_net, 6, 8U);
  sim_topology_line(&s_net, -60);
  mesh_set_net(&s_net.nodes[3].mesh, 7U);
  CHECK(send_text(&s_net, 0, MESH_ADDR_ALL, 5U, "net") == MESH_OK);
  sim_run(&s_net, 5000U);
  CHECK(s_net.nodes[2].delivered == 1U);
  CHECK(s_net.nodes[3].delivered == 0U);
  CHECK(s_net.nodes[4].delivered == 0U);
  CHECK(s_net.nodes[3].mesh.stats.rx_other_net > 0U);

  printf("Relais désactivé sur le nœud 3 : la suite de la ligne n'est pas atteinte\n");
  sim_init(&s_net, 5, 9U);
  sim_topology_line(&s_net, -60);
  mesh_set_relay(&s_net.nodes[2].mesh, false);
  CHECK(send_text(&s_net, 0, MESH_ADDR_ALL, 5U, "relais") == MESH_OK);
  sim_run(&s_net, 5000U);
  CHECK(s_net.nodes[2].delivered == 1U);              /* il la reçoit... */
  CHECK(s_net.nodes[3].delivered == 0U);              /* ...mais ne la passe pas */
}

static void test_rssi_filter_and_block(void)
{
  printf("Filtre RSSI : 6 nœuds tous à portée, mais seuls les proches retenus\n");
  sim_init(&s_net, 6, 10U);
  /* nœuds alignés à 1 m d'écart : -45 dBm à 1 m, 10 dB de moins par mètre */
  for (int a = 0; a < 6; a++)
  {
    for (int b = a + 1; b < 6; b++)
    {
      sim_link(&s_net, a, b, (int8_t)(-35 - (10 * (b - a))));
    }
  }
  CHECK(send_text(&s_net, 0, MESH_ADDR_ALL, 5U, "direct") == MESH_OK);
  sim_run(&s_net, 4000U);
  CHECK(s_net.nodes[5].last.hops == 0U);              /* sans filtre : reçue en direct */

  sim_init(&s_net, 6, 10U);
  for (int a = 0; a < 6; a++)
  {
    for (int b = a + 1; b < 6; b++)
    {
      sim_link(&s_net, a, b, (int8_t)(-35 - (10 * (b - a))));
    }
  }
  for (int i = 0; i < 6; i++)
  {
    mesh_set_rssi_min(&s_net.nodes[i].mesh, -60);   /* portée ramenée à 2 m */
  }
  CHECK(send_text(&s_net, 0, MESH_ADDR_ALL, 5U, "filtre") == MESH_OK);
  sim_run(&s_net, 6000U);
  CHECK(sim_count_reached(&s_net, 0) == 5);
  CHECK(s_net.nodes[5].last.hops >= 2U);              /* il a fallu des relais */
  CHECK(sim_total_events(&s_net, MESH_EV_FILTERED) > 0U);

  printf("Émetteur bloqué : le nœud 3 n'écoute plus le nœud 2\n");
  sim_init(&s_net, 3, 11U);
  sim_topology_full(&s_net, -50);
  mesh_block_set(&s_net.nodes[2].mesh, 2U, true);
  CHECK(mesh_is_blocked(&s_net.nodes[2].mesh, 2U));
  CHECK(send_text(&s_net, 1, MESH_ADDR_ALL, 3U, "bloque") == MESH_OK);
  sim_run(&s_net, 3000U);
  CHECK(s_net.nodes[2].last.hops == 1U);              /* reçue par le relais du nœud 1 */
  CHECK(s_net.nodes[2].last.last == 1U);
  mesh_block_clear(&s_net.nodes[2].mesh);
  CHECK(!mesh_is_blocked(&s_net.nodes[2].mesh, 2U));
}

static void test_neighbors_and_hello(void)
{
  mesh_neighbor_t nb;
  int             count = 0;
  bool            saw_3 = false;
  bool            saw_1 = false;

  printf("HELLO : table des voisins et liste des voisins dans le HELLO\n");
  sim_init(&s_net, 5, 12U);
  sim_topology_line(&s_net, -60);
  sim_link(&s_net, 1, 2, -45);
  for (int i = 0; i < 5; i++)
  {
    mesh_set_hello_period(&s_net.nodes[i].mesh, 3U);
  }
  sim_run(&s_net, 12000U);
  /* le nœud 2 (index 1) a pour voisins 1 et 3 */
  while (mesh_neighbor_at(&s_net.nodes[1].mesh, (uint8_t)count, &nb))
  {
    saw_1 = saw_1 || (nb.id == 1U);
    saw_3 = saw_3 || (nb.id == 3U);
    count++;
  }
  CHECK(count == 2);
  CHECK(saw_1 && saw_3);
  /* le dernier HELLO du nœud 2 reçu par le nœud 1 liste ses voisins */
  {
    mesh_node_t n;
    bool        found = false;
    for (uint8_t i = 0U; mesh_node_at(&s_net.nodes[0].mesh, i, &n); i++)
    {
      if (n.id == 2U)
      {
        found = true;
        CHECK(n.tag_known && (n.tag == 0x1001U));
        CHECK((n.flags & MESH_HELLO_RELAY) != 0U);
        CHECK((n.flags & MESH_HELLO_FIXED_ID) != 0U);
      }
    }
    CHECK(found);
  }
  /* le nœud 1 connaît les 4 autres (HELLO relayés) */
  {
    mesh_node_t n;
    uint8_t     k = 0U;
    while (mesh_node_at(&s_net.nodes[0].mesh, k, &n))
    {
      k++;
    }
    CHECK(k == 4U);
  }
}

static void test_id_conflict(void)
{
  printf("Conflit de numéro : deux cartes « automatiques » avec le même numéro\n");
  sim_init(&s_net, 4, 13U);
  sim_topology_full(&s_net, -50);
  for (int i = 0; i < 4; i++)
  {
    mesh_set_hello_period(&s_net.nodes[i].mesh, 2U);
  }
  /* index 1 et 3 : même numéro 42, non fixé ; étiquettes 0x1001 < 0x1003 */
  (void)mesh_set_id(&s_net.nodes[1].mesh, 42U, false);
  (void)mesh_set_id(&s_net.nodes[3].mesh, 42U, false);
  sim_run(&s_net, 10000U);
  CHECK(mesh_id(&s_net.nodes[1].mesh) == 42U);        /* la plus petite étiquette garde */
  CHECK(mesh_id(&s_net.nodes[3].mesh) != 42U);        /* la plus grande change */
  CHECK(mesh_previous_id(&s_net.nodes[3].mesh) == 42U);
  CHECK(s_net.nodes[3].ev_count[MESH_EV_ID_CHANGED] == 1U);
  CHECK(s_net.nodes[1].ev_count[MESH_EV_ID_CONFLICT] >= 1U);
  for (int a = 0; a < 4; a++)
  {
    for (int b = a + 1; b < 4; b++)
    {
      CHECK(mesh_id(&s_net.nodes[a].mesh) != mesh_id(&s_net.nodes[b].mesh));
    }
  }

  printf("Conflit avec un numéro fixé : c'est la carte « automatique » qui change\n");
  sim_init(&s_net, 3, 14U);
  sim_topology_full(&s_net, -50);
  for (int i = 0; i < 3; i++)
  {
    mesh_set_hello_period(&s_net.nodes[i].mesh, 2U);
  }
  (void)mesh_set_id(&s_net.nodes[0].mesh, 9U, false);    /* étiquette la plus petite */
  (void)mesh_set_id(&s_net.nodes[2].mesh, 9U, true);     /* fixé */
  sim_run(&s_net, 10000U);
  CHECK(mesh_id(&s_net.nodes[2].mesh) == 9U);
  CHECK(mesh_id(&s_net.nodes[0].mesh) != 9U);

  printf("Deux numéros fixés identiques : signalé, mais personne ne change\n");
  sim_init(&s_net, 2, 15U);
  sim_topology_full(&s_net, -50);
  for (int i = 0; i < 2; i++)
  {
    mesh_set_hello_period(&s_net.nodes[i].mesh, 2U);
    (void)mesh_set_id(&s_net.nodes[i].mesh, 5U, true);
  }
  sim_run(&s_net, 8000U);
  CHECK(mesh_id(&s_net.nodes[0].mesh) == 5U);
  CHECK(mesh_id(&s_net.nodes[1].mesh) == 5U);
  CHECK(s_net.nodes[0].mesh.stats.id_conflicts >= 1U);
  CHECK(s_net.nodes[1].mesh.stats.id_conflicts >= 1U);
}

static void test_queue_and_radio_refusal(void)
{
  mesh_t *m;

  printf("File d'émission pleine, radio qui refuse\n");
  sim_init(&s_net, 2, 16U);
  sim_topology_full(&s_net, -50);
  m = &s_net.nodes[0].mesh;
  for (int i = 0; i < (int)MESH_TXQ_LEN; i++)
  {
    CHECK(send_text(&s_net, 0, MESH_ADDR_ALL, 1U, "x") == MESH_OK);
  }
  CHECK(send_text(&s_net, 0, MESH_ADDR_ALL, 1U, "x") == MESH_ERR_QUEUE_FULL);
  CHECK(mesh_tx_queue_count(m) == MESH_TXQ_LEN);
  sim_run(&s_net, (MESH_TXQ_LEN + 1U) * RADIO_TX_TIME_MS);
  CHECK(mesh_tx_queue_count(m) == 0U);
  CHECK(s_net.nodes[1].delivered == MESH_TXQ_LEN);     /* toutes reçues, une par une */

  CHECK(mesh_send(m, 1U, MESH_T_TEXT, (const uint8_t *)"a", 1U, 1U, NULL) == MESH_ERR_DEST); /* soi-même */
  CHECK(mesh_send(m, 0U, MESH_T_TEXT, (const uint8_t *)"a", 1U, 1U, NULL) == MESH_ERR_DEST);
  {
    uint8_t big[MESH_PAYLOAD_MAX + 1U] = { 0 };
    CHECK(mesh_send(m, 2U, MESH_T_TEXT, big, (uint8_t)sizeof(big), 1U, NULL) == MESH_ERR_TOO_LONG);
  }

  s_net.nodes[0].radio_refuses = true;
  CHECK(send_text(&s_net, 0, MESH_ADDR_ALL, 1U, "refus") == MESH_OK);
  sim_run(&s_net, 1000U);
  CHECK(m->stats.tx_fail == 1U);                      /* abandon après 3 essais */
  CHECK(mesh_tx_queue_count(m) == 0U);
}

static void test_cache_expiry(void)
{
  mesh_frame_t f;
  uint8_t      pdu[MESH_PDU_MAX];
  uint8_t      len;
  sim_node_t  *node;

  printf("Mémoire des trames : copie ignorée, puis acceptée après 30 s\n");
  sim_init(&s_net, 1, 17U);
  node = &s_net.nodes[0];
  memset(&f, 0, sizeof(f));
  f.net = 0U; f.src = 50U; f.dst = MESH_ADDR_ALL; f.seq = 9U; f.ttl = 0U; f.last = 50U;
  f.type = MESH_T_TEXT; f.len = 1U; f.payload[0] = 'z';
  len = mesh_pdu_encode(&f, pdu, sizeof(pdu));
  mesh_on_pdu(&node->mesh, pdu, len, -50, 2000U);
  mesh_on_pdu(&node->mesh, pdu, len, -50, 2100U);    /* même émetteur : répétition */
  CHECK(node->delivered == 1U);
  CHECK(node->mesh.stats.rx_repeat == 1U);
  CHECK(node->mesh.stats.rx_dup == 0U);
  f.last = 60U;                                      /* relayée par le nœud 60 : doublon */
  f.hops = 1U;
  len = mesh_pdu_encode(&f, pdu, sizeof(pdu));
  mesh_on_pdu(&node->mesh, pdu, len, -50, 2200U);
  CHECK(node->delivered == 1U);
  CHECK(node->mesh.stats.rx_dup == 1U);
  mesh_on_pdu(&node->mesh, pdu, len, -50, 2000U + MESH_CACHE_LIFETIME_MS + 10U);
  CHECK(node->delivered == 2U);
  /* trame d'un autre format : ignorée */
  pdu[0] = 0x00U;
  mesh_on_pdu(&node->mesh, pdu, len, -50, 40000U);
  CHECK(node->mesh.stats.rx_invalid == 1U);
}

static void test_lossy_grid(void)
{
  int reached;

  printf("Grille 6 x 6, 20 %% de pertes et collisions : diffusion depuis un coin\n");
  sim_init(&s_net, 36, 18U);
  sim_topology_grid(&s_net, 6, -62);
  s_net.loss_permille = 200U;
  s_net.collisions    = true;
  CHECK(send_text(&s_net, 0, MESH_ADDR_ALL, 12U, "grille") == MESH_OK);
  sim_run(&s_net, 15000U);
  reached = sim_count_reached(&s_net, 0);
  printf("  atteints : %d / 35, relais : %u, annulés : %u, collisions : %u\n", reached,
         (unsigned)sim_total_events(&s_net, MESH_EV_RELAY),
         (unsigned)sim_total_events(&s_net, MESH_EV_SUPPRESSED), (unsigned)s_net.collided);
  CHECK(reached >= 33);
  CHECK(s_net.nodes[35].last.hops >= 9U);             /* coin opposé : 10 sauts au moins */
}

int main(void)
{
  test_line_broadcast();
  test_unknown_type_relayed();
  test_ttl_limit();
  test_unicast_ping_trace();
  test_flooding_cost_and_suppression();
  test_groups_and_networks();
  test_rssi_filter_and_block();
  test_neighbors_and_hello();
  test_id_conflict();
  test_queue_and_radio_refusal();
  test_cache_expiry();
  test_lossy_grid();
  return check_summary("réseau maillé simulé");
}
