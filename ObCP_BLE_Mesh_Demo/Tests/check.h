/**
 ******************************************************************************
 * @file    Tests/check.h
 * @brief   Mini-outil de test : CHECK(condition) compte les vérifications et
 *          affiche celles qui échouent (fichier, ligne, condition).
 ******************************************************************************
 */
#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>
#include <string.h>

extern int g_checks;      /* nombre de vérifications faites */
extern int g_failures;    /* nombre de vérifications en échec */

#define CHECK(cond)                                                            \
  do {                                                                         \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
      g_failures++;                                                            \
      printf("  ECHEC %s:%d : %s\n", __FILE__, __LINE__, #cond);               \
    }                                                                          \
  } while (0)

/* Compare deux chaînes ; en cas d'échec, les affiche (\n et \r visibles). */
void check_str_impl(const char *file, int line, const char *got, const char *expected);
#define CHECK_STR(got, expected) check_str_impl(__FILE__, __LINE__, (got), (expected))

/* Bilan : affiche le nombre de vérifications et rend le code de sortie. */
int check_summary(const char *name);

#endif /* CHECK_H */
