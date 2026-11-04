/**
 ******************************************************************************
 * @file    console.c
 * @brief   Console sur le port COM virtuel USB de l'ObCP (voir console.h).
 *
 *  Émission : le texte est confié au tampon USB de cdc_port.c ('\n' devient
 *  "\r\n" pour le terminal). Réception : console_readline() lit les
 *  caractères un par un et gère l'édition de la ligne :
 *    Entrée        : ligne terminée, rendue à app_loop()
 *    Retour arr.   : efface le dernier caractère (UTF-8 : « é » = 2 octets)
 *    Flèche haut   : rappelle la commande précédente (séquence ESC [ A)
 *    Ctrl-C        : abandonne la saisie
 *  Codes ANSI : séquences « ESC [ ... » interprétées par le terminal
 *  (couleurs, effacement de ligne « ESC [ K »).
 ******************************************************************************
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "console.h"
#include "board.h"
#include "cdc_port.h"
#include "util.h"

#define TX_STALL_MS   300U       /* terminal ouvert mais qui ne lit plus : abandon */

static bool     s_color = true;               /* couleurs ANSI actives          */
static char     s_line[CONSOLE_LINE_MAX];     /* ligne en cours de saisie       */
static uint16_t s_len;                        /* sa longueur (octets)           */
static char     s_hist[CONSOLE_LINE_MAX];     /* commande précédente (flèche ↑) */
static uint8_t  s_esc;                        /* séquence ESC [ en cours (0..2) */
static bool     s_prev_cr;                    /* dernier caractère : CR         */
static bool     s_prompt_visible;             /* invite affichée                */
static bool     s_async_prompt;   /* invite à réafficher après console_async_end() */
static uint32_t s_dropped;                    /* octets perdus, terminal fermé  */
static uint32_t s_open_seen;                  /* ouvertures du port déjà vues   */

static const char PROMPT[] = "obcp> ";

/* ---- Couleurs ------------------------------------------------------------- */
const char *C_OK(void)   { return s_color ? "\033[32m" : ""; }
const char *C_ERR(void)  { return s_color ? "\033[31m" : ""; }
const char *C_WARN(void) { return s_color ? "\033[33m" : ""; }
const char *C_INFO(void) { return s_color ? "\033[36m" : ""; }
const char *C_BOLD(void) { return s_color ? "\033[1m" : ""; }
const char *C_DIM(void)  { return s_color ? "\033[2m" : ""; }
const char *C_RST(void)  { return s_color ? "\033[0m" : ""; }

/* Voir console.h */
void console_set_color(bool on)
{
  s_color = on;
}

/* Voir console.h */
bool console_color(void)
{
  return s_color;
}

/* ---- Émission -------------------------------------------------------------- */
/* Terminal fermé : les octets restent dans le tampon USB (4 Ko) jusqu'à son
 * ouverture, au-delà ils sont perdus (comptés). Terminal ouvert : attente
 * de place, sauf s'il ne lit plus rien pendant TX_STALL_MS.                  */
static void out_raw(const uint8_t *data, size_t n)
{
  uint32_t t_progress = HAL_GetTick();

  while (n > 0U)
  {
    size_t k = cdc_port_write(data, n);

    data += k;
    n -= k;
    if (n == 0U)
    {
      break;
    }
    if (k > 0U)
    {
      t_progress = HAL_GetTick();
    }
    if (!cdc_port_open() || (__get_PRIMASK() != 0U) || ((HAL_GetTick() - t_progress) > TX_STALL_MS))
    {
      s_dropped += (uint32_t)n;
      break;
    }
  }
}

/* Voir console.h : conversion par blocs de 128 octets */
void console_write(const char *s, size_t n)
{
  uint8_t buf[128];
  size_t  len = 0U;
  size_t  i;

  for (i = 0U; i < n; i++)
  {
    if (len > (sizeof(buf) - 2U))
    {
      out_raw(buf, len);
      len = 0U;
    }
    if (s[i] == '\n')
    {
      buf[len++] = (uint8_t)'\r';
    }
    buf[len++] = (uint8_t)s[i];
  }
  if (len > 0U)
  {
    out_raw(buf, len);
  }
}

/* Voir console.h */
void console_write_raw(const uint8_t *data, size_t n)
{
  out_raw(data, n);
}

/* Voir console.h */
void console_puts(const char *s)
{
  console_write(s, strlen(s));
}

/* Voir console.h : mise en forme dans un tampon local (vsnprintf), puis écriture */
int console_printf(const char *fmt, ...)
{
  char    buf[384];
  va_list ap;
  int     n;

  va_start(ap, fmt);
  n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0)
  {
    return n;
  }
  if ((size_t)n >= sizeof(buf))
  {
    n = (int)sizeof(buf) - 1;
  }
  console_write(buf, (size_t)n);
  return n;
}

/* Attend que tout soit parti (terminal ouvert), 2 s au plus */
void console_flush(void)
{
  uint32_t t0 = HAL_GetTick();

  while (cdc_port_open() && !cdc_port_tx_empty() && ((HAL_GetTick() - t0) < 2000U))
  {
    cdc_port_tx_kick();
  }
}

/* Voir console.h */
uint32_t console_take_dropped(void)
{
  uint32_t n = s_dropped;
  s_dropped = 0U;
  return n;
}

/* Voir console.h : compare le compteur d'ouvertures de cdc_port.c à celui
 * déjà vu                                                               */
bool console_new_connection(bool *first)
{
  uint32_t c = cdc_port_open_count();

  if (c == s_open_seen)
  {
    return false;
  }
  if (first != NULL)
  {
    *first = (s_open_seen == 0U);
  }
  s_open_seen = c;
  return true;
}

/* Voir console.h */
void console_pad(const char *s, size_t width)
{
  static const char spaces[] = "                                        ";
  size_t n = util_utf8_len(s);

  console_puts(s);
  while (n < width)
  {
    size_t k = width - n;
    if (k > (sizeof(spaces) - 1U))
    {
      k = sizeof(spaces) - 1U;
    }
    console_write(spaces, k);
    n += k;
  }
}

/* Réaffiche l'invite et la saisie en cours (après un message de fond) */
static void redraw_input(void)
{
  console_printf("%s%s%s", C_BOLD(), PROMPT, C_RST());
  console_write(s_line, s_len);
}

/* Voir console.h */
void console_event(const char *fmt, ...)
{
  char    buf[384];
  va_list ap;
  int     n;

  va_start(ap, fmt);
  n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0)
  {
    return;
  }
  if ((size_t)n >= sizeof(buf))
  {
    n = (int)sizeof(buf) - 1;
  }
  if (s_prompt_visible)
  {
    console_puts(s_color ? "\r\033[K" : "\n");
  }
  console_write(buf, (size_t)n);
  if ((n == 0) || (buf[n - 1] != '\n'))
  {
    console_puts("\n");
  }
  if (s_prompt_visible)
  {
    redraw_input();
  }
}

/* Voir console.h */
void console_async_begin(void)
{
  s_async_prompt = s_prompt_visible;
  if (s_prompt_visible)
  {
    console_puts(s_color ? "\r\033[K" : "\n");
    s_prompt_visible = false;
  }
}

/* Voir console.h */
void console_async_end(void)
{
  if (s_async_prompt)
  {
    redraw_input();
    s_prompt_visible = true;
    s_async_prompt = false;
  }
}

/* ---- Réception ------------------------------------------------------------- */
/* Voir console.h */
int console_getc(void)
{
  return cdc_port_getc();
}

/* Voir console.h */
void console_rx_flush(void)
{
  cdc_port_rx_flush();
}

/* Voir console.h */
bool console_abort(void)
{
  bool hit = false;
  int  c;

  while ((c = console_getc()) >= 0)
  {
    if (c != '\n')                   /* LF d'un CR/LF : ignoré */
    {
      hit = true;
    }
  }
  if (hit)
  {
    s_prev_cr = false;
  }
  return hit;
}

/* Efface n caractères à gauche du curseur : retour arrière, espace, retour arrière */
static void erase_chars(uint16_t n)
{
  while (n-- > 0U)
  {
    console_puts("\b \b");
  }
}

/* Touche retour arrière : retire le dernier caractère de la saisie */
static void backspace(void)
{
  if (s_len == 0U)
  {
    return;
  }
  /* UTF-8 : supprime les octets de continuation puis l'octet de tête */
  while ((s_len > 0U) && (((uint8_t)s_line[s_len - 1U] & 0xC0U) == 0x80U))
  {
    s_len--;
  }
  if (s_len > 0U)
  {
    s_len--;
  }
  erase_chars(1U);
}

/* Nombre de colonnes occupées par n octets UTF-8 (les octets de
 * continuation 10xxxxxx ne comptent pas)                               */
static uint16_t utf8_columns(const char *s, uint16_t n)
{
  uint16_t cols = 0U;
  uint16_t i;
  for (i = 0U; i < n; i++)
  {
    if (((uint8_t)s[i] & 0xC0U) != 0x80U)
    {
      cols++;
    }
  }
  return cols;
}

/* Remplace la saisie affichée par txt (rappel de la commande précédente) */
static void replace_line(const char *txt)
{
  erase_chars(utf8_columns(s_line, s_len));
  s_len = (uint16_t)strlen(txt);
  if (s_len >= CONSOLE_LINE_MAX)
  {
    s_len = CONSOLE_LINE_MAX - 1U;
  }
  memcpy(s_line, txt, s_len);
  console_write(s_line, s_len);
}

/* Voir console.h */
bool console_readline(char *out, size_t len)
{
  int c;

  while ((c = console_getc()) >= 0)
  {
    bool was_cr = s_prev_cr;
    s_prev_cr = false;

    if (s_esc == 1U)
    {
      s_esc = (c == '[') ? 2U : 0U;
      continue;
    }
    if (s_esc == 2U)
    {
      s_esc = 0U;
      if (c == 'A')
      {
        replace_line(s_hist);       /* flèche haut : dernière commande */
      }
      else if (c == 'B')
      {
        replace_line("");           /* flèche bas : efface la saisie */
      }
      else
      {
        /* autres touches de fonction ignorées */
      }
      continue;
    }

    switch (c)
    {
      case 0x1B:
        s_esc = 1U;
        break;

      case '\n':
        if (was_cr)
        {
          break;                    /* LF d'un CR/LF */
        }
        /* fallthrough */
      case '\r':
        s_prev_cr = (c == '\r');
        console_puts("\n");
        s_line[s_len] = '\0';
        (void)snprintf(out, len, "%s", s_line);
        if (s_len > 0U)
        {
          memcpy(s_hist, s_line, (size_t)s_len + 1U);
        }
        s_len = 0U;
        s_prompt_visible = false;
        return true;

      case 0x08:
      case 0x7F:
        backspace();
        break;

      case 0x03:                    /* Ctrl-C */
        console_puts("^C\n");
        s_len = 0U;
        console_prompt();
        break;

      case 0x15:                    /* Ctrl-U */
        replace_line("");
        break;

      default:
        if ((c >= 0x20) && (s_len < (CONSOLE_LINE_MAX - 1U)))
        {
          char ch = (char)c;
          s_line[s_len++] = ch;
          console_write(&ch, 1U);
        }
        break;
    }
  }
  return false;
}

/* Voir console.h */
void console_prompt(void)
{
  s_len = 0U;
  redraw_input();
  s_prompt_visible = true;
}

/* Voir console.h */
void console_set_prompt_visible(bool on)
{
  s_prompt_visible = on;
}

/* ---- Initialisation ---------------------------------------------------------- */
/* Voir console.h */
void console_init(void)
{
  s_len = 0U;
  s_hist[0] = '\0';
  s_dropped = 0U;
  (void)setvbuf(stdout, NULL, _IONBF, 0);
}

/* printf() de la bibliothèque C redirigé vers la console : la bibliothèque
 * appelle _write() pour écrire ; cette version remplace celle de
 * syscalls.c (fichier généré, fonction déclarée « weak »).              */
int _write(int file, char *ptr, int len)
{
  (void)file;
  if (len > 0)
  {
    console_write(ptr, (size_t)len);
  }
  return len;
}
