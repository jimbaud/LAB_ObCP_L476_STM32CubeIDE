/**
 ******************************************************************************
 * @file    Tests/test_unit.c
 * @brief   Tests unitaires sur PC des modules sans matériel : tampon
 *          circulaire (fifo.c), assemblage des lignes (line_builder.c) et
 *          analyse des commandes (commands_parse, commands.c).
 ******************************************************************************
 */
#include <stdint.h>
#include "check.h"
#include "fifo.h"
#include "line_builder.h"
#include "commands.h"
#include "accelerometer.h"
#include "outputs.h"

/* ---- Fonctions appelées par commands.c, inutiles ici (analyse seule) ------ */
void outputs_set(output_t out, uint16_t value) { (void)out; (void)value; }
void outputs_all_off(void) { }
bool accel_read(accel_g_t *g) { (void)g; return false; }
bool accel_read_temperature(int *c) { (void)c; return false; }
void timer_1s_start(void) { }
void timer_1s_stop(void) { }
bool usb_console_print(const char *t) { (void)t; return true; }
bool usb_console_is_ready(void) { return false; }
bool ble_uart_print(const char *t) { (void)t; return true; }
bool ble_uart_is_connected(void) { return false; }

/* -------------------------------------------------------------------------- */
static void test_fifo(void)
{
  static uint8_t storage[16];
  fifo_t f = FIFO_INIT(storage);
  uint8_t out[32];

  printf("- fifo : écriture, lecture, tour du tableau, tout ou rien\n");
  CHECK(f.size == 16U);
  CHECK(fifo_used(&f) == 0U && fifo_free(&f) == 16U);
  CHECK(fifo_write(&f, (const uint8_t *)"abcdef", 6U));
  CHECK(fifo_used(&f) == 6U);
  CHECK(fifo_peek(&f, 0U, out, 3U) == 3U && memcmp(out, "abc", 3) == 0);
  CHECK(fifo_peek(&f, 4U, out, 10U) == 2U && memcmp(out, "ef", 2) == 0);
  CHECK(fifo_peek(&f, 6U, out, 1U) == 0U);
  fifo_drop(&f, 4U);
  CHECK(fifo_used(&f) == 2U);
  /* 14 octets libres : 15 refusés, rien n'est écrit */
  CHECK(!fifo_write(&f, (const uint8_t *)"0123456789ABCDE", 15U));
  CHECK(fifo_used(&f) == 2U);
  /* 14 octets acceptés, en faisant le tour du tableau */
  CHECK(fifo_write(&f, (const uint8_t *)"0123456789ABCD", 14U));
  CHECK(fifo_free(&f) == 0U);
  CHECK(fifo_peek(&f, 0U, out, 16U) == 16U && memcmp(out, "ef0123456789ABCD", 16) == 0);
  fifo_drop(&f, 100U);                               /* plus que le contenu : vide */
  CHECK(fifo_used(&f) == 0U);
  /* débordement des compteurs 16 bits */
  f.head = 65530U;
  f.tail = 65530U;
  CHECK(fifo_write(&f, (const uint8_t *)"0123456789", 10U));
  CHECK(fifo_used(&f) == 10U && f.head == 4U);
  CHECK(fifo_peek(&f, 0U, out, 10U) == 10U && memcmp(out, "0123456789", 10) == 0);
  fifo_clear(&f);
  CHECK(fifo_used(&f) == 0U);
  fifo_init(&f, storage, 16U);
  CHECK(fifo_used(&f) == 0U && f.buf == storage);
}

/* -------------------------------------------------------------------------- */
/* Pousse une suite de caractères ; rend le dernier statut non nul (ou NONE). */
static line_status_t feed(line_builder_t *lb, const char *s, int *lines)
{
  line_status_t last = LINE_NONE;

  for (; *s != '\0'; s++)
  {
    line_status_t st = line_builder_put(lb, *s);
    if (st != LINE_NONE)
    {
      last = st;
      if (lines != NULL)
      {
        (*lines)++;
      }
    }
  }
  return last;
}

static void test_line_builder(void)
{
  line_builder_t lb;
  int n = 0;
  char longline[CMD_LINE_MAX + 10];

  printf("- line_builder : fins de ligne, effacement, lignes vides et trop longues\n");
  line_builder_reset(&lb);
  CHECK(line_builder_is_empty(&lb));
  CHECK(feed(&lb, "r500\r", NULL) == LINE_READY);
  CHECK_STR(lb.text, "r500");
  CHECK(line_builder_is_empty(&lb));
  /* « \r\n » : la seconde fin de ligne donne une ligne vide, ignorée */
  CHECK(feed(&lb, "\n", NULL) == LINE_NONE);
  CHECK(feed(&lb, "v12\n", NULL) == LINE_READY);
  CHECK_STR(lb.text, "v12");
  /* effacement (Backspace et Delete) */
  CHECK(feed(&lb, "r5x\b0\x7F" "00\r", NULL) == LINE_READY);
  CHECK_STR(lb.text, "r500");
  CHECK(feed(&lb, "\b\b\ra\r", NULL) == LINE_READY);  /* effacer dans le vide */
  CHECK_STR(lb.text, "a");
  /* caractère nul ignoré */
  CHECK(line_builder_put(&lb, 't') == LINE_NONE);
  CHECK(line_builder_put(&lb, '\0') == LINE_NONE);
  CHECK(line_builder_put(&lb, '\r') == LINE_READY);
  CHECK_STR(lb.text, "t");
  /* fin de message sans fin de ligne */
  CHECK(feed(&lb, "r123", NULL) == LINE_NONE);
  CHECK(!line_builder_is_empty(&lb));
  CHECK(line_builder_end(&lb) == LINE_READY);
  CHECK_STR(lb.text, "r123");
  CHECK(line_builder_end(&lb) == LINE_NONE);         /* rien de plus */
  CHECK(line_builder_end(&lb) == LINE_NONE);
  /* plusieurs lignes dans un même envoi */
  n = 0;
  (void)feed(&lb, "r1\nv2\r\nb3\r", &n);
  CHECK(n == 3);
  CHECK_STR(lb.text, "b3");
  /* ligne trop longue : rejetée entière, la suivante est normale */
  memset(longline, 'x', sizeof(longline));
  longline[sizeof(longline) - 1] = '\0';
  CHECK(feed(&lb, longline, NULL) == LINE_NONE);
  CHECK(line_builder_put(&lb, '\r') == LINE_TOO_LONG);
  CHECK(feed(&lb, "m7\r", NULL) == LINE_READY);
  CHECK_STR(lb.text, "m7");
  CHECK(feed(&lb, longline, NULL) == LINE_NONE);
  CHECK(line_builder_end(&lb) == LINE_TOO_LONG);
  /* exactement CMD_LINE_MAX caractères : accepté */
  memset(longline, 'y', CMD_LINE_MAX);
  longline[CMD_LINE_MAX] = '\0';
  CHECK(feed(&lb, longline, NULL) == LINE_NONE);
  CHECK(line_builder_put(&lb, '\n') == LINE_READY);
  CHECK(strlen(lb.text) == CMD_LINE_MAX);
}

/* -------------------------------------------------------------------------- */
static void expect(const char *line, cmd_id_t id, uint16_t value)
{
  cmd_t c = commands_parse(line);
  g_checks++;
  if ((c.id != id) || (c.value != value))
  {
    g_failures++;
    printf("  ECHEC analyse de \"%s\" : id %d valeur %u (attendu id %d valeur %u)\n",
           line, (int)c.id, (unsigned)c.value, (int)id, (unsigned)value);
  }
}

static void test_parse(void)
{
  printf("- commands_parse : commandes de la version 2024 et cas d'erreur\n");
  /* commandes à valeur, minuscules et majuscules */
  expect("r500", CMD_RED, 500);        expect("R500", CMD_RED, 500);
  expect("v0", CMD_GREEN, 0);          expect("V65535", CMD_GREEN, 65535);
  expect("b50", CMD_BLUE, 50);         expect("B1", CMD_BLUE, 1);
  expect("m32768", CMD_MOTOR, 32768);  expect("M12", CMD_MOTOR, 12);
  expect("r 500", CMD_RED, 500);       /* espace avant la valeur (sscanf l'acceptait) */
  expect("  r500  ", CMD_RED, 500);    /* espaces autour */
  expect("r\t7", CMD_RED, 7);
  expect("r010", CMD_RED, 10);         /* décimal (sscanf %i lisait 8) */
  expect("r0", CMD_RED, 0);
  expect("r00000000000065535", CMD_RED, 65535);
  /* commandes d'une lettre */
  expect("a", CMD_ACCEL, 0);  expect("A", CMD_ACCEL, 0);
  expect("t", CMD_TEMPERATURE, 0);  expect("T", CMD_TEMPERATURE, 0);
  expect(" a ", CMD_ACCEL, 0);
  expect("?", CMD_HELP, 0);
  /* commandes en toutes lettres */
  expect("start timer", CMD_TIMER_START, 0);
  expect("stop timer", CMD_TIMER_STOP, 0);
  expect("#rst", CMD_RESET, 0);
  expect("cls", CMD_CLEAR_SCREEN, 0);
  expect("start timer ", CMD_TIMER_START, 0);
  /* lignes vides */
  expect("", CMD_EMPTY, 0);  expect("   ", CMD_EMPTY, 0);  expect("\t", CMD_EMPTY, 0);
  /* valeurs invalides */
  expect("r", CMD_BAD_VALUE, 0);  expect("R  ", CMD_BAD_VALUE, 0);
  expect("r65536", CMD_BAD_VALUE, 0);  expect("r99999999999", CMD_BAD_VALUE, 0);
  expect("r-1", CMD_BAD_VALUE, 0);  expect("r+5", CMD_BAD_VALUE, 0);
  expect("r5.5", CMD_BAD_VALUE, 0);  expect("r50x", CMD_BAD_VALUE, 0);
  expect("r50 60", CMD_BAD_VALUE, 0);  expect("m0x10", CMD_BAD_VALUE, 0);
  /* commandes inconnues */
  expect("red", CMD_UNKNOWN, 0);  expect("blue", CMD_UNKNOWN, 0);
  expect("x", CMD_UNKNOWN, 0);  expect("aa", CMD_UNKNOWN, 0);  expect("a1", CMD_UNKNOWN, 0);
  expect("??", CMD_UNKNOWN, 0);  expect("CLS", CMD_UNKNOWN, 0);
  expect("Start timer", CMD_UNKNOWN, 0);  expect("start  timer", CMD_UNKNOWN, 0);
  expect("start timerXYZ", CMD_UNKNOWN, 0);  expect("#rst2", CMD_UNKNOWN, 0);
  expect("False command", CMD_UNKNOWN, 0);
  /* ligne plus longue que CMD_LINE_MAX : inconnue */
  {
    char big[CMD_LINE_MAX + 2];
    memset(big, '1', sizeof(big));
    big[0] = 'r';
    big[sizeof(big) - 1] = '\0';
    expect(big, CMD_UNKNOWN, 0);
  }
  CHECK(strstr(commands_help_text(), "b50 for blue") != NULL);
}

int main(void)
{
  printf("test_unit\n");
  test_fifo();
  test_line_builder();
  test_parse();
  return check_summary("test_unit");
}
