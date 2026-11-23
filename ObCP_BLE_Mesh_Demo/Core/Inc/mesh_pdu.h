/**
 ******************************************************************************
 * @file    mesh_pdu.h
 * @brief   Format d'une trame du réseau maillé de l'ObCP : champs, adresses,
 *          types de messages, emballage dans une publicité Bluetooth.
 *
 *  OÙ VOYAGE UNE TRAME ?
 *  Le réseau n'utilise pas de connexion Bluetooth : chaque carte « diffuse »
 *  ses trames dans des paquets de publicité (« advertising »), ceux qu'un
 *  téléphone reçoit quand il cherche des appareils, et écoute en permanence
 *  ceux des autres. Un paquet de publicité transporte 31 octets de données,
 *  rangées en champs « longueur, type, données » (AD structures). La trame
 *  est placée dans un champ « données propres au fabricant » (type 0xFF) :
 *
 *    octet  0      1      2  3        4 ...
 *         +------+------+------+------+-----------------------------+
 *         | long.| 0xFF | 0xFF | 0xFF |   trame (10 à 24 octets)    |
 *         +------+------+------+------+-----------------------------+
 *           |      |      identifiant
 *           |      type   d'entreprise 0xFFFF (réservé aux essais)
 *           longueur du champ = 3 + longueur de la trame
 *
 *  LA TRAME : un en-tête de 10 octets, puis 0 à 14 octets de données.
 *
 *    octet  champ    rôle
 *      0    magic    0x4D (« M ») : reconnaît une trame de ce réseau
 *      1    version  0x01 : version du format
 *      2    net      numéro de réseau (0 à 255) : les autres sont ignorés
 *      3    src      origine : numéro du nœud qui a créé la trame
 *      4    dst      destination : un nœud, un groupe ou tous (voir plus bas)
 *      5    seq      numéro d'ordre chez l'origine (0 à 255, puis 0...)
 *      6    ttl      relais encore permis (« Time To Live »)
 *      7    hops     relais déjà traversés (0 : reçue directement de l'origine)
 *      8    last     dernier émetteur : l'origine, ou le dernier relais
 *      9    type     nature du message (MESH_T_xxx)
 *     10..  données  0 à 14 octets, selon le type
 *
 *  Le couple (src, seq) identifie une trame : un nœud qui la reçoit une
 *  seconde fois (par un autre chemin) la reconnaît et l'ignore. Tant que
 *  la trame est relayée, ttl + hops reste égal au TTL choisi par l'origine.
 *  Les nombres de plusieurs octets sont rangés poids faible en premier
 *  (« little endian »), comme dans le reste du Bluetooth.
 *
 *  ADRESSES (champs src, dst, last) :
 *    0          aucune (interdite)
 *    1 à 239    un nœud (numéro de la carte)
 *    240 à 254  groupes 1 à 15 (adresse = 239 + numéro du groupe)
 *    255        tous les nœuds
 *
 *  Exemple : trame HELLO du nœud 5, sur le réseau 0, relayée une fois par
 *  le nœud 9 (données omises) :
 *    4D 01 00 05 FF 2A 03 01 09 01 ...
 *    magic, version, réseau 0, origine 5, pour tous, n° 42, TTL 3, 1 relais
 *    traversé, dernier émetteur 9, type HELLO.
 ******************************************************************************
 */
#ifndef MESH_PDU_H
#define MESH_PDU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Constantes du format ------------------------------------------------- */
#define MESH_MAGIC             0x4DU   /**< 'M' */
#define MESH_VERSION           0x01U
#define MESH_HEADER_LEN        10U     /**< octets d'en-tête */
#define MESH_PAYLOAD_MAX       14U     /**< octets de données au plus */
#define MESH_PDU_MAX           (MESH_HEADER_LEN + MESH_PAYLOAD_MAX)   /**< 24 */

/** Emballage dans la publicité : longueur, type 0xFF, identifiant (2 octets). */
#define MESH_COMPANY_ID        0xFFFFU
#define MESH_ADV_OVERHEAD      4U
#define MESH_ADV_MAX           (MESH_ADV_OVERHEAD + MESH_PDU_MAX)     /**< 28 */

/** Place des champs dans la trame. */
enum
{
  MESH_OFS_MAGIC = 0,
  MESH_OFS_VERSION,
  MESH_OFS_NET,
  MESH_OFS_SRC,
  MESH_OFS_DST,
  MESH_OFS_SEQ,
  MESH_OFS_TTL,
  MESH_OFS_HOPS,
  MESH_OFS_LAST,
  MESH_OFS_TYPE,
  MESH_OFS_PAYLOAD
};

/* ---- Adresses ------------------------------------------------------------- */
#define MESH_ADDR_NONE         0U
#define MESH_ADDR_NODE_MIN     1U
#define MESH_ADDR_NODE_MAX     239U
#define MESH_ADDR_GROUP_FIRST  240U    /**< groupe 1 */
#define MESH_ADDR_GROUP_LAST   254U    /**< groupe 15 */
#define MESH_ADDR_ALL          255U
#define MESH_GROUP_COUNT       15U
/** Adresse du groupe g (1 à 15). */
#define MESH_GROUP_ADDR(g)     ((uint8_t)(239U + (g)))

/* ---- Types de messages ---------------------------------------------------- */
/** Nature du message (champ type) et contenu de ses données. */
typedef enum
{
  /** Présence d'un nœud, envoyée régulièrement à tous.
   *  [0..3] étiquette (32 bits, propre à la carte) ; [4] drapeaux (bit 0 :
   *  relais actif, bit 1 : numéro fixé par l'utilisateur) ; [5] n = nombre
   *  de voisins listés (0 à 4) ; puis n fois (numéro, RSSI en dBm signé). */
  MESH_T_HELLO      = 0x01,
  /** Demande d'écho. [0] numéro du ping. */
  MESH_T_PING       = 0x02,
  /** Réponse à un ping. [0] numéro du ping ; [1] relais traversés par le
   *  ping ; [2] RSSI de réception du ping (dBm signé). */
  MESH_T_PONG       = 0x03,
  /** Recherche de route : chaque relais ajoute son numéro à la fin des
   *  données. [0] numéro de la trace ; [1...] numéros des relais. */
  MESH_T_TRACE      = 0x04,
  /** Retour de la recherche de route (relais enregistrés de même).
   *  [0] numéro de la trace ; [1] k = nombre de relais à l'aller ;
   *  [2..k+1] relais de l'aller ; [k+2...] relais du retour. */
  MESH_T_TRACE_BACK = 0x05,
  /** Texte libre, 1 à 14 octets (UTF-8, sans '\0'). */
  MESH_T_TEXT       = 0x06,
  /** Couleur de la LED. [0] rouge, [1] vert, [2] bleu (0 à 255) ;
   *  [3] durée en secondes (0 : jusqu'à la commande suivante). */
  MESH_T_LED        = 0x07,
  /** Vague : chaque nœud allume sa LED d'une couleur qui dépend du nombre
   *  de relais traversés (rouge = reçue en direct, puis orange, jaune,
   *  vert...). Pas de données. */
  MESH_T_WAVE       = 0x08,
  /** Identification : la LED clignote en blanc. [0] durée en secondes. */
  MESH_T_IDENT      = 0x09,
  /** Demande des mesures. Pas de données. */
  MESH_T_MEASURE_REQ = 0x0A,
  /** Mesures. [0] drapeaux (bit 0 : accélération valable, bit 1 :
   *  température valable) ; [1] température en °C (signé) ; [2..7] X, Y, Z
   *  en mg (entiers signés de 16 bits). */
  MESH_T_MEASURE    = 0x0B,
  /** Réglage à distance. [0] réglage (numéro setting_t, voir settings.h) ;
   *  [1..2] valeur (entier signé de 16 bits). */
  MESH_T_CONFIG     = 0x0C
} mesh_type_t;

/** Drapeaux de HELLO. */
#define MESH_HELLO_RELAY       0x01U
#define MESH_HELLO_FIXED_ID    0x02U
#define MESH_HELLO_NEIGHBORS   4U      /**< voisins listés au plus */
#define MESH_HELLO_LEN(n)      ((uint8_t)(6U + 2U * (n)))

/** Drapeaux de MEASURE. */
#define MESH_MEASURE_ACCEL_OK  0x01U
#define MESH_MEASURE_TEMP_OK   0x02U
#define MESH_MEASURE_LEN       8U

/* ---- Trame décodée -------------------------------------------------------- */
/** Une trame, champ par champ (forme utilisée par le programme). */
typedef struct
{
  uint8_t net;                       /**< numéro de réseau */
  uint8_t src;                       /**< origine */
  uint8_t dst;                       /**< destination */
  uint8_t seq;                       /**< numéro d'ordre chez l'origine */
  uint8_t ttl;                       /**< relais encore permis */
  uint8_t hops;                      /**< relais déjà traversés */
  uint8_t last;                      /**< dernier émetteur */
  uint8_t type;                      /**< MESH_T_xxx */
  uint8_t len;                       /**< nombre d'octets de données */
  uint8_t payload[MESH_PAYLOAD_MAX]; /**< données */
} mesh_frame_t;

/* ---- Codage et décodage --------------------------------------------------- */

/**
 * @brief  Range une trame dans un tableau d'octets (format décrit plus haut).
 * @param  f     trame à coder
 * @param  out   destination
 * @param  size  taille de out (MESH_PDU_MAX suffit toujours)
 * @retval nombre d'octets écrits, 0 si la trame est invalide (données trop
 *         longues) ou si out est trop petit
 */
uint8_t mesh_pdu_encode(const mesh_frame_t *f, uint8_t *out, size_t size);

/**
 * @brief  Lit une trame reçue. Vérifie le magic, la version et la longueur.
 * @param  in   octets reçus
 * @param  len  nombre d'octets
 * @param  f    trame décodée (remplie seulement si la fonction rend true)
 * @retval true si c'est une trame valable de ce réseau maillé
 */
bool mesh_pdu_decode(const uint8_t *in, size_t len, mesh_frame_t *f);

/**
 * @brief  Emballe une trame codée dans des données de publicité Bluetooth
 *         (champ « données du fabricant », voir le schéma plus haut).
 * @retval nombre d'octets écrits dans adv, 0 si la place manque
 */
uint8_t mesh_adv_build(const uint8_t *pdu, uint8_t pdu_len, uint8_t *adv, size_t size);

/**
 * @brief  Cherche une trame du réseau maillé dans les données d'une
 *         publicité reçue (parcourt les champs « longueur, type, données »).
 * @param  adv      données de la publicité (31 octets au plus)
 * @param  adv_len  nombre d'octets
 * @param  pdu      reçoit l'adresse du début de la trame dans adv
 * @param  pdu_len  reçoit la longueur de la trame
 * @retval true si un champ 0xFF, identifiant 0xFFFF, commençant par le
 *         magic 0x4D a été trouvé
 */
bool mesh_adv_find(const uint8_t *adv, size_t adv_len, const uint8_t **pdu, uint8_t *pdu_len);

/* ---- Adresses et noms ----------------------------------------------------- */

/** Vrai pour un numéro de nœud (1 à 239). */
bool mesh_addr_is_node(uint8_t addr);

/** Vrai pour une adresse de groupe (240 à 254). */
bool mesh_addr_is_group(uint8_t addr);

/** Numéro du groupe (1 à 15) d'une adresse de groupe, 0 sinon. */
uint8_t mesh_addr_group(uint8_t addr);

/**
 * @brief  Écrit une adresse sous forme lisible : « 17 », « g3 », « tous ».
 * @retval nombre de caractères écrits (comme snprintf)
 */
int mesh_addr_format(uint8_t addr, char *text, size_t size);

/**
 * @brief  Lit une adresse tapée dans le terminal : « 17 », « g3 », « tous »
 *         (ou « * »). Les numéros de nœud vont de 1 à 239, les groupes de
 *         g1 à g15.
 * @retval true si le texte est une adresse valable (rangée dans *addr)
 */
bool mesh_addr_parse(const char *text, uint8_t *addr);

/**
 * @brief  Nom court d'un type de message : "HELLO", "PING"... ou "?".
 */
const char *mesh_type_name(uint8_t type);

/* ---- Petits outils pour les données --------------------------------------- */

/** Range un entier de 16 bits (poids faible en premier). */
void mesh_put_u16(uint8_t *p, uint16_t v);
/** Lit un entier de 16 bits (poids faible en premier). */
uint16_t mesh_get_u16(const uint8_t *p);
/** Range un entier de 32 bits (poids faible en premier). */
void mesh_put_u32(uint8_t *p, uint32_t v);
/** Lit un entier de 32 bits (poids faible en premier). */
uint32_t mesh_get_u32(const uint8_t *p);

#endif /* MESH_PDU_H */
