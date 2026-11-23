/** @file oa_assets.h @brief Ressources indexées 4 bpp originales. */
#ifndef OA_ASSETS_H
#define OA_ASSETS_H
#include <stdint.h>
typedef struct {uint8_t w,h;const uint8_t *data;} OaSprite;
#define OA_ASSET_ROBOT_0 0
#define OA_ASSET_ROBOT_1 1
#define OA_ASSET_ROBOT_2 2
#define OA_ASSET_ROBOT_3 3
#define OA_ASSET_ROBOT_4 4
#define OA_ASSET_ROBOT_5 5
#define OA_ASSET_WALKER_0 6
#define OA_ASSET_WALKER_1 7
#define OA_ASSET_HOPPER_0 8
#define OA_ASSET_HOPPER_1 9
#define OA_ASSET_FLYER_0 10
#define OA_ASSET_FLYER_1 11
#define OA_ASSET_TURRET_0 12
#define OA_ASSET_TURRET_1 13
#define OA_ASSET_BOSS_0 14
#define OA_ASSET_BOSS_1 15
#define OA_ASSET_TILE_0 16
#define OA_ASSET_TILE_1 17
#define OA_ASSET_TILE_2 18
#define OA_ASSET_TILE_3 19
#define OA_ASSET_TILE_4 20
#define OA_ASSET_TILE_5 21
#define OA_ASSET_TILE_6 22
#define OA_ASSET_TILE_7 23
extern const OaSprite oa_sprites[];
extern const uint8_t oa_font[64][7];
#endif
