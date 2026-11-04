/**
 ******************************************************************************
 * @file    util.c
 * @brief   Fonctions utilitaires (voir util.h, qui décrit chaque fonction).
 *
 *  Toutes ces fonctions sont « pures » : elles ne touchent pas au matériel
 *  et sont vérifiées sur PC par Tests/test_app.c.
 ******************************************************************************
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include "util.h"

/* Voir util.h */
uint32_t util_crc32(const void *data, size_t len)
{
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFUL;
  size_t   i;
  int      k;

  for (i = 0U; i < len; i++)
  {
    crc ^= p[i];
    for (k = 0; k < 8; k++)
    {
      crc = ((crc & 1UL) != 0UL) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
    }
  }
  return crc ^ 0xFFFFFFFFUL;
}

/* Voir util.h */
size_t util_strlcpy(char *dst, const char *src, size_t size)
{
  size_t n = strlen(src);
  if (size > 0U)
  {
    size_t c = (n < (size - 1U)) ? n : (size - 1U);
    memcpy(dst, src, c);
    dst[c] = '\0';
  }
  return n;
}

/* Voir util.h */
bool util_strieq(const char *a, const char *b)
{
  while ((*a != '\0') && (*b != '\0'))
  {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
    {
      return false;
    }
    a++;
    b++;
  }
  return (*a == '\0') && (*b == '\0');
}

/* Voir util.h */
size_t util_utf8_len(const char *s)
{
  size_t n = 0U;
  for (; *s != '\0'; s++)
  {
    if (((unsigned char)*s & 0xC0U) != 0x80U)
    {
      n++;
    }
  }
  return n;
}

/* Voir util.h */
uint32_t util_isqrt(uint64_t v)
{
  uint64_t r = 0U;
  uint64_t bit = (uint64_t)1U << 62;

  while (bit > v)
  {
    bit >>= 2;
  }
  while (bit != 0U)
  {
    if (v >= (r + bit))
    {
      v -= r + bit;
      r = (r >> 1) + bit;
    }
    else
    {
      r >>= 1;
    }
    bit >>= 2;
  }
  return (uint32_t)r;
}

/* Voir util.h */
bool util_parse_ip(const char *s, uint8_t ip[4])
{
  int i;
  for (i = 0; i < 4; i++)
  {
    unsigned v = 0U;
    int      digits = 0;
    while ((*s >= '0') && (*s <= '9'))
    {
      v = (v * 10U) + (unsigned)(*s - '0');
      digits++;
      s++;
      if ((digits > 3) || (v > 255U))
      {
        return false;
      }
    }
    if (digits == 0)
    {
      return false;
    }
    ip[i] = (uint8_t)v;
    if (i < 3)
    {
      if (*s != '.')
      {
        return false;
      }
      s++;
    }
  }
  return *s == '\0';
}

/* Voir util.h */
bool util_parse_long(const char *s, long *out)
{
  char *end;
  long  v;

  if ((s == NULL) || (*s == '\0'))
  {
    return false;
  }
  v = strtol(s, &end, 10);
  if ((*end != '\0') || (v == LONG_MAX) || (v == LONG_MIN))
  {
    return false;
  }
  *out = v;
  return true;
}

/* Voir util.h */
bool util_parse_bool(const char *s, bool *out)
{
  static const char *const yes[] = { "1", "on", "oui", "true", "yes", "o", "y" };
  static const char *const no[]  = { "0", "off", "non", "false", "no", "n" };
  size_t i;

  for (i = 0U; i < (sizeof(yes) / sizeof(yes[0])); i++)
  {
    if (util_strieq(s, yes[i]))
    {
      *out = true;
      return true;
    }
  }
  for (i = 0U; i < (sizeof(no) / sizeof(no[0])); i++)
  {
    if (util_strieq(s, no[i]))
    {
      *out = false;
      return true;
    }
  }
  return false;
}

/* Voir util.h */
void util_fmt_ip(char *out, size_t len, const uint8_t ip[4])
{
  (void)snprintf(out, len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

/* Voir util.h */
void util_fmt_mac(char *out, size_t len, const uint8_t mac[6])
{
  (void)snprintf(out, len, "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* Voir util.h */
void util_fmt_fixed(char *out, size_t len, int32_t value, unsigned decimals)
{
  uint32_t div = 1U;
  uint32_t mag;
  unsigned i;
  bool     neg = (value < 0);

  for (i = 0U; i < decimals; i++)
  {
    div *= 10U;
  }
  mag = neg ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
  if (decimals == 0U)
  {
    (void)snprintf(out, len, "%s%lu", neg ? "-" : "", (unsigned long)mag);
  }
  else
  {
    (void)snprintf(out, len, "%s%lu.%0*lu", neg ? "-" : "", (unsigned long)(mag / div),
                   (int)decimals, (unsigned long)(mag % div));
  }
}

/* Voir util.h */
void util_fmt_uptime(char *out, size_t len, uint32_t seconds)
{
  uint32_t d = seconds / 86400U;
  uint32_t h = (seconds / 3600U) % 24U;
  uint32_t m = (seconds / 60U) % 60U;
  uint32_t s = seconds % 60U;

  if (d > 0U)
  {
    (void)snprintf(out, len, "%lu j %02lu:%02lu:%02lu", (unsigned long)d, (unsigned long)h,
                   (unsigned long)m, (unsigned long)s);
  }
  else
  {
    (void)snprintf(out, len, "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
  }
}

/* Ajoute n octets si la place le permet (le résultat reste toujours terminé). */
static bool put(char *out, size_t len, size_t *pos, const char *s, size_t n)
{
  if ((*pos + n) >= len)
  {
    return false;
  }
  memcpy(&out[*pos], s, n);
  *pos += n;
  out[*pos] = '\0';
  return true;
}

/* Voir util.h */
size_t util_json_escape(char *out, size_t len, const char *in)
{
  size_t pos = 0U;

  if (len == 0U)
  {
    return 0U;
  }
  out[0] = '\0';
  for (; *in != '\0'; in++)
  {
    unsigned char c = (unsigned char)*in;
    char          tmp[8];
    bool          ok;

    if ((c == '"') || (c == '\\'))
    {
      tmp[0] = '\\';
      tmp[1] = (char)c;
      ok = put(out, len, &pos, tmp, 2U);
    }
    else if (c < 0x20U)
    {
      (void)snprintf(tmp, sizeof(tmp), "\\u%04x", c);
      ok = put(out, len, &pos, tmp, 6U);
    }
    else
    {
      ok = put(out, len, &pos, (const char *)&c, 1U);
    }
    if (!ok)
    {
      break;
    }
  }
  return pos;
}

/* Voir util.h */
size_t util_html_escape(char *out, size_t len, const char *in)
{
  size_t pos = 0U;

  if (len == 0U)
  {
    return 0U;
  }
  out[0] = '\0';
  for (; *in != '\0'; in++)
  {
    const char *rep;
    bool        ok;

    switch (*in)
    {
      case '<':  rep = "&lt;";   break;
      case '>':  rep = "&gt;";   break;
      case '&':  rep = "&amp;";  break;
      case '"':  rep = "&quot;"; break;
      case '\'': rep = "&#39;";  break;
      default:   rep = NULL;     break;
    }
    ok = (rep != NULL) ? put(out, len, &pos, rep, strlen(rep)) : put(out, len, &pos, in, 1U);
    if (!ok)
    {
      break;
    }
  }
  return pos;
}

/* Voir util.h */
int util_tokenize(char *line, char **argv, int max)
{
  int   argc = 0;
  char *s = line;

  for (;;)
  {
    while ((*s == ' ') || (*s == '\t'))
    {
      s++;
    }
    if (*s == '\0')
    {
      break;
    }
    if (argc >= max)
    {
      return -1;
    }
    if (*s == '"')
    {
      s++;
      argv[argc++] = s;
      while ((*s != '\0') && (*s != '"'))
      {
        s++;
      }
    }
    else
    {
      argv[argc++] = s;
      while ((*s != '\0') && (*s != ' ') && (*s != '\t'))
      {
        s++;
      }
    }
    if (*s != '\0')
    {
      *s = '\0';
      s++;
    }
  }
  return argc;
}

/* Voir util.h */
int util_rssi_quality(int rssi)
{
  if (rssi >= -50)
  {
    return 100;
  }
  if (rssi <= -100)
  {
    return 0;
  }
  return 2 * (rssi + 100);
}

/* Voir util.h */
const char *util_rssi_label(int rssi)
{
  if (rssi >= -50)
  {
    return "excellent";
  }
  if (rssi >= -60)
  {
    return "très bon";
  }
  if (rssi >= -67)
  {
    return "bon";
  }
  if (rssi >= -75)
  {
    return "moyen";
  }
  if (rssi >= -85)
  {
    return "faible";
  }
  return "très faible";
}

/* Voir util.h */
void util_bar(char *out, size_t len, int value, int vmin, int vmax, int width)
{
  static const char FULL[]  = "\xE2\x96\x88";   /* █ */
  static const char EMPTY[] = "\xE2\x96\x91";   /* ░ */
  size_t pos = 0U;
  int    filled;
  int    i;

  if (len == 0U)
  {
    return;
  }
  out[0] = '\0';
  if (vmax <= vmin)
  {
    return;
  }
  if (value < vmin)
  {
    value = vmin;
  }
  if (value > vmax)
  {
    value = vmax;
  }
  filled = (((value - vmin) * width) + ((vmax - vmin) / 2)) / (vmax - vmin);
  for (i = 0; i < width; i++)
  {
    if (!put(out, len, &pos, (i < filled) ? FULL : EMPTY, 3U))
    {
      break;
    }
  }
}

/* Voir util.h */
uint16_t util_channel_mhz(uint8_t channel)
{
  if (channel == 14U)
  {
    return 2484U;
  }
  if ((channel >= 1U) && (channel <= 13U))
  {
    return (uint16_t)(2407U + (5U * channel));
  }
  return 0U;
}

/* Ajoute s au texte du QR code en échappant \ ; , : " par un '\' */
static bool qr_put_escaped(char *out, size_t len, size_t *pos, const char *s)
{
  for (; *s != '\0'; s++)
  {
    if ((*s == '\\') || (*s == ';') || (*s == ',') || (*s == ':') || (*s == '"'))
    {
      if (!put(out, len, pos, "\\", 1U))
      {
        return false;
      }
    }
    if (!put(out, len, pos, s, 1U))
    {
      return false;
    }
  }
  return true;
}

/* Voir util.h */
size_t util_wifi_qr(char *out, size_t len, const char *ssid, const char *pass)
{
  size_t pos = 0U;
  bool   open = (pass == NULL) || (pass[0] == '\0');
  bool   ok;

  if (len == 0U)
  {
    return 0U;
  }
  out[0] = '\0';
  ok = put(out, len, &pos, open ? "WIFI:T:nopass;S:" : "WIFI:T:WPA;S:", open ? 16U : 13U) &&
       qr_put_escaped(out, len, &pos, ssid);
  if (ok && !open)
  {
    ok = put(out, len, &pos, ";P:", 3U) && qr_put_escaped(out, len, &pos, pass);
  }
  ok = ok && put(out, len, &pos, ";;", 2U);
  if (!ok)
  {
    out[0] = '\0';
    return 0U;
  }
  return pos;
}

/* ---- JSON ------------------------------------------------------------------- */
/* Voir util.h */
void jb_init(jbuf_t *j, char *buf, size_t cap)
{
  j->buf = buf;
  j->cap = cap;
  j->pos = 0U;
  j->overflow = (cap == 0U);
  j->need_comma = false;
  if (cap > 0U)
  {
    buf[0] = '\0';
  }
}

/* Ajoute n octets au document JSON ; signale le débordement sans dépasser */
static void jb_put(jbuf_t *j, const char *s, size_t n)
{
  if (j->overflow || ((j->pos + n) >= j->cap))
  {
    j->overflow = true;
    return;
  }
  memcpy(&j->buf[j->pos], s, n);
  j->pos += n;
  j->buf[j->pos] = '\0';
}

/* Voir util.h */
void jb_raw(jbuf_t *j, const char *fmt, ...)
{
  char    tmp[128];
  va_list ap;
  int     n;

  va_start(ap, fmt);
  n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if ((n < 0) || ((size_t)n >= sizeof(tmp)))
  {
    j->overflow = true;
    return;
  }
  jb_put(j, tmp, (size_t)n);
}

/* Voir util.h */
void jb_key(jbuf_t *j, const char *key)
{
  if (j->need_comma)
  {
    jb_put(j, ",", 1U);
  }
  jb_str(j, key);
  jb_put(j, ":", 1U);
  j->need_comma = true;
}

/* Voir util.h */
void jb_str(jbuf_t *j, const char *s)
{
  char tmp[8];
  jb_put(j, "\"", 1U);
  for (; *s != '\0'; s++)
  {
    unsigned char c = (unsigned char)*s;
    if ((c == '"') || (c == '\\'))
    {
      tmp[0] = '\\';
      tmp[1] = (char)c;
      jb_put(j, tmp, 2U);
    }
    else if (c < 0x20U)
    {
      (void)snprintf(tmp, sizeof(tmp), "\\u%04x", c);
      jb_put(j, tmp, 6U);
    }
    else
    {
      jb_put(j, (const char *)&c, 1U);
    }
  }
  jb_put(j, "\"", 1U);
}

/* Voir util.h */
void jb_open(jbuf_t *j, char c)
{
  jb_put(j, &c, 1U);
  j->need_comma = false;
}

/* Voir util.h */
void jb_close(jbuf_t *j, char c)
{
  jb_put(j, &c, 1U);
  j->need_comma = true;
}

/* Voir util.h */
void jb_item(jbuf_t *j)
{
  if (j->need_comma)
  {
    jb_put(j, ",", 1U);
  }
  j->need_comma = false;
}

/* Voir util.h */
void jb_kv_str(jbuf_t *j, const char *key, const char *val)
{
  jb_key(j, key);
  jb_str(j, val);
}

/* Voir util.h */
void jb_kv_int(jbuf_t *j, const char *key, long val)
{
  jb_key(j, key);
  jb_raw(j, "%ld", val);
}

/* Voir util.h */
void jb_kv_bool(jbuf_t *j, const char *key, bool val)
{
  jb_key(j, key);
  jb_raw(j, "%s", val ? "true" : "false");
}

/* Voir util.h */
void jb_kv_raw(jbuf_t *j, const char *key, const char *raw)
{
  jb_key(j, key);
  jb_put(j, raw, strlen(raw));
}
