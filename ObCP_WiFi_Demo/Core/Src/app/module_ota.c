/**
 ******************************************************************************
 * @file    module_ota.c
 * @brief   Commande « module ota » (voir module_ota.h).
 *
 *  Le fichier arrive par la console en YMODEM ; chaque bloc reçu est transmis
 *  aussitôt au module (le STM32 ne garde pas l'image : 2 Ko de tampon). Le
 *  module vérifie l'image (SHA-256 de l'en-tête) lors de AT+OTAFIN puis
 *  redémarre dessus ; en cas d'échec avant AT+OTAFIN, la session est
 *  abandonnée (AT+OTASTART=0) et le firmware en place est conservé.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "module_ota.h"
#include "app.h"
#include "board.h"
#include "console.h"
#include "demos.h"
#include "io.h"
#include "ymodem.h"

static const uint8_t k_blue[3] = { 0U, 0U, 255U };   /* progression : LED bleue */

typedef struct
{
  bool             started;        /* session ouverte dans le module (AT+OTASTART=1) */
  bool             header_ok;
  uint8_t          hbuf[NCP_FWU_HEADER_SIZE];
  uint16_t         hlen;
  uint32_t         announced;      /* taille annoncée par l'émetteur (0 : inconnue)  */
  uint32_t         total;          /* taille attendue d'après l'en-tête             */
  uint32_t         fed;            /* octets transmis au pilote                     */
  uint8_t          last_pct;
  ncp_fwu_header_t hdr;
  char             reason[176];
} ota_ctx_t;

static ota_ctx_t s_ota;

/* ---- Entrées / sorties de la console pour YMODEM -------------------------------- */
/* Lecture d'un octet du terminal avec délai d'attente ; les tâches de fond
 * continuent pendant l'attente (app_idle())                             */
static int io_getc(uint32_t timeout_ms, void *io)
{
  uint32_t t0 = HAL_GetTick();
  (void)io;
  for (;;)
  {
    int c = console_getc();
    if (c >= 0)
    {
      return c;
    }
    if ((HAL_GetTick() - t0) >= timeout_ms)
    {
      return -1;
    }
  }
}

/* Envoi d'un octet de contrôle YMODEM (ACK, NAK, 'C'...) au PC */
static void io_putc(uint8_t c, void *io)
{
  (void)io;
  console_write_raw(&c, 1U);
}

/* Horloge en ms pour le protocole */
static uint32_t io_tick(void *io)
{
  (void)io;
  return HAL_GetTick();
}

/* ---- Outils ---------------------------------------------------------------------- */
/* Vrai si needle apparaît dans s, sans tenir compte des majuscules */
static bool contains_nocase(const char *s, const char *needle)
{
  size_t n = strlen(needle);
  for (; *s != '\0'; s++)
  {
    size_t i = 0U;
    while ((i < n) && (s[i] != '\0') && (tolower((unsigned char)s[i]) == tolower((unsigned char)needle[i])))
    {
      i++;
    }
    if (i == n)
    {
      return true;
    }
  }
  return false;
}

/* Vrai si s se termine par suffix (« .ota »), sans tenir compte des majuscules */
static bool ends_with_nocase(const char *s, const char *suffix)
{
  size_t ls = strlen(s);
  size_t lx = strlen(suffix);
  return (ls >= lx) && contains_nocase(&s[ls - lx], suffix);
}

/* Version « 2.0.106 » du firmware en place (champ SDK, sinon début de « AT version ») */
static void current_version(char *out, size_t len)
{
  const char *src = (g_app.ncp.sdk_version[0] != '\0') ? g_app.ncp.sdk_version : g_app.ncp.at_version;
  size_t      i = 0U;

  while ((src[i] != '\0') && ((isdigit((unsigned char)src[i]) != 0) || (src[i] == '.')) && (i < (len - 1U)))
  {
    out[i] = src[i];
    i++;
  }
  out[i] = '\0';
  if (i == 0U)
  {
    (void)snprintf(out, len, "?");
  }
}

/* Comparaison de versions « a.b.c » : <0, 0 ou >0 */
static int version_cmp(const char *a, const char *b)
{
  while ((*a != '\0') || (*b != '\0'))
  {
    unsigned long va = 0UL;
    unsigned long vb = 0UL;
    while (isdigit((unsigned char)*a) != 0)
    {
      va = (va * 10UL) + (unsigned long)(*a++ - '0');
    }
    while (isdigit((unsigned char)*b) != 0)
    {
      vb = (vb * 10UL) + (unsigned long)(*b++ - '0');
    }
    if (va != vb)
    {
      return (va < vb) ? -1 : 1;
    }
    if (*a == '.')
    {
      a++;
    }
    else if (*a != '\0')
    {
      break;
    }
    if (*b == '.')
    {
      b++;
    }
    else if (*b != '\0')
    {
      break;
    }
  }
  return 0;
}

/* ---- Destinataire du fichier ------------------------------------------------------ */
/* Rappel YMODEM, bloc 0 (nom et taille du fichier) : refuse ce qui n'est pas
 * un firmware « mission T01 » (.ota, ni T02 ni mfg), ouvre la session de
 * mise à jour du module                                                */
static int on_file(const char *name, uint32_t size, void *ctx)
{
  ota_ctx_t *c = (ota_ctx_t *)ctx;
  int        st;

  c->announced = size;
  if (!ends_with_nocase(name, ".ota"))
  {
    (void)snprintf(c->reason, sizeof(c->reason), "« %s » : fichier .ota attendu (.xz.ota ou .bin.ota)", name);
    return -1;
  }
  if (contains_nocase(name, "t02") || contains_nocase(name, "mfg"))
  {
    (void)snprintf(c->reason, sizeof(c->reason),
                   "« %s » : image T02 ou de test (mfg) ; seule une image « mission T01 » convient", name);
    return -1;
  }
  if (!contains_nocase(name, "t01"))
  {
    (void)snprintf(c->reason, sizeof(c->reason),
                   "« %s » : nom inattendu, utiliser st67w611m_mission_t01_vX.Y.Z.xz.ota", name);
    return -1;
  }
  if ((size != 0U) && ((size <= NCP_FWU_HEADER_SIZE) || (size > (NCP_FWU_MAX_IMAGE + NCP_FWU_HEADER_SIZE))))
  {
    (void)snprintf(c->reason, sizeof(c->reason), "taille invalide (%lu octets)", (unsigned long)size);
    return -1;
  }
  st = ncp_fwu_begin();
  if (st != AT_OK)
  {
    (void)snprintf(c->reason, sizeof(c->reason), "le module refuse d'ouvrir la mise à jour (%s)", app_err_str(st));
    return -1;
  }
  c->started = true;
  io_led_set(0U);
  return 0;
}

/* Rappel YMODEM pour chaque bloc de données : le premier bloc doit contenir
 * un en-tête .ota valide ; tout est transmis au module (ncp_fwu_write) et
 * la progression s'affiche (LED bleue)                                 */
static int on_data(const uint8_t *data, uint32_t len, void *ctx)
{
  ota_ctx_t *c = (ota_ctx_t *)ctx;
  int        st;

  /* En-tête de 512 octets : contrôlé avant tout envoi au module */
  while ((len > 0U) && !c->header_ok)
  {
    uint32_t k = (uint32_t)NCP_FWU_HEADER_SIZE - c->hlen;
    k = (len < k) ? len : k;
    memcpy(&c->hbuf[c->hlen], data, k);
    c->hlen = (uint16_t)(c->hlen + k);
    data = &data[k];
    len -= k;
    if (c->hlen == NCP_FWU_HEADER_SIZE)
    {
      if (!ncp_fwu_parse_header(c->hbuf, NCP_FWU_HEADER_SIZE, &c->hdr))
      {
        (void)snprintf(c->reason, sizeof(c->reason),
                       "en-tête OTA invalide : ce n'est pas une image .ota du ST67W611M1");
        return -1;
      }
      c->total = NCP_FWU_HEADER_SIZE + c->hdr.length;
      if ((c->announced != 0U) && (c->announced != c->total))
      {
        (void)snprintf(c->reason, sizeof(c->reason),
                       "fichier tronqué ou modifié : %lu octets annoncés, %lu attendus d'après l'en-tête",
                       (unsigned long)c->announced, (unsigned long)c->total);
        return -1;
      }
      c->header_ok = true;
      st = ncp_fwu_write(c->hbuf, NCP_FWU_HEADER_SIZE);
      if (st != AT_OK)
      {
        (void)snprintf(c->reason, sizeof(c->reason), "envoi de l'en-tête au module : %s", app_err_str(st));
        return -1;
      }
      c->fed = NCP_FWU_HEADER_SIZE;
    }
  }
  if (len > 0U)
  {
    uint32_t left = c->total - c->fed;          /* bourrage éventuel ignoré */
    len = (len < left) ? len : left;
    if (len > 0U)
    {
      st = ncp_fwu_write(data, len);
      if (st != AT_OK)
      {
        (void)snprintf(c->reason, sizeof(c->reason), "transfert vers le module interrompu après %lu octets : %s",
                       (unsigned long)c->fed, app_err_str(st));
        return -1;
      }
      c->fed += len;
    }
  }
  if (c->total > 0U)
  {
    uint8_t pct = (uint8_t)(((uint64_t)c->fed * 100U) / c->total);
    if (pct != c->last_pct)
    {
      c->last_pct = pct;
      io_led_set(pct);                          /* progression visible sur la LED RVB (bleue) */
    }
  }
  return 0;
}

/* ---- Commande ----------------------------------------------------------------------- */
/* Comment envoyer le fichier depuis le PC (Tera Term, script, sz) */
static void print_instructions(const char *cur)
{
  console_printf("%sMise à jour du firmware du module ST67W611M1 (OTA par le terminal)%s\n", C_BOLD(), C_RST());
  console_printf("  Firmware actuel   : %s\n", cur);
  console_puts("  Fichier à envoyer : st67w611m_mission_t01_vX.Y.Z.xz.ota (ou .bin.ota), dossier\n"
               "                      NCP_Binaries de X-CUBE-ST67W61 sur GitHub (guide, chapitre 3)\n"
               "  Tera Term   : menu Fichier > Transfert > YMODEM > Envoyer (File > Transfer > YMODEM > Send)\n"
               "  Script PC   : python Tools/maj_module_ota.py --port <port> <fichier>\n"
               "  Linux/macOS : sz --ymodem <fichier> (paquet lrzsz)\n");
  console_printf("  Pendant le transfert (1 à 3 min), le terminal n'affiche rien ; la LED RVB\n"
                 "  (bleue) indique la progression. Des « C » s'affichent en attendant : normal.\n"
                 "  Ctrl-C ou q pour annuler avant le début.\n");
  console_printf("%sEn attente du fichier (YMODEM)...%s\n", C_INFO(), C_RST());
}

/* Après un transfert interrompu : laisse l'émetteur se taire et vide la réception */
static void end_of_transfer_cleanup(void)
{
  /* Laisse l'émetteur se taire puis oublie ses derniers octets (CAN, EOT...) */
  uint32_t t0 = HAL_GetTick();
  while ((HAL_GetTick() - t0) < 300U)
  {
    (void)console_getc();
  }
  console_rx_flush();
}

/* Voir module_ota.h */
int module_ota_run(void)
{
  ymodem_cfg_t    cfg;
  ymodem_result_t res;
  char            before[24];
  char            after[24];
  bool            log_was;
  bool            web_was;
  uint8_t         led_saved;
  uint8_t         rgb_saved[3];
  uint32_t        t0;
  int             st;

  if (!g_app.ncp_ok)
  {
    console_printf("%s✗ module ota : le module ne répond pas avec un firmware « mission T01 ».%s\n", C_ERR(), C_RST());
    console_puts("  La mise à jour par le terminal passe par ce firmware : pour un module d'usine\n"
                 "  trop ancien (en-tête SPI 0x5501) ou sans firmware, suivre le guide, chapitre 3\n"
                 "  (première programmation).\n");
    return APP_ERR_NOMODULE;
  }

  memset(&s_ota, 0, sizeof(s_ota));
  s_ota.last_pct = 0xFFU;
  current_version(before, sizeof(before));
  print_instructions(before);

  memset(&cfg, 0, sizeof(cfg));
  cfg.getc = io_getc;
  cfg.putc = io_putc;
  cfg.tick = io_tick;
  cfg.on_file = on_file;
  cfg.on_data = on_data;
  cfg.ctx = &s_ota;
  cfg.start_timeout_ms = OTA_START_TIMEOUT_MS;

  led_saved = io_led_get();
  io_led_color_get(rgb_saved);
  io_led_color_set(k_blue);
  log_was = at_get_log();
  at_set_log(false);                  /* aucun texte ne doit se mêler au protocole */
  console_rx_flush();
  t0 = HAL_GetTick();
  st = ymodem_receive(&cfg, &res);
  at_set_log(log_was);
  end_of_transfer_cleanup();
  console_puts("\n");

  if ((st == YM_OK) && (!s_ota.header_ok || (s_ota.fed != s_ota.total)))
  {
    (void)snprintf(s_ota.reason, sizeof(s_ota.reason), "fichier incomplet : %lu octets sur %lu",
                   (unsigned long)s_ota.fed, (unsigned long)s_ota.total);
    st = YM_ERR_SIZE;
  }
  if (st == YM_OK)
  {
    st = ncp_fwu_end();
    if (st != AT_OK)
    {
      (void)snprintf(s_ota.reason, sizeof(s_ota.reason), "envoi du dernier bloc au module : %s", app_err_str(st));
      st = YM_ERR_SINK;
    }
  }
  if (st != YM_OK)
  {
    int code = (st == YM_ERR_USER_CANCEL) ? APP_ERR_CANCELED : ((st == YM_ERR_SINK) ? APP_ERR_BADFILE : APP_ERR_TRANSFER);
    io_led_color_set(rgb_saved);
    io_led_set(led_saved);
    console_printf("%s✗ module ota : %s%s\n", C_ERR(),
                   ((st == YM_ERR_SINK) && (s_ota.reason[0] != '\0')) ? s_ota.reason : ymodem_status_str(st), C_RST());
    if (s_ota.started)
    {
      (void)ncp_fwu_abort();
      console_puts("  Mise à jour abandonnée : le module conserve son firmware actuel.\n");
    }
    return code;
  }

  console_printf("%s✓ Fichier reçu : %s (%lu octets en %lu s)%s\n", C_OK(), res.name, (unsigned long)res.received,
                 (unsigned long)((HAL_GetTick() - t0 + 500U) / 1000U), C_RST());
  console_printf("  Image %s, %s, %lu octets après l'en-tête\n", s_ota.hdr.version,
                 (strcmp(s_ota.hdr.type, "XZ") == 0) ? "compressée (XZ)" : "non compressée",
                 (unsigned long)s_ota.hdr.length);
  if (version_cmp(s_ota.hdr.version, before) == 0)
  {
    console_printf("%s  Même version que le firmware en place : réinstallation.%s\n", C_DIM(), C_RST());
  }
  else if (version_cmp(s_ota.hdr.version, before) < 0)
  {
    console_printf("%s  Version plus ancienne que le firmware en place : le module peut la refuser.%s\n",
                   C_WARN(), C_RST());
  }
  else
  {
    /* version plus récente : cas normal */
  }
  console_puts("Installation par le module : vérification de l'image puis redémarrage (jusqu'à 40 s)...\n");
  console_flush();

  web_was = web_running();
  web_module_restarted();
  mqttapp_module_restarted();
  st = ncp_fwu_finish(OTA_READY_TIMEOUT_MS);
  io_led_color_set(rgb_saved);
  io_led_set(led_saved);
  if (st == NCP_FWU_ERR_REJECTED)
  {
    (void)app_report(st, "installation");
    (void)ncp_fwu_abort();
    console_puts("  Le module conserve son firmware actuel.\n");
  }
  else if (st != AT_OK)
  {
    console_printf("%s  Le module n'a pas signalé son redémarrage (%s) : réinitialisation.%s\n", C_WARN(),
                   app_err_str(st), C_RST());
  }
  else
  {
    /* redémarrage signalé par « ready » */
  }

  HAL_Delay(500U);
  if (app_module_start(true) != AT_OK)
  {
    console_printf("%s  Le module ne répond plus : « module reset », puis voir le guide (chapitre 11).%s\n",
                   C_WARN(), C_RST());
    return g_app.ncp_err;
  }
  netapp_module_restarted();
  if (web_was)
  {
    console_puts("  Point d'accès et serveur web : relancés automatiquement dans quelques secondes.\n");
  }

  current_version(after, sizeof(after));
  if (st == NCP_FWU_ERR_REJECTED)
  {
    return st;
  }
  if (version_cmp(after, s_ota.hdr.version) == 0)
  {
    console_printf("%s✓ Firmware du module mis à jour : %s -> %s%s\n", C_OK(), before, after, C_RST());
    return 0;
  }
  console_printf("%s  Version signalée par le module : %s (attendue : %s).%s\n", C_WARN(), after, s_ota.hdr.version,
                 C_RST());
  return NCP_FWU_ERR_REJECTED;
}
