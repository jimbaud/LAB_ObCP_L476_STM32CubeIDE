/**
 ******************************************************************************
 * @file    Tests/sim_net.h
 * @brief   Simulation sur PC d'un réseau de cartes ObCP : chaque nœud simulé
 *          fait tourner le vrai code du réseau maillé (Core/Src/mesh.c), la
 *          radio est remplacée par un « milieu » simulé.
 *
 *  Modèle de la radio (volontairement simple) :
 *    - le temps avance par pas de 1 ms ;
 *    - une trame émise occupe la radio RADIO_TX_TIME_MS (320 ms) ; pendant ce
 *      temps, un « évènement de publicité » a lieu toutes les 100 ms plus un
 *      retard aléatoire de 0 à 10 ms, comme sur le module Bluetooth de l'ObCP ;
 *    - à chaque évènement, chaque nœud à portée (lien déclaré) reçoit la
 *      trame, sauf : perte aléatoire (loss_permille), collision (deux
 *      évènements entendus dans la même milliseconde : les deux sont
 *      perdus), ou émission en cours du récepteur dans cette milliseconde ;
 *    - le RSSI reçu est celui du lien, plus ou moins 3 dB.
 ******************************************************************************
 */
#ifndef SIM_NET_H
#define SIM_NET_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "mesh.h"

#define SIM_MAX_NODES   64
#define SIM_NO_LINK     127            /* pas de liaison entre deux nœuds */
#define SIM_EV_COUNT    16             /* taille de ev_count[] (>= nombre de MESH_EV_xxx) */

struct sim_net;
typedef struct sim_node sim_node_t;

/** Réaction de l'application simulée à une trame remise (réponse à un
 *  PING, à une TRACE...). */
typedef void (*sim_deliver_cb_t)(sim_node_t *node, const mesh_frame_t *f, int8_t rssi);

/** Un nœud simulé. */
struct sim_node
{
  mesh_t           mesh;               /* le vrai code du réseau */
  mesh_ops_t       ops;
  struct sim_net  *net;
  int              index;              /* place dans le tableau (0, 1...) */
  /* radio */
  bool             tx_active;
  uint32_t         tx_end;
  uint32_t         next_adv;
  uint8_t          pdu[MESH_PDU_MAX];
  uint8_t          len;
  bool             radio_refuses;      /* la radio refuse d'émettre (test) */
  /* relevés */
  uint32_t         adv_events;         /* évènements de publicité émis */
  uint32_t         delivered;          /* trames remises à l'application */
  mesh_frame_t     last;               /* dernière trame remise */
  int8_t           last_rssi;
  uint32_t         last_time;          /* instant de la dernière remise */
  uint32_t         first_time;         /* instant de la première remise (0 : jamais) */
  uint32_t         ev_count[SIM_EV_COUNT]; /* évènements MESH_EV_xxx reçus */
  sim_deliver_cb_t on_deliver;         /* réaction de l'application (NULL : aucune) */
};

/** Le réseau simulé. */
typedef struct sim_net
{
  int        n;                        /* nombre de nœuds */
  sim_node_t nodes[SIM_MAX_NODES];
  int8_t     rssi[SIM_MAX_NODES][SIM_MAX_NODES];  /* RSSI du lien a -> b */
  uint32_t   loss_permille;            /* perte de chaque réception, pour mille */
  bool       collisions;               /* modèle de collisions actif */
  uint32_t   now;                      /* temps simulé, en ms */
  uint32_t   rng;                      /* état du générateur aléatoire */
  uint32_t   adv_events;               /* total des évènements de publicité */
  uint32_t   receptions;               /* total des réceptions réussies */
  uint32_t   collided;                 /* réceptions perdues par collision */
  FILE      *trace;                    /* journal des évènements (NULL : aucun) */
} sim_net_t;

/**
 * @brief  Prépare un réseau de n nœuds sans liaison. Chaque nœud reçoit
 *         l'étiquette 0x1000 + index et le numéro fixe index + 1 ; les
 *         HELLO sont désactivés (les tests les activent au besoin).
 */
void sim_init(sim_net_t *s, int n, uint32_t seed);

/** Déclare une liaison symétrique a <-> b de puissance rssi (dBm). */
void sim_link(sim_net_t *s, int a, int b, int8_t rssi);

/** Topologies toutes faites. */
void sim_topology_line(sim_net_t *s, int8_t rssi);              /* 0-1-2-...-n-1 */
void sim_topology_full(sim_net_t *s, int8_t rssi);              /* tous à portée */
void sim_topology_grid(sim_net_t *s, int width, int8_t rssi);   /* grille, 4 voisins */

/** Fait avancer la simulation de ms millisecondes. */
void sim_run(sim_net_t *s, uint32_t ms);

/** Nombre aléatoire (générateur de la simulation). */
uint32_t sim_rand(sim_net_t *s);

/** Remet à zéro les relevés de tous les nœuds (delivered, ev_count...). */
void sim_clear_counters(sim_net_t *s);

/** Totaux sur tous les nœuds d'un évènement MESH_EV_xxx. */
uint32_t sim_total_events(const sim_net_t *s, mesh_event_t ev);

/** Nombre de nœuds (sauf except, -1 pour aucun) ayant reçu au moins une trame. */
int sim_count_reached(const sim_net_t *s, int except);

#endif /* SIM_NET_H */
