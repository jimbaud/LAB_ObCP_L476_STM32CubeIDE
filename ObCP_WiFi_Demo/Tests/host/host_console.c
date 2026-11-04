/*
 * Console sur PC pour test_sim : même API que Core/Src/app/console.c, entrée
 * clavier scriptée (avec instant d'arrivée), sortie capturée et recopiée
 * dans un fichier journal.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include "console.h"
#include "util.h"
#include "host.h"
#include "ncp_port.h"
#include "sim_module.h"

#define OUT_MAX   (512U * 1024U)
#define IN_MAX    16384U

static char     s_out[OUT_MAX];
static size_t   s_out_n;
static FILE    *s_log;
static bool     s_color = true;
static bool     s_prompt_visible;
static char     s_line[CONSOLE_LINE_MAX];
static uint16_t s_len;

typedef struct
{
  uint8_t  c;
  uint32_t at;
} hkey_t;

static hkey_t    s_in[IN_MAX];
static unsigned s_in_h;
static unsigned s_in_t;
static host_rx_hook_t s_rx_hook;
static int      s_fd = -1;              /* pseudo-terminal (essai avec le script PC) */

const char *C_OK(void)   { return s_color ? "\033[32m" : ""; }
const char *C_ERR(void)  { return s_color ? "\033[31m" : ""; }
const char *C_WARN(void) { return s_color ? "\033[33m" : ""; }
const char *C_INFO(void) { return s_color ? "\033[36m" : ""; }
const char *C_BOLD(void) { return s_color ? "\033[1m" : ""; }
const char *C_DIM(void)  { return s_color ? "\033[2m" : ""; }
const char *C_RST(void)  { return s_color ? "\033[0m" : ""; }

void console_init(void) {}
void console_set_color(bool on) { s_color = on; }
bool console_color(void) { return s_color; }

static void log_plain(const char *s, size_t n)
{
  size_t i = 0U;
  if (s_log == NULL)
  {
    return;
  }
  while (i < n)
  {
    if ((s[i] == '\r') && ((i + 3U) < n) && (memcmp(&s[i], "\r\033[K", 4) == 0))
    {
      fputc('\n', s_log);               /* effacement de ligne : nouvelle ligne dans le journal */
      i += 4U;
      continue;
    }
    if (s[i] == '\033')                 /* retire les séquences ANSI */
    {
      i++;
      if ((i < n) && (s[i] == '['))
      {
        i++;
        while ((i < n) && !(((s[i] >= 'A') && (s[i] <= 'Z')) || ((s[i] >= 'a') && (s[i] <= 'z'))))
        {
          i++;
        }
      }
      i++;
      continue;
    }
    if (s[i] != '\r')
    {
      fputc(s[i], s_log);
    }
    i++;
  }
  fflush(s_log);
}

static void out_append(const char *s, size_t n)
{
  if ((s_out_n + n) < (OUT_MAX - 1U))
  {
    memcpy(&s_out[s_out_n], s, n);
    s_out_n += n;
    s_out[s_out_n] = '\0';
  }
}

static void fd_write(const char *s, size_t n, bool crlf)
{
  size_t i;
  if (s_fd < 0)
  {
    return;
  }
  for (i = 0; i < n; i++)
  {
    if (crlf && (s[i] == '\n'))
    {
      (void)!write(s_fd, "\r", 1);
    }
    (void)!write(s_fd, &s[i], 1);
  }
}

void console_write(const char *s, size_t n)
{
  out_append(s, n);
  fd_write(s, n, true);
  log_plain(s, n);
}

void console_write_raw(const uint8_t *data, size_t n)
{
  out_append((const char *)data, n);    /* octets de protocole : pas dans le journal */
  fd_write((const char *)data, n, false);
}

void console_puts(const char *s)
{
  console_write(s, strlen(s));
}

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

void console_flush(void) {}

void console_pad(const char *s, size_t width)
{
  size_t n = util_utf8_len(s);
  console_puts(s);
  while (n++ < width)
  {
    console_write(" ", 1U);
  }
}

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
    console_puts(s_color ? "\r\033[K" : "\n");   /* comme Core/Src/app/console.c */
  }
  console_write(buf, (size_t)n);
  if ((n == 0) || (buf[n - 1] != '\n'))
  {
    console_puts("\n");
  }
  if (s_prompt_visible)
  {
    console_puts("obcp> ");
    console_write(s_line, s_len);
  }
}

/* Comme Core/Src/app/console.c : messages de plusieurs lignes en tâche de fond */
static bool s_async_prompt;

void console_async_begin(void)
{
  s_async_prompt = s_prompt_visible;
  if (s_prompt_visible)
  {
    console_puts(s_color ? "\r\033[K" : "\n");
    s_prompt_visible = false;
  }
}

void console_async_end(void)
{
  if (s_async_prompt)
  {
    console_puts("obcp> ");
    console_write(s_line, s_len);
    s_prompt_visible = true;
    s_async_prompt = false;
  }
}

static bool input_ready(void)
{
  /* lire un octet déjà arrivé ne coûte pas de temps simulé */
  return (s_in_t != s_in_h) && ((int32_t)(sim_now() - s_in[s_in_t].at) >= 0);
}

int console_getc(void)
{
  int c;
  if (s_fd >= 0)
  {
    uint8_t b;
    if (read(s_fd, &b, 1) == 1)
    {
      host_feed(&b, 1, 0);
    }
  }
  if (!input_ready() && (s_rx_hook != NULL))
  {
    s_rx_hook();                          /* l'émetteur simulé peut répondre */
  }
  if (!input_ready())
  {
    return -1;
  }
  c = s_in[s_in_t].c;
  s_in_t = (s_in_t + 1U) % IN_MAX;
  return c;
}

void console_rx_flush(void)
{
  while (input_ready())
  {
    s_in_t = (s_in_t + 1U) % IN_MAX;
  }
}

bool console_abort(void)
{
  bool hit = false;
  int  c;
  while ((c = console_getc()) >= 0)
  {
    if (c != '\n')
    {
      hit = true;
    }
  }
  return hit;
}

bool console_readline(char *out, size_t len)
{
  int c;
  while ((c = console_getc()) >= 0)
  {
    if ((c == '\r') || (c == '\n'))
    {
      s_line[s_len] = '\0';
      console_write(s_line, s_len);       /* écho de la commande dans le journal */
      console_puts("\n");
      (void)snprintf(out, len, "%s", s_line);
      s_len = 0U;
      s_prompt_visible = false;
      return true;
    }
    if ((c >= 0x20) && (s_len < (CONSOLE_LINE_MAX - 1U)))
    {
      s_line[s_len++] = (char)c;
    }
  }
  return false;
}

void console_prompt(void)
{
  s_len = 0U;
  console_puts("obcp> ");
  s_prompt_visible = true;
}

void console_set_prompt_visible(bool on)
{
  s_prompt_visible = on;
}

/* Connexions simulées du terminal au port COM virtuel */
static uint32_t s_open_count = 1U;      /* terminal ouvert dès le démarrage */
static uint32_t s_open_seen;

bool console_new_connection(bool *first)
{
  if (s_open_count == s_open_seen)
  {
    return false;
  }
  if (first != NULL)
  {
    *first = (s_open_seen == 0U);
  }
  s_open_seen = s_open_count;
  return true;
}

uint32_t console_take_dropped(void)
{
  return 0U;
}

void host_reconnect(void)
{
  s_open_count++;
}

/* ---- pilotage par le test -------------------------------------------------------- */
void host_type(const char *text, uint32_t at_tick)
{
  host_feed((const uint8_t *)text, strlen(text), at_tick);
}

void host_feed(const uint8_t *data, size_t n, uint32_t at_tick)
{
  size_t i;
  for (i = 0; i < n; i++)
  {
    s_in[s_in_h].c = data[i];
    s_in[s_in_h].at = at_tick;
    s_in_h = (s_in_h + 1U) % IN_MAX;
    assert(s_in_h != s_in_t);           /* file d'entrée pleine */
  }
}

void host_set_rx_hook(host_rx_hook_t hook)
{
  s_rx_hook = hook;
}

size_t host_output_len(void)
{
  return s_out_n;
}

void host_attach_fd(int fd)
{
  s_fd = fd;
}

bool host_input_pending(void)
{
  return s_in_t != s_in_h;
}

const char *host_output(void)
{
  return s_out;
}

void host_clear(void)
{
  s_out_n = 0U;
  s_out[0] = '\0';
}

void host_transcript(const char *path)
{
  s_log = fopen(path, "w");
}

bool host_prompt_visible(void)
{
  return s_prompt_visible;
}
