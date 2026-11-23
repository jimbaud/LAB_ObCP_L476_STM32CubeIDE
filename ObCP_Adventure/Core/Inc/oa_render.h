/** @file oa_render.h @brief Rendu par bandes sans framebuffer embarqué.
 * SPDX-License-Identifier: MIT */
#ifndef OA_RENDER_H
#define OA_RENDER_H
#include "oa_engine.h"
#define OA_BAND_H 8
/** @brief Informations matérielles, copiées au début d'une image. */
typedef struct {
    char lines[8][48];
    uint16_t fps10;
    int16_t cross_x, cross_y;
    uint8_t calibration_step;
    uint8_t long_hold_progress; /**< Retour visuel de l'appui long, de 0 à 90 pas. */
    bool save_error;
    bool save_pending;
    bool touch_needs_calibration; /**< Le menu indique comment ouvrir l'étalonnage sans coordonnées
                                     fiables. */
} OaOverlay;
/** @brief Compose huit lignes RGB565 ; y doit être multiple de huit entre 0 et 232. */
void oa_render_band(const OaGame *game, const OaOverlay *overlay, int y, uint16_t pixels[320 * 8]);
/** @brief Empreinte d'un bloc de 32x8 pixels ; utilisée pour éviter les transferts inchangés. */
uint32_t oa_render_hash(const uint16_t *band, unsigned block);
#endif
