/**
 * @file obcp_config.h
 * @brief Valeurs initiales de repère pour le montage ObCP.
 * @ingroup obcp_input
 *
 * Ces constantes définissent le paysage de départ. Les paramètres sauvegardés et
 * l’assistant peuvent ensuite remplacer le repère des jeux en RAM.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
/** @def OBCP_TOUCH_FLIPPED
 * @brief Demi-tour facultatif du tactile capacitif après la rotation paysage.
 */
/** @def OBCP_TILT_SWAP_XY
 * @brief Permutation initiale des deux axes bruts pour l'écran paysage.
 */
/** @def OBCP_TILT_SIGN_X
 * @brief Signe initial appliqué à la commande horizontale des jeux.
 */
/** @def OBCP_TILT_SIGN_Y
 * @brief Signe initial appliqué à la commande verticale des jeux.
 */
/** @def OBCP_LCD_MADCTL
 * @brief Orientation ILI9341 MV et ordre BGR, valeur 0x28 pour le paysage retenu.
 */

#ifndef OBCP_CONFIG_H
#define OBCP_CONFIG_H

/* Valeurs de départ pour le paysage de l'ObCP. L'assistant tactile AXES JEUX
 * peut remplacer ces signes et la permutation pour le montage utilisé. */
#define OBCP_TOUCH_FLIPPED 0
#define OBCP_TILT_SWAP_XY 1
#define OBCP_TILT_SIGN_X (1.0f)
#define OBCP_TILT_SIGN_Y (1.0f)
#define OBCP_LCD_MADCTL 0x28U /* MV + BGR, paysage type rotation 1. */

#endif
