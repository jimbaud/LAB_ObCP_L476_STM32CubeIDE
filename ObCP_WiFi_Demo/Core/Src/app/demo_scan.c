/**
 ******************************************************************************
 * @file    demo_scan.c
 * @brief   Démo 1 : scanner Wi-Fi, occupation des canaux et suivi du RSSI.
 *
 *  Le balayage est réalisé par le module (AT+CWLAP) ; chaque réseau trouvé
 *  est signalé par une ligne +CWLAP:(...) puis +CW:SCAN_DONE termine la
 *  recherche (voir ncp_wifi.c). Les résultats sont dans g_wifi.scan[].
 *
 *  Commandes : scan (tableau trié), channels (occupation des 13 canaux de
 *  la bande 2,4 GHz et canal conseillé), track (suivi du niveau d'un
 *  réseau, avec la couleur de la LED RVB et une sortie CSV pour le PC).
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "board.h"
#include "config.h"
#include "console.h"
#include "demos.h"
#include "io.h"
#include "color.h"
#include "util.h"

#define SCAN_TIMEOUT_MS      15000U   /* balayage actif de tous les canaux   */
#define SCAN_PASSIVE_MS      25000U   /* balayage passif (plus lent)         */
#define TRACK_SCAN_MS         6000U   /* balayage d'un seul canal (track)    */

static bool     s_have_scan;        /* un balayage a déjà eu lieu       */
static bool     s_scan_full;        /* tous les canaux, actif */
static uint32_t s_scan_tick;        /* instant du dernier balayage (ms) */

/* Voir demos.h */
uint32_t scan_age_s(void)
{
  return s_have_scan ? ((HAL_GetTick() - s_scan_tick) / 1000U) : UINT32_MAX;
}

/* Voir demos.h */
int scan_run(bool quiet)
{
  int st;

  if (!quiet)
  {
    console_puts("Balayage des canaux 1 à 13...\n");
  }
  st = wifi_scan(false, 0U, SCAN_TIMEOUT_MS);
  if (st == AT_OK)
  {
    s_have_scan = true;
    s_scan_full = true;
    s_scan_tick = HAL_GetTick();
  }
  return st;
}

/* ---- Présentation ------------------------------------------------------------ */
/* Voir demos.h : masque des normes, 8 = ax (Wi-Fi 6), 4 = n (Wi-Fi 4)... */
const char *scan_std_label(uint8_t proto)
{
  if ((proto & 8U) != 0U)
  {
    return "Wi-Fi 6";
  }
  if ((proto & 4U) != 0U)
  {
    return "Wi-Fi 4";
  }
  if ((proto & 2U) != 0U)
  {
    return "802.11g";
  }
  if ((proto & 1U) != 0U)
  {
    return "802.11b";
  }
  return "?";
}

/* Couleur d'affichage d'un niveau : vert >= -60 dBm, jaune >= -75 dBm, rouge */
static const char *rssi_color(int rssi)
{
  if (rssi >= -60)
  {
    return C_OK();
  }
  if (rssi >= -75)
  {
    return C_WARN();
  }
  return C_ERR();
}

/* SSID affichable : caractères de contrôle remplacés, coupé à cols colonnes
 * (UTF-8) avec « … » si nécessaire.                                         */
static void safe_ssid(char *out, size_t len, const char *ssid, size_t cols)
{
  size_t i;
  size_t c = 0U;

  if (ssid[0] == '\0')
  {
    (void)util_strlcpy(out, "(SSID masqué)", len);
    return;
  }
  (void)util_strlcpy(out, ssid, len);
  for (i = 0U; out[i] != '\0'; i++)
  {
    if ((unsigned char)out[i] < 0x20U)
    {
      out[i] = '?';
    }
  }
  if (util_utf8_len(out) <= cols)
  {
    return;
  }
  for (i = 0U; out[i] != '\0'; i++)
  {
    if (((unsigned char)out[i] & 0xC0U) != 0x80U)
    {
      if (c == (cols - 1U))
      {
        break;                            /* place pour « … » */
      }
      c++;
    }
  }
  if ((i + 4U) <= len)
  {
    memcpy(&out[i], "\xE2\x80\xA6", 4U);   /* « … » + fin de chaîne */
  }
}

/* Critère de tri du tableau (scan ... tri canal|ssid) */
typedef enum
{
  SORT_RSSI = 0,
  SORT_CHANNEL,
  SORT_SSID
} sort_t;

/* Vrai si le réseau a doit être affiché avant b selon le critère choisi */
static bool before(const wifi_ap_t *a, const wifi_ap_t *b, sort_t sort)
{
  switch (sort)
  {
    case SORT_CHANNEL:
      if (a->channel != b->channel)
      {
        return a->channel < b->channel;
      }
      return a->rssi > b->rssi;
    case SORT_SSID:
    {
      int c = strcmp(a->ssid, b->ssid);
      if (c != 0)
      {
        return c < 0;
      }
      return a->rssi > b->rssi;
    }
    case SORT_RSSI:
    default:
      return a->rssi > b->rssi;
  }
}

/* Trie les numéros des réseaux (idx) sans déplacer les résultats eux-mêmes */
static void sort_index(uint8_t *idx, uint8_t n, sort_t sort)
{
  uint8_t i;
  for (i = 0U; i < n; i++)
  {
    idx[i] = i;
  }
  for (i = 1U; i < n; i++)            /* tri par insertion (40 éléments au plus) */
  {
    uint8_t v = idx[i];
    int     j = (int)i - 1;
    while ((j >= 0) && before(&g_wifi.scan[v], &g_wifi.scan[idx[j]], sort))
    {
      idx[j + 1] = idx[j];
      j--;
    }
    idx[j + 1] = v;
  }
}

#define SSID_COLS   28U                  /* tableau tenant dans 80 colonnes */

/* Tableau des réseaux trouvés, avec barre de qualité et bilan */
static void print_table(sort_t sort, bool show_bssid)
{
  uint8_t idx[WIFI_SCAN_MAX];
  uint8_t n = g_wifi.scan_count;
  uint8_t i;
  uint8_t wifi6 = 0U;
  uint8_t open = 0U;

  sort_index(idx, n, sort);
  console_printf("%s #  ", C_BOLD());
  console_pad("SSID", SSID_COLS);
  console_printf(" Can. RSSI  Qualité         Sécurité   Norme%s%s\n", show_bssid ? "    BSSID" : "", C_RST());

  for (i = 0U; i < n; i++)
  {
    const wifi_ap_t *ap = &g_wifi.scan[idx[i]];
    char ssid[48];
    char bar[48];
    char mac[20];
    int  q = util_rssi_quality(ap->rssi);

    safe_ssid(ssid, sizeof(ssid), ap->ssid, SSID_COLS);
    util_bar(bar, sizeof(bar), q, 0, 100, 8);
    util_fmt_mac(mac, sizeof(mac), ap->bssid);
    if ((ap->proto & 8U) != 0U)
    {
      wifi6++;
    }
    if (ap->security == WIFI_SEC_OPEN)
    {
      open++;
    }

    console_printf("%2u  %s", (unsigned)(i + 1U), (ap->ssid[0] == '\0') ? C_DIM() : "");
    console_pad(ssid, SSID_COLS);
    console_printf("%s %3u  %s%4d%s  %s %3d %%  ", C_RST(), (unsigned)ap->channel, rssi_color(ap->rssi),
                   (int)ap->rssi, C_RST(), bar, q);
    console_pad(wifi_security_str(ap->security), 10U);
    if (show_bssid)
    {
      console_printf(" %-8s %s\n", scan_std_label(ap->proto), mac);
    }
    else
    {
      console_printf(" %s\n", scan_std_label(ap->proto));
    }
  }
  console_printf("%u réseau(x)", (unsigned)n);
  if (wifi6 > 0U)
  {
    console_printf(", dont %s%u en Wi-Fi 6%s (802.11ax)", C_OK(), (unsigned)wifi6, C_RST());
  }
  if (open > 0U)
  {
    console_printf(", %s%u ouvert(s)%s", C_WARN(), (unsigned)open, C_RST());
  }
  console_puts(".\n");
  if (g_wifi.scan_truncated)
  {
    console_printf("%s(liste limitée aux %u premiers réseaux)%s\n", C_DIM(), (unsigned)WIFI_SCAN_MAX, C_RST());
  }
}

/* ---- scan ------------------------------------------------------------------------ */
/* Voir demos.h : analyse des options, balayage, tableau */
int cmd_scan(int argc, char **argv)
{
  bool     passive = false;
  bool     bssid = false;
  long     ch = 0;
  sort_t   sort = SORT_RSSI;
  int      i;
  int      st;
  uint32_t t0;

  for (i = 1; i < argc; i++)
  {
    if (util_strieq(argv[i], "passif") || util_strieq(argv[i], "passive"))
    {
      passive = true;
    }
    else if (util_strieq(argv[i], "bssid") || util_strieq(argv[i], "mac"))
    {
      bssid = true;
    }
    else if ((util_strieq(argv[i], "canal") || util_strieq(argv[i], "ch") || util_strieq(argv[i], "channel")) &&
             ((i + 1) < argc))
    {
      i++;
      if (!util_parse_long(argv[i], &ch) || (ch < 1) || (ch > 13))
      {
        console_puts("Canal invalide (1 à 13).\n");
        return APP_ERR_USAGE;
      }
    }
    else if ((util_strieq(argv[i], "tri") || util_strieq(argv[i], "sort")) && ((i + 1) < argc))
    {
      i++;
      if (util_strieq(argv[i], "canal") || util_strieq(argv[i], "ch") || util_strieq(argv[i], "channel"))
      {
        sort = SORT_CHANNEL;
      }
      else if (util_strieq(argv[i], "ssid") || util_strieq(argv[i], "nom"))
      {
        sort = SORT_SSID;
      }
      else
      {
        sort = SORT_RSSI;
      }
    }
    else
    {
      console_puts("Usage : scan [passif] [canal <n>] [tri rssi|canal|ssid] [bssid]\n");
      return APP_ERR_USAGE;
    }
  }

  if (ch > 0)
  {
    console_printf("Balayage %s du canal %ld...\n", passive ? "passif" : "actif", ch);
  }
  else
  {
    console_printf("Balayage %s des canaux 1 à 13...\n", passive ? "passif" : "actif");
  }
  t0 = HAL_GetTick();
  st = wifi_scan(passive, (uint8_t)ch, passive ? SCAN_PASSIVE_MS : SCAN_TIMEOUT_MS);
  if (st != AT_OK)
  {
    return app_report(st, "balayage");
  }
  s_have_scan = true;
  s_scan_full = (ch == 0) && !passive;
  s_scan_tick = HAL_GetTick();

  if (g_wifi.scan_count == 0U)
  {
    console_puts("Aucun réseau trouvé.\n");
    return 0;
  }
  print_table(sort, bssid);
  console_printf("%sDurée du balayage : %lu ms. « channels » : occupation des canaux.%s\n", C_DIM(),
                 (unsigned long)(HAL_GetTick() - t0), C_RST());
  return 0;
}

/* ---- channels ------------------------------------------------------------------- */
/* Voir demos.h. Un réseau sur le canal c occupe aussi les canaux voisins
 * (c - 4 à c + 4, 20 MHz de large pour 5 MHz d'écart) : chaque réseau
 * ajoute à chaque canal un score proportionnel à sa puissance, pondéré
 * par le recouvrement (tableau overlap). Le canal conseillé est le moins
 * occupé parmi 1, 6 et 11, les seuls qui ne se chevauchent pas.        */
int cmd_channels(int argc, char **argv)
{
  static const uint8_t overlap[5] = { 100U, 70U, 40U, 15U, 5U };   /* % selon l'écart de canaux */
  uint32_t score[14] = { 0 };
  uint8_t  count[14] = { 0 };
  uint32_t max = 1U;
  uint8_t  best = 1U;
  uint8_t  link_ch = 0U;
  uint8_t  i;
  int      c;

  if (!s_have_scan || !s_scan_full || (scan_age_s() > 60U))
  {
    int st = scan_run(false);
    if (st != AT_OK)
    {
      return app_report(st, "balayage");
    }
  }
  if (wifi_is_connected())
  {
    wifi_link_t link;
    if (wifi_link_info(&link) == AT_OK)
    {
      link_ch = link.channel;
    }
  }

  for (i = 0U; i < g_wifi.scan_count; i++)
  {
    const wifi_ap_t *ap = &g_wifi.scan[i];
    int32_t          w = (int32_t)ap->rssi + 100;   /* -100 dBm -> 0, -40 dBm -> 60 */
    int              d;

    if ((ap->channel < 1U) || (ap->channel > 13U))
    {
      continue;
    }
    count[ap->channel]++;
    if (w < 1)
    {
      w = 1;
    }
    for (d = -4; d <= 4; d++)
    {
      c = (int)ap->channel + d;
      if ((c >= 1) && (c <= 13))
      {
        score[c] += ((uint32_t)w * overlap[(d < 0) ? -d : d]) / 100U;
      }
    }
  }
  for (c = 1; c <= 13; c++)
  {
    if (score[c] > max)
    {
      max = score[c];
    }
  }
  best = 1U;
  if (score[6] < score[best])
  {
    best = 6U;
  }
  if (score[11] < score[best])
  {
    best = 11U;
  }

  console_printf("%sCanal  Fréquence  Réseaux  Occupation%s\n", C_BOLD(), C_RST());
  for (c = 1; c <= 13; c++)
  {
    char        bar[96];
    uint32_t    pct = (score[c] * 100U) / max;
    const char *col = (pct >= 66U) ? C_ERR() : ((pct >= 33U) ? C_WARN() : C_OK());

    util_bar(bar, sizeof(bar), (int)score[c], 0, (int)max, 24);
    console_printf(" %2d%s   %4u MHz    %2u     %s%s%s %3lu %%%s\n", c,
                   ((c == 1) || (c == 6) || (c == 11)) ? "*" : " ",
                   (unsigned)util_channel_mhz((uint8_t)c), (unsigned)count[c], col, bar, C_RST(),
                   (unsigned long)pct, (c == (int)link_ch) ? "   <- réseau connecté" : "");
  }
  console_printf("Canal conseillé pour le point d'accès : %s%u%s (le moins chargé parmi 1, 6 et 11 (*),\n"
                 "les seuls canaux qui ne se chevauchent pas). Réglage actuel : ap.channel = %u.\n",
                 C_BOLD(), (unsigned)best, C_RST(), (unsigned)g_cfg.ap_channel);

  if ((argc >= 2) && (util_strieq(argv[1], "appliquer") || util_strieq(argv[1], "apply")))
  {
    g_cfg.ap_channel = best;
    g_app.cfg_dirty = true;
    console_printf("ap.channel = %u (« save » pour l'enregistrer ; utilisé au prochain « web start »).\n",
                   (unsigned)best);
  }
  return 0;
}

/* ---- track ------------------------------------------------------------------------ */
/* Statistiques du suivi (minimum, maximum, moyenne, écart-type) */
typedef struct
{
  uint32_t n;
  int32_t  min;
  int32_t  max;
  int64_t  sum;
  int64_t  sum2;
  uint32_t missing;
} stats_t;

/* Niveau (dBm) du point d'accès le plus fort portant ce SSID dans le dernier
 * balayage, et son canal ; 0 si le réseau est absent                   */
static int find_ssid(const char *ssid, uint8_t *channel)
{
  int     best = 0;
  bool    found = false;
  uint8_t i;

  for (i = 0U; i < g_wifi.scan_count; i++)
  {
    if (strcmp(g_wifi.scan[i].ssid, ssid) == 0)
    {
      if (!found || (g_wifi.scan[i].rssi > best))
      {
        best = g_wifi.scan[i].rssi;
        *channel = g_wifi.scan[i].channel;
        found = true;
      }
    }
  }
  return found ? best : 0;
}

/* Horodatage d'une mesure : heure locale si la RTC est réglée, sinon temps écoulé */
static void track_timestamp(char *out, size_t len, uint32_t t_ms)
{
  dt_t loc;
  if (netapp_time_synced() && netapp_local_time(&loc, NULL))
  {
    (void)snprintf(out, len, "%02u:%02u:%02u", loc.hour, loc.min, loc.sec);
  }
  else
  {
    (void)snprintf(out, len, "t+%5lu.%01lu s", (unsigned long)(t_ms / 1000U), (unsigned long)((t_ms % 1000U) / 100U));
  }
}

/* Voir demos.h. Deux modes : réseau auquel la carte est connectée (le niveau
 * est lu sur la liaison, rapide) ou autre réseau (balayage de son canal à
 * chaque mesure). La boucle tourne jusqu'à l'appui d'une touche, en
 * appelant app_idle() pour que les autres services continuent.          */
int cmd_track(int argc, char **argv)
{
  const char *ssid = NULL;
  bool        csv = false;
  bool        by_link;
  uint8_t     channel = 0U;
  uint8_t     led_saved = io_led_get();
  uint8_t     rgb_saved[3];
  uint32_t    t_start;
  uint32_t    last = 0U;
  uint32_t    misses = 0U;
  stats_t     s = { 0U, 0, 0, 0, 0, 0U };
  wifi_link_t link;
  int         i;

  io_led_color_get(rgb_saved);
  for (i = 1; i < argc; i++)
  {
    if (util_strieq(argv[i], "csv"))
    {
      csv = true;
    }
    else
    {
      ssid = argv[i];
    }
  }

  by_link = false;
  if (wifi_is_connected() && (wifi_link_info(&link) == AT_OK))
  {
    by_link = (ssid == NULL) || (strcmp(ssid, link.ssid) == 0);
    if (by_link)
    {
      ssid = link.ssid;
      channel = link.channel;
    }
  }
  if (ssid == NULL)
  {
    console_puts("Carte non connectée : préciser le réseau à suivre (track <ssid>).\n");
    return APP_ERR_USAGE;
  }
  if (!by_link)
  {
    int st;
    console_printf("Recherche de « %s »...\n", ssid);
    st = scan_run(true);
    if (st != AT_OK)
    {
      return app_report(st, "balayage");
    }
    if (find_ssid(ssid, &channel) == 0)
    {
      console_printf("%sRéseau « %s » introuvable (2,4 GHz uniquement ; respecter majuscules et espaces).%s\n",
                     C_ERR(), ssid, C_RST());
      return APP_ERR_NOTFOUND;
    }
  }

  if (csv)
  {
    console_puts("temps_ms;rssi_dbm;canal\n");
  }
  else
  {
    console_printf("Suivi de « %s%s%s » (canal %u, %s) toutes les %u ms - une touche pour arrêter\n",
                   C_BOLD(), ssid, C_RST(), (unsigned)channel,
                   by_link ? "liaison établie" : "balayage du canal", (unsigned)g_cfg.track_period);
  }

  t_start = HAL_GetTick();
  last = t_start - g_cfg.track_period;
  while (!console_abort())
  {
    uint32_t now = HAL_GetTick();
    if ((now - last) >= g_cfg.track_period)
    {
      int  rssi = 0;
      bool ok = false;

      last = now;
      if (by_link)
      {
        if (wifi_link_info(&link) == AT_OK)
        {
          rssi = link.rssi;
          channel = link.channel;
          ok = true;
        }
      }
      else
      {
        uint8_t ch = channel;
        /* après 5 absences consécutives, balayage complet (changement de canal ?) */
        if (wifi_scan(false, (misses >= 5U) ? 0U : channel, (misses >= 5U) ? SCAN_TIMEOUT_MS : TRACK_SCAN_MS) == AT_OK)
        {
          rssi = find_ssid(ssid, &ch);
          ok = (rssi != 0);
          if (ok)
          {
            channel = ch;
          }
        }
        s_scan_full = false;
      }

      if (ok)
      {
        misses = 0U;
        if ((s.n == 0U) || (rssi < s.min))
        {
          s.min = rssi;
        }
        if ((s.n == 0U) || (rssi > s.max))
        {
          s.max = rssi;
        }
        s.n++;
        s.sum += rssi;
        s.sum2 += (int64_t)rssi * rssi;
        {
          uint8_t rgb[3];                 /* LED RVB : rouge (faible) -> vert (fort) */
          color_quality((uint8_t)util_rssi_quality(rssi), rgb);
          io_led_color_set(rgb);
          io_led_set(100U);
        }
      }
      else
      {
        misses++;
        s.missing++;
      }

      if (csv)
      {
        if (ok)
        {
          console_printf("%lu;%d;%u\n", (unsigned long)(now - t_start), rssi, (unsigned)channel);
        }
        else
        {
          console_printf("%lu;;%u\n", (unsigned long)(now - t_start), (unsigned)channel);
        }
      }
      else
      {
        char ts[20];
        track_timestamp(ts, sizeof(ts), now - t_start);
        if (ok)
        {
          char bar[96];
          int  q = util_rssi_quality(rssi);
          util_bar(bar, sizeof(bar), rssi, -100, -30, 20);
          console_printf("  %s  %s%4d dBm%s  %s %3d %%  %s\n", ts, rssi_color(rssi), rssi, C_RST(), bar, q,
                         util_rssi_label(rssi));
        }
        else
        {
          console_printf("  %s  %s---- dBm  réseau non reçu%s\n", ts, C_ERR(), C_RST());
        }
      }
    }
    app_idle();
  }

  io_led_color_set(rgb_saved);
  io_led_set(led_saved);
  /* Bilan : moyenne et écart-type calculés en entiers (valeurs x 10) :
   * variance = (somme des carrés - somme² / n) / n                    */
  if (!csv && (s.n > 0U))
  {
    int64_t  mean_x10 = (s.sum * 10) / (int64_t)s.n;
    int64_t  var_x100 = ((s.sum2 * 100) - ((s.sum * s.sum * 100) / (int64_t)s.n)) / (int64_t)s.n;
    uint32_t sd_x10 = util_isqrt((var_x100 > 0) ? (uint64_t)var_x100 : 0U);
    char     m[12];
    char     sd[12];

    util_fmt_fixed(m, sizeof(m), (int32_t)mean_x10, 1U);
    util_fmt_fixed(sd, sizeof(sd), (int32_t)sd_x10, 1U);
    console_printf("Bilan : %lu mesure(s), min %ld dBm, moyenne %s dBm, max %ld dBm, écart-type %s dB",
                   (unsigned long)s.n, (long)s.min, m, (long)s.max, sd);
    if (s.missing > 0U)
    {
      console_printf(", %lu absence(s)", (unsigned long)s.missing);
    }
    console_puts(".\n");
  }
  return 0;
}
