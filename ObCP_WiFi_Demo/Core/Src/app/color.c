/**
 ******************************************************************************
 * @file    color.c
 * @brief   Couleurs de la LED RVB (voir color.h).
 *
 *  Une couleur est donnée par trois composantes rouge, vert, bleu de 0 à 255
 *  (« #FF8000 » en hexadécimal = orange). La LED mélange les trois couleurs :
 *  chaque composante devient un rapport cyclique PWM (color_duty()).
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "color.h"

/* Une couleur nommée (nom français et alias anglais) */
typedef struct
{
  const char *name;
  const char *alias;
  uint8_t     r;
  uint8_t     g;
  uint8_t     b;
} named_color_t;

static const named_color_t s_colors[] =
{
  { "rouge",   "red",     255U,   0U,   0U },
  { "vert",    "green",     0U, 255U,   0U },
  { "bleu",    "blue",      0U,   0U, 255U },
  { "jaune",   "yellow",  255U, 255U,   0U },
  { "cyan",    NULL,        0U, 255U, 255U },
  { "magenta", NULL,      255U,   0U, 255U },
  { "blanc",   "white",   255U, 255U, 255U },
  { "orange",  NULL,      255U,  80U,   0U },
  { "violet",  "purple",  128U,   0U, 255U },
  { "rose",    "pink",    255U,  40U,  80U },
};

#define N_COLORS   (sizeof(s_colors) / sizeof(s_colors[0]))

/* Égalité de deux textes sans tenir compte des majuscules */
static bool same_nocase(const char *a, const char *b)
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

/* Valeur d'un chiffre hexadécimal ('0'..'9', 'a'..'f', 'A'..'F'), -1 sinon */
static int hex_digit(char c)
{
  if ((c >= '0') && (c <= '9'))
  {
    return c - '0';
  }
  c = (char)tolower((unsigned char)c);
  if ((c >= 'a') && (c <= 'f'))
  {
    return c - 'a' + 10;
  }
  return -1;
}

/* « RRGGBB » : trois octets en hexadécimal */
static bool parse_hex6(const char *s, uint8_t rgb[3])
{
  int i;

  for (i = 0; i < 6; i++)
  {
    if (hex_digit(s[i]) < 0)
    {
      return false;
    }
  }
  if (s[6] != '\0')
  {
    return false;
  }
  for (i = 0; i < 3; i++)
  {
    rgb[i] = (uint8_t)((hex_digit(s[2 * i]) << 4) | hex_digit(s[(2 * i) + 1]));
  }
  return true;
}

/* « R,G,B » : trois nombres décimaux de 0 à 255 séparés par des virgules */
static bool parse_triplet(const char *s, uint8_t rgb[3])
{
  int  i;

  for (i = 0; i < 3; i++)
  {
    unsigned v = 0U;
    int      digits = 0;

    while (*s == ' ')
    {
      s++;
    }
    while ((*s >= '0') && (*s <= '9'))
    {
      v = (v * 10U) + (unsigned)(*s - '0');
      if (v > 255U)
      {
        return false;
      }
      s++;
      digits++;
    }
    if (digits == 0)
    {
      return false;
    }
    rgb[i] = (uint8_t)v;
    while (*s == ' ')
    {
      s++;
    }
    if (i < 2)
    {
      if ((*s != ',') && (*s != ';'))
      {
        return false;
      }
      s++;
    }
  }
  return *s == '\0';
}

/* Voir color.h : nom connu, puis R,G,B, puis #RRGGBB ou RRGGBB */
bool color_parse(const char *s, uint8_t rgb[3])
{
  size_t i;

  if (s == NULL)
  {
    return false;
  }
  while (*s == ' ')
  {
    s++;
  }
  for (i = 0U; i < N_COLORS; i++)
  {
    if (same_nocase(s, s_colors[i].name) ||
        ((s_colors[i].alias != NULL) && same_nocase(s, s_colors[i].alias)))
    {
      rgb[0] = s_colors[i].r;
      rgb[1] = s_colors[i].g;
      rgb[2] = s_colors[i].b;
      return true;
    }
  }
  if (*s == '#')
  {
    return parse_hex6(s + 1, rgb);
  }
  if ((strchr(s, ',') != NULL) || (strchr(s, ';') != NULL))
  {
    return parse_triplet(s, rgb);
  }
  return parse_hex6(s, rgb);
}

/* Voir color.h */
const char *color_name(const uint8_t rgb[3])
{
  size_t i;

  for (i = 0U; i < N_COLORS; i++)
  {
    if ((rgb[0] == s_colors[i].r) && (rgb[1] == s_colors[i].g) && (rgb[2] == s_colors[i].b))
    {
      return s_colors[i].name;
    }
  }
  return NULL;
}

/* Voir color.h */
void color_hex(const uint8_t rgb[3], char out[8])
{
  (void)snprintf(out, 8U, "#%02X%02X%02X", (unsigned)rgb[0], (unsigned)rgb[1], (unsigned)rgb[2]);
}

/* Voir color.h */
const char *color_names(void)
{
  return "rouge vert bleu jaune cyan magenta blanc orange violet rose";
}

/* Voir color.h : rouge et vert dosés linéairement de part et d'autre de 50 */
void color_quality(uint8_t q, uint8_t rgb[3])
{
  if (q > 100U)
  {
    q = 100U;
  }
  if (q <= 50U)
  {
    rgb[0] = 255U;                                      /* rouge -> jaune */
    rgb[1] = (uint8_t)(((uint32_t)q * 255U) / 50U);
  }
  else
  {
    rgb[0] = (uint8_t)(((uint32_t)(100U - q) * 255U) / 50U);   /* jaune -> vert */
    rgb[1] = 255U;
  }
  rgb[2] = 0U;
}

/* Voir color.h. L'œil perçoit la luminosité de façon non linéaire : la
 * correction gamma 2 élève la composante au carré (65025 = 255², calcul
 * en entiers), le résultat est ensuite proportionnel à pct.            */
uint16_t color_duty(uint8_t c, uint8_t pct, uint16_t period)
{
  uint32_t lin;

  if (pct > 100U)
  {
    pct = 100U;
  }
  lin = ((uint32_t)c * (uint32_t)c * (uint32_t)period) / 65025UL;     /* gamma 2 */
  return (uint16_t)((lin * (uint32_t)pct + 50U) / 100U);
}
