/**
 ******************************************************************************
 * @file    commands.c
 * @brief   Interpréteur des commandes (voir commands.h pour la liste).
 *
 *  Deux étapes bien séparées :
 *    1. commands_parse() lit la ligne et dit de quelle commande il s'agit
 *       (sans rien toucher : on peut la tester sur PC) ;
 *    2. commands_execute() agit (LED, PWM, capteur, minuterie) et répond.
 *
 *  Ajouter une commande « x » (exemple) :
 *    1. dans commands.h, ajouter CMD_X à l'énumération cmd_id_t ;
 *    2. dans commands_parse(), reconnaître le texte (strcmp(text, "x") == 0) ;
 *    3. dans commands_execute(), ajouter « case CMD_X: » avec l'action et la
 *       réponse (snprintf dans reply) ;
 *    4. compléter le texte d'aide s_help ci-dessous et le README.
 ******************************************************************************
 */
#include "commands.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "accelerometer.h"
#include "ble_uart.h"
#include "outputs.h"
#include "timer_1s.h"
#include "usb_console.h"

/* -------------------------------------------------------------------------- */
/* Textes envoyés (identiques à la version 2024)                              */
/* -------------------------------------------------------------------------- */

/* Aide affichée par « ? ». Seule correction : la ligne de la LED bleue disait
 * « for red ». Chaque ligne finit par « \n\r », comme toutes les réponses.   */
static const char s_help[] =
  "Type command : \n\r"
  "- R or r led and integer from 0 to 65535 like r50 for red led\n\r"
  "- V or v led and integer from 0 to 65535 like v50 for green\n\r"
  "- B or b led and integer from 0 to 65535 like b50 for blue\n\r"
  "- M or m and integer from 0 to 65535 like m50 for pwm output\n\r"
  "- A or a like a to read accelerometer\n\r"
  "- T or t like t to read temperature\n\r"
  "- cls to clear screen (USB)\n\r"
  "- start timer or stop timer (BLE)\n\r"
  "- #rst to reset functions\n\r";

/* Séquences « ANSI » comprises par les terminaux (PuTTY, Tera Term...) :
 * ESC[1;1H place le curseur en haut à gauche, ESC[2J efface l'écran.
 * ESC est le caractère de code 27 (0x1B). Voir
 * https://en.wikipedia.org/wiki/ANSI_escape_code                            */
#define ANSI_CLEAR_SCREEN   "\x1B[1;1H\x1B[2J"

#define MSG_FALSE_COMMAND   "False command\n\r"
#define MSG_FALSE_VALUE     "False value (0-65535)\n\r"        /* nouveau en v2 */
#define MSG_ACCEL_ERROR     "Accelerometer error\n\r"          /* nouveau en v2 */

/* -------------------------------------------------------------------------- */
/* Analyse                                                                    */
/* -------------------------------------------------------------------------- */

/* Vrai pour un espace ou une tabulation. */
static bool is_blank(char c)
{
  return (c == ' ') || (c == '\t');
}

/* Vrai pour un chiffre décimal. */
static bool is_digit(char c)
{
  return (c >= '0') && (c <= '9');
}

/* Lit la valeur qui suit la lettre de r, v, b ou m (texte après la lettre).
 * Accepte des espaces avant le nombre (« r 500 », comme la version 2024),
 * puis uniquement des chiffres décimaux : « r010 » vaut 10 (la version 2024,
 * qui utilisait sscanf("%i"), le lisait en octal : 8 !).
 * Rend CMD_BAD_VALUE si le nombre est absent, signé, suivi d'autre chose ou
 * supérieur à 65535 ; CMD_UNKNOWN si la lettre est suivie d'un mot
 * (« red », « blue »...), qui n'est pas une commande ; id sinon.          */
static cmd_id_t parse_value(const char *p, cmd_id_t id, uint16_t *value)
{
  uint32_t n = 0U;

  while (is_blank(*p))
  {
    p++;
  }
  if (*p == '\0' || *p == '-' || *p == '+')
  {
    return CMD_BAD_VALUE;               /* « r » seul, « r-5 », « r+5 » */
  }
  if (!is_digit(*p))
  {
    return CMD_UNKNOWN;                 /* « red », « blue »... */
  }
  while (is_digit(*p))
  {
    n = (n * 10U) + (uint32_t)(*p - '0');
    if (n > CMD_VALUE_MAX)
    {
      return CMD_BAD_VALUE;             /* trop grand (et arrêt avant tout débordement) */
    }
    p++;
  }
  if (*p != '\0')
  {
    return CMD_BAD_VALUE;               /* « r50x », « r5.5 », « r50 60 » */
  }
  *value = (uint16_t)n;
  return id;
}

/* Voir commands.h */
cmd_t commands_parse(const char *line)
{
  cmd_t  cmd = { CMD_UNKNOWN, 0U };
  char   text[CMD_LINE_MAX + 1U];
  size_t len;

  /* 1) Copie de la ligne sans les espaces du début et de la fin. */
  while (is_blank(*line))
  {
    line++;
  }
  len = strlen(line);
  if (len > CMD_LINE_MAX)
  {
    return cmd;                         /* ne peut pas être une commande connue */
  }
  memcpy(text, line, len + 1U);         /* + 1 : le '\0' final */
  while ((len > 0U) && is_blank(text[len - 1U]))
  {
    len--;
    text[len] = '\0';
  }
  if (len == 0U)
  {
    cmd.id = CMD_EMPTY;
    return cmd;
  }

  /* 2) Commandes écrites en entier (majuscules et minuscules comptent). */
  if (strcmp(text, "start timer") == 0)
  {
    cmd.id = CMD_TIMER_START;
  }
  else if (strcmp(text, "stop timer") == 0)
  {
    cmd.id = CMD_TIMER_STOP;
  }
  else if (strcmp(text, "#rst") == 0)
  {
    cmd.id = CMD_RESET;
  }
  else if (strcmp(text, "cls") == 0)
  {
    cmd.id = CMD_CLEAR_SCREEN;
  }
  else if (strcmp(text, "?") == 0)
  {
    cmd.id = CMD_HELP;
  }
  /* 3) Commandes d'une seule lettre, sans valeur. */
  else if ((strcmp(text, "a") == 0) || (strcmp(text, "A") == 0))
  {
    cmd.id = CMD_ACCEL;
  }
  else if ((strcmp(text, "t") == 0) || (strcmp(text, "T") == 0))
  {
    cmd.id = CMD_TEMPERATURE;
  }
  /* 4) Une lettre suivie d'une valeur : r, v, b, m. */
  else
  {
    switch (text[0])
    {
      case 'r': case 'R': cmd.id = parse_value(&text[1], CMD_RED,   &cmd.value); break;
      case 'v': case 'V': cmd.id = parse_value(&text[1], CMD_GREEN, &cmd.value); break;
      case 'b': case 'B': cmd.id = parse_value(&text[1], CMD_BLUE,  &cmd.value); break;
      case 'm': case 'M': cmd.id = parse_value(&text[1], CMD_MOTOR, &cmd.value); break;
      default:            cmd.id = CMD_UNKNOWN;                                  break;
    }
  }
  return cmd;
}

/* -------------------------------------------------------------------------- */
/* Envoi des réponses                                                         */
/* -------------------------------------------------------------------------- */

/* Envoie un texte vers une seule interface. */
static void send_to(cmd_source_t dst, const char *text)
{
  if (dst == CMD_SRC_USB)
  {
    (void)usb_console_print(text);
  }
  else
  {
    (void)ble_uart_print(text);
  }
}

/* Envoie la réponse à l'interface qui a reçu la commande, puis la recopie
 * sur l'autre interface si elle est active (même règle qu'en 2024). Une
 * réponse vide n'est pas envoyée (la version 2024 envoyait un paquet USB et
 * une notification BLE vides après « start timer », « #rst »...).          */
static void send_reply(cmd_source_t src, const char *text)
{
  if (text[0] == '\0')
  {
    return;
  }
  send_to(src, text);
  if ((src == CMD_SRC_BLE) && usb_console_is_ready())
  {
    send_to(CMD_SRC_USB, text);
  }
  if ((src == CMD_SRC_USB) && ble_uart_is_connected())
  {
    send_to(CMD_SRC_BLE, text);
  }
}

/* -------------------------------------------------------------------------- */
/* Exécution                                                                  */
/* -------------------------------------------------------------------------- */

/* Voir commands.h */
void commands_execute(const char *line, cmd_source_t src)
{
  cmd_t     cmd = commands_parse(line);
  char      reply[96];                  /* la plus longue réponse fait 41 caractères */
  accel_g_t g;
  int       celsius;

  reply[0] = '\0';

  switch (cmd.id)
  {
    case CMD_EMPTY:                     /* ligne vide : pas de réponse */
      return;

    case CMD_RED:
      outputs_set(OUTPUT_RED, cmd.value);
      (void)snprintf(reply, sizeof(reply), "Red = %u\n\r", (unsigned)cmd.value);
      break;

    case CMD_GREEN:
      outputs_set(OUTPUT_GREEN, cmd.value);
      (void)snprintf(reply, sizeof(reply), "Green = %u\n\r", (unsigned)cmd.value);
      break;

    case CMD_BLUE:
      outputs_set(OUTPUT_BLUE, cmd.value);
      (void)snprintf(reply, sizeof(reply), "Blue = %u\n\r", (unsigned)cmd.value);
      break;

    case CMD_MOTOR:
      outputs_set(OUTPUT_MOTOR, cmd.value);
      (void)snprintf(reply, sizeof(reply), "PWM = %u\n\r", (unsigned)cmd.value);
      break;

    case CMD_ACCEL:
      if (accel_read(&g))
      {
        /* %.2f : 2 chiffres après la virgule. Le type double est celui
         * qu'attend printf pour %f (un float y serait converti de toute façon). */
        (void)snprintf(reply, sizeof(reply), "Acceleration : X=%.2f Y=%.2f Z=%.2f\n\r",
                       (double)g.x, (double)g.y, (double)g.z);
      }
      else
      {
        (void)strcpy(reply, MSG_ACCEL_ERROR);
      }
      break;

    case CMD_TEMPERATURE:
      if (accel_read_temperature(&celsius))
      {
        (void)snprintf(reply, sizeof(reply), "T = %i\n\r", celsius);
      }
      else
      {
        (void)strcpy(reply, MSG_ACCEL_ERROR);
      }
      break;

    case CMD_HELP:                      /* aide : seulement à l'interface qui demande */
      send_to(src, s_help);
      return;

    case CMD_CLEAR_SCREEN:
      if (src == CMD_SRC_USB)
      {
        send_to(CMD_SRC_USB, ANSI_CLEAR_SCREEN);   /* vers le terminal seulement */
        return;
      }
      (void)strcpy(reply, MSG_FALSE_COMMAND);      /* « cls » par BLE : refusé (comme en 2024) */
      break;

    case CMD_TIMER_START:
      timer_1s_start();
      return;

    case CMD_TIMER_STOP:
      timer_1s_stop();
      return;

    case CMD_RESET:                     /* « #rst » : sorties à 0 et minuterie arrêtée */
      outputs_all_off();
      timer_1s_stop();
      return;

    case CMD_BAD_VALUE:
      (void)strcpy(reply, MSG_FALSE_VALUE);
      break;

    case CMD_UNKNOWN:
    default:
      (void)strcpy(reply, MSG_FALSE_COMMAND);
      break;
  }

  send_reply(src, reply);
}

/* Voir commands.h */
void commands_reject(cmd_source_t src)
{
  send_reply(src, MSG_FALSE_COMMAND);
}

/* Voir commands.h */
const char *commands_help_text(void)
{
  return s_help;
}
