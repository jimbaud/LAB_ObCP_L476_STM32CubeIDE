/** @file oa_game.c
 * @brief Machines à états, objets, adversaires, checkpoints et boss.
 * SPDX-License-Identifier: MIT
 */
#include "oa_engine.h"
#include "oa_ui.h"
#include <string.h>
static bool taken(const OaGame *g, unsigned i) {
    return (g->collected[i / 8] & (1u << (i % 8))) != 0;
}
static void take(OaGame *g, unsigned i) {
    g->collected[i / 8] |= (uint8_t)(1u << (i % 8));
}
void oa_settings_default(OaSettings *s) {
    memset(s, 0, sizeof(*s));
    s->tilt_dead_mg = 50;
    s->tilt_full_mg = 350;
    s->gesture_px = 18;
    s->touch_split = 96;
    s->tilt_axis = 0;
    s->affine[0] = OA_ONE;
    s->affine[4] = OA_ONE;
}
bool oa_settings_valid(const OaSettings *s) {
    if (s->tilt_dead_mg > 250 || s->tilt_full_mg < 150 || s->tilt_full_mg > 1000 ||
        s->tilt_full_mg <= s->tilt_dead_mg + 50 || s->control_mode > 1 || s->tilt_axis > 2 ||
        s->invert > 1 || s->gesture_px < 8 || s->gesture_px > 48 || s->touch_split < 64 ||
        s->touch_split > 112 || s->touch_profile > 3 || s->calibrated > 1 || s->lcd_fast > 1)
        return false;
    for (unsigned i = 0; i < 3; i++)
        if (s->neutral[i] < -2000 || s->neutral[i] > 2000)
            return false;
    for (unsigned i = 0; i < 6; i++)
        if (s->affine[i] < -OA_FX(8192) || s->affine[i] > OA_FX(8192))
            return false;
    return true;
}
bool oa_level_valid(const OaLevel *l) {
    if (!l || !l->tiles || !l->objects || !l->name || l->width < 24 || l->width > OA_MAX_COLS ||
        l->main_end >= l->width || l->count > OA_MAX_OBJECTS || l->exit_x >= l->main_end * 16 ||
        l->spawn_x >= l->width * 16 || l->spawn_y >= OA_WORLD_H)
        return false;
    unsigned enemies = 0, platforms = 0, blocks = 0;
    for (unsigned i = 0; i < l->width * OA_ROWS; i++) {
        if (l->tiles[i] >= OA_TILE_COUNT)
            return false;
        if (l->tiles[i] == OA_BLOCK && ++blocks > OA_MAX_BLOCK_CHANGES)
            return false;
    }
    for (unsigned i = 0; i < l->count; i++) {
        const OaObject *o = &l->objects[i];
        if (o->x < 0 || o->x >= l->width * 16 || o->y < 0 || o->y >= OA_WORLD_H)
            return false;
        if (o->kind >= OA_WALKER && o->kind <= OA_BOSS)
            enemies++;
        else if (o->kind == OA_PLATFORM) {
            platforms++;
            if (o->period < 60 || o->period > 1200 || o->a < 0 || o->a >= l->width * 16 ||
                o->b < 16 || o->b >= OA_WORLD_H)
                return false;
        } else if (o->kind == OA_PORTAL || o->kind == OA_RETURN) {
            if (o->a < 0 || o->a >= l->width * 16 || o->b < 0 || o->b >= OA_WORLD_H)
                return false;
        } else if (o->kind < OA_COIN || o->kind > OA_HEART)
            return false;
    }
    return enemies <= OA_MAX_ENEMIES && platforms <= OA_MAX_PLATFORMS;
}
bool oa_game_load(OaGame *g, unsigned n, bool checkpoint) {
    if (n >= OA_LEVEL_COUNT || !oa_level_valid(&oa_levels[n]))
        return false;
    const OaLevel *l = &oa_levels[n];
    bool preserve = checkpoint && n == g->level;
    if (!preserve) {
        memcpy(g->tiles, l->tiles, l->width * OA_ROWS);
        memset(g->collected, 0, sizeof(g->collected));
        g->checkpoint_x = l->spawn_x;
        g->checkpoint_y = l->spawn_y;
        g->secret_found = false;
    }
    g->level = (uint8_t)n;
    g->enemy_count = 0;
    g->platform_count = 0;
    memset(g->shots, 0, sizeof(g->shots));
    memset(&g->player, 0, sizeof(g->player));
    g->player.body =
        (OaBody){OA_FX(g->checkpoint_x), OA_FX(g->checkpoint_y), 0, 0, 12, 22, -1, false};
    g->player.facing = 1;
    g->player.invulnerable = OA_RESPAWN_PROTECTION;
    for (unsigned i = 0; i < l->count; i++) {
        const OaObject *o = &l->objects[i];
        if (o->kind >= OA_WALKER && o->kind <= OA_BOSS && !taken(g, i)) {
            OaEnemy *e = &g->enemies[g->enemy_count++];
            memset(e, 0, sizeof(*e));
            e->kind = o->kind;
            e->object_id = (uint8_t)i;
            e->alive = true;
            e->direction = -1;
            e->origin_x = o->x;
            e->origin_y = o->y;
            e->hp = o->kind == OA_BOSS ? (uint8_t)(3 + n / 3) : 1;
            int size = o->kind == OA_BOSS ? 32 : 14;
            e->body =
                (OaBody){OA_FX(o->x), OA_FX(o->y), 0, 0, (int16_t)size, (int16_t)size, -1, false};
        } else if (o->kind == OA_PLATFORM) {
            OaPlatform *p = &g->platforms[g->platform_count++];
            memset(p, 0, sizeof(*p));
            p->object_id = (uint8_t)i;
            p->x = OA_FX(o->x);
            p->y = OA_FX(o->y);
        }
    }
    g->camera = g->player.body.x - OA_FX(100);
    if (g->camera < 0)
        g->camera = 0;
    g->state = OA_PLAY;
    g->state_ticks = 0;
    return true;
}
void oa_game_init(OaGame *g) {
    memset(g, 0, sizeof(*g));
    oa_settings_default(&g->settings);
    g->lives = 3;
    (void)oa_game_load(g, 0, false);
    g->state = OA_MENU;
}
void oa_game_hurt(OaGame *g) {
    if (g->state != OA_PLAY || g->player.invulnerable)
        return;
    if (g->player.shield) {
        g->player.shield = 0;
        g->player.invulnerable = OA_RESPAWN_PROTECTION;
        return;
    }
    if (g->lives)
        g->lives--;
    g->state = OA_DEAD;
    g->state_ticks = 0;
    g->sound = OA_SOUND_HURT;
}
static void platforms_step(OaGame *g) {
    const OaLevel *l = &oa_levels[g->level];
    for (unsigned i = 0; i < g->platform_count; i++) {
        OaPlatform *p = &g->platforms[i];
        const OaObject *o = &l->objects[p->object_id];
        p->clock = (uint16_t)((p->clock + 1) % o->period);
        unsigned half = o->period / 2;
        unsigned phase = p->clock <= half ? p->clock : o->period - p->clock;
        int32_t x = OA_FX(o->x) + (int32_t)((int64_t)OA_FX(o->a - o->x) * phase / half);
        int32_t y = OA_FX(o->y) + (int32_t)((int64_t)OA_FX(o->b - o->y) * phase / half);
        p->dx = x - p->x;
        p->dy = y - p->y;
        p->x = x;
        p->y = y;
    }
}
static void shoot(OaGame *g, int x, int y, int direction) {
    for (unsigned i = 0; i < OA_MAX_SHOTS; i++)
        if (!g->shots[i].life) {
            g->shots[i] = (OaShot){OA_FX(x), OA_FX(y), OA_FX(direction * 96), 0, 180};
            break;
        }
}
static void enemies_step(OaGame *g) {
    OaBody *p = &g->player.body;
    int px = oa_pixel(p->x), py = oa_pixel(p->y);
    for (unsigned i = 0; i < g->enemy_count; i++) {
        OaEnemy *e = &g->enemies[i];
        if (!e->alive)
            continue;
        int ex = oa_pixel(e->body.x), ey = oa_pixel(e->body.y);
        if (ex < px - 420 || ex > px + 440)
            continue; /* Les objets lointains restent endormis. */
        e->clock++;
        if (e->phase)
            e->phase--;
        if (e->kind == OA_FLYER) {
            unsigned t = e->clock % 120;
            int tri = t < 60 ? (int)t : 120 - (int)t;
            e->body.y = OA_FX(e->origin_y - 12 + tri * 24 / 60);
            e->body.x += OA_FX(e->direction * 25) / 60;
            if (oa_pixel(e->body.x) < e->origin_x - 40 || oa_pixel(e->body.x) > e->origin_x + 40)
                e->direction = (int8_t)-e->direction;
        } else if (e->kind == OA_TURRET) {
            if (e->clock % 180 == 150)
                shoot(g, ex + 7, ey + 4, px < ex ? -1 : 1);
        } else {
            int speed = e->kind == OA_HOPPER ? 32 : 24;
            if (e->kind == OA_BOSS) {
                /* Cycle de quatre secondes : attente, annonce d'une demi-
                 * seconde, charge puis récupération. Le sens est figé avant
                 * la charge pour laisser au joueur une possibilité d'esquive. */
                unsigned t = e->clock % 240;
                if (t == 120)
                    e->direction = px < ex ? -1 : 1;
                speed = t >= 150 && t < 205 ? 110 + (int)g->level * 3 : 0;
                if (t == 150 && g->level >= 7)
                    shoot(g, ex + 16, ey + 9, e->direction);
                if (ex < e->origin_x - 95)
                    e->direction = 1;
                if (ex > e->origin_x + 95)
                    e->direction = -1;
            }
            if (e->kind == OA_WALKER && e->body.ground) {
                int ahead = ex + (e->direction > 0 ? e->body.w + 3 : -3);
                uint8_t below = oa_tile_at(g, ahead / 16, (ey + e->body.h + 3) / 16);
                if (below == OA_AIR || below == OA_SPIKES || below == OA_WATER)
                    e->direction = (int8_t)-e->direction;
            }
            e->body.vx = OA_FX(e->direction * speed);
            if (e->kind == OA_HOPPER && e->body.ground && e->clock % 90 == 0)
                e->body.vy = OA_FX(-270);
            e->body.vy += OA_FX(OA_GRAVITY) / OA_HZ;
            if (e->body.vy > OA_FX(OA_TERMINAL_SPEED))
                e->body.vy = OA_FX(OA_TERMINAL_SPEED);
            int32_t vx = e->body.vx;
            oa_body_move(g, &e->body, vx / 60, e->body.vy / 60, false);
            if (vx && !e->body.vx)
                e->direction = (int8_t)-e->direction;
        }
        ex = oa_pixel(e->body.x);
        ey = oa_pixel(e->body.y);
        if (ey > 200) {
            e->alive = false;
            take(g, e->object_id);
            continue;
        }
        if (oa_overlap(px, py, p->w, p->h, ex, ey, e->body.w, e->body.h)) {
            if (p->vy > 0 && py + p->h <= ey + 11 && e->kind != OA_TURRET) {
                p->y = e->body.y - p->h * OA_ONE;
                p->vy = OA_FX(-270);
                if (!e->phase) {
                    if (e->hp)
                        e->hp--;
                    e->phase = 45;
                    if (!e->hp) {
                        e->alive = false;
                        take(g, e->object_id);
                        g->score += e->kind == OA_BOSS ? 2000 : 200;
                        g->sound = OA_SOUND_BONUS;
                    }
                }
            } else if (!e->phase)
                oa_game_hurt(g);
        }
    }
    for (unsigned i = 0; i < OA_MAX_SHOTS; i++) {
        OaShot *s = &g->shots[i];
        if (!s->life)
            continue;
        s->life--;
        s->x += s->vx / 60;
        s->y += s->vy / 60;
        int x = oa_pixel(s->x), y = oa_pixel(s->y);
        if (x < 0 || x >= oa_levels[g->level].width * 16 || y < 0 || y >= OA_WORLD_H ||
            oa_tile_at(g, x / 16, y / 16) == OA_GROUND) {
            s->life = 0;
            continue;
        }
        if (oa_overlap(px, py, p->w, p->h, x, y, 6, 6)) {
            oa_game_hurt(g);
            s->life = 0;
        }
    }
}
static void objects_step(OaGame *g, const OaInput *in) {
    const OaLevel *l = &oa_levels[g->level];
    OaBody *p = &g->player.body;
    int x = oa_pixel(p->x), y = oa_pixel(p->y);
    for (unsigned i = 0; i < l->count; i++) {
        const OaObject *o = &l->objects[i];
        if (o->kind >= OA_PLATFORM || taken(g, i))
            continue;
        if (!oa_overlap(x, y, p->w, p->h, o->x, o->y, 16, 16))
            continue;
        switch (o->kind) {
        case OA_COIN:
            take(g, i);
            g->coins++;
            g->score += 50;
            g->sound = OA_SOUND_COIN;
            if (g->coins % 100 == 0 && g->lives < 9)
                g->lives++;
            break;
        case OA_SHIELD:
            take(g, i);
            g->player.shield = OA_POWERUP_TICKS;
            g->sound = OA_SOUND_BONUS;
            break;
        case OA_BOOTS:
            take(g, i);
            g->player.boots = OA_POWERUP_TICKS;
            g->sound = OA_SOUND_BONUS;
            break;
        case OA_HEART:
            take(g, i);
            if (g->lives < 9)
                g->lives++;
            g->sound = OA_SOUND_BONUS;
            break;
        case OA_CHECKPOINT:
            take(g, i);
            g->checkpoint_x = (uint16_t)o->x;
            g->checkpoint_y = (uint16_t)o->y;
            g->save_requested = true;
            g->score += 100;
            break;
        case OA_PORTAL:
        case OA_RETURN:
            if (in->jump_pressed) {
                p->x = OA_FX(o->a);
                p->y = OA_FX(o->b);
                p->vx = 0;
                p->vy = 0;
                g->camera = p->x - OA_FX(100);
                g->secret_found = true;
                g->player.buffer = 0;
                g->player.coyote = 0;
            }
            break;
        default:
            break;
        }
    }
}
void oa_game_step(OaGame *g, const OaInput *in) {
    g->sound = OA_SOUND_NONE;
    g->state_ticks++;
    /* La navigation consomme l'événement avant la physique. Le contact qui
     * ouvre la pause est bloqué jusqu'au relâchement : aucun clic traversant. */
    if (oa_ui_step(g, in))
        return;
    if (g->state == OA_DEAD) {
        if (g->state_ticks >= 60) {
            if (g->lives)
                (void)oa_game_load(g, g->level, true);
            else
                g->state = OA_GAMEOVER;
        }
        return;
    }
    if (g->state == OA_CLEAR) {
        if (g->state_ticks >= 90) {
            if (g->level + 1 < OA_LEVEL_COUNT)
                (void)oa_game_load(g, g->level + 1, false);
            else
                g->state = OA_VICTORY;
        }
        return;
    }
    if (g->state == OA_GAMEOVER || g->state == OA_VICTORY) {
        if (in->confirm || in->pointer_pressed || in->pause) {
            g->lives = 3;
            g->resume_available = false;
            oa_ui_open_menu(g);
            g->ui_blocked = in->pointer_down;
        }
        return;
    }
    if (g->state != OA_PLAY)
        return;
    g->tick++; /* Animations et simulation restent figées dans la pause. */
    platforms_step(g);
    oa_player_step(g, in);
    if (g->state != OA_PLAY)
        return;
    objects_step(g, in);
    enemies_step(g);
    if (g->state != OA_PLAY)
        return;
    OaBody *p = &g->player.body;
    int32_t target = p->x - OA_FX(130);
    const OaLevel *l = &oa_levels[g->level];
    int32_t min = 0, max = OA_FX(l->main_end * 16 - 320);
    if (oa_pixel(p->x) >= l->main_end * 16) {
        min = OA_FX(l->main_end * 16);
        max = OA_FX(l->width * 16 - 320);
    }
    if (target < min)
        target = min;
    if (target > max)
        target = max;
    g->camera += (target - g->camera) / 6;
    bool boss = false;
    for (unsigned i = 0; i < g->enemy_count; i++)
        if (g->enemies[i].kind == OA_BOSS && g->enemies[i].alive)
            boss = true;
    if (!boss && oa_pixel(p->x) >= l->exit_x && oa_pixel(p->x) < l->main_end * 16) {
        g->score += 1000;
        g->state = OA_CLEAR;
        g->state_ticks = 0;
        g->sound = OA_SOUND_CLEAR;
        if (g->level + 1 < OA_LEVEL_COUNT && g->unlocked < g->level + 1)
            g->unlocked = g->level + 1;
        g->save_requested = true;
    }
}
