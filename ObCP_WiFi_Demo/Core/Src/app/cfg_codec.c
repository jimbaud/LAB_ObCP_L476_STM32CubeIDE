/**
 ******************************************************************************
 * @file    cfg_codec.c
 * @brief   Configuration de la démo (voir cfg_codec.h).
 *
 *  La table s_items[] décrit chaque paramètre : sa clé (« wifi.ssid »), son
 *  type, sa position dans la structure cfg_t (offsetof) et sa taille, ses
 *  bornes et son texte d'aide. Les fonctions cfg_set(), cfg_get(),
 *  cfg_sanitize() parcourent cette table : aucun code n'est à écrire par
 *  paramètre, il suffit d'ajouter une ligne.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "cfg_codec.h"
#include "app_config.h"
#include "util.h"

/* Position (offsetof) et taille d'un champ de cfg_t, pour la table */
#define FIELD(m)   (uint16_t)offsetof(cfg_t, m), (uint16_t)sizeof(((cfg_t *)0)->m)

/* Table des paramètres (ordre d'affichage de la commande « config ») */
static const cfg_item_t s_items[] =
{
  /* clé             type       champ             min    max    secret aide */
  { "wifi.ssid",    CFG_T_STR,  FIELD(sta_ssid),     0,    32, false, "SSID du réseau Wi-Fi (box, routeur...)" },
  { "wifi.pass",    CFG_T_STR,  FIELD(sta_pass),     0,    64, true,  "mot de passe Wi-Fi (vide = réseau ouvert)" },
  { "wifi.auto",    CFG_T_BOOL, FIELD(sta_auto),     0,     1, false, "connexion au démarrage + reconnexion automatique" },
  { "wifi.country", CFG_T_STR,  FIELD(country),      2,     2, false, "pays : EU, US, JP, CN ou 00 (mondial)" },
  { "ip.dhcp",      CFG_T_BOOL, FIELD(dhcp),         0,     1, false, "1 = DHCP, 0 = adresse IP fixe" },
  { "ip.addr",      CFG_T_IP,   FIELD(ip),           0,     0, false, "adresse IP fixe" },
  { "ip.mask",      CFG_T_IP,   FIELD(mask),         0,     0, false, "masque de sous-réseau (IP fixe)" },
  { "ip.gw",        CFG_T_IP,   FIELD(gw),           0,     0, false, "passerelle (IP fixe)" },
  { "ip.dns",       CFG_T_IP,   FIELD(dns),          0,     0, false, "serveur DNS (IP fixe ; 0.0.0.0 = passerelle)" },
  { "net.hostname", CFG_T_STR,  FIELD(hostname),     0,    32, false, "nom d'hôte DHCP (vide = " HOSTNAME_PREFIX "<id>)" },
  { "time.server",  CFG_T_STR,  FIELD(ntp_server),   1,    47, false, "serveur de temps NTP" },
  { "time.tz",      CFG_T_INT,  FIELD(tz_min),    -720,   840, false, "fuseau horaire en minutes (60 = Paris)" },
  { "time.dst",     CFG_T_BOOL, FIELD(dst_eu),       0,     1, false, "heure d'été européenne automatique" },
  { "time.auto",    CFG_T_BOOL, FIELD(ntp_auto),     0,     1, false, "synchronisation NTP à chaque connexion" },
  { "ap.ssid",      CFG_T_STR,  FIELD(ap_ssid),      0,    32, false, "SSID du point d'accès (vide = " AP_SSID_PREFIX "<id>)" },
  { "ap.pass",      CFG_T_STR,  FIELD(ap_pass),      0,    63, true,  "mot de passe du point d'accès (vide = ouvert, sinon 8 car. min.)" },
  { "ap.channel",   CFG_T_INT,  FIELD(ap_channel),   1,    13, false, "canal du point d'accès (1 à 13)" },
  { "ap.ip",        CFG_T_IP,   FIELD(ap_ip),        0,     0, false, "adresse du point d'accès (x.y.z.1)" },
  { "web.port",     CFG_T_INT,  FIELD(web_port),     1, 65535, false, "port du serveur web" },
  { "web.auto",     CFG_T_BOOL, FIELD(web_auto),     0,     1, false, "serveur web et point d'accès lancés à la mise sous tension" },
  { "mqtt.host",    CFG_T_STR,  FIELD(mqtt_host),    1,    63, false, "adresse du broker MQTT" },
  { "mqtt.port",    CFG_T_INT,  FIELD(mqtt_port),    1, 65535, false, "port du broker (1883 = MQTT sans TLS)" },
  { "mqtt.user",    CFG_T_STR,  FIELD(mqtt_user),    0,    31, false, "identifiant MQTT (vide = sans authentification)" },
  { "mqtt.pass",    CFG_T_STR,  FIELD(mqtt_pass),    0,    63, true,  "mot de passe MQTT" },
  { "mqtt.topic",   CFG_T_STR,  FIELD(mqtt_topic),   0,    63, false, "racine des topics (vide = " MQTT_TOPIC_PREFIX "<id>)" },
  { "mqtt.period",  CFG_T_INT,  FIELD(mqtt_period),  2,  3600, false, "période de télémétrie (s)" },
  { "mqtt.auto",    CFG_T_BOOL, FIELD(mqtt_auto),    0,     1, false, "connexion MQTT automatique après le Wi-Fi" },
  { "scan.period",  CFG_T_INT,  FIELD(track_period), 200, 60000, false, "période du suivi RSSI (ms)" },
  { "term.color",   CFG_T_BOOL, FIELD(color),        0,     1, false, "couleurs ANSI dans le terminal" },
};

#define N_ITEMS   (sizeof(s_items) / sizeof(s_items[0]))

/* Voir cfg_codec.h */
void cfg_defaults(cfg_t *c)
{
  static const uint8_t ap_ip[4] = DEF_AP_IP;

  memset(c, 0, sizeof(*c));
  c->sta_auto = 1U;
  (void)util_strlcpy(c->country, DEF_COUNTRY, sizeof(c->country));
  c->dhcp = 1U;
  c->ip[0] = 192U; c->ip[1] = 168U; c->ip[2] = 1U; c->ip[3] = 50U;
  c->mask[0] = 255U; c->mask[1] = 255U; c->mask[2] = 255U; c->mask[3] = 0U;
  c->gw[0] = 192U; c->gw[1] = 168U; c->gw[2] = 1U; c->gw[3] = 1U;
  (void)util_strlcpy(c->ntp_server, DEF_NTP_SERVER, sizeof(c->ntp_server));
  c->tz_min = DEF_TZ_MIN;
  c->dst_eu = DEF_DST_EU;
  c->ntp_auto = 1U;
  (void)util_strlcpy(c->ap_pass, DEF_AP_PASS, sizeof(c->ap_pass));
  c->ap_channel = DEF_AP_CHANNEL;
  memcpy(c->ap_ip, ap_ip, 4);
  c->web_port = DEF_WEB_PORT;
  (void)util_strlcpy(c->mqtt_host, DEF_MQTT_HOST, sizeof(c->mqtt_host));
  c->mqtt_port = DEF_MQTT_PORT;
  c->mqtt_period = DEF_MQTT_PERIOD_S;
  c->mqtt_auto = 0U;
  c->track_period = DEF_TRACK_PERIOD_MS;
  c->color = 1U;
  c->web_auto = 0U;
}

/* Voir cfg_codec.h */
const cfg_item_t *cfg_items(size_t *count)
{
  *count = N_ITEMS;
  return s_items;
}

/* Voir cfg_codec.h */
const cfg_item_t *cfg_find(const char *key)
{
  size_t i;
  for (i = 0U; i < N_ITEMS; i++)
  {
    if (util_strieq(key, s_items[i].key))
    {
      return &s_items[i];
    }
  }
  return NULL;
}

/* Lit un champ entier de 8 ou 16 bits, signé si sa borne minimale est négative */
static int32_t get_int(const cfg_t *c, const cfg_item_t *it)
{
  const uint8_t *p = (const uint8_t *)c + it->offset;
  if (it->size == 1U)
  {
    return *p;
  }
  if (it->min < 0)
  {
    int16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
  }
  {
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
  }
}

/* Écrit un champ entier de 8 ou 16 bits (pendant de get_int()) */
static void put_int(cfg_t *c, const cfg_item_t *it, int32_t v)
{
  uint8_t *p = (uint8_t *)c + it->offset;
  if (it->size == 1U)
  {
    *p = (uint8_t)v;
  }
  else if (it->min < 0)
  {
    int16_t s = (int16_t)v;
    memcpy(p, &s, sizeof(s));
  }
  else
  {
    uint16_t u = (uint16_t)v;
    memcpy(p, &u, sizeof(u));
  }
}

/* Texte sans guillemet ni caractère de contrôle (il serait recopié dans des
 * commandes AT entre guillemets)                                       */
static bool str_chars_ok(const char *s)
{
  for (; *s != '\0'; s++)
  {
    if ((*s == '"') || ((unsigned char)*s < 0x20U) || (*s == 0x7F))
    {
      return false;
    }
  }
  return true;
}

/* Voir cfg_codec.h : contrôle selon le type, puis contrôles particuliers
 * (pays, mot de passe WPA2 de 8 caractères, pas de joker MQTT...)     */
int cfg_set(cfg_t *c, const char *key, const char *value)
{
  const cfg_item_t *it = cfg_find(key);
  uint8_t          *field;

  if (it == NULL)
  {
    return CFG_ERR_KEY;
  }
  field = (uint8_t *)c + it->offset;

  switch (it->type)
  {
    case CFG_T_STR:
    {
      size_t n = strlen(value);
      if (!str_chars_ok(value))
      {
        return CFG_ERR_CHARS;
      }
      if ((n < (size_t)it->min) || (n > (size_t)it->max) || (n >= it->size))
      {
        return CFG_ERR_RANGE;
      }
      if (strcmp(it->key, "wifi.country") == 0)
      {
        char up[3] = { (char)toupper((unsigned char)value[0]), (char)toupper((unsigned char)value[1]), '\0' };
        if ((strcmp(up, "EU") != 0) && (strcmp(up, "US") != 0) && (strcmp(up, "JP") != 0) &&
            (strcmp(up, "CN") != 0) && (strcmp(up, "00") != 0))
        {
          return CFG_ERR_VALUE;
        }
        memcpy(field, up, 3);
        return CFG_OK;
      }
      if ((strcmp(it->key, "ap.pass") == 0) && (n > 0U) && (n < 8U))
      {
        return CFG_ERR_RANGE;          /* WPA2 : 8 caractères minimum */
      }
      if ((strcmp(it->key, "mqtt.topic") == 0) && ((strchr(value, '+') != NULL) || (strchr(value, '#') != NULL)))
      {
        return CFG_ERR_CHARS;          /* pas de joker dans la racine */
      }
      memcpy(field, value, n + 1U);
      return CFG_OK;
    }

    case CFG_T_BOOL:
    {
      bool b;
      if (!util_parse_bool(value, &b))
      {
        return CFG_ERR_VALUE;
      }
      *field = b ? 1U : 0U;
      return CFG_OK;
    }

    case CFG_T_INT:
    {
      long v;
      if (!util_parse_long(value, &v))
      {
        return CFG_ERR_VALUE;
      }
      if ((v < it->min) || (v > it->max))
      {
        return CFG_ERR_RANGE;
      }
      put_int(c, it, (int32_t)v);
      return CFG_OK;
    }

    case CFG_T_IP:
    {
      uint8_t ip[4];
      if (!util_parse_ip(value, ip))
      {
        return CFG_ERR_VALUE;
      }
      if (strcmp(it->key, "ap.ip") == 0)
      {
        ip[3] = 1U;                    /* le point d'accès est toujours en x.y.z.1 */
      }
      memcpy(field, ip, 4);
      return CFG_OK;
    }

    default:
      return CFG_ERR_KEY;
  }
}

/* Voir cfg_codec.h */
void cfg_get(const cfg_t *c, const cfg_item_t *it, char *out, size_t len, bool masked)
{
  const uint8_t *field = (const uint8_t *)c + it->offset;

  switch (it->type)
  {
    case CFG_T_STR:
      if (masked && it->secret && (field[0] != 0U))
      {
        (void)util_strlcpy(out, "********", len);
      }
      else
      {
        (void)util_strlcpy(out, (const char *)field, len);
      }
      break;
    case CFG_T_BOOL:
      (void)util_strlcpy(out, (*field != 0U) ? "1" : "0", len);
      break;
    case CFG_T_INT:
      (void)snprintf(out, len, "%ld", (long)get_int(c, it));
      break;
    case CFG_T_IP:
      util_fmt_ip(out, len, field);
      break;
    default:
      (void)util_strlcpy(out, "?", len);
      break;
  }
}

/* Voir cfg_codec.h */
const char *cfg_err_str(int err)
{
  switch (err)
  {
    case CFG_OK:        return "OK";
    case CFG_ERR_KEY:   return "clé inconnue (voir « config »)";
    case CFG_ERR_VALUE: return "valeur invalide";
    case CFG_ERR_RANGE: return "valeur hors limites ou longueur incorrecte";
    case CFG_ERR_CHARS: return "caractère interdit (guillemet, caractère de contrôle ou joker MQTT)";
    default:            return "?";
  }
}

/* Voir cfg_codec.h : un champ invalide reprend sa valeur d'usine */
void cfg_sanitize(cfg_t *c)
{
  size_t   i;
  cfg_t    def;
  bool     have_def = false;

  for (i = 0U; i < N_ITEMS; i++)
  {
    const cfg_item_t *it = &s_items[i];
    uint8_t          *field = (uint8_t *)c + it->offset;

    if (it->type == CFG_T_STR)
    {
      field[it->size - 1U] = 0U;                       /* chaîne toujours terminée */
      if (!str_chars_ok((const char *)field) || (strlen((const char *)field) < (size_t)it->min))
      {
        if (!have_def)
        {
          cfg_defaults(&def);
          have_def = true;
        }
        memcpy(field, (const uint8_t *)&def + it->offset, it->size);
      }
    }
    else if (it->type == CFG_T_INT)
    {
      int32_t v = get_int(c, it);
      if ((v < it->min) || (v > it->max))
      {
        if (!have_def)
        {
          cfg_defaults(&def);
          have_def = true;
        }
        put_int(c, it, get_int(&def, it));
      }
    }
    else if (it->type == CFG_T_BOOL)
    {
      *field = (*field != 0U) ? 1U : 0U;
    }
    else
    {
      /* adresses IP : toute valeur est acceptable */
    }
  }
  c->ap_ip[3] = 1U;
}

/* ---- Format de stockage ---------------------------------------------------------- */
/* En-tête de 16 octets placé devant la structure en flash */
typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t crc;
  uint32_t reserved;
} cfg_hdr_t;

/* Voir cfg_codec.h. Le reste du tampon est à 0xFF, valeur de la flash effacée */
size_t cfg_pack(const cfg_t *c, uint8_t *buf, size_t len)
{
  cfg_hdr_t h;
  size_t    total = sizeof(h) + sizeof(*c);

  total = (total + 7U) & ~(size_t)7U;                    /* double mot (flash STM32L4) */
  if ((len < total) || (total > CFG_BLOB_MAX))
  {
    return 0U;
  }
  memset(buf, 0xFF, total);
  h.magic = CFG_MAGIC;
  h.version = CFG_VERSION;
  h.size = (uint16_t)sizeof(*c);
  h.crc = util_crc32(c, sizeof(*c));
  h.reserved = 0U;
  memcpy(buf, &h, sizeof(h));
  memcpy(&buf[sizeof(h)], c, sizeof(*c));
  return total;
}

/* Voir cfg_codec.h */
int cfg_unpack(cfg_t *c, const uint8_t *buf, size_t len)
{
  cfg_hdr_t h;

  if (len < sizeof(h))
  {
    return CFG_UNPACK_EMPTY;
  }
  memcpy(&h, buf, sizeof(h));
  if (h.magic != CFG_MAGIC)
  {
    return CFG_UNPACK_EMPTY;
  }
  /* Format plus récent que le programme, ou taille incohérente : refusé */
  if ((h.version == 0U) || (h.version > CFG_VERSION) || (h.size == 0U) || (h.size > sizeof(*c)) ||
      ((h.version == CFG_VERSION) && (h.size != sizeof(*c))) || (len < (sizeof(h) + h.size)))
  {
    return CFG_UNPACK_VERSION;
  }
  if (util_crc32(&buf[sizeof(h)], h.size) != h.crc)
  {
    return CFG_UNPACK_CRC;
  }
  /* Valeurs d'usine d'abord, puis les champs enregistrés par-dessus : une
   * configuration d'une version plus ancienne (plus courte) est ainsi complétée. */
  cfg_defaults(c);
  memcpy(c, &buf[sizeof(h)], h.size);
  if (h.version < 2U)
  {
    c->web_auto = 0U;                     /* champ ajouté dans la version 2 */
  }
  cfg_sanitize(c);
  return CFG_UNPACK_OK;
}
