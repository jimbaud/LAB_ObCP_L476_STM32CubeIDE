/**
 ******************************************************************************
 * @file    cfg_codec.h
 * @brief   Configuration de la démo : structure, valeurs d'usine, accès par
 *          nom de clé (commandes « set » / « config ») et format de stockage
 *          en flash (en-tête + CRC32). Indépendant du matériel (testé sur PC).
 *
 *  Ajouter un paramètre (exemple : un seuil d'alarme) :
 *    1. ajouter le champ À LA FIN de cfg_t (les configurations déjà
 *       enregistrées restent lisibles) ;
 *    2. lui donner sa valeur d'usine dans cfg_defaults() (cfg_codec.c) ;
 *    3. ajouter une ligne dans la table s_items[] (clé, type, bornes, aide) :
 *       les commandes set / config le gèrent alors automatiquement ;
 *    4. augmenter CFG_VERSION et, dans cfg_unpack(), donner sa valeur
 *       d'usine au champ quand la configuration lue est plus ancienne.
 ******************************************************************************
 */
#ifndef CFG_CODEC_H
#define CFG_CODEC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Tous les paramètres de la démo (variable globale g_cfg, config.c). */
typedef struct
{
  /* Wi-Fi station */
  char     sta_ssid[33];
  char     sta_pass[65];
  uint8_t  sta_auto;          /* connexion au démarrage + reconnexion auto */
  char     country[3];
  /* IP */
  uint8_t  dhcp;
  uint8_t  ip[4];
  uint8_t  mask[4];
  uint8_t  gw[4];
  uint8_t  dns[4];            /* 0.0.0.0 = passerelle (IP fixe) / DHCP */
  char     hostname[33];
  /* Heure */
  char     ntp_server[48];
  int16_t  tz_min;
  uint8_t  dst_eu;
  uint8_t  ntp_auto;
  /* Point d'accès + serveur web */
  char     ap_ssid[33];
  char     ap_pass[65];
  uint8_t  ap_channel;
  uint8_t  ap_ip[4];
  uint16_t web_port;
  /* MQTT */
  char     mqtt_host[64];
  uint16_t mqtt_port;
  char     mqtt_user[32];
  char     mqtt_pass[64];
  char     mqtt_topic[64];
  uint16_t mqtt_period;       /* s */
  uint8_t  mqtt_auto;
  /* Divers */
  uint16_t track_period;      /* ms */
  uint8_t  color;
  /* Ajouts de la version 2 du format (programme 1.2.0) : toujours à la fin */
  uint8_t  web_auto;          /* serveur web démarré à la mise sous tension */
} cfg_t;

/** Type d'un paramètre (détermine l'analyse de la valeur tapée). */
typedef enum
{
  CFG_T_STR = 0,       /* texte                         */
  CFG_T_BOOL,          /* 0 / 1, on / off, oui / non    */
  CFG_T_INT,           /* entier borné (8 ou 16 bits)   */
  CFG_T_IP             /* adresse IPv4 a.b.c.d          */
} cfg_type_t;

/** Description d'un paramètre : une ligne de la table s_items[] (cfg_codec.c). */
typedef struct
{
  const char *key;
  cfg_type_t  type;
  uint16_t    offset;
  uint16_t    size;          /* taille du champ (octets)          */
  int32_t     min;           /* bornes (entiers) ou longueur min   */
  int32_t     max;
  bool        secret;        /* valeur masquée à l'affichage      */
  const char *help;
} cfg_item_t;

/* Codes de retour de cfg_set() */
#define CFG_OK            0
#define CFG_ERR_KEY      (-1)   /* clé inconnue                       */
#define CFG_ERR_VALUE    (-2)   /* valeur invalide (format)           */
#define CFG_ERR_RANGE    (-3)   /* hors bornes / trop long            */
#define CFG_ERR_CHARS    (-4)   /* caractère interdit (" ou contrôle) */

/** @brief  Remplit c avec les valeurs d'usine (app_config.h). */
void              cfg_defaults(cfg_t *c);

/** @brief  Table de tous les paramètres ; *count reçoit leur nombre. */
const cfg_item_t *cfg_items(size_t *count);

/** @brief  Description d'un paramètre par sa clé (majuscules indifférentes), NULL si inconnu. */
const cfg_item_t *cfg_find(const char *key);

/**
 * @brief  Modifie un paramètre à partir de sa valeur en texte (commande « set »).
 * @param  c      configuration à modifier.
 * @param  key    clé, par exemple "wifi.ssid".
 * @param  value  valeur tapée, contrôlée selon le type et les bornes.
 * @retval CFG_OK, ou CFG_ERR_KEY / CFG_ERR_VALUE / CFG_ERR_RANGE / CFG_ERR_CHARS.
 */
int               cfg_set(cfg_t *c, const char *key, const char *value);

/**
 * @brief  Valeur d'un paramètre en texte.
 * @param  masked  true : les secrets (mots de passe) sont remplacés par des '*'.
 */
void              cfg_get(const cfg_t *c, const cfg_item_t *it, char *out, size_t len, bool masked);

/** @brief  Texte en français d'un code CFG_ERR_xxx. */
const char       *cfg_err_str(int err);

/** @brief  Remet en ordre une configuration lue (chaînes terminées, valeurs dans leurs bornes). */
void              cfg_sanitize(cfg_t *c);

/* ---- Stockage : en-tête de 16 octets + structure, complété à 8 octets ---- */
#define CFG_MAGIC        0x43373657UL     /* « W67C » */
/* Version du format : 1 = programmes 1.0 et 1.1 ; 2 = ajout de web.auto (1.2).
 * Une configuration enregistrée par une version plus ancienne est relue :
 * les champs ajoutés depuis (en fin de structure) prennent leur valeur
 * d'usine. Une version plus récente que celle du programme est refusée.   */
#define CFG_VERSION      2U
#define CFG_BLOB_MAX     1024U

#define CFG_UNPACK_OK         0
#define CFG_UNPACK_EMPTY     (-1)   /* zone vierge / pas de configuration */
#define CFG_UNPACK_VERSION   (-2)   /* format plus récent que le programme  */
#define CFG_UNPACK_CRC       (-3)   /* données corrompues                 */

/**
 * @brief  Prépare l'image à écrire en flash : en-tête (magic, version, taille,
 *         CRC32) + structure, complétée à un multiple de 8 octets.
 * @retval taille de l'image, 0 si le tampon est trop petit.
 */
size_t cfg_pack(const cfg_t *c, uint8_t *buf, size_t len);

/**
 * @brief  Relit une image enregistrée (contrôle du magic, de la version et du CRC).
 * @retval CFG_UNPACK_OK, CFG_UNPACK_EMPTY, CFG_UNPACK_VERSION ou CFG_UNPACK_CRC.
 */
int    cfg_unpack(cfg_t *c, const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CFG_CODEC_H */
