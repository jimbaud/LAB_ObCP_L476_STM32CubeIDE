/**
 ******************************************************************************
 * @file    cli.c
 * @brief   Interpréteur de commandes du terminal : table des commandes, aide,
 *          commandes générales (configuration, LED, capteurs, outils AT).
 *
 *  Principe : chaque commande est une ligne de la table s_cmds[] (nom,
 *  alias, fonction à appeler, groupe d'aide, syntaxe, textes d'aide).
 *  cli_exec() découpe la ligne tapée en mots (util_tokenize), cherche le
 *  premier mot dans la table et appelle la fonction correspondante avec
 *  argc / argv, comme main() en C. Les commandes des démos sont dans
 *  demo_scan.c, demo_net.c, demo_web.c et demo_mqtt.c.
 *
 *  Ajouter une commande « bonjour » :
 *    1. écrire  static int cmd_bonjour(int argc, char **argv) { ... return 0; }
 *       et déclarer son prototype avec les autres ci-dessous ;
 *    2. ajouter dans s_cmds[] :
 *       { "bonjour", NULL, cmd_bonjour, 0U, false, "bonjour", "dit bonjour", NULL },
 *    elle apparaît alors dans « help » et dans « help bonjour ».
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "app.h"
#include "app_config.h"
#include "board.h"
#include "config.h"
#include "console.h"
#include "demos.h"
#include "io.h"
#include "module_ota.h"
#include "color.h"
#include "uart_bridge.h"
#include "util.h"

#define ARGV_MAX   12        /* mots au plus sur une ligne de commande */

/* Description d'une commande (une ligne de la table s_cmds[]) */
typedef struct
{
  const char *name;          /* nom tapé                                         */
  const char *alias;         /* autre nom accepté (français...), peut être NULL  */
  cmd_fn_t    fn;            /* fonction exécutée                                */
  uint8_t     group;         /* rubrique de l'aide (index dans s_groups[])       */
  bool        needs_module;  /* refusée si le module Wi-Fi n'a pas démarré       */
  const char *usage;         /* syntaxe affichée par l'aide                      */
  const char *help;          /* description en une ligne                         */
  const char *detail;        /* aide détaillée (help <commande>), peut être NULL */
} cmd_t;

static int cmd_help(int argc, char **argv);
static int cmd_info(int argc, char **argv);
static int cmd_config(int argc, char **argv);
static int cmd_set(int argc, char **argv);
static int cmd_save(int argc, char **argv);
static int cmd_auto(int argc, char **argv);
static int cmd_factory(int argc, char **argv);
static int cmd_reboot(int argc, char **argv);
static int cmd_led(int argc, char **argv);
static int cmd_pwm(int argc, char **argv);
static int cmd_sensors(int argc, char **argv);
static int cmd_dfu(int argc, char **argv);
static int cmd_log(int argc, char **argv);
static int cmd_stats(int argc, char **argv);
static int cmd_module(int argc, char **argv);
static int cmd_at(int argc, char **argv);

/* Rubriques de l'aide (champ group de cmd_t) */
static const char *const s_groups[] =
{
  "Général",
  "Démo 1 - Scanner Wi-Fi et suivi du signal",
  "Démo 2 - Connexion et services réseau",
  "Démo 3 - Serveur web en point d'accès (SoftAP)",
  "Démo 4 - Télémétrie MQTT",
  "Outils avancés"
};

/* Table des commandes : l'ordre est celui de l'aide */
static const cmd_t s_cmds[] =
{
  { "help", "aide", cmd_help, 0U, false, "help [commande | demo]",
    "aide, détail d'une commande, guide rapide des démos", NULL },
  { "info", NULL, cmd_info, 0U, false, "info",
    "état de la carte, du module et des services", NULL },
  { "config", NULL, cmd_config, 0U, false, "config [filtre]",
    "liste des paramètres (ex. « config mqtt »)", NULL },
  { "set", NULL, cmd_set, 0U, false, "set <clé> <valeur>",
    "modifie un paramètre en RAM",
    "  Exemples :\n"
    "    set wifi.ssid \"Ma Box\"        (guillemets si le texte contient des espaces)\n"
    "    set wifi.pass motdepasse\n"
    "    set time.tz 60                (UTC+1, heure d'été gérée par time.dst)\n"
    "    set mqtt.host test.mosquitto.org\n"
    "  « config » liste toutes les clés ; « save » enregistre en flash.\n" },
  { "save", NULL, cmd_save, 0U, false, "save",
    "enregistre les paramètres en flash", NULL },
  { "auto", NULL, cmd_auto, 0U, false, "auto [web | mqtt | wifi] [on | off]",
    "services lancés à la mise sous tension (batterie)",
    "  auto              : ce qui démarre seul à la mise sous tension\n"
    "  auto web on       : point d'accès + serveur web (web.auto)\n"
    "  auto mqtt on      : télémétrie MQTT après la connexion à la box (mqtt.auto)\n"
    "  auto wifi off     : plus de connexion automatique à la box (wifi.auto)\n"
    "  Puis « save » pour enregistrer. Sur batterie, sans terminal, la carte rejoint\n"
    "  la box enregistrée, publie sur MQTT et/ou ouvre son point d'accès. Le réglage\n"
    "  se fait aussi depuis la page web (carte « Démarrage automatique »).\n" },
  { "factory", NULL, cmd_factory, 0U, false, "factory [erase]",
    "valeurs d'usine (erase : efface aussi la flash)",
    "  Astuce : maintenir le bouton User1 de l'ObCP pendant le reset charge aussi\n"
    "  les valeurs d'usine, sans toucher à la flash.\n" },
  { "reboot", NULL, cmd_reboot, 0U, false, "reboot",
    "redémarre la carte", NULL },
  { "led", NULL, cmd_led, 0U, false, "led [<0-100> | on | off] [couleur]",
    "LED RVB de l'ObCP : luminosité et couleur",
    "  Exemples :\n"
    "    led 50            luminosité 50 % (couleur inchangée)\n"
    "    led rouge         couleur rouge (allume la LED si elle était éteinte)\n"
    "    led bleu 20       couleur et luminosité\n"
    "    led #FF8000       notation hexadécimale, ou led 255,128,0\n"
    "    led off | on      extinction / rallumage\n"
    "  Couleurs : rouge vert bleu jaune cyan magenta blanc orange violet rose.\n"
    "  Le bouton User1 de l'ObCP allume ou éteint la LED.\n" },
  { "pwm", NULL, cmd_pwm, 0U, false, "pwm <0-100 | on | off>",
    "sortie PWM du bornier de l'ObCP (1 kHz)",
    "  Rapport cyclique de 0 à 100 % sur le bornier PWM (J13) : la charge (moteur,\n"
    "  ventilateur, bande de LED... 2,5 W au plus) se branche entre +5 V et la borne\n"
    "  PWM, le transistor IRLML2803 de l'ObCP la relie à la masse.\n" },
  { "sensors", "capteurs", cmd_sensors, 0U, false, "sensors [suivi]",
    "température, VDDA, accéléromètre, boutons",
    "  Température : capteur interne du STM32 (étalonné en usine), donc celle de la puce.\n"
    "  Accéléromètre (LIS3DH ou LIS3DSH) : accélérations en g (1 g = pesanteur) et\n"
    "  inclinaisons (tangage, roulis) calculées quand la carte est immobile.\n"
    "  « sensors suivi » rafraîchit l'affichage jusqu'à l'appui d'une touche.\n" },
  { "time", "heure", cmd_time, 0U, false, "time",
    "heure de la RTC (UTC et locale)", NULL },

  { "scan", NULL, cmd_scan, 1U, true, "scan [passif] [canal <n>] [tri ...] [bssid]",
    "recherche les réseaux Wi-Fi 2,4 GHz",
    "  Exemples :\n"
    "    scan                 balayage actif de tous les canaux, tri par signal\n"
    "    scan passif          écoute des balises seulement (plus lent, discret)\n"
    "    scan canal 6         un seul canal\n"
    "    scan tri canal       tri par canal (tri ssid : ordre alphabétique)\n"
    "    scan bssid           ajoute l'adresse MAC (BSSID) de chaque point d'accès\n"
    "  Colonne Norme : Wi-Fi 6 = 802.11ax, Wi-Fi 4 = 802.11n (norme la plus récente\n"
    "  annoncée par le point d'accès).\n" },
  { "channels", "canaux", cmd_channels, 1U, true, "channels [appliquer]",
    "occupation des canaux 1 à 13 et canal conseillé",
    "  Score d'occupation : chaque réseau compte sur son canal et sur les canaux\n"
    "  voisins qu'il chevauche, pondéré par la puissance reçue.\n"
    "  « channels appliquer » règle ap.channel sur le meilleur canal parmi 1, 6, 11.\n" },
  { "track", "suivi", cmd_track, 1U, true, "track [ssid] [csv]",
    "suivi du niveau de signal en direct (touche = arrêt)",
    "  Sans SSID : réseau auquel la carte est connectée (sinon le SSID est obligatoire).\n"
    "  Période : paramètre scan.period (ms). La LED RVB suit la qualité du signal\n"
    "  (rouge : faible, jaune : moyen, vert : fort).\n"
    "  « csv » : sortie « temps_ms;rssi_dbm;canal » pour Tools/rssi_logger.py.\n" },

  { "connect", "wifi", cmd_connect, 2U, true, "connect [ssid [mot de passe]]",
    "connexion à un réseau Wi-Fi",
    "  Sans argument : réseau enregistré (wifi.ssid / wifi.pass).\n"
    "  Exemple : connect \"Ma Box\" monmotdepasse   puis « save » pour le mémoriser.\n"
    "  Le ST67W611M1 fonctionne en 2,4 GHz uniquement (pas de 5 GHz).\n" },
  { "disconnect", NULL, cmd_disconnect, 2U, true, "disconnect",
    "déconnexion du réseau", NULL },
  { "status", "etat", cmd_status, 2U, true, "status",
    "état de la liaison Wi-Fi et configuration IP", NULL },
  { "ip", NULL, cmd_ip, 2U, true, "ip [dhcp | <ip> <masque> <passerelle> [dns]]",
    "adressage DHCP ou IP fixe",
    "  Exemples :\n"
    "    ip                                          affiche le mode actuel\n"
    "    ip 192.168.1.50 255.255.255.0 192.168.1.1   IP fixe (DNS = passerelle)\n"
    "    ip 192.168.1.50 255.255.255.0 192.168.1.1 1.1.1.1\n"
    "    ip dhcp                                     retour au DHCP\n"
    "  Si la carte est connectée, la liaison est relancée avec le nouveau réglage.\n" },
  { "dns", NULL, cmd_dns, 2U, true, "dns <nom>",
    "résolution d'un nom en adresse IP", NULL },
  { "ping", NULL, cmd_ping, 2U, true, "ping <hôte> [nombre] [intervalle ms]",
    "test d'accessibilité (ICMP)",
    "  Exemples : ping 8.8.8.8 ; ping www.st.com 10 500\n" },
  { "ntp", NULL, cmd_ntp, 2U, true, "ntp [serveur]",
    "mise à l'heure de la RTC par SNTP",
    "  Serveur par défaut : time.server. Fuseau : time.tz (minutes), heure d'été\n"
    "  européenne : time.dst. La RTC reste en UTC et garde l'heure pendant un reset.\n" },

  { "web", NULL, cmd_web, 3U, true, "web start | stop | status",
    "point d'accès Wi-Fi + serveur web embarqué",
    "  web start : crée le réseau ap.ssid (mot de passe ap.pass) et démarre le\n"
    "  serveur HTTP. Se connecter avec un téléphone puis ouvrir l'adresse affichée.\n"
    "  Pages : tableau de bord (capteurs, LED RVB, sortie PWM), réseaux voisins,\n"
    "  configuration Wi-Fi.\n"
    "  Si la station est connectée, la page est aussi servie sur le réseau local.\n" },

  { "mqtt", NULL, cmd_mqtt, 4U, true, "mqtt start | stop | status | pub | sub | trace",
    "client MQTT : télémétrie et commandes à distance",
    "  mqtt start : connexion au broker mqtt.host, publication périodique sur\n"
    "    <racine>/telemetry, écoute des commandes sur <racine>/cmd.\n"
    "  Commandes acceptées sur <racine>/cmd : led=0..100, color=<nom|#RRGGBB>,\n"
    "    pwm=0..100, period=2..3600, report, ping, msg=<texte>.\n"
    "  mqtt stop | status : arrêt (publie « offline ») / état et compteurs.\n"
    "  mqtt pub <topic> <message> : publie (topic relatif à la racine sauf s'il\n"
    "    commence par '/', ex. mqtt pub /maison/test bonjour).\n"
    "  mqtt sub <topic> : s'abonne à un topic (les messages s'affichent ici).\n"
    "  mqtt trace off : n'affiche plus chaque publication de télémétrie.\n" },

  { "at", NULL, cmd_at, 5U, true, "at <commande>",
    "envoie une commande AT brute (ex. at AT+GMR)",
    "  La ligne est transmise telle quelle ; on peut aussi taper directement\n"
    "  « AT+GMR ». Attention : certaines commandes perturbent l'application.\n" },
  { "log", NULL, cmd_log, 5U, false, "log on | off",
    "affiche le trafic AT avec le module", NULL },
  { "stats", NULL, cmd_stats, 5U, false, "stats",
    "compteurs SPI et AT", NULL },
  { "dfu", NULL, cmd_dfu, 5U, false, "dfu",
    "redémarre l'ObCP dans le chargeur USB DFU (programmation)",
    "  Équivaut à l'interrupteur Prog/Run sur Prog suivi d'un appui sur Reset, sans\n"
    "  y toucher (utile quand la X-NUCLEO les recouvre). Programmer ensuite avec\n"
    "  STM32CubeProgrammer (port USB, Start programming) ; « Run after programming »\n"
    "  ou un reset relance le programme.\n" },
  { "module", NULL, cmd_module, 5U, false, "module [reset | ota | boot]",
    "versions du firmware, redémarrage, mise à jour",
    "  module        : versions du firmware du ST67W611M1 (attendu : mission T01 v2.0.x)\n"
    "  module reset  : redémarre le module (CHIP_EN) et réinitialise le Wi-Fi\n"
    "  module ota    : met à jour le firmware du module avec un fichier envoyé par le\n"
    "                  terminal en YMODEM (Tera Term, Tools/maj_module_ota.py, sz) :\n"
    "                  st67w611m_mission_t01_vX.Y.Z.xz.ota de X-CUBE-ST67W61 (GitHub).\n"
    "                  Aucun câblage : le fichier passe par le lien SPI. Le module doit\n"
    "                  déjà exécuter un firmware mission T01 (guide, chapitre 3).\n"
    "  module boot   : première programmation du module avec QConn_Flash de ST, sans\n"
    "                  adaptateur : le module passe en mode téléchargement et l'ObCP\n"
    "                  relaie le port COM vers son UART (D0/D1). Fermer le terminal puis\n"
    "                  lancer QConn_Flash_Cmd sur ce même port (guide, chapitre 3).\n" },
};

#define N_CMDS   (sizeof(s_cmds) / sizeof(s_cmds[0]))

/* -------------------------------------------------------------------------- */
/* Vrai si s commence par p, sans tenir compte des majuscules */
static bool prefix_nocase(const char *s, const char *p)
{
  while (*p != '\0')
  {
    if (tolower((unsigned char)*s) != tolower((unsigned char)*p))
    {
      return false;
    }
    s++;
    p++;
  }
  return true;
}

/* Commande de la table par son nom ou son alias, NULL si inconnue */
static const cmd_t *find_cmd(const char *name)
{
  size_t i;
  for (i = 0U; i < N_CMDS; i++)
  {
    if (util_strieq(name, s_cmds[i].name) ||
        ((s_cmds[i].alias != NULL) && util_strieq(name, s_cmds[i].alias)))
    {
      return &s_cmds[i];
    }
  }
  return NULL;
}

/* Affiche « Usage : <syntaxe> » d'une commande */
static void print_usage(const cmd_t *c)
{
  console_printf("Usage : %s%s%s\n", C_BOLD(), c->usage, C_RST());
}

/* Rappel de ncp_raw() : chaque ligne de réponse du module est affichée */
static void on_raw_line(const char *line, void *ctx)
{
  (void)ctx;
  console_printf("  %s\n", line);
}

/* Commande AT brute tapée par l'utilisateur (« at AT+GMR » ou « AT+GMR ») */
static int at_raw(const char *cmd)
{
  int st;

  while (*cmd == ' ')
  {
    cmd++;
  }
  if (*cmd == '\0')
  {
    console_puts("Usage : at <commande AT>   (ex. at AT+GMR)\n");
    return APP_ERR_USAGE;
  }
  if (!g_app.ncp_ok)
  {
    return app_report(APP_ERR_NOMODULE, "commande AT");
  }
  console_printf("%s-> %s%s\n", C_DIM(), cmd, C_RST());
  st = ncp_raw(cmd, on_raw_line, NULL, 10000U);
  console_printf("%s%s%s\n", (st == AT_OK) ? C_OK() : C_ERR(), (st == AT_OK) ? "OK" : app_err_str(st), C_RST());
  return st;
}

/* Voir demos.h */
int cli_exec(char *line)
{
  char        *argv[ARGV_MAX];
  int          argc;
  const cmd_t *c;

  while ((*line == ' ') || (*line == '\t'))
  {
    line++;
  }
  if (*line == '\0')
  {
    return 0;
  }
  /* Commande AT brute : « at <cmd> » ou ligne commençant par « AT+ » / « AT » */
  if (prefix_nocase(line, "at ") || prefix_nocase(line, "at+") || util_strieq(line, "at"))
  {
    return at_raw(prefix_nocase(line, "at ") ? &line[3] : line);
  }

  argc = util_tokenize(line, argv, ARGV_MAX);
  if (argc < 0)
  {
    console_printf("%sTrop d'arguments (%d au maximum).%s\n", C_ERR(), ARGV_MAX - 1, C_RST());
    return APP_ERR_USAGE;
  }
  if (argc == 0)
  {
    return 0;
  }
  if (util_strieq(argv[0], "?"))
  {
    return cmd_help(argc, argv);
  }
  c = find_cmd(argv[0]);
  if (c == NULL)
  {
    console_printf("%sCommande inconnue : « %s ». Tapez « help ».%s\n", C_ERR(), argv[0], C_RST());
    return APP_ERR_USAGE;
  }
  if (c->needs_module && !g_app.ncp_ok)
  {
    return app_report(APP_ERR_NOMODULE, c->name);
  }
  return c->fn(argc, argv);
}

/* ---- help -------------------------------------------------------------------- */
/* « help demo » : guide rapide des quatre démos, avec les noms et adresses
 * propres à cette carte (point d'accès, topics MQTT)                    */
static void help_demo(void)
{
  uint8_t ip[4];
  char    ips[16];

  memcpy(ip, g_cfg.ap_ip, 4);
  ip[3] = 1U;
  util_fmt_ip(ips, sizeof(ips), ip);

  console_printf("%sGuide rapide%s\n", C_BOLD(), C_RST());
  console_printf("%s Démo 1 - Scanner Wi-Fi%s\n", C_INFO(), C_RST());
  console_puts("   scan                      réseaux visibles : signal, canal, sécurité, Wi-Fi 6\n"
               "   channels                  occupation des canaux et canal conseillé\n"
               "   track <ssid>              signal en direct (une touche pour arrêter)\n");
  console_printf("%s ObCP - LED RVB, sortie PWM, accéléromètre%s\n", C_INFO(), C_RST());
  console_puts("   led rouge 30    led off    pwm 50    sensors suivi (inclinez la carte)\n");
  console_printf("%s Démo 2 - Connexion et services réseau%s\n", C_INFO(), C_RST());
  console_puts("   connect \"Ma Box\" motdepasse   puis   save   (connexion auto au démarrage)\n"
               "   status    ping 8.8.8.8    dns www.st.com    ntp    time\n"
               "   ip 192.168.1.50 255.255.255.0 192.168.1.1     (ip dhcp pour revenir)\n");
  console_printf("%s Démo 3 - Serveur web en point d'accès%s\n", C_INFO(), C_RST());
  console_printf("   web start                 puis rejoindre le réseau « %s »\n", config_ap_ssid());
  console_printf("                             (mot de passe « %s ») et ouvrir http://%s/\n",
                 (g_cfg.ap_pass[0] != '\0') ? g_cfg.ap_pass : "aucun", ips);
  console_printf("%s Démo 4 - Télémétrie MQTT%s\n", C_INFO(), C_RST());
  console_printf("   mqtt start                publication sur %s/telemetry\n", config_mqtt_root());
  console_printf("   Sur un PC : mosquitto_sub -h %s -t \"%s/#\" -v\n", g_cfg.mqtt_host, config_mqtt_root());
  console_printf("               mosquitto_pub -h %s -t \"%s/cmd\" -m \"led=80\"\n", g_cfg.mqtt_host,
                 config_mqtt_root());
  console_printf("%s Sur batterie, sans terminal%s\n", C_INFO(), C_RST());
  console_puts("   auto web on   auto mqtt on   save    services lancés à la mise sous tension\n");
}

/* « help » : liste des commandes par rubrique ; « help <commande> » : détail */
static int cmd_help(int argc, char **argv)
{
  size_t   i;
  unsigned g;

  if (argc >= 2)
  {
    const cmd_t *c;
    if (util_strieq(argv[1], "demo") || util_strieq(argv[1], "démo") || util_strieq(argv[1], "demos"))
    {
      help_demo();
      return 0;
    }
    c = find_cmd(argv[1]);
    if (c == NULL)
    {
      console_printf("%sCommande inconnue : « %s ».%s\n", C_ERR(), argv[1], C_RST());
      return APP_ERR_USAGE;
    }
    print_usage(c);
    console_printf("  %s\n", c->help);
    if (c->alias != NULL)
    {
      console_printf("  Alias : %s\n", c->alias);
    }
    if (c->detail != NULL)
    {
      console_puts(c->detail);
    }
    return 0;
  }

  for (g = 0U; g < (sizeof(s_groups) / sizeof(s_groups[0])); g++)
  {
    console_printf("%s%s%s\n", C_INFO(), s_groups[g], C_RST());
    for (i = 0U; i < N_CMDS; i++)
    {
      if (s_cmds[i].group == g)
      {
        console_puts("  ");
        if (util_utf8_len(s_cmds[i].usage) > 40U)
        {
          console_printf("%s\n  ", s_cmds[i].usage);   /* syntaxe longue : aide à la ligne */
          console_pad("", 40U);
        }
        else
        {
          console_pad(s_cmds[i].usage, 40U);
        }
        console_printf(" %s\n", s_cmds[i].help);
      }
    }
  }
  console_puts("« help <commande> » pour le détail, « help demo » pour le guide rapide.\n"
               "Flèche haut : rappel de la commande précédente ; Ctrl-C : annule la saisie.\n");
  return 0;
}

/* ---- info -------------------------------------------------------------------- */
/* « info » : résumé de l'état de la carte, du module et des services */
static int cmd_info(int argc, char **argv)
{
  char up[24];
  char uid[28];

  (void)argc;
  (void)argv;
  util_fmt_uptime(up, sizeof(up), HAL_GetTick() / 1000U);
  board_uid_full(uid, sizeof(uid));

  console_printf("%s%s%s, version %s (compilée le %s)\n", C_BOLD(), APP_NAME, C_RST(), APP_VERSION, __DATE__);
  console_printf("  Carte          : %s\n", APP_BOARD);
  console_printf("  Identifiant    : %s (UID %s)\n", board_uid_short(), uid);
  console_printf("  MCU            : STM32L476RG, %lu MHz (%s), RTC sur %s\n",
                 (unsigned long)(HAL_RCC_GetSysClockFreq() / 1000000UL),
                 board_hse_ok() ? "quartz 8 MHz" : "horloge interne", board_lse_ok() ? "quartz LSE" : "LSI");
  console_puts("  Console        : port COM virtuel USB de l'ObCP (classe CDC)\n");
  console_printf("  En marche      : %s\n", up);
  if (g_app.ncp_ok)
  {
    console_printf("  Module Wi-Fi   : %sOK%s, firmware %s\n", C_OK(), C_RST(),
                   (g_app.ncp.sdk_version[0] != '\0') ? g_app.ncp.sdk_version : g_app.ncp.at_version);
  }
  else
  {
    console_printf("  Module Wi-Fi   : %sindisponible%s (%s)\n", C_ERR(), C_RST(), app_err_str(g_app.ncp_err));
  }
  if (g_app.module_restarts > 0U)
  {
    console_printf("  Redémarrages   : %lu redémarrage(s) inattendu(s) du module\n", (unsigned long)g_app.module_restarts);
  }
  console_printf("  Station        : %s\n", !g_app.ncp_ok ? "-" :
                 netapp_sta_ready() ? "connectée (détails : status)" :
                 wifi_is_connected() ? "associée, en attente d'adresse IP" : "non connectée");
  console_printf("  Point d'accès  : %s\n", g_wifi.ap_running ? config_ap_ssid() : "arrêté");
  console_printf("  Serveur web    : %s\n", web_running() ? "actif (web status)" : "arrêté");
  console_printf("  MQTT           : %s\n", mqttapp_connected() ? "connecté (mqtt status)" : "déconnecté");
  netapp_print_time();
  console_printf("  Configuration  : %s%s\n", config_stored() ? "enregistrée en flash" : "non enregistrée",
                 g_app.cfg_dirty ? " (modifications non enregistrées : save)" : "");
  {
    char summary[96];
    app_autostart_summary(summary, sizeof(summary));
    console_printf("  Démarrage auto : %s\n", summary);
  }
  return 0;
}

/* ---- configuration -------------------------------------------------------------- */
/* « config [filtre] » : liste des paramètres (ceux dont la clé contient le filtre) */
static int cmd_config(int argc, char **argv)
{
  size_t            n;
  size_t            i;
  const cfg_item_t *items = cfg_items(&n);
  char              val[72];

  console_printf("%sParamètres%s (set <clé> <valeur>, puis save)\n", C_BOLD(), C_RST());
  for (i = 0U; i < n; i++)
  {
    if ((argc >= 2) && (strstr(items[i].key, argv[1]) == NULL))
    {
      continue;
    }
    cfg_get(&g_cfg, &items[i], val, sizeof(val), true);
    console_printf("  %-13s = %s", items[i].key, C_BOLD());
    console_pad(val, 20U);
    console_printf("%s %s%s%s\n", C_RST(), C_DIM(), items[i].help, C_RST());
  }
  console_printf("Noms automatiques : point d'accès « %s », hôte « %s »,\n                    topics « %s/... »\n",
                 config_ap_ssid(), config_hostname(), config_mqtt_root());
  if (g_app.cfg_dirty)
  {
    console_printf("%sModifications non enregistrées : tapez « save ».%s\n", C_WARN(), C_RST());
  }
  return 0;
}

/* Après « set » : applique tout de suite ce qui peut l'être et dit quand le
 * nouveau réglage sera pris en compte                                  */
static void set_hint(const char *key)
{
  const char *hint = NULL;

  if (prefix_nocase(key, "wifi.country") && g_app.ncp_ok)
  {
    (void)at_cmdf(2000U, NULL, NULL, NULL, "AT+CWCOUNTRY=0,\"%s\"", g_cfg.country);
    hint = "appliqué immédiatement";
  }
  else if (prefix_nocase(key, "net.hostname") && g_app.ncp_ok)
  {
    (void)at_cmdf(2000U, NULL, NULL, NULL, "AT+CWHOSTNAME=\"%s\"", config_hostname());
    hint = "pris en compte à la prochaine connexion";
  }
  else if (prefix_nocase(key, "wifi.auto"))
  {
    hint = "pris en compte au prochain démarrage et après une coupure";
  }
  else if (prefix_nocase(key, "web.auto"))
  {
    hint = "pris en compte à la prochaine mise sous tension";
  }
  else if (prefix_nocase(key, "mqtt.auto"))
  {
    hint = "pris en compte à la prochaine connexion Wi-Fi et au démarrage";
  }
  else if (prefix_nocase(key, "wifi.") || prefix_nocase(key, "ip."))
  {
    hint = "pris en compte à la prochaine connexion, commande connect";
  }
  else if (prefix_nocase(key, "time.server"))
  {
    hint = "utilisé à la prochaine synchronisation, commande ntp";
  }
  else if (prefix_nocase(key, "time."))
  {
    hint = "appliqué immédiatement";
  }
  else if (prefix_nocase(key, "ap.") || prefix_nocase(key, "web."))
  {
    hint = "pris en compte au prochain « web start »";
  }
  else if (prefix_nocase(key, "mqtt.period"))
  {
    hint = "appliqué immédiatement";
  }
  else if (prefix_nocase(key, "mqtt."))
  {
    hint = "pris en compte au prochain « mqtt start »";
  }
  else if (prefix_nocase(key, "term.color"))
  {
    console_set_color(g_cfg.color != 0U);
    hint = "appliqué immédiatement";
  }
  else
  {
    hint = "appliqué immédiatement";
  }
  console_printf("%s  (%s ; « save » pour l'enregistrer)%s\n", C_DIM(), hint, C_RST());
}

/* « set <clé> <valeur> » : modifie un paramètre en RAM (« save » l'enregistre) */
static int cmd_set(int argc, char **argv)
{
  char              value[CONSOLE_LINE_MAX];
  char              shown[72];
  const cfg_item_t *it;
  int               st;
  int               i;
  size_t            pos = 0U;

  if (argc < 2)
  {
    console_puts("Usage : set <clé> <valeur>   (« config » pour la liste des clés)\n");
    return APP_ERR_USAGE;
  }
  it = cfg_find(argv[1]);
  if (it == NULL)
  {
    console_printf("%sClé inconnue : « %s » (voir « config »).%s\n", C_ERR(), argv[1], C_RST());
    return APP_ERR_USAGE;
  }
  /* valeur = arguments restants séparés par un espace (set wifi.ssid Ma Box) */
  value[0] = '\0';
  for (i = 2; i < argc; i++)
  {
    int n = snprintf(&value[pos], sizeof(value) - pos, "%s%s", (i > 2) ? " " : "", argv[i]);
    if ((n < 0) || ((size_t)n >= (sizeof(value) - pos)))
    {
      break;
    }
    pos += (size_t)n;
  }
  st = cfg_set(&g_cfg, it->key, value);
  if (st != CFG_OK)
  {
    console_printf("%s%s : %s%s\n", C_ERR(), it->key, cfg_err_str(st), C_RST());
    if (it->type == CFG_T_INT)
    {
      console_printf("  Valeurs admises : %ld à %ld\n", (long)it->min, (long)it->max);
    }
    return APP_ERR_USAGE;
  }
  g_app.cfg_dirty = true;
  cfg_get(&g_cfg, it, shown, sizeof(shown), true);
  console_printf("%s = %s%s%s\n", it->key, C_BOLD(), shown, C_RST());
  set_hint(it->key);
  return 0;
}

/* « save » : enregistre tous les paramètres en flash */
static int cmd_save(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  if (config_save() != 0)
  {
    return app_report(APP_ERR_FLASH, "enregistrement");
  }
  g_app.cfg_dirty = false;
  console_printf("%s\xE2\x9C\x93 Configuration enregistrée en flash (0x%08lX).%s\n", C_OK(),
                 (unsigned long)CONFIG_FLASH_ADDR, C_RST());
  return 0;
}

/* ---- auto : démarrage automatique --------------------------------------------------- */
/* « auto [web | mqtt | wifi] [on | off] » : raccourci pour les paramètres
 * web.auto, mqtt.auto et wifi.auto, puis résumé de ce qui démarre seul  */
static int cmd_auto(int argc, char **argv)
{
  bool        wifi;
  bool        on;
  const char *key = NULL;
  uint8_t     ip[4];
  char        ips[16];

  if (argc >= 2)
  {
    if (util_strieq(argv[1], "web"))
    {
      key = "web.auto";
    }
    else if (util_strieq(argv[1], "mqtt"))
    {
      key = "mqtt.auto";
    }
    else if (util_strieq(argv[1], "wifi"))
    {
      key = "wifi.auto";
    }
    else
    {
      /* argument inconnu : usage ci-dessous */
    }
    if ((key == NULL) || (argc < 3) || !util_parse_bool(argv[2], &on))
    {
      console_puts("Usage : auto [web | mqtt | wifi] [on | off]   (puis save)\n");
      return APP_ERR_USAGE;
    }
    (void)cfg_set(&g_cfg, key, on ? "1" : "0");
    g_app.cfg_dirty = true;
  }

  wifi = (g_cfg.sta_auto != 0U) && (g_cfg.sta_ssid[0] != '\0');
  memcpy(ip, g_cfg.ap_ip, 4);
  ip[3] = 1U;
  util_fmt_ip(ips, sizeof(ips), ip);
  console_printf("%sDémarrage automatique%s (à la mise sous tension, utile sur batterie)\n", C_BOLD(), C_RST());
  if (g_cfg.sta_ssid[0] == '\0')
  {
    console_puts("  Wi-Fi (box)   : aucun réseau enregistré (connect <ssid> <mot de passe> puis save)\n");
  }
  else
  {
    console_printf("  Wi-Fi (box)   : %s « %s »%s\n", wifi ? "connexion à" : "non, réseau enregistré", g_cfg.sta_ssid,
                   wifi ? ", reconnexion automatique" : " (auto wifi on)");
  }
  if (g_cfg.web_auto != 0U)
  {
    console_printf("  Serveur web   : oui, point d'accès « %s » (mot de passe %s), http://%s/\n", config_ap_ssid(),
                   (g_cfg.ap_pass[0] != '\0') ? g_cfg.ap_pass : "aucun", ips);
  }
  else
  {
    console_puts("  Serveur web   : non (auto web on)\n");
  }
  if (g_cfg.mqtt_auto != 0U)
  {
    console_printf("  MQTT          : %s\n", wifi ? "oui, après la connexion à la box" :
                   "oui, mais pas de connexion automatique à la box : MQTT ne démarrera pas");
  }
  else
  {
    console_puts("  MQTT          : non (auto mqtt on)\n");
  }
  if (g_app.cfg_dirty)
  {
    console_printf("%sModifications non enregistrées : « save » pour les garder après la mise hors tension.%s\n",
                   C_WARN(), C_RST());
  }
  return 0;
}

/* « factory [erase] » : valeurs d'usine en RAM (erase : efface aussi la flash) */
static int cmd_factory(int argc, char **argv)
{
  cfg_defaults(&g_cfg);
  console_set_color(g_cfg.color != 0U);
  g_app.cfg_dirty = true;
  if ((argc >= 2) && util_strieq(argv[1], "erase"))
  {
    if (config_erase() != 0)
    {
      return app_report(APP_ERR_FLASH, "effacement");
    }
    g_app.cfg_dirty = false;
    console_puts("Flash de configuration effacée. ");
  }
  console_puts("Valeurs d'usine rétablies (« save » pour les enregistrer, « reboot » pour redémarrer).\n");
  return 0;
}

/* « reboot » : redémarre la carte */
static int cmd_reboot(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  if (g_app.cfg_dirty)
  {
    console_printf("%sAttention : modifications non enregistrées perdues.%s\n", C_WARN(), C_RST());
  }
  console_puts("Redémarrage...\n");
  console_flush();
  board_reset();
  return 0;
}

/* ---- LED RVB, sortie PWM, capteurs --------------------------------------------- */
/* label : début de ligne (« LED RVB : » seul, ou aligné dans « sensors ») */
static void print_led(const char *label)
{
  uint8_t     rgb[3];
  char        hex[8];
  const char *name;

  io_led_color_get(rgb);
  color_hex(rgb, hex);
  name = color_name(rgb);
  console_printf("%s%u %%, couleur %s%s%s%s\n", label, (unsigned)io_led_get(),
                 (name != NULL) ? name : hex, (name != NULL) ? " (" : "", (name != NULL) ? hex : "",
                 (name != NULL) ? ")" : "");
}

/* « led [0-100 | on | off] [couleur] » : les arguments peuvent être donnés
 * dans n'importe quel ordre ; v = 101 signifie « rallumer au dernier niveau » */
static int cmd_led(int argc, char **argv)
{
  int     i;
  long    v = -1;
  bool    have_color = false;
  uint8_t rgb[3];

  if (argc < 2)
  {
    print_led("LED RVB : ");
    return 0;
  }
  for (i = 1; i < argc; i++)
  {
    long n;
    if (util_strieq(argv[i], "on"))
    {
      v = 101;                        /* dernier niveau non nul */
    }
    else if (util_strieq(argv[i], "off"))
    {
      v = 0;
    }
    else if (util_strieq(argv[i], "couleur") || util_strieq(argv[i], "color"))
    {
      /* mot facultatif : led couleur rouge */
    }
    else if (util_parse_long(argv[i], &n) && (strchr(argv[i], ',') == NULL))
    {
      if ((n < 0) || (n > 100))
      {
        console_puts("Luminosité de 0 à 100 %.\n");
        return APP_ERR_USAGE;
      }
      v = n;
    }
    else if (color_parse(argv[i], rgb))
    {
      have_color = true;
    }
    else
    {
      console_printf("Couleur inconnue : « %s » (%s, #RRGGBB ou R,G,B).\n", argv[i], color_names());
      return APP_ERR_USAGE;
    }
  }
  if (have_color)
  {
    io_led_color_set(rgb);
    if ((v < 0) && (io_led_get() == 0U))
    {
      v = 101;                        /* nouvelle couleur sur une LED éteinte : on l'allume */
    }
  }
  if (v == 101)
  {
    io_led_set(0U);
    io_led_toggle();
  }
  else if (v >= 0)
  {
    io_led_set((uint8_t)v);
  }
  print_led("LED RVB : ");
  return 0;
}

/* « pwm <0-100 | on | off> » : rapport cyclique de la sortie du bornier */
static int cmd_pwm(int argc, char **argv)
{
  long v;

  if (argc < 2)
  {
    console_printf("Sortie PWM : %u %%\n", (unsigned)io_pwm_get());
    return 0;
  }
  if (util_strieq(argv[1], "on"))
  {
    v = 100;
  }
  else if (util_strieq(argv[1], "off"))
  {
    v = 0;
  }
  else if (!util_parse_long(argv[1], &v) || (v < 0) || (v > 100))
  {
    console_puts("Usage : pwm <0-100 | on | off>\n");
    return APP_ERR_USAGE;
  }
  io_pwm_set((uint8_t)v);
  console_printf("Sortie PWM : %u %% (1 kHz)\n", (unsigned)io_pwm_get());
  return 0;
}

/* Valeur en g avec signe : « +0.98 » */
static void fmt_g(char *out, size_t len, int16_t mg)
{
  char t[8];
  util_fmt_fixed(t, sizeof(t), mg / 10, 2U);
  (void)snprintf(out, len, "%s%s", (mg >= 0) ? "+" : "", t);
}

/* Affiche toutes les mesures : sur plusieurs lignes, ou sur une seule ligne
 * réécrite sur place (« sensors suivi » : \r ramène le curseur en début de ligne) */
static void print_sensors(bool one_line)
{
  io_sensors_t s;
  char         t[12];
  char         v[12];
  char         ax[12];
  char         ay[12];
  char         az[12];
  char         pi[12];
  char         ro[12];

  io_read(&s);
  util_fmt_fixed(t, sizeof(t), s.temp_x10, 1U);
  util_fmt_fixed(v, sizeof(v), (int32_t)((s.vdda_mv + 5U) / 10U), 2U);
  fmt_g(ax, sizeof(ax), s.acc_mg[0]);
  fmt_g(ay, sizeof(ay), s.acc_mg[1]);
  fmt_g(az, sizeof(az), s.acc_mg[2]);
  util_fmt_fixed(pi, sizeof(pi), s.pitch_x10, 1U);
  util_fmt_fixed(ro, sizeof(ro), s.roll_x10, 1U);
  if (one_line)
  {
    if (s.acc_ok)
    {
      console_printf("%sPuce %s °C | X %s Y %s Z %s g | tangage %5s° roulis %6s° | User1 %s | SW1 %s",
                     console_color() ? "\r\033[K" : "\r", t, ax, ay, az, pi, ro,
                     s.user ? "appuyé " : "relâché", s.sw1 ? "appuyé " : "relâché");
    }
    else
    {
      console_printf("%sPuce %s °C | VDDA %s V | accéléromètre absent | User1 %s | SW1 %s",
                     console_color() ? "\r\033[K" : "\r", t, v,
                     s.user ? "appuyé " : "relâché", s.sw1 ? "appuyé " : "relâché");
    }
  }
  else
  {
    console_printf("  Température puce : %s °C (capteur interne)\n", t);
    console_printf("  VDDA             : %s V\n", v);
    if (s.acc_ok)
    {
      console_printf("  Accélération     : X %s g  Y %s g  Z %s g (%s, ±2 g)\n", ax, ay, az,
                     (io_acc_name() != NULL) ? io_acc_name() : "?");
      console_printf("  Inclinaison      : tangage %s°, roulis %s° (carte immobile)\n", pi, ro);
    }
    else
    {
      console_printf("  Accéléromètre    : %sabsent ou non reconnu (WHO_AM_I = 0x%02X, 0x33 ou 0x3F attendu)%s\n",
                     C_WARN(), (unsigned)io_acc_whoami(), C_RST());
    }
    console_printf("  Bouton User1     : %s (%lu appuis)\n", s.user ? "appuyé" : "relâché",
                   (unsigned long)io_button_count(IO_BTN_USER));
    console_printf("  Bouton SW1       : %s (%lu appuis, X-NUCLEO)\n", s.sw1 ? "appuyé" : "relâché",
                   (unsigned long)io_button_count(IO_BTN_SW1));
    print_led("  LED RVB          : ");
    console_printf("  Sortie PWM       : %u %%\n", (unsigned)io_pwm_get());
  }
}

/* « sensors [suivi] » : mesures, une fois ou en continu (4 par seconde) jusqu'à
 * l'appui d'une touche ; app_idle() garde les services actifs pendant ce temps */
static int cmd_sensors(int argc, char **argv)
{
  if ((argc >= 2) && (util_strieq(argv[1], "suivi") || util_strieq(argv[1], "loop")))
  {
    uint32_t t = 0U;
    console_puts("Mesures en continu (une touche pour arrêter)\n");
    while (!console_abort())
    {
      if ((HAL_GetTick() - t) >= 250U)
      {
        t = HAL_GetTick();
        print_sensors(true);
      }
      app_idle();
    }
    console_puts("\n");
    return 0;
  }
  print_sensors(false);
  return 0;
}

/* ---- Chargeur USB DFU ------------------------------------------------------------- */
/* « dfu » : après confirmation (touche o), redémarre dans le chargeur de ST */
static int cmd_dfu(int argc, char **argv)
{
  uint32_t t0;
  int      c = -1;

  (void)argc;
  (void)argv;
  console_printf("%sRedémarrage dans le chargeur USB DFU de ST (programmation de l'ObCP).%s\n",
                 C_WARN(), C_RST());
  console_puts("  Le port COM disparaît ; dans STM32CubeProgrammer : USB, rafraîchir, Connect,\n"
               "  puis Erasing & programming (ou STM32_Programmer_CLI -c port=usb1 ...).\n"
               "  Le programme repart après « Run after programming » ou un reset.\n");
  if (g_app.cfg_dirty)
  {
    console_printf("%s  Attention : modifications de configuration non enregistrées (save).%s\n", C_WARN(), C_RST());
  }
  console_puts("Confirmer avec la touche o (toute autre touche annule) : ");
  t0 = HAL_GetTick();
  while (((HAL_GetTick() - t0) < 15000U) && (c < 0))
  {
    c = console_getc();
    if ((c == '\r') || (c == '\n'))
    {
      c = -1;                          /* fin de la ligne de commande précédente */
    }
  }
  if ((c != 'o') && (c != 'O') && (c != 'y') && (c != 'Y'))
  {
    console_puts("\nAnnulé.\n");
    return APP_ERR_CANCELED;
  }
  console_puts("o\nPassage en DFU...\n");
  console_flush();
  board_reboot_to_dfu();
  return 0;
}

/* ---- outils ---------------------------------------------------------------------- */
/* « at » : présent dans la table pour l'aide ; la ligne est traitée par cli_exec() */
static int cmd_at(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  return at_raw("");           /* « at » seul : traité dans cli_exec ; ici pour l'aide */
}

/* « log on | off » : journal du trafic AT avec le module */
static int cmd_log(int argc, char **argv)
{
  bool on;
  if ((argc < 2) || !util_parse_bool(argv[1], &on))
  {
    console_printf("Journal AT : %s (usage : log on|off)\n", at_get_log() ? "activé" : "désactivé");
    return 0;
  }
  at_set_log(on);
  console_printf("Journal AT %s.\n", on ? "activé (mots de passe masqués)" : "désactivé");
  return 0;
}

/* « stats » : compteurs des couches SPI et AT du pilote (diagnostic) */
static int cmd_stats(int argc, char **argv)
{
  const ncp_spi_stats_t *s = ncp_spi_stats();
  const at_stats_t      *a = at_stats();

  (void)argc;
  (void)argv;
  console_printf("SPI : %lu trames émises (%lu octets), %lu reçues (%lu octets)\n",
                 (unsigned long)s->tx_frames, (unsigned long)s->tx_bytes,
                 (unsigned long)s->rx_frames, (unsigned long)s->rx_bytes);
  console_printf("      en-têtes invalides %lu, délais dépassés %lu, renvois (rx_stall) %lu, erreurs E/S %lu\n",
                 (unsigned long)s->hdr_errors, (unsigned long)s->timeouts, (unsigned long)s->stalls,
                 (unsigned long)s->io_errors);
  console_printf("AT  : %lu lignes, %lu évènements, %lu débordements, %lu trames non AT\n",
                 (unsigned long)a->lines, (unsigned long)a->urcs, (unsigned long)a->overflows,
                 (unsigned long)a->other_frames);
  return 0;
}

/* « module [reset | ota | boot] » : versions, redémarrage, mise à jour du module */
static int cmd_module(int argc, char **argv)
{
  if ((argc >= 2) && util_strieq(argv[1], "ota"))
  {
    return module_ota_run();
  }
  if ((argc >= 2) && util_strieq(argv[1], "boot"))
  {
    static const char *const ends[] = { "bouton", "terminal rouvert", "inactivité" };
    bridge_result_t r;

    console_printf("%sPremière programmation du module : l'ObCP relaie le port COM vers l'UART du module.%s\n",
                   C_WARN(), C_RST());
    console_puts("  1. Fermer ce terminal (le port COM doit être libre).\n"
                 "  2. Dans le dossier ST67W6X_Scripts\\Binaries de X-CUBE-ST67W61 (guide, chapitre 3) :\n"
                 "       QConn_Flash\\QConn_Flash_Cmd.exe --port COMx --config NCP_Binaries\\cfg_obcp.ini\n"
                 "                                       --efuse=NCP_Binaries\\efusedata.bin\n"
                 "     (COMx = ce même port ; cfg_obcp.ini préparé comme indiqué dans le guide).\n"
                 "  3. Quand QConn_Flash a terminé, rouvrir ce terminal sans rien taper : la\n"
                 "     passerelle s'arrête après 2 s de silence et le module redémarre.\n"
                 "  LED magenta pendant la passerelle. Arrêt immédiat : bouton User1 (ObCP) ou\n"
                 "  SW1 (X-NUCLEO) ; arrêt automatique après 2 min sans échange.\n");
    console_flush();
    web_module_restarted();
    mqttapp_module_restarted();
    g_app.ncp_ok = false;
    g_app.ncp_err = NCP_ERR_NOREADY;
    /* Même séquence que l'utilitaire NCP_Loader de ST : BOOT = 1 pendant le
     * redémarrage (CHIP_EN 0 -> 1), puis BOOT relâché : le chargeur en ROM du
     * module attend alors QConn_Flash sur son UART.                          */
    ncp_port_cs(false);
    ncp_port_boot(true);
    ncp_port_chip_enable(false);
    HAL_Delay(100);
    ncp_port_chip_enable(true);
    HAL_Delay(100);
    ncp_port_boot(false);

    uart_bridge_run(&r);

    console_printf("\n%sPasserelle terminée (%s) au bout de %lu s.\n"
                   "  %lu octets envoyés au module, %lu reçus, dernier débit %lu bit/s.%s\n",
                   C_INFO(), ends[(r.end <= BRIDGE_END_IDLE) ? (int)r.end : 0], (unsigned long)r.seconds,
                   (unsigned long)r.to_module, (unsigned long)r.from_module, (unsigned long)r.baud, C_RST());
    if (app_module_start(true) == AT_OK)
    {
      netapp_module_restarted();
      return 0;
    }
    return g_app.ncp_err;
  }
  if ((argc >= 2) && util_strieq(argv[1], "reset"))
  {
    web_module_restarted();
    mqttapp_module_restarted();
    if (app_module_start(true) == AT_OK)
    {
      netapp_module_restarted();
      return 0;
    }
    return g_app.ncp_err;
  }
  if (!g_app.ncp_ok)
  {
    console_printf("Module : %sindisponible%s (%s). « module reset » pour réessayer.\n", C_ERR(), C_RST(),
                   app_err_str(g_app.ncp_err));
    return g_app.ncp_err;
  }
  console_printf("Module ST67W611M1 : firmware « mission T01 » (pile TCP/IP dans le module)\n");
  app_print_module_info();
  console_printf("%s« module ota » : mise à jour du firmware du module par le terminal (help module).%s\n",
                 C_DIM(), C_RST());
  return 0;
}
