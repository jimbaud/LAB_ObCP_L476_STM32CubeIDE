/** @file oa_engine.h
 * @brief Contrats du moteur portable d'ObCP Adventure (ISO C11).
 * @details Les positions et vitesses sont en Q16.16, en pixels et pixels/s.
 * Aucun fichier HAL n'est inclus : ce moteur est compilé sans modification
 * sur le PC et sur le Cortex-M4. Les tableaux ont tous une capacité fixe.
 * SPDX-License-Identifier: MIT
 */
#ifndef OA_ENGINE_H
#define OA_ENGINE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "oa_config.h"
#define OA_SCREEN_W 320
#define OA_SCREEN_H 240
#define OA_WORLD_H 176
#define OA_TILE 16
#define OA_ROWS 11
#define OA_MAX_COLS 256
#define OA_MAX_OBJECTS 128
#define OA_MAX_ENEMIES 24
#define OA_MAX_PLATFORMS 8
#define OA_MAX_SHOTS 12
#define OA_LEVEL_COUNT 10
#define OA_HZ 60
#define OA_ONE INT32_C(65536)
#define OA_FX(n) ((int32_t)((n) * OA_ONE))
/** @brief Types de tuiles ; la géométrie est indépendante du graphisme. */
typedef enum {
    OA_AIR,
    OA_GROUND,
    OA_BRICK,
    OA_ONEWAY,
    OA_SPIKES,
    OA_BLOCK,
    OA_CRUMBLE,
    OA_WATER,
    OA_TILE_COUNT
} OaTile;
/** @brief Données éditables des objets d'un niveau. */
typedef enum {
    OA_COIN = 1,
    OA_SHIELD,
    OA_BOOTS,
    OA_CHECKPOINT,
    OA_PORTAL,
    OA_RETURN,
    OA_HEART,
    OA_PLATFORM,
    OA_WALKER = 16,
    OA_HOPPER,
    OA_FLYER,
    OA_TURRET,
    OA_BOSS
} OaKind;
/** @brief Les chemins de plateformes relient (x,y) à (a,b), en pixels. */
typedef struct {
    uint8_t kind, variant;
    int16_t x, y, a, b;
    uint16_t period;
} OaObject;
/** @brief Niveau constant en Flash ; tilemap rangée ligne par ligne. */
typedef struct {
    const char *name;
    uint16_t width, main_end, spawn_x, spawn_y, exit_x;
    uint8_t biome, count;
    const uint8_t *tiles;
    const OaObject *objects;
} OaLevel;
/** @brief Commandes communes, indépendantes du capteur ou du clavier. */
typedef struct {
    int16_t move;      /**< Axe horizontal saturé à [-1000,+1000]. */
    int16_t x, y;      /**< Coordonnées du contact, en pixels écran. */
    int8_t menu_delta; /**< Navigation ponctuelle, -1 ou +1. */
    int8_t ui_adjust;  /**< Réglage ponctuel au clavier : -1 ou +1. */
    bool jump_pressed, jump_held, run, pause, confirm, back;
    bool pointer_down;      /**< Contact présent, utilisé aussi pour bloquer un clic traversant. */
    bool pointer_pressed;   /**< Nouveau contact, une seule fois par appui. */
    bool pointer_released;  /**< Relâchement normal ; x/y gardent la dernière position valide. */
    bool pointer_cancelled; /**< Perte du capteur : ne doit jamais valider un bouton. */
    bool pointer_long;      /**< Seuil de 1,5 s franchi une seule fois, même sans coordonnées. */
    bool ui_next_page;      /**< Touche Tab du simulateur ; les onglets servent au doigt. */
} OaInput;
/** @brief Paramètres persistants. Les coefficients tactiles sont en Q16.16. */
typedef struct {
    uint16_t tilt_dead_mg, tilt_full_mg;
    int16_t neutral[3];
    int32_t affine[6];
    uint8_t control_mode, tilt_axis, invert, gesture_px, touch_split, touch_profile;
    uint8_t calibrated, lcd_fast;
} OaSettings;
/** @brief Rectangle physique : x/y en Q16.16 ; w/h en pixels entiers. */
typedef struct {
    int32_t x, y, vx, vy;
    int16_t w, h;
    int8_t support;
    bool ground;
} OaBody;
/** @brief Joueur : compteurs en pas de simulation de 1/60 s. */
typedef struct {
    OaBody body;
    uint16_t shield, boots, invulnerable;
    uint8_t coyote, buffer;
    int8_t facing;
    bool jump_previous;
} OaPlayer;
/** @brief Ennemi associé à un objet constant, sans allocation dynamique. */
typedef struct {
    OaBody body;
    int16_t origin_x, origin_y;
    uint16_t clock;
    uint8_t kind, object_id, hp, phase;
    int8_t direction;
    bool alive;
} OaEnemy;
/** @brief Plateforme : mouvement triangulaire déterministe et déplacement relatif. */
typedef struct {
    int32_t x, y, dx, dy;
    uint16_t clock;
    uint8_t object_id;
} OaPlatform;
/** @brief Projectile borné ; sa position est en Q16.16. */
typedef struct {
    int32_t x, y, vx, vy;
    uint16_t life;
} OaShot;
typedef enum {
    OA_MENU,
    OA_PLAY,
    OA_PAUSE,
    OA_DEAD,
    OA_CLEAR,
    OA_GAMEOVER,
    OA_VICTORY,
    OA_SETTINGS,
    OA_DIAGNOSTIC,
    OA_CALIBRATION
} OaState;
typedef enum {
    OA_SOUND_NONE,
    OA_SOUND_JUMP,
    OA_SOUND_COIN,
    OA_SOUND_HURT,
    OA_SOUND_BONUS,
    OA_SOUND_CLEAR
} OaSound;
/** @brief État complet, copiable pour figer une image pendant les transferts DMA. */
typedef struct {
    OaSettings settings, settings_draft; /**< Brouillon publié seulement par ENREGISTRER. */
    OaPlayer player;
    OaEnemy enemies[OA_MAX_ENEMIES];
    OaPlatform platforms[OA_MAX_PLATFORMS];
    OaShot shots[OA_MAX_SHOTS];
    uint8_t tiles[OA_MAX_COLS * OA_ROWS], collected[OA_MAX_OBJECTS / 8];
    uint32_t tick, score;
    int32_t camera;
    uint16_t state_ticks, coins, checkpoint_x, checkpoint_y;
    uint8_t level, unlocked, lives, enemy_count, platform_count, menu_item, setting_item;
    uint8_t settings_page;    /**< Onglet : commandes, inclinaison, tactile. */
    uint8_t ui_pressed;       /**< OaUiId capturé à l'appui, validé au relâchement. */
    uint8_t ui_notice;        /**< OaUiNotice, sans imposer une dépendance circulaire d'en-têtes. */
    uint16_t ui_notice_ticks; /**< Durée du message en pas d'interface de 1/60 s. */
    OaState state;
    OaState calibration_return; /**< Écran parent à retrouver après l'assistant. */
    OaSound sound;
    bool save_requested, secret_found;
    bool resume_available; /**< État de partie encore présent en RAM, indépendant du journal. */
    bool ui_blocked;       /**< Attend le relâchement du contact qui vient d'ouvrir la pause. */
    bool calibration_tilt; /**< Type d'étalonnage demandé, indépendant du mode de jeu actif. */
} OaGame;
extern const OaLevel oa_levels[OA_LEVEL_COUNT];
/** @brief Arrondit un Q16.16 vers moins l'infini, y compris les négatifs. */
int32_t oa_pixel(int32_t value);
/** @brief Lit une tuile avec limites : côtés solides, bas ouvert pour les chutes. */
uint8_t oa_tile_at(const OaGame *game, int col, int row);
/** @brief Vérifie dimensions, tuiles, objets et destinations d'un niveau. */
bool oa_level_valid(const OaLevel *level);
/** @brief Déplace un rectangle par parcours de pas d'au plus un pixel. */
void oa_body_move(OaGame *game, OaBody *body, int32_t dx, int32_t dy, bool platforms);
/** @brief Met à jour le joueur d'exactement 1/60 seconde. */
void oa_player_step(OaGame *game, const OaInput *input);
/** @brief Valeurs par défaut et validation stricte des paramètres. */
void oa_settings_default(OaSettings *settings);
bool oa_settings_valid(const OaSettings *settings);
/** @brief Initialise le menu et une première carte ; ne lit aucun périphérique. */
void oa_game_init(OaGame *game);
/** @brief Charge une carte constante et instancie ses entités dans les pools. */
bool oa_game_load(OaGame *game, unsigned level, bool checkpoint);
/** @brief Un pas de jeu, menus compris. Ne doit être appelé qu'à 60 Hz. */
void oa_game_step(OaGame *game, const OaInput *input);
/** @brief Inflige des dégâts avec protection, vies et transition de mort. */
void oa_game_hurt(OaGame *game);
/** @brief Test d'intersection de deux rectangles à intervalles semi-ouverts. */
bool oa_overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh);
#endif
