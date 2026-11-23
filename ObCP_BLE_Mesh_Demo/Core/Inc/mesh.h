/**
 ******************************************************************************
 * @file    mesh.h
 * @brief   Réseau maillé par « inondation contrôlée » (managed flooding) :
 *          émission, réception, relais, doublons, TTL, voisins.
 *
 *  LE PRINCIPE
 *  Il n'y a ni routeur ni table de routage : chaque trame est diffusée à
 *  tous les voisins, et chaque nœud qui la reçoit pour la première fois la
 *  rediffuse à son tour (il la « relaie »). La trame se répand ainsi de
 *  proche en proche, comme une rumeur, jusqu'aux nœuds hors de portée de
 *  l'émetteur. Trois règles empêchent l'inondation de durer sans fin :
 *
 *    1. MÉMOIRE (cache) : chaque nœud retient les trames déjà vues, par
 *       leur couple (origine, numéro d'ordre). Une copie reçue par un autre
 *       chemin est ignorée : on ne relaie jamais deux fois la même trame.
 *    2. TTL (« Time To Live ») : l'origine fixe le nombre de relais permis ;
 *       chaque relais le diminue de 1 ; à 0, on ne relaie plus.
 *    3. ATTENTE ALÉATOIRE : un relais attend 20 à 300 ms (tirage au sort)
 *       avant de réémettre, pour que les voisins ne parlent pas tous en
 *       même temps. S'il entend pendant ce temps assez d'autres relais de
 *       la même trame (réglage « suppr »), il renonce : ses voisins l'ont
 *       déjà reçue (« suppression » des relais inutiles).
 *
 *  RÉPÉTITIONS ET DOUBLONS
 *  Chaque émission est répétée à 3 ou 4 évènements de publicité (voir
 *  RADIO_TX_TIME_MS) : un voisin reçoit donc souvent plusieurs fois la même
 *  émission. Ce sont des « répétitions » (même émetteur), simplement
 *  comptées. Une copie qui arrive d'un AUTRE émetteur (un relais) est un
 *  « doublon » : c'est elle qui compte pour la suppression.
 *
 *  À la réception d'une trame :
 *
 *      trame reçue
 *        │ autre réseau, format faux ............................ ignorée
 *        │ RSSI < seuil « rssimin », émetteur bloqué ......... ignorée (filtrée)
 *        │ voisin « last » noté (table des voisins)
 *        │ déjà vue (origine, n°) ? oui : même émetteur → répétition, ignorée ;
 *        │                                autre émetteur → doublon, compté pour la suppression
 *        │ notée dans la mémoire
 *        │ pour moi (mon numéro, mon groupe, tous) ? ...... oui : remise à l'application
 *        │ adressée à moi seul ? .................... oui : terminé (pas de relais)
 *        │ relais désactivé, ou TTL = 0 ? ......................... pas de relais
 *        └ relais programmé : TTL - 1, relais traversés + 1, émetteur = moi, après une
 *          attente aléatoire (annulé si assez d'autres relais entendus)
 *
 *  INDÉPENDANT DU MATÉRIEL
 *  Ce module ne connaît ni le module Bluetooth ni la HAL : il reçoit les
 *  trames et le temps (en ms) par ses paramètres, et émet par des
 *  fonctions fournies par l'utilisateur (structure mesh_ops_t). Le même
 *  code tourne sur l'ObCP (node.c) et sur PC, où des dizaines de nœuds
 *  simulés se parlent (Tests/test_network.c, Tests/simu_reseau.c).
 *
 *  Toutes les variables d'un nœud sont rangées dans une structure mesh_t :
 *  sur l'ObCP il y en a une seule (dans node.c), sur PC une par nœud simulé.
 ******************************************************************************
 */
#ifndef MESH_H
#define MESH_H

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "mesh_pdu.h"

/** TTL à utiliser par mesh_send() : celui des réglages. */
#define MESH_TTL_USE_DEFAULT   0xFFU

/* ========================================================================== */
/* Interface avec la radio et l'application                                   */
/* ========================================================================== */

/** Évènements signalés à l'application (journal, outil PC, statistiques). */
typedef enum
{
  MESH_EV_TX = 0,       /**< trame de ce nœud émise (origine) */
  MESH_EV_RELAY,        /**< trame relayée */
  MESH_EV_RX,           /**< trame reçue pour la première fois */
  MESH_EV_DUP,          /**< copie d'une trame déjà vue, relayée par un autre émetteur */
  MESH_EV_ECHO,         /**< sa propre trame, relayée par un voisin (une fois par voisin) */
  MESH_EV_FILTERED,     /**< trame ignorée : RSSI trop faible ou émetteur bloqué */
  MESH_EV_SUPPRESSED,   /**< relais annulé : assez d'autres relais entendus */
  MESH_EV_TTL_END,      /**< pas de relais : TTL épuisé */
  MESH_EV_DROPPED,      /**< trame perdue : file d'émission ou de relais pleine */
  MESH_EV_TX_FAIL,      /**< trame abandonnée : la radio a refusé de l'émettre */
  MESH_EV_ID_CONFLICT,  /**< un autre nœud utilise le même numéro */
  MESH_EV_ID_CHANGED    /**< ce nœud a changé de numéro (voir mesh_previous_id) */
} mesh_event_t;

/** Fonctions fournies par l'utilisateur du module. */
typedef struct
{
  /** La radio peut-elle émettre une trame maintenant ? */
  bool (*tx_ready)(void *ctx);
  /** Émet une trame codée (pdu, len octets). Rend false si la radio refuse. */
  bool (*tx_start)(void *ctx, const uint8_t *pdu, uint8_t len);
  /** Remet à l'application une trame qui lui est destinée (rssi : puissance
   *  de la copie reçue, en dBm). */
  void (*deliver)(void *ctx, const mesh_frame_t *f, int8_t rssi);
  /** Signale un évènement (f peut être NULL pour MESH_EV_ID_CHANGED). Peut
   *  être NULL si l'application n'en a pas besoin. */
  void (*event)(void *ctx, mesh_event_t ev, const mesh_frame_t *f, int8_t rssi);
  /** Rend un nombre aléatoire de 32 bits. */
  uint32_t (*random)(void *ctx);
  /** Paramètre passé à chacune de ces fonctions. */
  void *ctx;
} mesh_ops_t;

/* ========================================================================== */
/* Réglages, tables et compteurs                                              */
/* ========================================================================== */

/** Réglages modifiables pendant le fonctionnement (commandes du terminal). */
typedef struct
{
  uint8_t  ttl;            /**< TTL des trames émises (0 à MESH_TTL_MAX) */
  bool     relay;          /**< relaie les trames des autres */
  uint8_t  suppress;       /**< annule un relais après N autres relais entendus (0 : jamais) */
  int8_t   rssi_min;       /**< ignore les trames plus faibles (dBm, MESH_RSSI_MIN_OFF : aucun seuil) */
  uint8_t  hello_period_s; /**< période des HELLO en secondes (0 : pas de HELLO) */
} mesh_settings_t;

/** Un voisin direct (entendu sans intermédiaire). */
typedef struct
{
  uint8_t  id;             /**< numéro (0 : case libre) */
  int8_t   rssi;           /**< puissance reçue moyenne, en dBm */
  uint16_t frames;         /**< trames reçues de lui (relayées ou non) */
  uint32_t last_ms;        /**< dernière trame reçue (temps en ms) */
} mesh_neighbor_t;

/** Un nœud connu du réseau (origine d'au moins une trame reçue). */
typedef struct
{
  uint8_t  id;             /**< numéro (0 : case libre) */
  uint8_t  hops;           /**< relais traversés par sa dernière trame reçue */
  uint8_t  flags;          /**< drapeaux de son dernier HELLO */
  bool     tag_known;      /**< étiquette reçue (HELLO) */
  uint32_t tag;            /**< étiquette (identité de la carte) */
  uint32_t last_ms;        /**< dernière trame reçue (temps en ms) */
} mesh_node_t;

/** Compteurs (commande « stats »). */
typedef struct
{
  uint32_t rx_frames;      /**< trames reçues, toutes confondues */
  uint32_t rx_invalid;     /**< format incorrect */
  uint32_t rx_other_net;   /**< autre numéro de réseau */
  uint32_t rx_filtered;    /**< RSSI trop faible ou émetteur bloqué */
  uint32_t rx_first;       /**< premières copies */
  uint32_t rx_repeat;      /**< répétitions d'une émission déjà reçue (même émetteur) */
  uint32_t rx_dup;         /**< copies relayées par un autre émetteur (doublons) */
  uint32_t rx_echo;        /**< ses propres trames, relayées par un voisin */
  uint32_t delivered;      /**< trames destinées à ce nœud */
  uint32_t tx_origin;      /**< trames émises par ce nœud */
  uint32_t tx_relay;       /**< trames relayées */
  uint32_t relay_suppressed; /**< relais annulés (suppression) */
  uint32_t relay_ttl_end;  /**< relais impossibles : TTL épuisé */
  uint32_t dropped;        /**< trames perdues : file pleine */
  uint32_t tx_fail;        /**< trames refusées par la radio */
  uint32_t id_conflicts;   /**< numéros en double détectés */
} mesh_stats_t;

/* ========================================================================== */
/* État d'un nœud                                                             */
/* ========================================================================== */

/** Un relais en attente de son tour (délai aléatoire). */
typedef struct
{
  bool         used;
  uint8_t      heard;      /**< autres émetteurs de la même trame entendus */
  uint32_t     due_ms;     /**< instant prévu de l'émission */
  mesh_frame_t f;          /**< trame à émettre (TTL, relais, émetteur déjà mis à jour) */
} mesh_pending_t;

/** Une trame dans la file d'émission. */
typedef struct
{
  bool         relay;      /**< relais (true) ou trame de ce nœud (false) */
  mesh_frame_t f;
} mesh_txq_item_t;

/** Émetteurs retenus par trame déjà vue (voir mesh_cache_entry_t). */
#define MESH_CACHE_FROM_MAX    4U

/** Une trame déjà vue (mémoire anti-doublons). On retient aussi les
 *  émetteurs (champ « last ») dont on l'a reçue : chaque émission est
 *  répétée à 3 ou 4 évènements de publicité, et une copie venant d'un
 *  émetteur déjà entendu n'est qu'une répétition de la même émission, pas
 *  le relais d'un autre nœud. */
typedef struct
{
  bool     used;
  uint8_t  src;
  uint8_t  seq;
  uint8_t  from_count;     /**< émetteurs retenus dans from[] */
  uint8_t  from[MESH_CACHE_FROM_MAX]; /**< émetteurs déjà entendus */
  uint32_t t_ms;           /**< instant de la première réception */
} mesh_cache_entry_t;

/** Toutes les variables d'un nœud. Les champs se lisent et se modifient
 *  par les fonctions ci-dessous (mesh_xxx). */
typedef struct
{
  const mesh_ops_t  *ops;
  mesh_settings_t    set;
  uint8_t            net;          /**< numéro de réseau */
  uint8_t            id;           /**< numéro de ce nœud */
  uint8_t            prev_id;      /**< numéro précédent (après un changement) */
  bool               id_fixed;     /**< numéro choisi par l'utilisateur */
  uint8_t            id_attempt;   /**< changements automatiques déjà faits */
  uint32_t           tag;          /**< étiquette de la carte (identifiant unique) */
  uint8_t            seq;          /**< dernier numéro d'ordre utilisé */
  uint16_t           groups;       /**< groupes suivis : bit g-1 pour le groupe g */
  uint8_t            blocked[32];  /**< émetteurs bloqués : 1 bit par numéro (256 bits) */
  uint32_t           now;          /**< dernier temps connu, en ms */
  uint32_t           next_hello_ms;
  uint32_t           tx_retry_ms;
  uint8_t            tx_attempts;
  mesh_cache_entry_t cache[MESH_CACHE_SIZE];
  mesh_pending_t     pending[MESH_PENDING_MAX];
  mesh_txq_item_t    txq[MESH_TXQ_LEN];
  uint8_t            txq_head;
  uint8_t            txq_count;
  mesh_neighbor_t    neighbors[MESH_NEIGHBORS_MAX];
  mesh_node_t        nodes[MESH_NODES_MAX];
  mesh_stats_t       stats;
} mesh_t;

/** Résultat de mesh_send(). */
typedef enum
{
  MESH_OK = 0,
  MESH_ERR_DEST,           /**< destination invalide, ou ce nœud lui-même */
  MESH_ERR_TOO_LONG,       /**< plus de MESH_PAYLOAD_MAX octets de données */
  MESH_ERR_QUEUE_FULL      /**< file d'émission pleine : réessayer plus tard */
} mesh_result_t;

/* ========================================================================== */
/* Fonctions                                                                  */
/* ========================================================================== */

/**
 * @brief  Prépare un nœud : réglages par défaut (app_config.h), tables
 *         vides, numéro tiré de l'étiquette (voir mesh_id_from_tag).
 * @param  m    nœud
 * @param  ops  fonctions de la radio et de l'application (gardées par adresse)
 * @param  tag  étiquette de la carte : nombre de 32 bits propre à chaque
 *              carte (sur l'ObCP, calculé à partir de l'identifiant unique
 *              du STM32)
 * @param  now  temps actuel, en ms
 */
void mesh_init(mesh_t *m, const mesh_ops_t *ops, uint32_t tag, uint32_t now);

/**
 * @brief  Numéro de nœud (1 à 239) déduit d'une étiquette. attempt = 0 donne
 *         le numéro de départ ; 1, 2... d'autres numéros, essayés après un
 *         conflit.
 */
uint8_t mesh_id_from_tag(uint32_t tag, uint8_t attempt);

/**
 * @brief  Émet une trame créée par ce nœud.
 * @param  m        nœud
 * @param  dst      destination (nœud, groupe ou MESH_ADDR_ALL)
 * @param  type     type de message (MESH_T_xxx)
 * @param  data     données (peut être NULL si len = 0)
 * @param  len      nombre d'octets de données (MESH_PAYLOAD_MAX au plus)
 * @param  ttl      TTL, ou MESH_TTL_USE_DEFAULT pour celui des réglages
 * @param  seq_out  reçoit le numéro d'ordre de la trame (peut être NULL)
 * @retval MESH_OK si la trame attend son tour dans la file d'émission
 */
mesh_result_t mesh_send(mesh_t *m, uint8_t dst, uint8_t type, const uint8_t *data,
                        uint8_t len, uint8_t ttl, uint8_t *seq_out);

/**
 * @brief  Traite une trame reçue par la radio (voir le schéma plus haut).
 * @param  m     nœud
 * @param  pdu   trame codée (sans l'emballage de la publicité)
 * @param  len   nombre d'octets
 * @param  rssi  puissance de réception, en dBm
 * @param  now   temps actuel, en ms
 */
void mesh_on_pdu(mesh_t *m, const uint8_t *pdu, uint8_t len, int8_t rssi, uint32_t now);

/**
 * @brief  À appeler souvent (à chaque tour de boucle) : relais arrivés à
 *         échéance, HELLO périodiques, émission de la trame suivante dès
 *         que la radio est libre.
 */
void mesh_process(mesh_t *m, uint32_t now);

/* ---- Identité ------------------------------------------------------------- */

/** Numéro de ce nœud. */
uint8_t mesh_id(const mesh_t *m);
/** Numéro précédent (après MESH_EV_ID_CHANGED). */
uint8_t mesh_previous_id(const mesh_t *m);
/** Vrai si le numéro a été choisi par l'utilisateur (mesh_set_id fixed). */
bool mesh_id_is_fixed(const mesh_t *m);
/** Étiquette de la carte. */
uint32_t mesh_tag(const mesh_t *m);

/**
 * @brief  Change le numéro du nœud.
 * @param  id     nouveau numéro (1 à 239), ou 0 pour revenir au numéro
 *                automatique tiré de l'étiquette
 * @param  fixed  true : numéro choisi par l'utilisateur, jamais changé
 *                automatiquement en cas de conflit
 * @retval false si id n'est pas un numéro de nœud
 */
bool mesh_set_id(mesh_t *m, uint8_t id, bool fixed);

/** Numéro de réseau. */
uint8_t mesh_net(const mesh_t *m);
/** Change de réseau : les tables (mémoire, voisins, nœuds, files) sont vidées. */
void mesh_set_net(mesh_t *m, uint8_t net);

/* ---- Réglages ------------------------------------------------------------- */

/** Réglages actuels (lecture seule ; les modifier par les fonctions suivantes). */
const mesh_settings_t *mesh_settings(const mesh_t *m);
/** TTL des trames émises (limité à MESH_TTL_MAX). */
void mesh_set_ttl(mesh_t *m, uint8_t ttl);
/** Active ou désactive le relais des trames des autres. */
void mesh_set_relay(mesh_t *m, bool on);
/** Seuil de suppression des relais (0 : jamais). */
void mesh_set_suppress(mesh_t *m, uint8_t n);
/** Seuil de RSSI (dBm), MESH_RSSI_MIN_OFF pour aucun. */
void mesh_set_rssi_min(mesh_t *m, int8_t dbm);
/** Période des HELLO en secondes (0 : aucun). Le prochain part bientôt. */
void mesh_set_hello_period(mesh_t *m, uint8_t seconds);
/** Envoie un HELLO dans les prochaines centaines de ms. */
void mesh_hello_soon(mesh_t *m);

/* ---- Groupes et émetteurs bloqués ------------------------------------------ */

/** Rejoint (join = true) ou quitte le groupe g (1 à 15). */
bool mesh_group_set(mesh_t *m, uint8_t g, bool join);
/** Vrai si le nœud suit le groupe g. */
bool mesh_group_has(const mesh_t *m, uint8_t g);
/** Bloque ou débloque un émetteur : ses trames (et ses relais) sont ignorées. */
void mesh_block_set(mesh_t *m, uint8_t id, bool blocked);
/** Vrai si l'émetteur id est bloqué. */
bool mesh_is_blocked(const mesh_t *m, uint8_t id);
/** Débloque tous les émetteurs. */
void mesh_block_clear(mesh_t *m);

/* ---- Tables et compteurs --------------------------------------------------- */

/**
 * @brief  Voisin n° index de la table (cases libres et voisins trop anciens
 *         sautés). Pour parcourir : for (i = 0; mesh_neighbor_at(m, i, &n); i++).
 * @retval false quand il n'y en a plus
 */
bool mesh_neighbor_at(const mesh_t *m, uint8_t index, mesh_neighbor_t *out);

/** Nœud connu n° index (même principe que mesh_neighbor_at). */
bool mesh_node_at(const mesh_t *m, uint8_t index, mesh_node_t *out);

/** Compteurs. */
const mesh_stats_t *mesh_stats(const mesh_t *m);
/** Remet les compteurs à zéro. */
void mesh_stats_clear(mesh_t *m);

/** Nombre de trames dans la file d'émission (relais en attente non compris). */
uint8_t mesh_tx_queue_count(const mesh_t *m);

#endif /* MESH_H */
