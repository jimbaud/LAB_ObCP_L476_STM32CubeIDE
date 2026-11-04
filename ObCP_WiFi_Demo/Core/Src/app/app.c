/**
 ******************************************************************************
 * @file    app.c
 * @brief   Application de démonstration (voir app.h) : démarrage, boucle
 *          principale, tâches de fond et fonctions communes aux démos.
 *
 *  Pour suivre le programme, partir de app_init() puis de app_loop() :
 *
 *    main()  (Core/Src/main.c, généré par CubeMX)
 *     ├─ initialisation du matériel (MX_xxx_Init), board_post_init()
 *     ├─ app_init() : console, configuration, LED/capteurs, bannière,
 *     │               module Wi-Fi, connexion et services automatiques
 *     └─ while (1) app_loop() :
 *          ├─ connection_poll() : terminal (ré)ouvert ?
 *          ├─ console_readline() + cli_exec() : commande tapée
 *          └─ app_idle() : ncp_poll() (trames du module), netapp_poll()
 *                          (Wi-Fi, NTP), web_poll() (serveur web),
 *                          mqttapp_poll() (MQTT), boutons
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "app_config.h"
#include "board.h"
#include "config.h"
#include "console.h"
#include "demos.h"
#include "io.h"
#include "color.h"
#include "util.h"

app_t g_app;          /* état général de l'application (voir app.h) */

/* -------------------------------------------------------------------------- */
/* Journal du trafic AT (commande « log on ») : les mots de passe sont masqués */
/* -------------------------------------------------------------------------- */
/* Remplace par des '*' le texte du index-ième champ entre guillemets :
 * AT+CWJAP="Box","secret" -> AT+CWJAP="Box","******" (index = 2)       */
static void mask_quoted(char *s, int index)
{
  int   q = 0;
  bool  in = false;
  char *p;

  for (p = s; *p != '\0'; p++)
  {
    if (*p == '"')
    {
      in = !in;
      if (in)
      {
        q++;
      }
      continue;
    }
    if (in && (q == index))
    {
      *p = '*';
    }
  }
}

/* Voir ncp_at.h : remplace la version vide du pilote ; affiche chaque ligne
 * échangée avec le module (caractères de contrôle remplacés par des '.') */
void ncp_at_log_hook(const char *dir, const char *text, size_t len)
{
  char   buf[132];
  size_t n = (len < (sizeof(buf) - 1U)) ? len : (sizeof(buf) - 1U);
  size_t i;

  for (i = 0U; i < n; i++)
  {
    buf[i] = (((unsigned char)text[i] < 0x20U) || (text[i] == 0x7F)) ? '.' : text[i];
  }
  buf[n] = '\0';
  if (at_starts(buf, "AT+CWJAP=") || at_starts(buf, "AT+CWSAP="))
  {
    mask_quoted(buf, 2);
  }
  else if (at_starts(buf, "AT+MQTTUSERCFG="))
  {
    mask_quoted(buf, 3);
  }
  else
  {
    /* rien à masquer */
  }
  console_event("%s  %s %s%s%s", C_DIM(), dir, buf, (len > n) ? " [...]" : "", C_RST());
}

/* -------------------------------------------------------------------------- */
/* Voir app.h. Chaque couche a sa plage de codes : AT (0 à -6), Wi-Fi (-20 à
 * -24), MQTT (-30), démarrage du module (-40 à -43), application (-50...). */
const char *app_err_str(int err)
{
  static char tmp[24];

  if ((err <= 0) && (err >= AT_SENDFAIL))
  {
    return at_status_str((at_status_t)err);
  }
  if ((err <= WIFI_ERR_REASON) && (err >= WIFI_ERR_NOTCONN))
  {
    return wifi_err_str(err);
  }
  switch (err)
  {
    case MQTT_ERR_NOTCONN:
      return "connexion au broker impossible (adresse, port, accès Internet ?)";
    case NCP_ERR_NOREADY:
    case NCP_ERR_ARCH:
    case NCP_FWU_ERR_REJECTED:
    case NCP_FWU_ERR_STATE:
      return ncp_err_str(err);
    case APP_ERR_NOMODULE:
      return "module Wi-Fi non disponible (voir « module reset »)";
    case APP_ERR_NOSTA:
      return "station non connectée (commande « connect »)";
    case APP_ERR_USAGE:
      return "arguments invalides";
    case APP_ERR_FLASH:
      return "écriture en flash impossible";
    case APP_ERR_NOTFOUND:
      return "introuvable";
    case APP_ERR_CANCELED:
      return "annulé";
    case APP_ERR_TRANSFER:
      return "échec du transfert du fichier";
    case APP_ERR_BADFILE:
      return "fichier refusé";
    default:
      (void)snprintf(tmp, sizeof(tmp), "erreur %d", err);
      return tmp;
  }
}

/* Voir app.h */
int app_report(int err, const char *what)
{
  console_printf("%s\xE2\x9C\x97 %s : %s%s\n", C_ERR(), what, app_err_str(err), C_RST());
  return err;
}

/* -------------------------------------------------------------------------- */
/* Voir app.h */
void app_print_module_info(void)
{
  char mac[20];

  util_fmt_mac(mac, sizeof(mac), g_app.sta_mac);
  /* La version du firmware est celle du SDK (2.0.x) ; « AT version » (1.0.0) est
   * celle du jeu de commandes AT. */
  if (g_app.ncp.sdk_version[0] != '\0')
  {
    console_printf("  Firmware     : %s\n", g_app.ncp.sdk_version);
    console_printf("  Version AT   : %s\n", (g_app.ncp.at_version[0] != '\0') ? g_app.ncp.at_version : "?");
  }
  else
  {
    console_printf("  Firmware AT  : %s\n", (g_app.ncp.at_version[0] != '\0') ? g_app.ncp.at_version : "?");
  }
  if (g_app.ncp.mac_version[0] != '\0')
  {
    console_printf("  MAC Wi-Fi 6  : %s\n", g_app.ncp.mac_version);
  }
  console_printf("  Adresse MAC  : %s\n", mac);
}

/* Voir app.h : démarrage du module (ncp_start), puis initialisation du
 * Wi-Fi (pays) et du réseau (nom d'hôte, adresse du point d'accès). En
 * cas d'échec, un conseil adapté à la cause est affiché.               */
int app_module_start(bool verbose)
{
  int st;

  g_app.ncp_ok = false;
  if (verbose)
  {
    console_puts("Démarrage du module Wi-Fi ST67W611M1... ");
  }
  st = ncp_start(&g_app.ncp);
  if (st == AT_OK)
  {
    st = wifi_init(g_cfg.country);
  }
  if (st == AT_OK)
  {
    st = net_init(config_hostname(), g_cfg.ap_ip);
  }
  if (st != AT_OK)
  {
    g_app.ncp_err = st;
    if (verbose)
    {
      console_printf("%séchec%s\n", C_ERR(), C_RST());
    }
    (void)app_report(st, "module Wi-Fi");
    if (ncp_spi_stats()->hdr_errors > 0U)
    {
      console_printf("%s  Le module répond sur le SPI avec un en-tête invalide (magic 0x%04X au lieu de 0x55AA) :\n",
                     C_WARN(), (unsigned)ncp_spi_stats()->last_bad_magic);
      console_printf("  son firmware est trop ancien ou d'un autre type. Le mettre à jour avec le binaire\n"
                     "  st67w611m_mission_t01_v2.0.106.bin (guide, chapitre 3 ; « help module »).%s\n", C_RST());
    }
    else if ((st == NCP_ERR_NOREADY) || (st == AT_TIMEOUT) || (st == AT_IO))
    {
      console_printf("%s  Vérifier : X-NUCLEO-67W61M1 bien enfichée, cavaliers JP1 et JP2 en place,\n"
                     "  firmware « mission T01 » programmé dans le module (guide, chapitre 3).\n"
                     "  Puis : « module reset ».%s\n", C_WARN(), C_RST());
    }
    return st;
  }
  (void)net_sta_mac(g_app.sta_mac);
  mqtt_set_msg_cb(mqttapp_on_message);
  g_app.ncp_ok = true;
  g_app.ncp_err = 0;
  if (verbose)
  {
    console_printf("%sOK%s\n", C_OK(), C_RST());
    app_print_module_info();
  }
  return AT_OK;
}

/* -------------------------------------------------------------------------- */
/* Voir app.h : texte construit morceau par morceau (pos = longueur écrite) */
void app_autostart_summary(char *out, size_t len)
{
  bool   wifi = (g_cfg.sta_auto != 0U) && (g_cfg.sta_ssid[0] != '\0');
  size_t pos = 0U;
  int    n;

  out[0] = '\0';
  if (wifi)
  {
    n = snprintf(out, len, "connexion à « %s »", g_cfg.sta_ssid);
    pos = ((n > 0) && ((size_t)n < len)) ? (size_t)n : 0U;
  }
  if ((g_cfg.web_auto != 0U) && (pos < len))
  {
    n = snprintf(&out[pos], len - pos, "%sserveur web", (pos > 0U) ? ", " : "");
    pos += ((n > 0) && ((size_t)n < (len - pos))) ? (size_t)n : 0U;
  }
  if ((g_cfg.mqtt_auto != 0U) && (pos < len))
  {
    n = snprintf(&out[pos], len - pos, "%sMQTT%s", (pos > 0U) ? ", " : "", wifi ? "" : " (sans réseau enregistré)");
    pos += ((n > 0) && ((size_t)n < (len - pos))) ? (size_t)n : 0U;
  }
  if (pos == 0U)
  {
    (void)util_strlcpy(out, "aucun service (commande « auto »)", len);
  }
}

/* -------------------------------------------------------------------------- */
/* Voir app.h. Exemple de résultat (abrégé) :
 * {"id":"A1B2C3","fw":"1.2.0","uptime":42,"temp":24.5,"vdda":3.30,"ax":0.012,
 *  ...,"led":40,"rgb":"#FFFFFF","pwm":0,"rssi":-47,"ssid":"Box","mqtt":true} */
size_t app_status_json(char *out, size_t len, bool with_rssi)
{
  jbuf_t       j;
  io_sensors_t s;
  char         tmp[24];
  char         iso[24];
  char         hex[8];
  uint8_t      rgb[3];
  int16_t      rssi = 0;
  bool         have_rssi = false;

  io_read(&s);
  if (with_rssi && g_app.ncp_ok && wifi_is_connected())
  {
    have_rssi = (wifi_rssi(&rssi) == AT_OK);
  }

  jb_init(&j, out, len);
  jb_open(&j, '{');
  jb_kv_str(&j, "id", board_uid_short());
  jb_kv_str(&j, "fw", APP_VERSION);
  jb_kv_int(&j, "uptime", (long)(HAL_GetTick() / 1000U));
  util_fmt_fixed(tmp, sizeof(tmp), s.temp_x10, 1U);
  jb_kv_raw(&j, "temp", tmp);
  util_fmt_fixed(tmp, sizeof(tmp), (int32_t)((s.vdda_mv + 5U) / 10U), 2U);
  jb_kv_raw(&j, "vdda", tmp);
  if (s.acc_ok)
  {
    util_fmt_fixed(tmp, sizeof(tmp), s.acc_mg[0], 3U);
    jb_kv_raw(&j, "ax", tmp);
    util_fmt_fixed(tmp, sizeof(tmp), s.acc_mg[1], 3U);
    jb_kv_raw(&j, "ay", tmp);
    util_fmt_fixed(tmp, sizeof(tmp), s.acc_mg[2], 3U);
    jb_kv_raw(&j, "az", tmp);
    util_fmt_fixed(tmp, sizeof(tmp), s.pitch_x10, 1U);
    jb_kv_raw(&j, "pitch", tmp);
    util_fmt_fixed(tmp, sizeof(tmp), s.roll_x10, 1U);
    jb_kv_raw(&j, "roll", tmp);
  }
  else
  {
    jb_kv_raw(&j, "ax", "null");
  }
  jb_kv_int(&j, "led", io_led_get());
  io_led_color_get(rgb);
  color_hex(rgb, hex);
  jb_kv_str(&j, "rgb", hex);
  jb_kv_int(&j, "pwm", io_pwm_get());
  jb_kv_bool(&j, "user", s.user);
  jb_kv_bool(&j, "sw1", s.sw1);
  jb_kv_int(&j, "user_count", (long)io_button_count(IO_BTN_USER));
  jb_kv_int(&j, "sw1_count", (long)io_button_count(IO_BTN_SW1));
  if (have_rssi)
  {
    jb_kv_int(&j, "rssi", rssi);
  }
  else
  {
    jb_kv_raw(&j, "rssi", "null");
  }
  jb_kv_str(&j, "ssid", (g_app.ncp_ok && wifi_is_connected()) ? g_cfg.sta_ssid : "");
  jb_kv_str(&j, "ip", netapp_ip());
  netapp_iso_utc(iso, sizeof(iso));
  if (iso[0] != '\0')
  {
    jb_kv_str(&j, "time", iso);
  }
  else
  {
    jb_kv_raw(&j, "time", "null");
  }
  jb_kv_int(&j, "ap_clients", g_wifi.ap_running ? g_wifi.ap_clients : 0);
  jb_kv_bool(&j, "mqtt", mqttapp_connected());
  jb_close(&j, '}');
  return j.overflow ? 0U : j.pos;
}

/* -------------------------------------------------------------------------- */
/* Bannière de démarrage : version, identifiant, horloges, capteurs */
static void print_banner(void)
{
  const char *line = "==============================================================\n";

  console_printf("\n%s%s", C_INFO(), line);
  console_printf("  %s%s%s%s - version %s\n", C_BOLD(), APP_NAME, C_RST(), C_INFO(), APP_VERSION);
  console_printf("  %s - identifiant %s\n", APP_BOARD, board_uid_short());
  console_printf("%s%s", line, C_RST());
  console_printf("  MCU          : STM32L476RG à %lu MHz, quartz 8 MHz %s, quartz 32 kHz %s\n",
                 (unsigned long)(HAL_RCC_GetSysClockFreq() / 1000000UL),
                 board_hse_ok() ? "OK" : "non utilisé (horloge interne)",
                 board_lse_ok() ? "OK" : "non utilisé (RTC sur LSI, moins précise)");
  if (io_acc_name() != NULL)
  {
    console_printf("  Capteurs     : accéléromètre %s OK, LED RVB, sortie PWM\n", io_acc_name());
  }
  else
  {
    console_printf("  Capteurs     : %saccéléromètre absent ou non reconnu (WHO_AM_I = 0x%02X)%s, LED RVB, sortie PWM\n",
                   C_WARN(), (unsigned)io_acc_whoami(), C_RST());
  }
}

/* Rapport d'une faute survenue avant le dernier redémarrage */
static void print_fault_report(void)
{
  static const char *const names[] = { "HardFault", "MemManage", "BusFault", "UsageFault", "NMI" };
  board_fault_t f;

  if (!board_fault_take(&f))
  {
    return;
  }
  console_printf("  %sRedémarrage après une faute %s : PC=0x%08lX LR=0x%08lX CFSR=0x%08lX HFSR=0x%08lX BFAR=0x%08lX%s\n",
                 C_ERR(), names[(f.kind < 5U) ? f.kind : 0U], (unsigned long)f.pc, (unsigned long)f.lr,
                 (unsigned long)f.cfsr, (unsigned long)f.hfsr, (unsigned long)f.bfar, C_RST());
}

/* Voir app.h */
void app_init(void)
{
  bool factory = false;

  /* 1) console, configuration enregistrée, LED / PWM / accéléromètre */
  console_init();
  g_app.cfg_load_status = config_load();
  io_init();

  /* User1 maintenu pendant le démarrage : valeurs d'usine (non enregistrées) */
  if (HAL_GPIO_ReadPin(BTN_USER_PORT, BTN_USER_PIN) == GPIO_PIN_RESET)
  {
    cfg_defaults(&g_cfg);
    factory = true;
    g_app.cfg_dirty = true;
  }
  console_set_color(g_cfg.color != 0U);

  /* 2) bannière (conservée dans le tampon USB jusqu'à l'ouverture du terminal) */
  print_banner();
  print_fault_report();
  if (factory)
  {
    console_printf("  Config.      : %sUser1 appuyé au démarrage -> valeurs d'usine (« save » pour les garder)%s\n",
                   C_WARN(), C_RST());
  }
  else if (g_app.cfg_load_status == CFG_UNPACK_OK)
  {
    console_printf("  Config.      : chargée depuis la flash (réseau « %s »)\n",
                   (g_cfg.sta_ssid[0] != '\0') ? g_cfg.sta_ssid : "aucun");
  }
  else if (g_app.cfg_load_status == CFG_UNPACK_EMPTY)
  {
    console_puts("  Config.      : valeurs d'usine (aucune configuration enregistrée)\n");
  }
  else
  {
    console_printf("  Config.      : %sconfiguration en flash invalide ou d'une version plus récente -> valeurs d'usine%s\n",
                   C_WARN(), C_RST());
  }
  {
    char summary[96];
    app_autostart_summary(summary, sizeof(summary));
    console_printf("  Démarrage    : %s\n", summary);
  }
  console_puts("\n");

  /* 3) module Wi-Fi, puis services automatiques : connexion à la box
   *    (wifi.auto), serveur web (web.auto) ; MQTT (mqtt.auto) démarre
   *    ensuite de lui-même dès que la connexion à la box est établie.  */
  (void)app_module_start(true);
  if (g_app.ncp_ok)
  {
    netapp_boot();
    web_boot();
  }
  console_printf("\nTapez %shelp%s pour la liste des commandes, %shelp demo%s pour le guide rapide.\n\n",
                 C_BOLD(), C_RST(), C_BOLD(), C_RST());
  console_prompt();
}

/* Voir app.h */
void app_idle(void)
{
  static bool busy;           /* app_idle() déjà en cours (appel imbriqué) */
  uint8_t     btn;

  if (busy)
  {
    return;
  }
  busy = true;

  /* Trames du module ; redémarrage spontané du module : chaque démo remet
   * son état à zéro, puis le module est réinitialisé.                 */
  if (g_app.ncp_ok)
  {
    ncp_poll();
    if (ncp_restarted())
    {
      g_app.module_restarts++;
      console_event("%s\xE2\x9A\xA0 Le module Wi-Fi a redémarré de lui-même : réinitialisation...%s",
                    C_WARN(), C_RST());
      web_module_restarted();
      mqttapp_module_restarted();
      if (app_module_start(false) == AT_OK)
      {
        netapp_module_restarted();
      }
    }
  }
  /* Tâches de fond des démos 2, 3 et 4 */
  if (g_app.ncp_ok)
  {
    netapp_poll();
    web_poll();
    mqttapp_poll();
  }

  /* Boutons : User1 bascule la LED ; les appuis sont publiés sur MQTT */
  btn = io_buttons_poll();
  if ((btn & IO_BTN_USER) != 0U)
  {
    io_led_toggle();
    console_event("Bouton User1 : LED %s (%u %%)", (io_led_get() > 0U) ? "allumée" : "éteinte",
                  (unsigned)io_led_get());
    mqttapp_button(IO_BTN_USER);
  }
  if ((btn & IO_BTN_SW1) != 0U)
  {
    console_event("Bouton SW1 (X-NUCLEO) appuyé");
    mqttapp_button(IO_BTN_SW1);
  }
  busy = false;
}

/* Terminal (ré)ouvert sur le port COM virtuel : le contenu du tampon USB
 * (message de démarrage...) lui est envoyé automatiquement ; à partir de la
 * deuxième ouverture, rappel de l'identité de la carte et de l'invite.     */
static void connection_poll(void)
{
  bool     first = false;
  uint32_t lost;

  if (!console_new_connection(&first))
  {
    return;
  }
  lost = console_take_dropped();
  if (lost > 0U)
  {
    console_printf("\n%s(%lu octets de messages perdus pendant que le terminal était fermé)%s\n", C_DIM(),
                   (unsigned long)lost, C_RST());
  }
  if (!first)
  {
    console_set_prompt_visible(false);
    console_printf("\n%s%s%s - %s - identifiant %s : terminal reconnecté (%shelp%s pour l'aide)\n",
                   C_BOLD(), APP_NAME, C_RST(), APP_BOARD, board_uid_short(), C_BOLD(), C_RST());
    console_prompt();
  }
}

/* Voir app.h */
void app_loop(void)
{
  char line[CONSOLE_LINE_MAX];

  connection_poll();

  if (console_readline(line, sizeof(line)))
  {
    (void)cli_exec(line);
    console_prompt();
  }
  app_idle();
}
