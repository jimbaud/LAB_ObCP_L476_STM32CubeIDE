/**
 ******************************************************************************
 * @file    console.h
 * @brief   Console sur le port COM virtuel USB de l'ObCP (classe CDC, voir
 *          cdc_port.h) : émission tamponnée (les messages produits avant
 *          l'ouverture du terminal y sont conservés), éditeur de ligne
 *          (effacement, rappel de la dernière commande avec la flèche haut,
 *          Ctrl-C) et couleurs ANSI désactivables.
 ******************************************************************************
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONSOLE_LINE_MAX   192U      /* longueur maximale d'une ligne tapée */

/* Séquences ANSI de couleur (vides si les couleurs sont désactivées).
 * Exemple : console_printf("%sOK%s\n", C_OK(), C_RST()) affiche OK en vert. */
const char *C_OK(void);      /* vert   */
const char *C_ERR(void);     /* rouge  */
const char *C_WARN(void);    /* jaune  */
const char *C_INFO(void);    /* cyan   */
const char *C_BOLD(void);    /* gras   */
const char *C_DIM(void);     /* atténué */
const char *C_RST(void);     /* normal */

/** @brief  Initialise la console (saisie vide, printf() redirigé vers l'USB). */
void console_init(void);
/** @brief  Active ou désactive les couleurs ANSI (paramètre term.color). */
void console_set_color(bool on);
/** @brief  Couleurs actives ? */
bool console_color(void);

/** @brief  Écrit n octets de texte ; chaque '\n' devient "\r\n" (retour à la ligne du terminal). */
void console_write(const char *s, size_t n);
/** @brief  Octets bruts, sans conversion (protocole de transfert de fichier YMODEM). */
void console_write_raw(const uint8_t *data, size_t n);
/** @brief  Écrit une chaîne. */
void console_puts(const char *s);
/** @brief  Écrit un texte mis en forme comme printf() (384 caractères au plus). */
int  console_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/** @brief  Attend que tout soit parti vers le terminal ouvert (2 s au plus). */
void console_flush(void);
/** @brief  Écrit s puis des espaces jusqu'à width colonnes (alignement de tableaux, texte UTF-8). */
void console_pad(const char *s, size_t width);

/**
 * @brief  Message asynchrone (événement de fond : connexion perdue, message
 *         MQTT...) : efface la ligne en cours de saisie, affiche le message
 *         puis réaffiche l'invite et la saisie, qui n'est pas perdue.
 */
void console_event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/**
 * @brief  Début d'un affichage de plusieurs lignes en tâche de fond : efface
 *         la ligne en cours de saisie (les messages s'écrivent ensuite
 *         normalement, puis console_async_end()).
 */
void console_async_begin(void);
/** @brief  Fin d'un affichage en tâche de fond : réaffiche l'invite et la saisie. */
void console_async_end(void);

/** @brief  Lit un caractère reçu. @retval le caractère, ou -1 si aucun. */
int  console_getc(void);
/** @brief  Oublie les octets reçus non lus. */
void console_rx_flush(void);
/** @brief  Vrai si une touche a été frappée (vide le tampon) : arrêt d'une commande longue. */
bool console_abort(void);

/**
 * @brief  Éditeur de ligne : traite les caractères reçus (effacement, flèche
 *         haut, Ctrl-C...). Non bloquant.
 * @param  out  reçoit la ligne tapée quand l'utilisateur appuie sur Entrée.
 * @param  len  taille de out.
 * @retval true quand une ligne complète est disponible.
 */
bool console_readline(char *out, size_t len);
/** @brief  Affiche l'invite « obcp> » et vide la saisie. */
void console_prompt(void);
/** @brief  Indique si l'invite est affichée (false pendant l'exécution d'une commande). */
void console_set_prompt_visible(bool on);

/**
 * @brief  Détecte une nouvelle ouverture du port par un terminal.
 * @param  first  reçoit true pour la première ouverture depuis le démarrage.
 * @retval true une fois à chaque nouvelle ouverture.
 */
bool console_new_connection(bool *first);
/** @brief  Octets perdus (tampon plein alors qu'aucun terminal n'était ouvert), remis à zéro. */
uint32_t console_take_dropped(void);

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_H */
