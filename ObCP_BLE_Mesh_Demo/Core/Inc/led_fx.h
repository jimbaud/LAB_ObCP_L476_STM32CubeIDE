/**
 ******************************************************************************
 * @file    led_fx.h
 * @brief   Effets de la LED tricolore sans jamais attendre : couleur de fond,
 *          éclairs d'activité, affichage temporaire (vague), clignotement
 *          (identification), battement de cœur.
 *
 *  Plusieurs effets peuvent être demandés en même temps : le plus
 *  prioritaire s'affiche, les autres reprennent quand il se termine.
 *
 *    priorité  effet          déclenché par                       durée
 *    1 (haute) clignotement   ident, conflit de numéro            quelques secondes
 *    2         affichage      vague (couleur selon les relais)    1,5 s
 *    3         éclair         trame relayée, reçue, émise         60 ms
 *    4         fond           commande led                        réglable ou sans fin
 *    5 (basse) battement      rien d'autre allumé                 40 ms toutes les 3 s
 *
 *  Les couleurs se donnent sur 8 bits (0 à 255 par composante) ; la valeur
 *  envoyée à la PWM est v² (0 à 65025) : l'œil perçoit mieux les faibles
 *  niveaux, et une petite valeur donne vraiment une lumière faible.
 ******************************************************************************
 */
#ifndef LED_FX_H
#define LED_FX_H

#include <stdbool.h>
#include <stdint.h>

/** Une couleur sur 8 bits par composante. */
typedef struct
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
} led_color_t;

/** Quelques couleurs. */
#define LED_BLACK    ((led_color_t){ 0U, 0U, 0U })
#define LED_WHITE    ((led_color_t){ 255U, 255U, 255U })

/**
 * @brief  Prépare le module (LED éteinte). outputs_init() doit avoir été
 *         appelée avant.
 */
void led_fx_init(void);

/**
 * @brief  Couleur de fond (commande « led »).
 * @param  c            couleur ; noire : fond éteint
 * @param  duration_ms  durée, 0 : jusqu'au prochain changement
 */
void led_fx_set_color(led_color_t c, uint32_t duration_ms);

/** Couleur de fond actuelle. */
led_color_t led_fx_color(void);

/** Éclair bref d'activité (ignoré si les éclairs sont désactivés). */
void led_fx_flash(led_color_t c, uint16_t ms);

/** Affichage temporaire prioritaire (vague). */
void led_fx_show(led_color_t c, uint32_t ms);

/**
 * @brief  Clignotement prioritaire (identification).
 * @param  c            couleur
 * @param  duration_ms  durée totale
 * @param  period_ms    période (moitié allumée, moitié éteinte)
 */
void led_fx_blink(led_color_t c, uint32_t duration_ms, uint16_t period_ms);

/** Éteint tout (fond et effets en cours). */
void led_fx_off(void);

/** Autorise ou non les éclairs d'activité (réglage « flash »). */
void led_fx_set_activity(bool on);

/** Vrai si les éclairs d'activité sont autorisés. */
bool led_fx_activity(void);

/** Couleur de la vague pour un nombre de relais traversés : rouge (0),
 *  orange, jaune, vert, cyan, bleu, violet, puis rose (7 et plus). La
 *  carte qui lance la vague s'allume en blanc. */
led_color_t led_fx_hops_color(uint8_t relays);

/** Nom français de la couleur de led_fx_hops_color. */
const char *led_fx_hops_color_name(uint8_t relays);

/**
 * @brief  À appeler à chaque tour de boucle : calcule la couleur à afficher
 *         et met à jour les PWM si elle a changé.
 * @param  now  temps actuel en ms (HAL_GetTick)
 */
void led_fx_process(uint32_t now);

#endif /* LED_FX_H */
