/** @file oa_input.h @brief Transformation d'un contact unique et de l'inclinaison.
 * SPDX-License-Identifier: MIT */
#ifndef OA_INPUT_H
#define OA_INPUT_H
#include "oa_engine.h"
/** @brief Échantillon tactile : valid=false produit CANCEL et libère les appuis. */
typedef struct {
    uint16_t raw_x, raw_y, z;
    int16_t x, y;
    uint32_t sequence;
    bool valid, down;
    bool raw_valid, raw_down; /**< Contact physique indépendant de la transformation écran. */
} OaTouchSample;
typedef struct {
    bool previous, gesture, armed;
    int16_t origin_y, last_x, last_y;
    uint8_t lane;
    uint16_t hold;
} OaTouchInput;
typedef struct {
    uint16_t ticks;
} OaHoldInput;
/** @brief Front unique après 90 pas (1,5 s), sans dépendre des coordonnées. */
bool oa_input_long_press(OaHoldInput *state, bool valid, bool down);
typedef struct {
    int32_t filtered[3];
    bool initialized;
    uint32_t last_ms;
} OaTiltInput;
/** @brief Produit direction, saut et capture du pointeur à partir d'un seul doigt. */
OaInput oa_input_touch(OaTouchInput *state, const OaTouchSample *sample,
                       const OaSettings *settings);
/** @brief Met à jour le filtre à 100 Hz ; une mesure n'est consommée qu'une fois. */
void oa_tilt_sample(OaTiltInput *state, const int16_t mg[3], uint32_t now_ms);
/** @brief Renvoie un axe neutre si le dernier échantillon a plus de 50 ms. */
int16_t oa_tilt_axis(const OaTiltInput *state, const OaSettings *settings, uint32_t now_ms);
typedef struct {
    int16_t x, y;
} OaCalPoint;
/** @brief Calcule une transformation affine sur trois points non alignés. */
bool oa_calibration_fit(OaSettings *settings, const OaCalPoint raw[3], const OaCalPoint screen[3]);
/** @brief Transforme un point avec intermédiaires 64 bits, puis vérifie les limites. */
bool oa_calibration_map(const OaSettings *settings, int raw_x, int raw_y, int16_t *x, int16_t *y);
#endif
