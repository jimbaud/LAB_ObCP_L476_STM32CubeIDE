/**
 ******************************************************************************
 * @file    Tests/simu_reseau.c
 * @brief   Simulateur pédagogique : une trame diffusée à tous dans un réseau
 *          de cartes ObCP simulées, avec le vrai code du réseau maillé.
 *
 *  Utilisation (dans le dossier Tests) :
 *      make simu                                    réglages par défaut
 *      make simu ARGS="topo=ligne n=8 ttl=3"        autres réglages
 *      ./build/simu_reseau aide                     liste des réglages
 *
 *  Réglages (clé=valeur, dans n'importe quel ordre) :
 *      topo=grille|ligne|complet|hasard   forme du réseau (grille)
 *      n=16          nombre de nœuds, 2 à 64
 *      ttl=4         TTL de la trame (0 à 15)
 *      suppr=3       suppression des relais (0 : aucune)
 *      perte=10      réceptions perdues au hasard, en %
 *      collisions=1  modèle de collisions (0 ou 1)
 *      graine=1      tirage aléatoire (autre graine = autre déroulement)
 *      journal       affiche chaque évènement, milliseconde par milliseconde
 *
 *  Le nœud 1 envoie une trame TEXTE à tous ; le programme affiche quand et
 *  après combien de relais chaque nœud l'a reçue, puis les compteurs : relais,
 *  relais annulés, doublons, collisions. Idée de travail : prévoir le
 *  résultat avant de lancer, puis comparer (questions dans le guide de
 *  découverte, Doc/Guide_Decouverte_BLE_Mesh_ObCP.pdf, « Le prix de
 *  l'inondation »).
 ******************************************************************************
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim_net.h"

static sim_net_t s_net;

typedef struct
{
  char     topo[16];
  int      n;
  int      ttl;
  int      suppr;
  int      loss_percent;
  int      collisions;
  unsigned seed;
  int      journal;
} options_t;

static void usage(void)
{
  printf("Réglages (clé=valeur) : topo=grille|ligne|complet|hasard  n=16  ttl=4  suppr=3\n"
         "                        perte=10  collisions=1  graine=1  journal\n"
         "Exemple : make simu ARGS=\"topo=ligne n=8 ttl=3 suppr=0\"\n");
}

static int parse_options(int argc, char **argv, options_t *o)
{
  strcpy(o->topo, "grille");
  o->n = 16; o->ttl = 4; o->suppr = 3; o->loss_percent = 10;
  o->collisions = 1; o->seed = 1U; o->journal = 0;
  for (int i = 1; i < argc; i++)
  {
    const char *a = argv[i];
    if (strncmp(a, "topo=", 5) == 0)            { snprintf(o->topo, sizeof(o->topo), "%s", a + 5); }
    else if (strncmp(a, "n=", 2) == 0)          { o->n = atoi(a + 2); }
    else if (strncmp(a, "ttl=", 4) == 0)        { o->ttl = atoi(a + 4); }
    else if (strncmp(a, "suppr=", 6) == 0)      { o->suppr = atoi(a + 6); }
    else if (strncmp(a, "perte=", 6) == 0)      { o->loss_percent = atoi(a + 6); }
    else if (strncmp(a, "collisions=", 11) == 0){ o->collisions = atoi(a + 11); }
    else if (strncmp(a, "graine=", 7) == 0)     { o->seed = (unsigned)strtoul(a + 7, NULL, 10); }
    else if (strcmp(a, "journal") == 0)         { o->journal = 1; }
    else { usage(); return 0; }
  }
  if ((o->n < 2) || (o->n > SIM_MAX_NODES) || (o->ttl < 0) || (o->ttl > 15)
      || (o->suppr < 0) || (o->suppr > 255) || (o->loss_percent < 0) || (o->loss_percent > 100))
  {
    printf("Réglage hors limites.\n");
    usage();
    return 0;
  }
  return 1;
}

/* Côté de la grille : le plus petit carré qui contient n nœuds. */
static int grid_width(int n)
{
  int w = 1;
  while ((w * w) < n)
  {
    w++;
  }
  return w;
}

/* Nœuds posés au hasard dans un carré de 10 m ; liaison si la distance est
 * inférieure à 3,5 m, RSSI de -45 dBm à 1 m, 20 dB de moins par décade. */
static void topology_random(sim_net_t *s)
{
  double x[SIM_MAX_NODES];
  double y[SIM_MAX_NODES];

  for (int i = 0; i < s->n; i++)
  {
    x[i] = (double)(sim_rand(s) % 1000U) / 100.0;
    y[i] = (double)(sim_rand(s) % 1000U) / 100.0;
  }
  for (int a = 0; a < s->n; a++)
  {
    for (int b = a + 1; b < s->n; b++)
    {
      double d = sqrt(((x[a] - x[b]) * (x[a] - x[b])) + ((y[a] - y[b]) * (y[a] - y[b])));
      if (d < 3.5)
      {
        double rssi = -45.0 - (20.0 * log10((d < 0.3) ? 0.3 : d));
        sim_link(s, a, b, (int8_t)rssi);
      }
    }
  }
}

static int count_links(const sim_net_t *s, int i)
{
  int k = 0;
  for (int j = 0; j < s->n; j++)
  {
    k += (s->rssi[i][j] != SIM_NO_LINK) ? 1 : 0;
  }
  return k;
}

int main(int argc, char **argv)
{
  options_t o;
  int       width = 0;
  uint32_t  t0;
  uint32_t  latest = 0U;
  int       reached;

  if (!parse_options(argc, argv, &o))
  {
    return 1;
  }
  sim_init(&s_net, o.n, o.seed);
  if (strcmp(o.topo, "ligne") == 0)        { sim_topology_line(&s_net, -60); }
  else if (strcmp(o.topo, "complet") == 0) { sim_topology_full(&s_net, -55); }
  else if (strcmp(o.topo, "hasard") == 0)  { topology_random(&s_net); }
  else                                     { width = grid_width(o.n); sim_topology_grid(&s_net, width, -62); }
  s_net.loss_permille = (uint32_t)o.loss_percent * 10U;
  s_net.collisions    = (o.collisions != 0);
  for (int i = 0; i < o.n; i++)
  {
    mesh_set_suppress(&s_net.nodes[i].mesh, (uint8_t)o.suppr);
  }

  printf("Réseau « %s » de %d nœuds, TTL %d, suppr %d, pertes %d %%, collisions %s, graine %u\n",
         o.topo, o.n, o.ttl, o.suppr, o.loss_percent, o.collisions ? "oui" : "non", o.seed);
  {
    int links = 0;
    for (int i = 0; i < o.n; i++)
    {
      links += count_links(&s_net, i);
    }
    printf("Liaisons radio : %d (%.1f voisins par nœud en moyenne)\n\n", links / 2,
           (double)links / (double)o.n);
  }

  if (o.journal)
  {
    s_net.trace = stdout;
  }
  t0 = s_net.now;
  (void)mesh_send(&s_net.nodes[0].mesh, MESH_ADDR_ALL, MESH_T_TEXT, (const uint8_t *)"bonjour", 7U,
                  (uint8_t)o.ttl, NULL);
  sim_run(&s_net, 15000U);
  s_net.trace = NULL;

  /* Réception nœud par nœud */
  printf("%snœud  reçue à   relais  de (dernier émetteur)\n", o.journal ? "\n" : "");
  for (int i = 1; i < o.n; i++)
  {
    const sim_node_t *node = &s_net.nodes[i];
    if (node->delivered > 0U)
    {
      uint32_t dt = node->first_time - t0;
      latest = (dt > latest) ? dt : latest;
      printf("%4d  %6u ms  %6u  %u\n", i + 1, (unsigned)dt, (unsigned)node->last.hops,
             (unsigned)node->last.last);
    }
    else
    {
      printf("%4d  jamais reçue%s\n", i + 1, (count_links(&s_net, i) == 0) ? " (aucun voisin)" : "");
    }
  }

  /* Carte des relais traversés, pour une grille */
  if (width > 0)
  {
    printf("\nRelais traversés pour atteindre chaque nœud (O = origine, 0 = reçue en direct,\n"
           ". = jamais atteint) :\n");
    for (int i = 0; i < o.n; i++)
    {
      if (i == 0)                              { printf("   O"); }
      else if (s_net.nodes[i].delivered > 0U)  { printf("%4u", (unsigned)s_net.nodes[i].last.hops); }
      else                                     { printf("   ."); }
      if (((i + 1) % width) == 0)
      {
        printf("\n");
      }
    }
    printf("\n");
  }

  reached = sim_count_reached(&s_net, 0);
  printf("\nBilan\n");
  printf("  nœuds atteints        : %d sur %d\n", reached, o.n - 1);
  printf("  dernière arrivée      : %u ms après l'envoi\n", (unsigned)latest);
  printf("  émissions             : 1 de l'origine + %u relais\n",
         (unsigned)sim_total_events(&s_net, MESH_EV_RELAY));
  printf("  relais annulés        : %u (suppression)\n", (unsigned)sim_total_events(&s_net, MESH_EV_SUPPRESSED));
  printf("  relais impossibles    : %u (TTL épuisé)\n", (unsigned)sim_total_events(&s_net, MESH_EV_TTL_END));
  printf("  doublons reçus        : %u (copies venues d'un autre émetteur)\n",
         (unsigned)sim_total_events(&s_net, MESH_EV_DUP));
  printf("  évènements de pub.    : %u, réceptions : %u, collisions : %u\n",
         (unsigned)s_net.adv_events, (unsigned)s_net.receptions, (unsigned)s_net.collided);
  printf("  occupation radio      : %.1f s d'émission cumulée (%u ms par trame)\n",
         (double)((sim_total_events(&s_net, MESH_EV_RELAY) + 1U) * RADIO_TX_TIME_MS) / 1000.0,
         (unsigned)RADIO_TX_TIME_MS);
  return 0;
}
