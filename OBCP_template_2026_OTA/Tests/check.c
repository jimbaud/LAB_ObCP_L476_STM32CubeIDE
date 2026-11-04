/**
 ******************************************************************************
 * @file    Tests/check.c
 * @brief   Mini-outil de test (voir check.h).
 ******************************************************************************
 */
#include "check.h"

int g_checks;
int g_failures;

/* Écrit s en rendant visibles les caractères de contrôle. */
static void print_escaped(const char *s)
{
  for (; *s != '\0'; s++)
  {
    switch (*s)
    {
      case '\n': fputs("\\n", stdout); break;
      case '\r': fputs("\\r", stdout); break;
      case 27:   fputs("\\e", stdout); break;
      default:   putchar(*s);          break;
    }
  }
}

void check_str_impl(const char *file, int line, const char *got, const char *expected)
{
  g_checks++;
  if (strcmp(got, expected) != 0)
  {
    g_failures++;
    printf("  ECHEC %s:%d\n    obtenu  : \"", file, line);
    print_escaped(got);
    printf("\"\n    attendu : \"");
    print_escaped(expected);
    printf("\"\n");
  }
}

int check_summary(const char *name)
{
  printf("%s : %d vérifications, %d échec(s)\n", name, g_checks, g_failures);
  return (g_failures == 0) ? 0 : 1;
}
