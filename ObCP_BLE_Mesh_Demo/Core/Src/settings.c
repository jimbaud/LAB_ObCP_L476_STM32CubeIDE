/**
 ******************************************************************************
 * @file    settings.c
 * @brief   Description des réglages (voir settings.h).
 ******************************************************************************
 */
#include "settings.h"

#include <stdio.h>
#include <string.h>

/* Description d'un réglage : nom, limites, unité, aide. */
typedef struct
{
  const char *name;
  int32_t     min;
  int32_t     max;
  const char *unit;      /* "" : sans unité ; NULL : réglage on / off */
  const char *help;
} setting_desc_t;

/* Dans l'ordre de setting_t (la case 0 n'est pas un réglage). */
static const setting_desc_t k_settings[SETTING_END] =
{
  [0]                = { "?",         0,    0,   "",    "" },
  [SETTING_TTL]      = { "ttl",       0,    15,  "",    "0 à 15" },
  [SETTING_RELAY]    = { "relais",    0,    1,   NULL,  "on ou off" },
  [SETTING_SUPPRESS] = { "suppr",     0,    10,  "",    "0 (jamais) à 10" },
  [SETTING_RSSI_MIN] = { "rssimin",   -100, -20, "dBm", "-100 à -20 (dBm), ou off" },
  [SETTING_TX_POWER] = { "puissance", -18,  8,   "dBm", "-18, -15, -11, -8, -5, -2, 2, 5 ou 8 (dBm)" },
  [SETTING_HELLO]    = { "hello",     0,    255, "s",   "0 (aucun) à 255 (s)" },
  [SETTING_AUTO]     = { "auto",      0,    255, "s",   "0 (aucun) à 255 (s)" },
  [SETTING_FLASH]    = { "flash",     0,    1,   NULL,  "on ou off" }
};

/* Vrai si key désigne un réglage de la table. */
static bool valid_key(setting_t key)
{
  return (key >= SETTING_TTL) && (key < SETTING_END);
}

/* Voir settings.h */
const char *settings_name(setting_t key)
{
  return valid_key(key) ? k_settings[key].name : "?";
}

/* Voir settings.h */
bool settings_from_name(const char *name, setting_t *key)
{
  for (int k = SETTING_TTL; k < SETTING_END; k++)
  {
    if (strcmp(name, k_settings[k].name) == 0)
    {
      *key = (setting_t)k;
      return true;
    }
  }
  return false;
}

/* Voir settings.h */
bool settings_is_boolean(setting_t key)
{
  return valid_key(key) && (k_settings[key].unit == NULL);
}

/* Voir settings.h */
bool settings_value_ok(setting_t key, int32_t value)
{
  if (!valid_key(key))
  {
    return false;
  }
  if ((key == SETTING_RSSI_MIN) && (value == SETTING_RSSI_OFF))
  {
    return true;
  }
  return (value >= k_settings[key].min) && (value <= k_settings[key].max);
}

/* Lit un entier décimal, signe « - » permis, 4 chiffres au plus. */
static bool parse_int(const char *text, int32_t *value)
{
  int32_t v        = 0;
  bool    negative = false;
  size_t  digits   = 0U;

  if (*text == '-')
  {
    negative = true;
    text++;
  }
  while (*text != '\0')
  {
    if ((*text < '0') || (*text > '9') || (digits >= 4U))
    {
      return false;
    }
    v = (v * 10) + (int32_t)(*text - '0');
    digits++;
    text++;
  }
  if (digits == 0U)
  {
    return false;
  }
  *value = negative ? -v : v;
  return true;
}

/* Voir settings.h */
bool settings_parse_value(setting_t key, const char *text, int32_t *value)
{
  int32_t v;

  if (!valid_key(key))
  {
    return false;
  }
  if (settings_is_boolean(key))
  {
    if ((strcmp(text, "on") == 0) || (strcmp(text, "oui") == 0) || (strcmp(text, "1") == 0))
    {
      *value = 1;
      return true;
    }
    if ((strcmp(text, "off") == 0) || (strcmp(text, "non") == 0) || (strcmp(text, "0") == 0))
    {
      *value = 0;
      return true;
    }
    return false;
  }
  if ((key == SETTING_RSSI_MIN) && ((strcmp(text, "off") == 0) || (strcmp(text, "aucun") == 0)))
  {
    *value = SETTING_RSSI_OFF;
    return true;
  }
  if (!parse_int(text, &v) || !settings_value_ok(key, v))
  {
    return false;
  }
  *value = v;
  return true;
}

/* Voir settings.h */
int settings_format_value(setting_t key, int32_t value, char *text, unsigned size)
{
  if (settings_is_boolean(key))
  {
    return snprintf(text, size, "%s", (value != 0) ? "on" : "off");
  }
  if ((key == SETTING_RSSI_MIN) && (value == SETTING_RSSI_OFF))
  {
    return snprintf(text, size, "off");
  }
  if ((key == SETTING_HELLO || key == SETTING_AUTO) && (value == 0))
  {
    return snprintf(text, size, "0 (aucun)");
  }
  if (valid_key(key) && (k_settings[key].unit[0] != '\0'))
  {
    return snprintf(text, size, "%ld %s", (long)value, k_settings[key].unit);
  }
  return snprintf(text, size, "%ld", (long)value);
}

/* Voir settings.h */
const char *settings_help(setting_t key)
{
  return valid_key(key) ? k_settings[key].help : "";
}
