/**
 ******************************************************************************
 * @file    line_builder.c
 * @brief   Assemblage des lignes de commande (voir line_builder.h).
 ******************************************************************************
 */
#include "line_builder.h"

#define CHAR_BACKSPACE  '\b'      /* code 8 : touche d'effacement (Ctrl+H) */
#define CHAR_DELETE     '\x7F'    /* code 127 : touche d'effacement de certains terminaux */

/* Voir line_builder.h */
void line_builder_reset(line_builder_t *lb)
{
  lb->len      = 0U;
  lb->text[0]  = '\0';
  lb->too_long = false;
  lb->done     = false;
}

/* Termine la ligne : rend LINE_READY, LINE_TOO_LONG ou LINE_NONE (vide). */
static line_status_t finish_line(line_builder_t *lb)
{
  line_status_t status;

  if (lb->too_long)
  {
    status = LINE_TOO_LONG;
  }
  else if (lb->len == 0U)
  {
    return LINE_NONE;                  /* ligne vide : on l'ignore */
  }
  else
  {
    status = LINE_READY;
  }
  lb->text[lb->len] = '\0';            /* fin de chaîne pour les fonctions str...() */
  lb->done = true;                     /* text[] reste lisible jusqu'au prochain appel */
  return status;
}

/* Voir line_builder.h */
line_status_t line_builder_put(line_builder_t *lb, char c)
{
  if (lb->done)
  {
    line_builder_reset(lb);            /* la ligne précédente a été rendue */
  }

  switch (c)
  {
    case '\r':
    case '\n':
      return finish_line(lb);

    case CHAR_BACKSPACE:
    case CHAR_DELETE:
      if ((lb->len > 0U) && !lb->too_long)
      {
        lb->len--;
      }
      return LINE_NONE;

    case '\0':
      return LINE_NONE;                /* un caractère nul couperait la chaîne */

    default:
      if (lb->len < CMD_LINE_MAX)
      {
        lb->text[lb->len] = c;
        lb->len++;
      }
      else
      {
        lb->too_long = true;           /* on continue jusqu'à la fin de ligne */
      }
      return LINE_NONE;
  }
}

/* Voir line_builder.h */
line_status_t line_builder_end(line_builder_t *lb)
{
  if (lb->done)
  {
    line_builder_reset(lb);
    return LINE_NONE;
  }
  return finish_line(lb);
}

/* Voir line_builder.h */
bool line_builder_is_empty(const line_builder_t *lb)
{
  return lb->done || ((lb->len == 0U) && !lb->too_long);
}
