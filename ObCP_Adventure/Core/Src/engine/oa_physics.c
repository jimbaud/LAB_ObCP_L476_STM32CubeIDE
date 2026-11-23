/** @file oa_physics.c
 * @brief Intégration à pas fixe et collisions par parcours borné.
 * @details Le parcours d'un pixel empêche de traverser une tuile entre deux
 * positions. Les fractions sont conservées ; une collision replace exactement
 * le bord du rectangle. Le joueur et les ennemis utilisent le même solveur.
 * SPDX-License-Identifier: MIT
 */
#include "oa_engine.h"
#include <limits.h>
int32_t oa_pixel(int32_t v) {
    return v >= 0 ? v / OA_ONE : (int32_t)(-((-(int64_t)v + OA_ONE - 1) / OA_ONE));
}
static int floor_tile(int p) {
    return p >= 0 ? p / OA_TILE : -((-p + OA_TILE - 1) / OA_TILE);
}
uint8_t oa_tile_at(const OaGame *g, int c, int r) {
    const OaLevel *l = &oa_levels[g->level];
    if (c < 0 || c >= l->width)
        return OA_GROUND;
    if (r < 0 || r >= OA_ROWS)
        return OA_AIR;
    return g->tiles[r * l->width + c];
}
static bool solid(uint8_t t) {
    return t == OA_GROUND || t == OA_BRICK || t == OA_BLOCK || t == OA_CRUMBLE;
}
bool oa_overlap(int x, int y, int w, int h, int X, int Y, int W, int H) {
    return x < X + W && x + w > X && y < Y + H && y + h > Y;
}
void oa_body_move(OaGame *g, OaBody *b, int32_t dx, int32_t dy, bool platforms) {
    /* Une vitesse corrompue ne doit pas créer une boucle sans fin. La limite
     * dépasse les déplacements normaux (moins de 11 pixels/pas). */
    const int32_t limit = OA_FX(OA_MOVE_LIMIT);
    if (dx > limit)
        dx = limit;
    if (dx < -limit)
        dx = -limit;
    if (dy > limit)
        dy = limit;
    if (dy < -limit)
        dy = -limit;
    while (dx != 0) {
        /* Le bord droit/bas est exclusif : retrancher une unité Q16 évite
         * de considérer comme occupée la tuile seulement tangente au corps. */
        int32_t d = dx > OA_ONE ? OA_ONE : (dx < -OA_ONE ? -OA_ONE : dx), next = b->x + d;
        int edge = floor_tile(oa_pixel(next + (d > 0 ? b->w * OA_ONE - 1 : 0)));
        int top = floor_tile(oa_pixel(b->y)),
            bottom = floor_tile(oa_pixel(b->y + b->h * OA_ONE - 1));
        bool hit = false;
        for (int r = top; r <= bottom; r++)
            if (solid(oa_tile_at(g, edge, r)))
                hit = true;
        if (hit) {
            b->x = OA_FX(d > 0 ? edge * OA_TILE - b->w : (edge + 1) * OA_TILE);
            b->vx = 0;
            break;
        }
        b->x = next;
        dx -= d;
    }
    b->ground = false;
    b->support = -1;
    while (dy != 0) {
        int32_t d = dy > OA_ONE ? OA_ONE : (dy < -OA_ONE ? -OA_ONE : dy), next = b->y + d;
        int edge = floor_tile(oa_pixel(next + (d > 0 ? b->h * OA_ONE - 1 : 0)));
        int left = floor_tile(oa_pixel(b->x)),
            right = floor_tile(oa_pixel(b->x + b->w * OA_ONE - 1));
        bool hit = false;
        for (int c = left; c <= right; c++) {
            uint8_t t = oa_tile_at(g, c, edge);
            bool one = t == OA_ONEWAY && d > 0 && b->y + b->h * OA_ONE <= OA_FX(edge * OA_TILE);
            if (solid(t) || one) {
                hit = true;
                /* Un bloc frappé sous sa face libère un bonus une seule fois. */
                if (d < 0 && t == OA_BLOCK && b == &g->player.body && edge >= 0 && edge < OA_ROWS) {
                    g->tiles[edge * oa_levels[g->level].width + c] = OA_BRICK;
                    g->score += 100;
                    g->player.shield = 360;
                    g->sound = OA_SOUND_BONUS;
                }
            }
        }
        if (hit) {
            b->y = OA_FX(d > 0 ? edge * OA_TILE - b->h : (edge + 1) * OA_TILE);
            b->vy = 0;
            b->ground = d > 0;
            break;
        }
        if (platforms && d > 0) {
            for (unsigned i = 0; i < g->platform_count; i++) {
                const OaPlatform *p = &g->platforms[i];
                int px = oa_pixel(p->x), py = oa_pixel(p->y);
                if (oa_overlap(oa_pixel(b->x), py, b->w, 1, px, py, 32, 1) &&
                    b->y + b->h * OA_ONE <= p->y && next + b->h * OA_ONE >= p->y) {
                    b->y = p->y - b->h * OA_ONE;
                    b->vy = 0;
                    b->ground = true;
                    b->support = (int8_t)i;
                    hit = true;
                    break;
                }
            }
            if (hit)
                break;
        }
        b->y = next;
        dy -= d;
    }
}
void oa_player_step(OaGame *g, const OaInput *in) {
    OaPlayer *p = &g->player;
    OaBody *b = &p->body;
    if (p->shield)
        p->shield--;
    if (p->boots)
        p->boots--;
    if (p->invulnerable)
        p->invulnerable--;
    bool was_ground = b->ground;
    if (was_ground)
        p->coyote = OA_COYOTE_TICKS;
    if (in->jump_pressed)
        p->buffer = OA_JUMP_BUFFER_TICKS;
    if (b->support >= 0 && (unsigned)b->support < g->platform_count) {
        OaPlatform *s = &g->platforms[(unsigned)b->support];
        int32_t wanted_y = b->y + s->dy;
        /* Le transport se résout contre les solides : une plateforme ne pousse
         * pas simplement le joueur à travers un mur. */
        oa_body_move(g, b, s->dx, s->dy, false);
        if (s->dy < 0 && b->y != wanted_y && b->y + b->h * OA_ONE > s->y) {
            p->shield = 0;
            p->invulnerable = 0;
            oa_game_hurt(g);
            return;
        }
    }
    int axis = in->move;
    if (axis > 1000)
        axis = 1000;
    if (axis < -1000)
        axis = -1000;
    int speed = in->run ? OA_RUN_SPEED : OA_WALK_SPEED;
    if (p->boots)
        speed += OA_BOOTS_SPEED_EXTRA;
    int32_t target = (int32_t)((int64_t)OA_FX(speed) * axis / 1000);
    int32_t acceleration = OA_FX(axis ? OA_MOVE_ACCELERATION : OA_BRAKE_ACCELERATION) / OA_HZ;
    if (b->vx < target) {
        b->vx += acceleration;
        if (b->vx > target)
            b->vx = target;
    }
    if (b->vx > target) {
        b->vx -= acceleration;
        if (b->vx < target)
            b->vx = target;
    }
    if (axis)
        p->facing = axis < 0 ? -1 : 1;
    if (p->buffer && p->coyote) {
        b->vy = OA_FX(p->boots ? -OA_BOOTS_JUMP_SPEED : -OA_JUMP_SPEED);
        p->buffer = 0;
        p->coyote = 0;
        b->ground = false;
        g->sound = OA_SOUND_JUMP;
    }
    /* Relâcher le geste coupe la montée, sans modifier les collisions. */
    if (p->jump_previous && !in->jump_held && b->vy < OA_FX(-OA_RELEASE_SPEED))
        b->vy = OA_FX(-OA_RELEASE_SPEED);
    p->jump_previous = in->jump_held;
    b->vy += OA_FX(OA_GRAVITY) / OA_HZ;
    if (b->vy > OA_FX(OA_TERMINAL_SPEED))
        b->vy = OA_FX(OA_TERMINAL_SPEED);
    oa_body_move(g, b, b->vx / OA_HZ, b->vy / OA_HZ, true);
    /* Décrémenter après la décision conserve les six pas complets de grâce. */
    if (!was_ground && p->coyote)
        p->coyote--;
    if (!in->jump_pressed && p->buffer)
        p->buffer--;
    int x = oa_pixel(b->x), y = oa_pixel(b->y);
    if (y > OA_WORLD_H + 32) {
        p->shield = 0;
        p->invulnerable = 0;
        oa_game_hurt(g);
        return;
    }
    /* Le bord intérieur du corps évite qu'un simple contact tangent tue. */
    for (int r = floor_tile(y + 3); r <= floor_tile(y + b->h - 2); r++)
        for (int c = floor_tile(x + 2); c <= floor_tile(x + b->w - 3); c++) {
            uint8_t t = oa_tile_at(g, c, r);
            if (t == OA_SPIKES || t == OA_WATER)
                oa_game_hurt(g);
        }
}
