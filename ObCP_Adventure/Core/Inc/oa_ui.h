/** @file oa_ui.h @brief Géométrie et transitions de l'interface tactile portable.
 * @details Les mêmes rectangles servent au dessin et aux tests de contact.
 * Les actions de menu se valident au relâchement et n'utilisent qu'un doigt.
 * SPDX-License-Identifier: MIT */
#ifndef OA_UI_H
#define OA_UI_H
#include "oa_engine.h"
/** @brief Zone écran en pixels ; les bords droit et bas sont exclus du contact. */
typedef struct {
    int16_t x, y; /**< Coin supérieur gauche. */
    int16_t w, h; /**< Largeur et hauteur, toujours positives pour un bouton visible. */
} OaRect;
/** @brief Identifiants d'actions, partagés entre dessin, navigation et tests. */
typedef enum {
    OA_UI_NONE,
    OA_UI_NEW,
    OA_UI_CONTINUE,
    OA_UI_SETTINGS,
    OA_UI_DIAGNOSTIC,
    OA_UI_BACK,
    OA_UI_APPLY,
    OA_UI_TAB_MODE,
    OA_UI_TAB_TILT,
    OA_UI_TAB_TOUCH,
    OA_UI_MODE_TOUCH,
    OA_UI_MODE_TILT,
    OA_UI_CAL_TOUCH,
    OA_UI_CAL_TILT,
    OA_UI_DEC0,
    OA_UI_INC0,
    OA_UI_DEC1,
    OA_UI_INC1,
    OA_UI_DEC2,
    OA_UI_INC2,
    OA_UI_RESUME,
    OA_UI_MAIN,
    OA_UI_CAL_CANCEL
} OaUiId;
/** @brief Message transitoire ; le rendu lui associe un libellé lisible. */
typedef enum {
    OA_UI_NOTICE_NONE,
    OA_UI_NOTICE_SAVED,
    OA_UI_NOTICE_CANCELLED,
    OA_UI_NOTICE_CAL_READY,
    OA_UI_NOTICE_CAL_SAVED,
    OA_UI_NOTICE_CAL_CANCELLED,
    OA_UI_NOTICE_NATIVE,
    OA_UI_NOTICE_CAL_ERROR,
    OA_UI_NOTICE_CAL_TIMEOUT,
    OA_UI_NOTICE_CAL_NO_SENSOR
} OaUiNotice;
/** @brief Rectangle semi-ouvert, identique aux conventions des collisions. */
bool oa_ui_contains(OaRect rect, int x, int y);
/** @brief Calcule un bouton visible dans l'état/page courants ; false si absent. */
bool oa_ui_rect(const OaGame *game, OaUiId id, OaRect *rect);
/** @brief Rectangle d'une commande de jeu : 1 gauche, 2 droite, 3 saut. */
bool oa_ui_control_rect(const OaSettings *settings, unsigned control, OaRect *rect);
/** @brief Commande réellement touchée ; zéro dans les espaces entre boutons. */
unsigned oa_ui_control_at(const OaSettings *settings, int x, int y);
/** @brief Ouvre un brouillon sans modifier les réglages actifs. */
void oa_ui_open_settings(OaGame *game);
/** @brief Retour au menu ; la partie peut être reprise en RAM sans rechargement. */
void oa_ui_open_menu(OaGame *game);
/** @brief Lance l'étalonnage demandé, indépendant du mode de commande actif. */
void oa_ui_calibrate(OaGame *game, bool tilt, OaState return_state);
/** @brief Termine une session : NULL annule, sinon copie vers brouillon ou réglages actifs. */
void oa_ui_end_calibration(OaGame *game, const OaSettings *result, OaUiNotice notice);
/** @brief Consomme un pas d'interface ; true si la physique doit rester figée. */
bool oa_ui_step(OaGame *game, const OaInput *input);
/** @brief Libellés compréhensibles, sans codes numériques de mode ou de capteur. */
const char *oa_ui_main_label(const OaGame *game, unsigned item);
/** @brief Écrit la valeur expliquée d'une ligne de réglage, avec une taille bornée. */
void oa_ui_setting_value(const OaGame *game, unsigned row, char *text, size_t capacity);
#endif
