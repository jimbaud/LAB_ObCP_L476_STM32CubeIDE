/**
 ******************************************************************************
 * @file    commands.c
 * @brief   Commandes du terminal (voir commands.h pour la liste).
 *
 *  Ajouter une commande « x » (exemple) :
 *    1. dans commands.h, ajouter CMD_X à l'énumération cmd_id_t ;
 *    2. dans commands_parse(), reconnaître le mot (word_is(w[0], "x")) et
 *       lire ses paramètres ;
 *    3. dans commands_execute(), ajouter « case CMD_X: » avec l'action et
 *       la réponse (report_printf) ;
 *    4. compléter le texte d'aide s_help ci-dessous, la documentation, et un test
 *       dans Tests/test_unit.c.
 ******************************************************************************
 */
#include "commands.h"

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "main.h"                 /* HAL_GetTick */
#include "mesh.h"
#include "mesh_radio.h"
#include "node.h"
#include "report.h"
#include "usb_console.h"

/* ========================================================================== */
/* Textes                                                                     */
/* ========================================================================== */

static const char s_help[] =
  "dest = numéro de nœud (1 à 239), groupe (g1 à g15) ou tous\r\n"
  "VOIR\r\n"
  "  info                  identité et réglages de cette carte\r\n"
  "  voisins               cartes entendues en direct (RSSI)\r\n"
  "  noeuds                cartes connues dans le réseau\r\n"
  "  stats [raz]           compteurs (raz : remise à zéro)\r\n"
  "  mesure                mesures de cette carte\r\n"
  "ENVOYER\r\n"
  "  msg dest texte        texte de 14 octets au plus\r\n"
  "  led dest couleur [s]  rouge vert bleu blanc jaune cyan magenta orange\r\n"
  "                        violet rose eteint, ou r v b (0 à 255)\r\n"
  "  vague                 vague de couleurs (aussi : bouton SW1)\r\n"
  "  ident dest [s]        fait clignoter la LED en blanc\r\n"
  "  ping dest             demande d'écho, temps d'aller-retour\r\n"
  "  trace n               route vers le nœud n\r\n"
  "  mesure dest           demande les mesures\r\n"
  "  regle dest réglage valeur   change un réglage à distance\r\n"
  "RÉGLER (sans valeur : affiche la valeur)\r\n"
  "  ttl 0..15   relais on|off   suppr 0..10   rssimin -100..-20|off\r\n"
  "  puissance dBm   hello s   auto s   flash on|off\r\n"
  "  id n | id auto        numéro de cette carte\r\n"
  "  reseau n              numéro de réseau (0 à 255)\r\n"
  "  groupe [+g|-g]        rejoindre ou quitter un groupe\r\n"
  "  bloque [n]            ignorer l'émetteur n ; debloque n|tous\r\n"
  "  radio [auto|simultane|alterne]   écoute pendant l'émission\r\n"
  "DIVERS\r\n"
  "  journal on|off        affiche chaque trame émise, relayée, reçue\r\n"
  "  pc on|off             lignes JSON pour l'outil PC\r\n"
  "  sauve | oublie        enregistre ou efface les réglages (EEPROM)\r\n"
  "  cls                   efface l'écran\r\n"
  "  #ota                  redémarre dans le chargeur Bluetooth ObCP-OTA\r\n";

/* ESC[1;1H place le curseur en haut à gauche, ESC[2J efface l'écran. */
#define ANSI_CLEAR_SCREEN   "\x1B[1;1H\x1B[2J"

/* Couleurs nommées de la commande led. */
typedef struct
{
  const char *name;
  led_color_t color;
} named_color_t;

static const named_color_t k_colors[] =
{
  { "rouge",   { 255U, 0U, 0U } },     { "vert",    { 0U, 255U, 0U } },
  { "bleu",    { 0U, 0U, 255U } },     { "blanc",   { 255U, 255U, 255U } },
  { "jaune",   { 255U, 200U, 0U } },   { "cyan",    { 0U, 255U, 255U } },
  { "magenta", { 255U, 0U, 255U } },   { "orange",  { 255U, 80U, 0U } },
  { "violet",  { 160U, 0U, 255U } },   { "rose",    { 255U, 40U, 110U } },
  { "eteint",  { 0U, 0U, 0U } },       { "éteint",  { 0U, 0U, 0U } },
  { "noir",    { 0U, 0U, 0U } },       { "off",     { 0U, 0U, 0U } }
};

/* Voir commands.h */
const char *commands_help_text(void)
{
  return s_help;
}

/* ========================================================================== */
/* Analyse                                                                    */
/* ========================================================================== */

#define MAX_WORDS  8U

/* Découpe la ligne en mots (séparés par des espaces ou tabulations) dans
 * buf, en minuscules (« PING 5 » vaut « ping 5 ») ; rest[i] pointe sur la
 * suite de la ligne d'origine à partir du mot i (pour le texte de « msg »,
 * qui garde ses espaces et ses majuscules). Rend le nombre de mots. */
static uint32_t split_words(const char *line, char *buf, size_t size, const char *w[MAX_WORDS],
                            const char *rest[MAX_WORDS])
{
  uint32_t n   = 0U;
  size_t   out = 0U;
  size_t   i   = 0U;

  while ((line[i] != '\0') && (n < MAX_WORDS))
  {
    while ((line[i] == ' ') || (line[i] == '\t'))
    {
      i++;
    }
    if (line[i] == '\0')
    {
      break;
    }
    rest[n] = &line[i];
    w[n]    = &buf[out];
    while ((line[i] != '\0') && (line[i] != ' ') && (line[i] != '\t') && (out < (size - 1U)))
    {
      char c = line[i++];
      buf[out++] = ((c >= 'A') && (c <= 'Z')) ? (char)(c - 'A' + 'a') : c;
    }
    buf[out++] = '\0';
    n++;
    if (out >= size)
    {
      break;
    }
  }
  return n;
}

/* Vrai si le mot w est exactement le mot attendu. */
static bool word_is(const char *w, const char *expected)
{
  return strcmp(w, expected) == 0;
}

/* Entier décimal sans signe de 1 à 3 chiffres, entre lo et hi. */
static bool parse_uint(const char *text, int32_t lo, int32_t hi, int32_t *value)
{
  int32_t v = 0;
  size_t  n = 0U;

  for (; text[n] != '\0'; n++)
  {
    if ((text[n] < '0') || (text[n] > '9') || (n >= 3U))
    {
      return false;
    }
    v = (v * 10) + (int32_t)(text[n] - '0');
  }
  if ((n == 0U) || (v < lo) || (v > hi))
  {
    return false;
  }
  *value = v;
  return true;
}

/* Marque la commande comme incorrecte, avec le message d'erreur à afficher. */
static void set_error(cmd_t *cmd, const char *message)
{
  cmd->id    = CMD_ERROR;
  cmd->error = message;
}

/* Destination obligatoire en position i. */
static bool need_dest(cmd_t *cmd, uint32_t n, const char *w[], uint32_t i, const char *usage)
{
  if (n <= i)
  {
    set_error(cmd, usage);
    return false;
  }
  if (!mesh_addr_parse(w[i], &cmd->dst))
  {
    set_error(cmd, "Destination invalide : 1 à 239, g1 à g15 ou tous.");
    return false;
  }
  return true;
}

/* Couleur de la commande led : un nom (w[i]) ou trois nombres. Rend le
 * nombre de mots lus (0 si la couleur est incorrecte). */
static uint32_t parse_color(const char *w[], uint32_t n, uint32_t i, led_color_t *c)
{
  int32_t r;
  int32_t g;
  int32_t b;

  for (size_t k = 0U; k < (sizeof(k_colors) / sizeof(k_colors[0])); k++)
  {
    if ((n > i) && word_is(w[i], k_colors[k].name))
    {
      *c = k_colors[k].color;
      return 1U;
    }
  }
  if ((n >= (i + 3U)) && parse_uint(w[i], 0, 255, &r) && parse_uint(w[i + 1U], 0, 255, &g)
      && parse_uint(w[i + 2U], 0, 255, &b))
  {
    c->r = (uint8_t)r;
    c->g = (uint8_t)g;
    c->b = (uint8_t)b;
    return 3U;
  }
  return 0U;
}

/* « on » / « off » en position 1 (facultatif). */
static void parse_on_off(cmd_t *cmd, uint32_t n, const char *w[], cmd_id_t id, const char *usage)
{
  cmd->id = id;
  if (n < 2U)
  {
    return;                               /* sans valeur : affiche l'état */
  }
  cmd->has_value = true;
  if (word_is(w[1], "on"))
  {
    cmd->value = 1;
  }
  else if (word_is(w[1], "off"))
  {
    cmd->value = 0;
  }
  else
  {
    set_error(cmd, usage);
  }
}

/* Voir commands.h */
void commands_parse(const char *line, cmd_t *cmd)
{
  char        buf[CMD_LINE_MAX + MAX_WORDS + 1U];
  const char *w[MAX_WORDS];
  const char *rest[MAX_WORDS];
  uint32_t    n = split_words(line, buf, sizeof(buf), w, rest);
  setting_t   key;

  memset(cmd, 0, sizeof(*cmd));
  cmd->id = CMD_EMPTY;
  if (n == 0U)
  {
    return;
  }

  if (word_is(w[0], "aide") || word_is(w[0], "?") || word_is(w[0], "help"))
  {
    cmd->id = CMD_HELP;
  }
  else if (word_is(w[0], "info"))
  {
    cmd->id = CMD_INFO;
  }
  else if (word_is(w[0], "voisins"))
  {
    cmd->id = CMD_NEIGHBORS;
  }
  else if (word_is(w[0], "noeuds") || word_is(w[0], "nœuds"))
  {
    cmd->id = CMD_NODES;
  }
  else if (word_is(w[0], "stats"))
  {
    cmd->id = ((n >= 2U) && word_is(w[1], "raz")) ? CMD_STATS_CLEAR : CMD_STATS;
  }
  else if (word_is(w[0], "mesure"))
  {
    cmd->id = CMD_MEASURE_LOCAL;
    if ((n >= 2U) && need_dest(cmd, n, w, 1U, ""))
    {
      cmd->id = CMD_MEASURE_REQ;
    }
  }
  else if (word_is(w[0], "msg"))
  {
    if (need_dest(cmd, n, w, 1U, "Usage : msg dest texte"))
    {
      if (n < 3U)
      {
        set_error(cmd, "Usage : msg dest texte");
      }
      else
      {
        cmd->id = CMD_MSG;
        (void)snprintf(cmd->text, sizeof(cmd->text), "%s", rest[2]);
      }
    }
  }
  else if (word_is(w[0], "led"))
  {
    if (need_dest(cmd, n, w, 1U, "Usage : led dest couleur [secondes]"))
    {
      uint32_t used = parse_color(w, n, 2U, &cmd->color);
      cmd->id = CMD_LED;
      if (used == 0U)
      {
        set_error(cmd, "Couleur : rouge vert bleu blanc jaune cyan magenta orange violet rose eteint, ou r v b (0 à 255)");
      }
      else if (n > (2U + used))
      {
        cmd->has_value = true;
        if ((n > (3U + used)) || !parse_uint(w[2U + used], 0, 255, &cmd->value))
        {
          set_error(cmd, "Durée : 0 à 255 secondes (0 : sans limite)");
        }
      }
    }
  }
  else if (word_is(w[0], "vague"))
  {
    cmd->id = CMD_WAVE;
  }
  else if (word_is(w[0], "ident"))
  {
    if (need_dest(cmd, n, w, 1U, "Usage : ident dest [secondes]"))
    {
      cmd->id    = CMD_IDENT;
      cmd->value = (int32_t)NODE_IDENT_DEFAULT_S;
      if ((n >= 3U) && !parse_uint(w[2], 1, 255, &cmd->value))
      {
        set_error(cmd, "Durée : 1 à 255 secondes");
      }
    }
  }
  else if (word_is(w[0], "ping"))
  {
    if (need_dest(cmd, n, w, 1U, "Usage : ping dest"))
    {
      cmd->id = CMD_PING;
    }
  }
  else if (word_is(w[0], "trace"))
  {
    if (need_dest(cmd, n, w, 1U, "Usage : trace n (numéro d'un nœud)"))
    {
      cmd->id = CMD_TRACE;
      if (!mesh_addr_is_node(cmd->dst))
      {
        set_error(cmd, "trace vise un seul nœud : trace n, avec n de 1 à 239");
      }
    }
  }
  else if (word_is(w[0], "regle"))
  {
    if (need_dest(cmd, n, w, 1U, "Usage : regle dest réglage valeur (exemple : regle tous rssimin -65)"))
    {
      if ((n < 4U) || !settings_from_name(w[2], &cmd->key))
      {
        set_error(cmd, "Usage : regle dest réglage valeur ; réglages : ttl relais suppr rssimin puissance hello auto flash");
      }
      else if (!settings_parse_value(cmd->key, w[3], &cmd->value))
      {
        set_error(cmd, settings_help(cmd->key));
        cmd->bad_value = true;
      }
      else
      {
        cmd->id        = CMD_REMOTE_SET;
        cmd->has_value = true;
      }
    }
  }
  else if (settings_from_name(w[0], &key))
  {
    cmd->id  = CMD_SET;
    cmd->key = key;
    if (n >= 2U)
    {
      cmd->has_value = true;
      if (!settings_parse_value(key, w[1], &cmd->value))
      {
        set_error(cmd, settings_help(key));
        cmd->bad_value = true;
      }
    }
  }
  else if (word_is(w[0], "id"))
  {
    cmd->id = CMD_ID;
    if (n >= 2U)
    {
      cmd->has_value = true;
      if (word_is(w[1], "auto"))
      {
        cmd->value = 0;
      }
      else if (!parse_uint(w[1], 1, 239, &cmd->value))
      {
        set_error(cmd, "Usage : id n (1 à 239) ou id auto");
      }
    }
  }
  else if (word_is(w[0], "reseau") || word_is(w[0], "réseau"))
  {
    cmd->id = CMD_NET;
    if (n >= 2U)
    {
      cmd->has_value = true;
      if (!parse_uint(w[1], 0, 255, &cmd->value))
      {
        set_error(cmd, "Usage : reseau n (0 à 255)");
      }
    }
  }
  else if (word_is(w[0], "groupe"))
  {
    cmd->id = CMD_GROUP;
    if (n >= 2U)
    {
      const char *g = w[1];
      cmd->has_value = true;
      cmd->join      = (g[0] != '-');
      if ((g[0] == '+') || (g[0] == '-'))
      {
        g++;
      }
      if ((g[0] == 'g') || (g[0] == 'G'))
      {
        g++;
      }
      if (!parse_uint(g, 1, 15, &cmd->value))
      {
        set_error(cmd, "Usage : groupe +g (rejoindre) ou groupe -g (quitter), g de 1 à 15");
      }
    }
  }
  else if (word_is(w[0], "bloque"))
  {
    cmd->id = CMD_BLOCK;
    if (n >= 2U)
    {
      cmd->has_value = true;
      if (!parse_uint(w[1], 1, 239, &cmd->value))
      {
        set_error(cmd, "Usage : bloque n (1 à 239)");
      }
    }
  }
  else if (word_is(w[0], "debloque") || word_is(w[0], "débloque"))
  {
    cmd->id        = CMD_UNBLOCK;
    cmd->has_value = true;
    if ((n >= 2U) && word_is(w[1], "tous"))
    {
      cmd->value = 0;
    }
    else if ((n < 2U) || !parse_uint(w[1], 1, 239, &cmd->value))
    {
      set_error(cmd, "Usage : debloque n (1 à 239) ou debloque tous");
    }
  }
  else if (word_is(w[0], "radio"))
  {
    cmd->id = CMD_RADIO;
    if (n >= 2U)
    {
      cmd->has_value = true;
      if (word_is(w[1], "auto"))
      {
        cmd->value = CMD_RADIO_AUTO;
      }
      else if (word_is(w[1], "simultane") || word_is(w[1], "simultané"))
      {
        cmd->value = CMD_RADIO_SIMULTANEOUS;
      }
      else if (word_is(w[1], "alterne") || word_is(w[1], "alterné"))
      {
        cmd->value = CMD_RADIO_ALTERNATE;
      }
      else
      {
        set_error(cmd, "Usage : radio auto, radio simultane ou radio alterne");
      }
    }
  }
  else if (word_is(w[0], "journal"))
  {
    parse_on_off(cmd, n, w, CMD_JOURNAL, "Usage : journal on ou journal off");
  }
  else if (word_is(w[0], "pc"))
  {
    parse_on_off(cmd, n, w, CMD_PC, "Usage : pc on ou pc off");
  }
  else if (word_is(w[0], "sauve"))
  {
    cmd->id = CMD_SAVE;
  }
  else if (word_is(w[0], "oublie"))
  {
    cmd->id = CMD_FORGET;
  }
  else if (word_is(w[0], "cls"))
  {
    cmd->id = CMD_CLS;
  }
  else if (word_is(w[0], "#ota"))
  {
    cmd->id = CMD_OTA;
  }
  else
  {
    set_error(cmd, "Commande inconnue. Tapez « aide » pour la liste des commandes.");
  }
}

/* ========================================================================== */
/* Exécution                                                                  */
/* ========================================================================== */

/* Destination lisible. */
static const char *dest_text(uint8_t dst, char *buf, size_t size)
{
  (void)mesh_addr_format(dst, buf, size);
  return buf;
}

/* Vrai si la radio fonctionne ; sinon explique pourquoi rien ne partira. */
static bool radio_ok(void)
{
  if (node_radio_status() == RADIO_OK)
  {
    return true;
  }
  report_printf("Radio hors service (%s) : rien ne peut être envoyé.\r\n",
                mesh_radio_status_text(node_radio_status()));
  return false;
}

/* Réponse à un envoi refusé par le réseau. */
static void report_send_error(mesh_result_t r)
{
  switch (r)
  {
    case MESH_ERR_DEST:
      report_print("Destination impossible (c'est le numéro de cette carte ?).\r\n");
      break;
    case MESH_ERR_TOO_LONG:
      report_print("Données trop longues.\r\n");
      break;
    case MESH_ERR_QUEUE_FULL:
      report_print("File d'émission pleine : réessayez dans un instant.\r\n");
      break;
    default:
      break;
  }
}

/* « (ttl 4) » : TTL utilisé pour les trames de cette carte. */
static unsigned current_ttl(void)
{
  return mesh_settings(node_mesh())->ttl;
}

/* Mode de la pile du module, pour « info » (voir mesh_radio.h). */
static const char *stack_text(radio_stack_t stack)
{
  switch (stack)
  {
    case RADIO_STACK_MODE4:         return "mode 4 accepté (« radio simultane » possible)";
    case RADIO_STACK_MODE4_REFUSED: return "mode 4 refusé";
    default:                        return "antérieure à 7.1b (pas de mode 4)";
  }
}

/* « écoute arrêtée pendant l'émission (automatique) » : mode d'écoute de la
 * radio et origine de ce choix, pour « info » et « radio ». */
static void print_radio_mode(void)
{
  report_printf("écoute %s pendant l'émission", (mesh_radio_mode() == RADIO_MODE_SIMULTANEOUS) ? "maintenue" : "arrêtée");
  if (mesh_radio_mode_forced())
  {
    report_print(" (imposé)");
  }
  else if (mesh_radio_fallback_code() != 0U)
  {
    report_printf(" (le module a refusé d'émettre pendant l'écoute, code 0x%02X)",
                  (unsigned)mesh_radio_fallback_code());
  }
  else
  {
    report_print(" (automatique)");
  }
}

/* Durée en pas de 0,625 ms écrite en ms avec une décimale : 168 -> « 105,0 ms ». */
static void print_steps_ms(uint32_t steps)
{
  uint32_t us = steps * 625U;

  report_printf("%lu,%lu ms", (unsigned long)(us / 1000U), (unsigned long)((us % 1000U) / 100U));
}

/* Commande « info » : identité, réseau, groupes, radio, module, réglages,
 * émetteurs bloqués et état de l'EEPROM. */
static void show_info(void)
{
  const mesh_t *m = node_mesh();
  uint8_t       a[6];
  char          v[24];
  bool          any = false;

  if (report_is_json())
  {
    report_info_json(m);
  }
  mesh_radio_get_address(a);
  report_printf("%s %s (version %s)\r\n", APP_NAME, APP_VERSION,
                app_is_ota() ? "OTA, chargée par Bluetooth à 0x08080000" : "USB, chargée par le câble à 0x08000000");
  report_printf("  nœud       %u (%s), étiquette %08lX\r\n", (unsigned)mesh_id(m),
                mesh_id_is_fixed(m) ? "fixé" : "automatique", (unsigned long)mesh_tag(m));
  report_printf("  réseau     %u\r\n", (unsigned)mesh_net(m));
  report_print("  groupes   ");
  for (uint8_t g = 1U; g <= MESH_GROUP_COUNT; g++)
  {
    if (mesh_group_has(m, g))
    {
      report_printf(" %u", (unsigned)g);
      any = true;
    }
  }
  report_print(any ? "\r\n" : " aucun\r\n");
  if (node_radio_status() == RADIO_OK)
  {
    report_printf("  radio      %02X:%02X:%02X:%02X:%02X:%02X, %d dBm, ", a[5], a[4], a[3], a[2], a[1], a[0],
                  (int)mesh_radio_tx_power());
    print_radio_mode();
    report_print("\r\n");
  }
  else
  {
    report_printf("  radio      HORS SERVICE : %s\r\n", mesh_radio_status_text(node_radio_status()));
  }
  if (mesh_radio_fw_version() != 0U)
  {
    /* Même puce dans les deux modules de l'ObCP (SPBTLE-RF, BlueNRG-M0). */
    (void)mesh_radio_version_text(mesh_radio_fw_version(), v, sizeof(v));
    report_printf("  module     puce BlueNRG-MS %u.%u, pile %s%s%s\r\n",
                  (unsigned)(mesh_radio_hw_version() >> 4), (unsigned)(mesh_radio_hw_version() & 0x0FU), v,
                  (node_radio_status() == RADIO_OK) ? ", " : "",
                  (node_radio_status() == RADIO_OK) ? stack_text(mesh_radio_stack()) : "");
  }
  report_print("  réglages  ");
  for (int k = SETTING_TTL; k < SETTING_END; k++)
  {
    (void)settings_format_value((setting_t)k, node_setting_get((setting_t)k), v, sizeof(v));
    report_printf(" %s %s%s", settings_name((setting_t)k), v, ((k + 1) < SETTING_END) ? "," : "\r\n");
    if (k == SETTING_TX_POWER)
    {
      report_print("\r\n            ");   /* suite sur une deuxième ligne : moins de 100 colonnes */
    }
  }
  any = false;
  report_print("  bloqués   ");
  for (uint32_t id = MESH_ADDR_NODE_MIN; id <= MESH_ADDR_NODE_MAX; id++)
  {
    if (mesh_is_blocked(m, (uint8_t)id))
    {
      report_printf(" %u", (unsigned)id);
      any = true;
    }
  }
  report_print(any ? "\r\n" : " aucun\r\n");
  report_printf("  EEPROM     %s\r\n", node_store_text());
}

/* Commande « voisins » : cartes entendues en direct, triées par numéro. */
static void show_neighbors(void)
{
  const mesh_t   *m = node_mesh();
  mesh_neighbor_t list[MESH_NEIGHBORS_MAX];
  uint8_t         n = 0U;
  uint32_t        now = HAL_GetTick();

  while ((n < MESH_NEIGHBORS_MAX) && mesh_neighbor_at(m, n, &list[n]))
  {
    n++;
  }
  /* tri par puissance décroissante (tri par insertion : la liste est courte) */
  for (uint8_t i = 1U; i < n; i++)
  {
    mesh_neighbor_t x = list[i];
    uint8_t         j = i;
    while ((j > 0U) && (list[j - 1U].rssi < x.rssi))
    {
      list[j] = list[j - 1U];
      j--;
    }
    list[j] = x;
  }
  if (n == 0U)
  {
    report_print("Aucun voisin entendu depuis 60 s.\r\n");
    return;
  }
  report_printf("Voisins directs (%u) :\r\n  nœud     RSSI   trames   vu il y a\r\n", (unsigned)n);
  for (uint8_t i = 0U; i < n; i++)
  {
    uint32_t age = now - list[i].last_ms;
    report_printf("  %4u  %4d dBm  %6u  %4lu.%lu s\r\n", (unsigned)list[i].id, (int)list[i].rssi,
                  (unsigned)list[i].frames, (unsigned long)(age / 1000U), (unsigned long)((age % 1000U) / 100U));
  }
}

/* Commande « noeuds » : cartes connues du réseau (origines des trames). */
static void show_nodes(void)
{
  const mesh_t *m = node_mesh();
  mesh_node_t   nd;
  uint8_t       n = 0U;
  uint32_t      now = HAL_GetTick();

  while (mesh_node_at(m, n, &nd))
  {
    uint32_t age = now - nd.last_ms;
    if (n == 0U)
    {
      report_print("Nœuds connus :\r\n  nœud  relais  vu il y a  étiquette\r\n");
    }
    report_printf("  %4u  %6u  %4lu.%lu s  ", (unsigned)nd.id, (unsigned)nd.hops,
                  (unsigned long)(age / 1000U), (unsigned long)((age % 1000U) / 100U));
    if (nd.tag_known)
    {
      report_printf("%08lX%s\r\n", (unsigned long)nd.tag, ((nd.flags & MESH_HELLO_RELAY) != 0U) ? "" : " (ne relaie pas)");
    }
    else
    {
      report_print("(pas encore de HELLO)\r\n");
    }
    n++;
  }
  if (n == 0U)
  {
    report_print("Aucun nœud connu (rien reçu depuis 120 s).\r\n");
  }
  else
  {
    report_printf("%u nœud%s\r\n", (unsigned)n, (n > 1U) ? "s" : "");
  }
}

/* Commande « stats » : compteurs du réseau maillé et de la radio. */
static void show_stats(void)
{
  const mesh_stats_t  *s = mesh_stats(node_mesh());
  const radio_stats_t *r = mesh_radio_stats();

  report_print("Réseau maillé\r\n");
  report_printf("  trames reçues      %lu (autre réseau %lu, mal formées %lu, filtrées %lu)\r\n",
                (unsigned long)s->rx_frames, (unsigned long)s->rx_other_net, (unsigned long)s->rx_invalid,
                (unsigned long)s->rx_filtered);
  report_printf("  dont               premières copies %lu, répétitions %lu, doublons %lu, échos %lu\r\n",
                (unsigned long)s->rx_first, (unsigned long)s->rx_repeat, (unsigned long)s->rx_dup,
                (unsigned long)s->rx_echo);
  report_printf("  pour cette carte   %lu\r\n", (unsigned long)s->delivered);
  report_printf("  émises             %lu, relayées %lu\r\n", (unsigned long)s->tx_origin,
                (unsigned long)s->tx_relay);
  report_printf("  relais annulés     %lu, fin de TTL %lu, perdues (file pleine) %lu, refusées %lu\r\n",
                (unsigned long)s->relay_suppressed, (unsigned long)s->relay_ttl_end, (unsigned long)s->dropped,
                (unsigned long)s->tx_fail);
  report_printf("  numéros en double  %lu\r\n", (unsigned long)s->id_conflicts);
  report_print("Radio\r\n");
  report_printf("  publicités         entendues %lu, du réseau maillé %lu ; file pleine %lu\r\n",
                (unsigned long)r->adv_reports, (unsigned long)r->mesh_frames, (unsigned long)r->rx_overflow);
  report_printf("  émissions          %lu, refusées %lu", (unsigned long)r->tx_started, (unsigned long)r->tx_refused);
  if ((r->tx_refused > 0U) && (mesh_radio_last_refusal() != 0U))
  {
    report_printf(" (dernier code 0x%02X)", (unsigned)mesh_radio_last_refusal());
  }
  report_printf(", arrêts refusés %lu\r\n", (unsigned long)r->tx_stop_errors);
  report_printf("  écoute             lancements %lu, refus %lu, évènements perdus %lu, redémarrages du module %lu\r\n",
                (unsigned long)r->scan_starts, (unsigned long)r->scan_errors, (unsigned long)r->events_lost,
                (unsigned long)r->module_restarts);
}

/* Commande « mesure » sans destination : mesures de cette carte. */
static void show_measure(void)
{
  node_measure_t m;

  node_measure(&m);
  report_measure_local(&m);
}

/* Réglage local : affiche ou change. */
static void do_set(const cmd_t *cmd)
{
  char    text[24];
  int32_t applied = cmd->value;

  if (!cmd->has_value)
  {
    (void)settings_format_value(cmd->key, node_setting_get(cmd->key), text, sizeof(text));
    report_printf("%s = %s   (valeurs : %s)\r\n", settings_name(cmd->key), text, settings_help(cmd->key));
    return;
  }
  if (!node_setting_set(cmd->key, cmd->value, &applied))
  {
    report_printf("Réglage refusé : %s %ld\r\n", settings_name(cmd->key), (long)cmd->value);
    return;
  }
  (void)settings_format_value(cmd->key, applied, text, sizeof(text));
  report_printf("%s = %s\r\n", settings_name(cmd->key), text);
}

/* Réglage à distance ; appliqué aussi ici si la carte est concernée. */
static void do_remote_set(const cmd_t *cmd)
{
  char          d[8];
  char          text[24];
  mesh_result_t r;
  const mesh_t *m = node_mesh();
  bool          me_too = (cmd->dst == MESH_ADDR_ALL)
                         || (mesh_addr_is_group(cmd->dst) && mesh_group_has(m, mesh_addr_group(cmd->dst)));

  if (!radio_ok())
  {
    return;
  }
  r = node_send_setting(cmd->dst, cmd->key, cmd->value);
  if (r != MESH_OK)
  {
    report_send_error(r);
    return;
  }
  (void)settings_format_value(cmd->key, cmd->value, text, sizeof(text));
  report_printf("Réglage envoyé à %s : %s = %s", dest_text(cmd->dst, d, sizeof(d)), settings_name(cmd->key), text);
  if (me_too && node_setting_set(cmd->key, cmd->value, NULL))
  {
    report_print(" (appliqué aussi sur cette carte)");
  }
  report_print("\r\n");
}

/* Voir commands.h */
void commands_execute(const char *line)
{
  cmd_t         cmd;
  char          d[8];
  mesh_result_t r;
  mesh_t       *m = node_mesh();

  commands_parse(line, &cmd);
  switch (cmd.id)
  {
    case CMD_EMPTY:
      break;

    case CMD_ERROR:
      if (cmd.bad_value)
      {
        report_printf("Valeur refusée pour %s (valeurs : %s)\r\n", settings_name(cmd.key), cmd.error);
      }
      else
      {
        report_printf("%s\r\n", cmd.error);
      }
      break;

    case CMD_HELP:
      report_print(s_help);
      break;

    case CMD_INFO:
      show_info();
      break;

    case CMD_NEIGHBORS:
      show_neighbors();
      break;

    case CMD_NODES:
      show_nodes();
      break;

    case CMD_STATS:
      show_stats();
      break;

    case CMD_STATS_CLEAR:
      mesh_stats_clear(m);
      mesh_radio_stats_clear();
      report_print("Compteurs remis à zéro.\r\n");
      break;

    case CMD_MEASURE_LOCAL:
      show_measure();
      break;

    case CMD_MEASURE_REQ:
      if (radio_ok())
      {
        r = node_request_measure(cmd.dst);
        if (r == MESH_OK)
        {
          report_printf("Demande de mesures envoyée à %s (ttl %u)\r\n", dest_text(cmd.dst, d, sizeof(d)), current_ttl());
        }
        report_send_error(r);
      }
      break;

    case CMD_MSG:
      if (radio_ok())
      {
        r = node_send_text(cmd.dst, cmd.text);
        if (r == MESH_OK)
        {
          report_printf("Message envoyé à %s (ttl %u)%s\r\n", dest_text(cmd.dst, d, sizeof(d)), current_ttl(),
                        (strlen(cmd.text) > MESH_PAYLOAD_MAX) ? ", coupé à 14 octets" : "");
        }
        report_send_error(r);
      }
      break;

    case CMD_LED:
      if (radio_ok())
      {
        r = node_send_led(cmd.dst, cmd.color, (uint8_t)cmd.value);
        if (r == MESH_OK)
        {
          report_printf("Couleur %u %u %u envoyée à %s", (unsigned)cmd.color.r, (unsigned)cmd.color.g,
                        (unsigned)cmd.color.b, dest_text(cmd.dst, d, sizeof(d)));
          if (cmd.value > 0)
          {
            report_printf(" pour %ld s\r\n", (long)cmd.value);
          }
          else
          {
            report_print("\r\n");
          }
        }
        report_send_error(r);
      }
      break;

    case CMD_WAVE:
      if (radio_ok())
      {
        r = node_send_wave();
        if (r == MESH_OK)
        {
          report_printf("Vague lancée (ttl %u) : rouge en direct, puis orange, jaune, vert, cyan...\r\n", current_ttl());
        }
        report_send_error(r);
      }
      break;

    case CMD_IDENT:
      if (radio_ok())
      {
        r = node_send_ident(cmd.dst, (uint8_t)cmd.value);
        if (r == MESH_OK)
        {
          report_printf("Identification demandée à %s pendant %ld s\r\n", dest_text(cmd.dst, d, sizeof(d)),
                        (long)cmd.value);
        }
        report_send_error(r);
      }
      break;

    case CMD_PING:
      if (radio_ok())
      {
        uint8_t id = 0U;
        r = node_ping(cmd.dst, &id);
        if (r == MESH_OK)
        {
          report_printf("Ping n°%u envoyé à %s (ttl %u)\r\n", (unsigned)id, dest_text(cmd.dst, d, sizeof(d)),
                        current_ttl());
        }
        report_send_error(r);
      }
      break;

    case CMD_TRACE:
      if (radio_ok())
      {
        uint8_t id = 0U;
        r = node_trace(cmd.dst, &id);
        if (r == MESH_OK)
        {
          report_printf("Trace n°%u envoyée à %u (ttl %u) : chaque relais y ajoute son numéro\r\n", (unsigned)id,
                        (unsigned)cmd.dst, current_ttl());
        }
        report_send_error(r);
      }
      break;

    case CMD_REMOTE_SET:
      do_remote_set(&cmd);
      break;

    case CMD_SET:
      do_set(&cmd);
      break;

    case CMD_ID:
      if (cmd.has_value)
      {
        (void)node_set_id((uint8_t)cmd.value);
        report_printf("Numéro : %u (%s)%s\r\n", (unsigned)mesh_id(m), mesh_id_is_fixed(m) ? "fixé" : "automatique",
                      mesh_id_is_fixed(m) ? ". « sauve » pour le garder après un redémarrage" : "");
      }
      else
      {
        report_printf("Numéro : %u (%s)\r\n", (unsigned)mesh_id(m), mesh_id_is_fixed(m) ? "fixé" : "automatique");
      }
      break;

    case CMD_NET:
      if (cmd.has_value)
      {
        node_set_net((uint8_t)cmd.value);
      }
      report_printf("Réseau : %u%s\r\n", (unsigned)mesh_net(m), cmd.has_value ? " (tables vidées)" : "");
      break;

    case CMD_GROUP:
      if (cmd.has_value)
      {
        (void)mesh_group_set(m, (uint8_t)cmd.value, cmd.join);
        report_printf("Groupe %ld %s\r\n", (long)cmd.value, cmd.join ? "rejoint" : "quitté");
      }
      else
      {
        bool any = false;
        report_print("Groupes suivis :");
        for (uint8_t g = 1U; g <= MESH_GROUP_COUNT; g++)
        {
          if (mesh_group_has(m, g))
          {
            report_printf(" %u", (unsigned)g);
            any = true;
          }
        }
        report_print(any ? "\r\n" : " aucun\r\n");
      }
      break;

    case CMD_BLOCK:
      if (cmd.has_value)
      {
        mesh_block_set(m, (uint8_t)cmd.value, true);
        report_printf("Émetteur %ld bloqué : ses trames (et ses relais) sont ignorées\r\n", (long)cmd.value);
      }
      else
      {
        bool any = false;
        report_print("Émetteurs bloqués :");
        for (uint32_t id = MESH_ADDR_NODE_MIN; id <= MESH_ADDR_NODE_MAX; id++)
        {
          if (mesh_is_blocked(m, (uint8_t)id))
          {
            report_printf(" %u", (unsigned)id);
            any = true;
          }
        }
        report_print(any ? "\r\n" : " aucun\r\n");
      }
      break;

    case CMD_UNBLOCK:
      if (cmd.value == 0)
      {
        mesh_block_clear(m);
        report_print("Plus aucun émetteur bloqué\r\n");
      }
      else
      {
        mesh_block_set(m, (uint8_t)cmd.value, false);
        report_printf("Émetteur %ld débloqué\r\n", (long)cmd.value);
      }
      break;

    case CMD_RADIO:
      if (cmd.has_value)
      {
        if (cmd.value == CMD_RADIO_AUTO)
        {
          mesh_radio_set_mode(RADIO_MODE_ALTERNATE, false);
        }
        else
        {
          mesh_radio_set_mode((cmd.value == CMD_RADIO_ALTERNATE) ? RADIO_MODE_ALTERNATE : RADIO_MODE_SIMULTANEOUS,
                              true);
        }
      }
      report_print("Radio : ");
      print_radio_mode();
      report_print("\r\n");
      if (mesh_radio_mode() == RADIO_MODE_SIMULTANEOUS)
      {
        report_print((mesh_radio_stack() == RADIO_STACK_MODE4)
                     ? "Si le module refuse d'émettre pendant l'écoute, retour automatique au mode alterné\r\n"
                     : "Attention : la pile du module n'est pas en mode 4 ; il refusera sans doute d'émettre "
                       "pendant l'écoute (retour automatique au mode alterné)\r\n");
      }
      if (!cmd.has_value)
      {
        uint32_t period;
        uint32_t free_slot;

        if (mesh_radio_time_base(&period, &free_slot) && (period > 0U))
        {
          /* Base de temps du module (PM0237) : écoute et publicité s'y partagent la période. */
          report_print("Base de temps du module : période ");
          print_steps_ms(period);
          report_print(", plus grand créneau libre ");
          print_steps_ms(free_slot);
          report_print("\r\n");
        }
      }
      break;

    case CMD_JOURNAL:
      if (cmd.has_value)
      {
        report_set_journal(cmd.value != 0);
      }
      report_printf("Journal des trames : %s\r\n", report_is_journal() ? "on" : "off");
      break;

    case CMD_PC:
      if (cmd.has_value)
      {
        report_set_json(cmd.value != 0);
        usb_console_set_echo(cmd.value == 0);
        if (cmd.value != 0)
        {
          report_info_json(m);
        }
      }
      report_printf("Mode %s\r\n", report_is_json() ? "PC (lignes JSON)" : "texte");
      break;

    case CMD_SAVE:
    {
      store_status_t s = node_save();
      if (s == STORE_OK)
      {
        report_printf("Réglages enregistrés dans l'EEPROM (numéro %s, réseau %u)\r\n",
                      mesh_id_is_fixed(m) ? "fixé" : "automatique", (unsigned)mesh_net(m));
      }
      else
      {
        report_printf("Enregistrement impossible : %s\r\n", config_store_status_text(s));
      }
      break;
    }

    case CMD_FORGET:
    {
      store_status_t s = node_forget();
      if (s == STORE_OK)
      {
        report_print("Réglages enregistrés effacés : valeurs par défaut au prochain démarrage\r\n");
      }
      else
      {
        report_printf("Effacement impossible : %s\r\n", config_store_status_text(s));
      }
      break;
    }

    case CMD_CLS:
      report_print(ANSI_CLEAR_SCREEN);
      break;

    case CMD_OTA:
      report_print("Redémarrage dans le chargeur Bluetooth ObCP-OTA (sans chargeur : simple redémarrage)\r\n");
      app_request_ota();
      break;

    default:
      break;
  }
}

/* Voir commands.h */
void commands_reject(void)
{
  report_print("Ligne trop longue, ignorée.\r\n");
}
