/**
 ******************************************************************************
 * @file    line_builder.h
 * @brief   Assemblage des lignes de commande à partir des caractères reçus
 *          (USB ou Bluetooth).
 *
 *  Les caractères arrivent un par un (frappe dans un terminal) ou par
 *  paquets (programme Python, application Android). Ce module les range dans
 *  une ligne jusqu'à une fin de ligne :
 *    - '\r' (retour chariot, touche Entrée de la plupart des terminaux) ou
 *      '\n' (saut de ligne) terminent la ligne ; une ligne vide est ignorée,
 *      ce qui permet aussi la fin de ligne « \r\n » de Windows ;
 *    - la touche d'effacement (code 8 « Backspace » ou 127 « Delete »)
 *      retire le dernier caractère de la ligne ;
 *    - un caractère nul (code 0) est ignoré ;
 *    - au-delà de CMD_LINE_MAX caractères, la ligne est marquée trop longue
 *      et sera rejetée entière à la fin de ligne.
 *
 *  line_builder_end() termine la ligne en cours sans caractère de fin :
 *  c'est ainsi qu'un message reçu en un seul bloc (paquet USB de plusieurs
 *  octets, écriture Bluetooth) compte comme une ligne complète, comme dans
 *  la version 2024 (envoi « r500 » sans fin de ligne).
 *
 *  Exemple :
 *  @code
 *    line_builder_t lb;
 *    line_builder_reset(&lb);
 *    for (chaque caractère c reçu)
 *    {
 *      if (line_builder_put(&lb, c) == LINE_READY)
 *      {
 *        traiter(lb.text);            // ligne complète, terminée par '\0'
 *      }
 *    }
 *  @endcode
 ******************************************************************************
 */
#ifndef LINE_BUILDER_H
#define LINE_BUILDER_H

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"

/** Résultat de line_builder_put() et line_builder_end(). */
typedef enum
{
  LINE_NONE = 0,     /**< pas (encore) de ligne complète */
  LINE_READY,        /**< une ligne complète est dans text[] */
  LINE_TOO_LONG      /**< la ligne dépassait CMD_LINE_MAX caractères : rejetée */
} line_status_t;

/** Ligne en cours d'assemblage. */
typedef struct
{
  char     text[CMD_LINE_MAX + 1U]; /**< caractères reçus, '\0' final quand prête */
  uint16_t len;                     /**< nombre de caractères dans text[] */
  bool     too_long;                /**< trop de caractères reçus pour cette ligne */
  bool     done;                    /**< ligne rendue : la suivante repart de zéro */
} line_builder_t;

/**
 * @brief  Vide la ligne en cours.
 */
void line_builder_reset(line_builder_t *lb);

/**
 * @brief  Ajoute un caractère reçu à la ligne en cours.
 * @param  lb  ligne en cours
 * @param  c   caractère reçu
 * @retval LINE_READY quand c termine une ligne non vide (lire lb->text),
 *         LINE_TOO_LONG quand c termine une ligne trop longue,
 *         LINE_NONE sinon
 */
line_status_t line_builder_put(line_builder_t *lb, char c);

/**
 * @brief  Termine la ligne en cours comme si une fin de ligne était reçue
 *         (fin d'un message reçu en un seul bloc).
 * @retval LINE_READY, LINE_TOO_LONG ou LINE_NONE (rien en cours)
 */
line_status_t line_builder_end(line_builder_t *lb);

/**
 * @brief  Indique si aucun caractère n'attend dans la ligne en cours.
 */
bool line_builder_is_empty(const line_builder_t *lb);

#endif /* LINE_BUILDER_H */
