/**
 ******************************************************************************
 * @file    color.h
 * @brief   Couleurs de la LED RVB : noms, notation #RRGGBB, dégradé de
 *          qualité du signal, correction de luminosité (gamma).
 *          Indépendant du matériel (testé sur PC).
 ******************************************************************************
 */
#ifndef COLOR_H
#define COLOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Lit une couleur : un nom (rouge, vert, bleu, jaune, cyan, magenta,
 *         blanc, orange, violet, rose ; aussi en anglais), « #RRGGBB »,
 *         « RRGGBB » ou « R,G,B » (0..255).
 * @param  s    texte à lire.
 * @param  rgb  reçoit les composantes rouge, vert, bleu (0 à 255).
 * @retval false si le texte n'est pas reconnu.
 */
bool        color_parse(const char *s, uint8_t rgb[3]);

/** @brief  Nom de la couleur si elle correspond exactement à un nom connu, sinon NULL. */
const char *color_name(const uint8_t rgb[3]);

/** @brief  Écrit la couleur en notation « #RRGGBB » (8 octets avec le zéro final). */
void        color_hex(const uint8_t rgb[3], char out[8]);

/** @brief  Liste des noms de couleurs connus (texte d'aide). */
const char *color_names(void);

/**
 * @brief  Couleur d'une qualité de signal : rouge (0) -> jaune (50) -> vert (100).
 * @param  q    qualité de 0 à 100.
 * @param  rgb  reçoit la couleur.
 */
void        color_quality(uint8_t q, uint8_t rgb[3]);

/**
 * @brief  Rapport cyclique PWM d'une composante de couleur : la composante est
 *         corrigée (gamma 2 : (c / 255)², pour des nuances qui paraissent
 *         régulières à l'œil) puis multipliée par la luminosité pct.
 * @param  c       composante 0 à 255.
 * @param  pct     luminosité 0 à 100 %.
 * @param  period  période du timer (valeur maximale du rapport cyclique).
 * @retval valeur à écrire dans le registre de comparaison (0 à period).
 */
uint16_t    color_duty(uint8_t c, uint8_t pct, uint16_t period);

#ifdef __cplusplus
}
#endif

#endif /* COLOR_H */
