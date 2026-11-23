/** @file test_engine.c
 * @brief Tests réels du moteur, du journal et des transactions simulées.
 * @details Les bus sont des doubles déterministes : leur succès ne valide
 * aucun signal électrique. Les tests ne dépendent ni de SDL ni d'une carte.
 * SPDX-License-Identifier: MIT
 */
#include "oa_engine.h"
#include "oa_input.h"
#include "oa_render.h"
#include "oa_devices.h"
#include "oa_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions, scenarios;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        assertions++;                                                                              \
        if (!(x)) {                                                                                \
            fprintf(stderr, "ECHEC %s:%d : %s\n", __FILE__, __LINE__, #x);                         \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static OaGame a, b;
static void flat(OaGame *g) {
    oa_game_init(g);
    g->state = OA_PLAY;
    g->enemy_count = 0;
    g->platform_count = 0;
    unsigned w = oa_levels[0].width;
    memset(g->tiles, 0, sizeof(g->tiles));
    memset(g->tiles + 10 * w, OA_GROUND, w);
    g->player.body = (OaBody){OA_FX(64), OA_FX(138), 0, 0, 12, 22, -1, true};
    g->player.invulnerable = 0;
}
static void physics_tests(void) {
    scenarios++;
    flat(&a);
    OaInput in = {0};
    for (int i = 0; i < 120; i++)
        oa_player_step(&a, &in);
    CHECK(a.player.body.y == OA_FX(138));
    CHECK(a.player.body.ground);
    CHECK(a.player.body.x == OA_FX(64));
    scenarios++;
    flat(&a);
    in.move = 1000;
    for (int i = 0; i < 30; i++)
        oa_player_step(&a, &in);
    CHECK(a.player.body.vx == OA_FX(96));
    in.move = 0;
    for (int i = 0; i < 10; i++)
        oa_player_step(&a, &in);
    CHECK(a.player.body.vx == 0);
    scenarios++;
    flat(&a);
    in = (OaInput){.jump_pressed = true, .jump_held = true};
    int min_long = 138;
    for (int i = 0; i < 90; i++) {
        oa_player_step(&a, &in);
        in.jump_pressed = false;
        if (oa_pixel(a.player.body.y) < min_long)
            min_long = oa_pixel(a.player.body.y);
    }
    CHECK(min_long >= 64 && min_long <= 72);
    CHECK(a.player.body.ground);
    flat(&a);
    in = (OaInput){.jump_pressed = true, .jump_held = true};
    int min_short = 138;
    for (int i = 0; i < 90; i++) {
        if (i == 5)
            in.jump_held = false;
        oa_player_step(&a, &in);
        in.jump_pressed = false;
        if (oa_pixel(a.player.body.y) < min_short)
            min_short = oa_pixel(a.player.body.y);
    }
    CHECK(min_short > min_long + 20);
    scenarios++;
    flat(&a);
    unsigned w = oa_levels[0].width;
    for (int r = 5; r < 10; r++)
        a.tiles[r * w + 7] = OA_BRICK;
    in = (OaInput){.move = 1000, .run = true};
    for (int i = 0; i < 80; i++)
        oa_player_step(&a, &in);
    CHECK(a.player.body.x == OA_FX(100));
    scenarios++;
    flat(&a);
    a.player.body = (OaBody){OA_FX(40), OA_FX(130), 0, 0, 12, 22, -1, false};
    for (int r = 0; r < 10; r++)
        a.tiles[r * w + 7] = OA_GROUND;
    oa_body_move(&a, &a.player.body, OA_FX(64), 0, false);
    CHECK(oa_pixel(a.player.body.x) <= 100);
    scenarios++;
    CHECK(oa_pixel(-1) == -1);
    CHECK(oa_pixel(-OA_ONE - 1) == -2);
    CHECK(oa_pixel(OA_ONE + 1) == 1);
    CHECK(oa_tile_at(&a, -1, 5) == OA_GROUND);
    CHECK(oa_tile_at(&a, 0, 11) == OA_AIR);
    scenarios++;
    flat(&a);
    a.player.body.y = OA_FX(85);
    a.player.body.ground = false;
    a.tiles[7 * w + 4] = OA_ONEWAY;
    oa_body_move(&a, &a.player.body, 0, OA_FX(20), false);
    CHECK(a.player.body.y == OA_FX(90));
    CHECK(a.player.body.ground);
    a.player.body.y = OA_FX(124);
    oa_body_move(&a, &a.player.body, 0, OA_FX(-40), false);
    CHECK(a.player.body.y == OA_FX(84));
    scenarios++;
    flat(&a);
    a.player.body.ground = false;
    a.player.coyote = 6;
    in = (OaInput){0};
    for (int i = 0; i < 5; i++)
        oa_player_step(&a, &in);
    in.jump_pressed = true;
    in.jump_held = true;
    oa_player_step(&a, &in);
    CHECK(a.player.body.vy < 0);
    flat(&a);
    a.player.body.ground = false;
    a.player.body.y = OA_FX(40);
    a.player.coyote = 0;
    in = (OaInput){.jump_pressed = true, .jump_held = true};
    oa_player_step(&a, &in);
    CHECK(a.player.body.vy >= 0);
    scenarios++;
    flat(&a);
    a.platform_count = 1;
    a.platforms[0].x = OA_FX(60);
    a.platforms[0].y = OA_FX(84);
    a.platforms[0].dy = OA_FX(-6);
    a.player.body.y = OA_FX(64);
    a.player.body.support = 0;
    a.player.body.ground = true;
    a.tiles[3 * oa_levels[0].width + 4] = OA_BRICK;
    in = (OaInput){0};
    oa_player_step(&a, &in);
    CHECK(a.state == OA_DEAD);
    scenarios++;
    flat(&a);
    a.player.body.y = OA_FX(135);
    a.player.body.vy = OA_FX(100);
    a.player.body.ground = false;
    in = (OaInput){.jump_pressed = true, .jump_held = true};
    oa_player_step(&a, &in);
    in.jump_pressed = false;
    for (int i = 0; i < 5; i++)
        oa_player_step(&a, &in);
    CHECK(a.player.body.vy < 0);
    scenarios++;
    flat(&a);
    a.platform_count = 1;
    a.platforms[0].x = OA_FX(60);
    a.platforms[0].y = OA_FX(112);
    a.player.body.y = OA_FX(85);
    a.player.body.ground = false;
    oa_body_move(&a, &a.player.body, 0, OA_FX(10), true);
    CHECK(a.player.body.y == OA_FX(90));
    CHECK(a.player.body.support == 0);
    scenarios++;
    flat(&a);
    a.tiles[6 * w + 4] = OA_BLOCK;
    in = (OaInput){.jump_pressed = true, .jump_held = true};
    for (int i = 0; i < 12; i++) {
        oa_player_step(&a, &in);
        in.jump_pressed = false;
    }
    CHECK(a.tiles[6 * w + 4] == OA_BRICK);
    CHECK(a.score == 100);
}
static void level_state_tests(void) {
    scenarios++;
    for (unsigned i = 0; i < OA_LEVEL_COUNT; i++) {
        CHECK(oa_level_valid(&oa_levels[i]));
        CHECK(oa_game_load(&a, i, false));
        CHECK(a.level == i);
        CHECK(a.enemy_count <= OA_MAX_ENEMIES);
    }
    OaLevel bad = oa_levels[0];
    bad.width = 257;
    CHECK(!oa_level_valid(&bad));
    CHECK(!oa_game_load(&a, 10, false));
    /* Au-delà de la capacité du journal, la reprise perdrait des blocs. */
    static uint8_t edited[OA_MAX_COLS * OA_ROWS];
    bad = oa_levels[0];
    memcpy(edited, bad.tiles, bad.width * OA_ROWS);
    bad.tiles = edited;
    unsigned existing = 0;
    for (unsigned i = 0; i < bad.width * OA_ROWS; i++)
        if (edited[i] == OA_BLOCK)
            existing++;
    for (unsigned i = 0; existing < OA_MAX_BLOCK_CHANGES; i++)
        if (edited[i] == OA_AIR) {
            edited[i] = OA_BLOCK;
            existing++;
        }
    CHECK(oa_level_valid(&bad));
    for (unsigned i = 0; i < bad.width * OA_ROWS; i++)
        if (edited[i] == OA_AIR) {
            edited[i] = OA_BLOCK;
            break;
        }
    CHECK(!oa_level_valid(&bad));
    scenarios++;
    oa_game_init(&a);
    OaInput in = {.confirm = true};
    oa_game_step(&a, &in);
    CHECK(a.state == OA_PLAY);
    in = (OaInput){.pause = true};
    oa_game_step(&a, &in);
    CHECK(a.state == OA_PAUSE);
    int32_t y = a.player.body.y;
    in = (OaInput){0};
    for (int i = 0; i < 60; i++)
        oa_game_step(&a, &in);
    CHECK(a.player.body.y == y);
    in.confirm = true;
    oa_game_step(&a, &in);
    CHECK(a.state == OA_PLAY);
    scenarios++;
    flat(&a);
    a.player.shield = 60;
    oa_game_hurt(&a);
    CHECK(a.state == OA_PLAY);
    CHECK(a.player.shield == 0);
    CHECK(a.player.invulnerable > 0);
    a.player.invulnerable = 0;
    oa_game_hurt(&a);
    CHECK(a.state == OA_DEAD);
    CHECK(a.lives == 2);
    in = (OaInput){0};
    for (int i = 0; i < 60; i++)
        oa_game_step(&a, &in);
    CHECK(a.state == OA_PLAY);
    scenarios++;
    oa_game_init(&a);
    in = (OaInput){.confirm = true};
    oa_game_step(&a, &in);
    b = a;
    for (unsigned t = 0; t < 1000; t++) {
        in = (OaInput){.move = t % 200 < 120 ? 1000 : -1000,
                       .jump_pressed = t % 70 == 0,
                       .jump_held = t % 70 < 20,
                       .run = true};
        oa_game_step(&a, &in);
        oa_game_step(&b, &in);
        if (t % 2 == 0) {
            uint16_t band[2560];
            oa_render_band(&a, NULL, 0, band);
        } /* Le rendu n'altère pas le monde. */
        CHECK(a.player.body.x == b.player.body.x && a.player.body.y == b.player.body.y &&
              a.score == b.score && a.state == b.state);
    }
    scenarios++;
    for (unsigned level = 0; level < 10; level++) {
        oa_game_init(&a);
        CHECK(oa_game_load(&a, level, false));
        a.enemy_count = 0; /* Ce test porte sur le chemin géométrique seulement. */
        unsigned deaths = 0;
        for (unsigned t = 0; t < 7000 && oa_pixel(a.player.body.x) < oa_levels[level].exit_x; t++) {
            int px = oa_pixel(a.player.body.x);
            bool danger = false;
            for (int d = 24; d <= 50; d += 4) {
                int col = (px + d) / 16;
                if (oa_tile_at(&a, col, 10) == OA_AIR || oa_tile_at(&a, col, 9) == OA_SPIKES)
                    danger = true;
            }
            uint8_t obstacle = oa_tile_at(&a, (px + 20) / 16,
                                          (oa_pixel(a.player.body.y) + a.player.body.h - 1) / 16);
            if (obstacle == OA_BLOCK || obstacle == OA_BRICK || obstacle == OA_GROUND)
                danger = true;
            OaInput bot = {.move = 1000,
                           .run = true,
                           .jump_held = true,
                           .jump_pressed = a.player.body.ground && danger};
            if (!a.player.body.ground && a.player.body.vy > 0 && danger &&
                oa_tile_at(&a, px / 16, 10) != OA_AIR)
                bot.move = 0;
            oa_game_step(&a, &bot);
            if (a.state == OA_DEAD || a.state == OA_GAMEOVER) {
                deaths++;
                break;
            }
        }
        printf("  niveau %2u parcouru par le pilote automatique : x=%ld y=%ld etat=%u morts=%u\n", level + 1,
               (long)oa_pixel(a.player.body.x), (long)oa_pixel(a.player.body.y), a.state, deaths);
        CHECK(deaths == 0);
        CHECK(oa_pixel(a.player.body.x) >= oa_levels[level].exit_x);
    }
}
static void input_tests(void) {
    scenarios++;
    OaSettings cfg;
    oa_settings_default(&cfg);
    CHECK(oa_settings_valid(&cfg));
    OaTouchInput state = {0};
    OaTouchSample t = {.x = 40, .y = 222, .valid = true, .down = true};
    OaInput in = oa_input_touch(&state, &t, &cfg);
    CHECK(in.move == -1000 && in.pointer_pressed && !in.jump_pressed);
    t.y = 195;
    in = oa_input_touch(&state, &t, &cfg);
    CHECK(in.move == -1000 && in.jump_pressed && in.jump_held);
    in = oa_input_touch(&state, &t, &cfg);
    CHECK(!in.jump_pressed && in.jump_held);
    t.y = 222;
    in = oa_input_touch(&state, &t, &cfg);
    CHECK(!in.jump_held);
    t.y = 195;
    in = oa_input_touch(&state, &t, &cfg);
    CHECK(in.jump_pressed);
    t.valid = false;
    in = oa_input_touch(&state, &t, &cfg);
    CHECK(!in.move && !in.jump_held && !in.jump_pressed);
    scenarios++;
    OaTiltInput tilt = {0};
    int16_t mg[3] = {0, 0, 1000};
    oa_tilt_sample(&tilt, mg, 0);
    CHECK(oa_tilt_axis(&tilt, &cfg, 0) == 0);
    mg[0] = 600;
    for (unsigned i = 1; i < 60; i++)
        oa_tilt_sample(&tilt, mg, i * 10);
    CHECK(oa_tilt_axis(&tilt, &cfg, 590) == 1000);
    cfg.invert = 1;
    CHECK(oa_tilt_axis(&tilt, &cfg, 590) == -1000);
    CHECK(oa_tilt_axis(&tilt, &cfg, 650) == 0);
    scenarios++;
    OaCalPoint raw[3] = {{100, 200}, {3900, 200}, {100, 3800}},
               screen[3] = {{30, 40}, {290, 40}, {30, 210}};
    CHECK(oa_calibration_fit(&cfg, raw, screen));
    int16_t x, y;
    CHECK(oa_calibration_map(&cfg, 3900, 3800, &x, &y));
    CHECK(x >= 289 && x <= 290 && y >= 209 && y <= 210);
    memset(raw, 0, sizeof(raw));
    CHECK(!oa_calibration_fit(&cfg, raw, screen));
    scenarios++;
    for (unsigned i = 0; i < 256; i++) {
        oa_settings_default(&cfg);
        cfg.gesture_px = (uint8_t)i;
        CHECK(oa_settings_valid(&cfg) == (i >= 8 && i <= 48));
    }
    oa_settings_default(&cfg);
    cfg.touch_profile = 4;
    CHECK(!oa_settings_valid(&cfg));
    cfg.touch_profile = 0;
    cfg.tilt_full_mg = 0;
    CHECK(!oa_settings_valid(&cfg));
}
/** Vérifie la progression réelle d'un boss : piétinements, invulnérabilité,
 * sortie verrouillée puis victoire. Les placements contrôlent le scénario,
 * ils ne prétendent pas représenter le parcours d'un joueur humain. */
static void gameplay_tests(void) {
    scenarios++;
    for (unsigned n = 4; n < 10; n += (n == 4 ? 3 : 2)) {
        oa_game_init(&a);
        CHECK(oa_game_load(&a, n, false));
        OaEnemy *boss = NULL;
        for (unsigned i = 0; i < a.enemy_count; i++)
            if (a.enemies[i].kind == OA_BOSS)
                boss = &a.enemies[i];
        CHECK(boss != NULL);
        unsigned hp = boss->hp;
        for (unsigned h = 0; h < hp; h++) {
            boss->phase = 0;
            boss->clock = 0;
            a.player.body.x = boss->body.x + OA_FX(5);
            a.player.body.y = boss->body.y - OA_FX(22);
            a.player.body.vy = OA_FX(100);
            a.player.body.vx = 0;
            a.player.body.ground = false;
            a.player.invulnerable = 0;
            OaInput in = {0};
            oa_game_step(&a, &in);
            CHECK(boss->hp == hp - h - 1);
            CHECK(a.state == OA_PLAY);
            CHECK(a.player.body.vy < 0);
        }
        CHECK(!boss->alive);
        a.player.body.x = OA_FX(oa_levels[n].exit_x);
        a.player.body.y = OA_FX(138);
        a.player.body.vx = 0;
        a.player.body.vy = 0;
        OaInput in = {0};
        oa_game_step(&a, &in);
        CHECK(a.state == OA_CLEAR);
        for (unsigned t = 0; t < 90; t++)
            oa_game_step(&a, &in);
        CHECK(n == 9 ? a.state == OA_VICTORY : a.level == n + 1);
    }
    scenarios++;
    oa_game_init(&a);
    CHECK(oa_game_load(&a, 2, false));
    a.enemy_count = 0;
    const OaLevel *l = &oa_levels[2];
    const OaObject *checkpoint = NULL, *portal = NULL;
    for (unsigned i = 0; i < l->count; i++) {
        if (l->objects[i].kind == OA_CHECKPOINT)
            checkpoint = &l->objects[i];
        if (l->objects[i].kind == OA_PORTAL)
            portal = &l->objects[i];
    }
    CHECK(checkpoint && portal);
    a.player.body.x = OA_FX(checkpoint->x);
    a.player.body.y = OA_FX(138);
    OaInput in = {0};
    oa_game_step(&a, &in);
    CHECK(a.checkpoint_x == checkpoint->x);
    CHECK(a.save_requested);
    a.player.invulnerable = 0;
    oa_game_hurt(&a);
    for (unsigned i = 0; i < 60; i++)
        oa_game_step(&a, &in);
    CHECK(a.player.body.x == OA_FX(checkpoint->x));
    scenarios++;
    a.player.body.x = OA_FX(portal->x);
    a.player.body.y = OA_FX(138);
    a.player.body.ground = true;
    in.jump_pressed = true;
    in.jump_held = true;
    oa_game_step(&a, &in);
    CHECK(a.secret_found);
    CHECK(a.player.body.x == OA_FX(portal->a));
    scenarios++;
    flat(&a);
    a.player.body.y = OA_FX(40);
    a.player.body.ground = false;
    a.player.coyote = 6;
    in = (OaInput){0};
    for (unsigned i = 0; i < 5; i++)
        oa_player_step(&a, &in);
    in.jump_pressed = true;
    in.jump_held = true;
    oa_player_step(&a, &in);
    CHECK(a.player.body.vy < 0);
    flat(&a);
    a.player.body.y = OA_FX(40);
    a.player.body.ground = false;
    a.player.coyote = 6;
    in = (OaInput){0};
    for (unsigned i = 0; i < 6; i++)
        oa_player_step(&a, &in);
    in.jump_pressed = true;
    oa_player_step(&a, &in);
    CHECK(a.player.body.vy >= 0);
}
typedef struct {
    uint8_t mem[8192];
    unsigned writes;
    bool fail;
    bool tear;
    unsigned tear_write, tear_bytes;
} FakeNv;
static bool nr(void *v, uint16_t a, uint8_t *p, size_t n) {
    FakeNv *f = v;
    if (f->fail || a + n > 8192)
        return false;
    memcpy(p, f->mem + a, n);
    return true;
}
static bool nw(void *v, uint16_t a, const uint8_t *p, size_t n) {
    FakeNv *f = v;
    if (f->fail || a + n > 8192 || n > 32 || a / 32 != (a + n - 1) / 32)
        return false;
    if (f->tear && f->writes == f->tear_write) {
        size_t partial = f->tear_bytes < n ? f->tear_bytes : n;
        memcpy(f->mem + a, p, partial);
        f->writes++;
        return false;
    }
    memcpy(f->mem + a, p, n);
    f->writes++;
    return true;
}
static bool ready(void *v) {
    return !((FakeNv *)v)->fail;
}
static void finish_store(OaStorage *s) {
    for (unsigned t = 0; t < 30; t++)
        oa_storage_task(s, t);
    CHECK(s->state == 0 && !s->error);
}
static void storage_tests(void) {
    scenarios++;
    CHECK(oa_crc32((const uint8_t *)"123456789", 9) == UINT32_C(0xCBF43926));
    static FakeNv nv, snapshot, cut;
    memset(&nv, 0, sizeof(nv));
    memset(nv.mem, 0xFF, 8192);
    OaNv io = {&nv, nr, nw, ready};
    OaStorage store;
    oa_game_init(&a);
    CHECK(!oa_storage_open(&store, io, &a));
    CHECK(store.writable);
    a.score = 321;
    a.coins = 15;
    a.settings.control_mode = 1;
    CHECK(oa_storage_request(&store, &a, 0));
    finish_store(&store);
    snapshot = nv;
    scenarios++;
    /* Coupure au milieu de chacun des neuf transferts de page, après
     * chacun des 32 octets possibles : 288 interruptions partielles. */
    for (unsigned page = 0; page < 9; page++)
        for (unsigned byte = 0; byte < 32; byte++) {
            cut = snapshot;
            cut.tear = true;
            cut.tear_write = cut.writes + page;
            cut.tear_bytes = byte;
            OaNv torn_io = {&cut, nr, nw, ready};
            OaStorage torn_store;
            oa_game_init(&a);
            CHECK(oa_storage_open(&torn_store, torn_io, &a));
            a.score = 789;
            CHECK(oa_storage_request(&torn_store, &a, 0));
            for (unsigned t = 0; t < 30; t++)
                oa_storage_task(&torn_store, t);
            cut.tear = false;
            oa_game_init(&b);
            OaStorage recovered;
            CHECK(oa_storage_open(&recovered, torn_io, &b));
            CHECK(b.score == 321 || b.score == 789);
        }
    oa_game_init(&b);
    CHECK(oa_storage_open(&store, io, &b));
    CHECK(b.score == 321 && b.coins == 15 && b.settings.control_mode == 1);
    scenarios++;
    for (unsigned stop = 0; stop < 13; stop++) {
        cut = snapshot;
        OaNv cio = {&cut, nr, nw, ready};
        oa_game_init(&a);
        CHECK(oa_storage_open(&store, cio, &a));
        a.score = 789;
        CHECK(oa_storage_request(&store, &a, 0));
        for (unsigned i = 0; i < stop; i++)
            oa_storage_task(&store, i);
        oa_game_init(&b);
        OaStorage recovered;
        CHECK(oa_storage_open(&recovered, cio, &b));
        CHECK(b.score == 321 || b.score == 789);
    }
    scenarios++;
    cut = snapshot;
    cut.mem[OA_SAVE_BASE + 256 + 40] ^= 1;
    io.context = &cut;
    oa_game_init(&a);
    CHECK(!oa_storage_open(&store, io, &a));
    CHECK(!store.writable);
    oa_storage_allow_initialize(&store);
    CHECK(store.writable);
    CHECK(oa_storage_request(&store, &a, 0));
    finish_store(&store);
    scenarios++;
    io.context = &nv;
    CHECK(oa_storage_open(&store, io, &a));
    for (unsigned i = 0; i < 35; i++) {
        a.score = i;
        CHECK(oa_storage_request(&store, &a, 0));
        finish_store(&store);
    }
    oa_game_init(&b);
    CHECK(oa_storage_open(&store, io, &b));
    CHECK(b.score == 34);
    scenarios++;
    nv.fail = true;
    CHECK(!oa_storage_request(&store, &a, 0) || store.state != 0);
    oa_storage_task(&store, 100);
    CHECK(store.error);
    nv.fail = false;
}
typedef struct {
    bool ft, tsc, stm, bad_format, mode1_only, pen_down, reject_ctrl;
    uint8_t reg, accel_id, registers[128];
    uint8_t touch_registers[128], fifo[64], fifo_count, fifo_byte;
    unsigned max_touch;
} FakeBus;
static bool iw(void *v, uint8_t addr, const uint8_t *p, size_t n) {
    FakeBus *f = v;
    if (addr == 0x38 && !f->ft)
        return false;
    if (addr == 0x48 && !f->tsc)
        return false;
    if (n)
        f->reg = p[0];
    return true;
}
static bool ir(void *v, uint8_t addr, uint8_t *p, size_t n) {
    FakeBus *f = v;
    memset(p, 0, n);
    if (addr == 0x38) {
        if (!f->ft)
            return false;
        if (f->reg == 0xA8)
            p[0] = 0x11;
        if (f->reg == 0xA3)
            p[0] = 6;
    } else {
        if (!f->tsc)
            return false;
        p[0] = 0x42;
        if (n > 1)
            p[1] = f->bad_format ? 1 : 0xA0;
    }
    return true;
}
static bool sp(void *v, OaDevice d, const uint8_t *tx, uint8_t *rx, size_t n, uint32_t hz,
               unsigned mode) {
    FakeBus *f = v;
    (void)mode;
    memset(rx, 0xFF, n);
    if (d == OA_DEV_TOUCH) {
        if (f->mode1_only && mode != 1)
            return false;
        if (hz > f->max_touch)
            f->max_touch = hz;
        if (!f->stm)
            return true;
        if (n == 2) {
            uint8_t r = tx[0];
            if (!(r == 0x40 && f->reject_ctrl))
                f->touch_registers[r] = tx[1];
            if (r == 0x4B && tx[1] == 1) {
                f->fifo_count = 0;
                f->fifo_byte = 0;
            }
        }
        if (n == 3) {
            uint8_t r = tx[0] & 0x7F;
            rx[2] = r == 0 ? 8 : (r == 1 ? 0x11 : f->touch_registers[r]);
            if (r == 0x40)
                rx[2] |= f->pen_down ? 0x80 : 0;
            if (r == 0x4C)
                rx[2] = f->fifo_count;
            if (r == 0x57) {
                rx[2] = f->fifo[f->fifo_byte++];
                if (f->fifo_byte % 4 == 0 && f->fifo_count)
                    f->fifo_count--;
            }
        }
        return true;
    }
    if (d == OA_DEV_ACCEL) {
        uint8_t r = tx[0] & (f->accel_id == 0x33 ? 0x3F : 0x7F);
        if (tx[0] & 0x80) {
            for (size_t i = 1; i < n; i++)
                rx[i] = f->registers[r + i - 1];
            if (r == 0x0F)
                rx[1] = f->accel_id;
        } else if (n == 2)
            f->registers[r] = tx[1];
        return true;
    }
    return false;
}
static void delay(void *v, uint32_t n) {
    (void)v;
    (void)n;
}
static void driver_tests(void) {
    FakeBus f = {0};
    OaIo io = {&f, sp, iw, ir, delay};
    OaTouch t;
    scenarios++;
    CHECK(oa_touch_begin(&t, io, OA_TOUCH_AUTO) == OA_TOUCH_AUTO);
    CHECK(t.candidates == 0);
    f.ft = true;
    CHECK(oa_touch_begin(&t, io, OA_TOUCH_AUTO) == OA_TOUCH_FT);
    CHECK(t.proof[1] == OA_PROOF_IDENTITY);
    scenarios++;
    f.tsc = true;
    CHECK(oa_touch_begin(&t, io, OA_TOUCH_AUTO) == OA_TOUCH_AUTO);
    CHECK(t.ambiguous);
    CHECK(oa_touch_begin(&t, io, OA_TOUCH_TSC) == OA_TOUCH_TSC);
    CHECK(t.proof[2] == OA_PROOF_FUNCTIONAL);
    f.bad_format = true;
    CHECK(oa_touch_begin(&t, io, OA_TOUCH_TSC) == OA_TOUCH_AUTO);
    CHECK(t.proof[2] == OA_PROOF_ELECTRICAL);
    scenarios++;
    f.ft = false;
    f.tsc = false;
    f.stm = true;
    CHECK(oa_touch_begin(&t, io, OA_TOUCH_AUTO) == OA_TOUCH_STMPE);
    CHECK(f.max_touch <= 1000000);
    CHECK(t.identity == 0x0811);
    CHECK(f.touch_registers[0x40] == 1); /* Régression : XYZ, pas XY seul. */
    f.mode1_only = true;
    CHECK(oa_touch_begin(&t, io, OA_TOUCH_STMPE) == OA_TOUCH_STMPE);
    CHECK(t.stm_mode == 1);
    f.mode1_only = false;
    scenarios++;
    OaSettings settings;
    oa_settings_default(&settings);
    f.pen_down = true;
    f.fifo_count = 2;
    memcpy(f.fifo, (uint8_t[]){0x10, 0x02, 0x00, 50, 0x32, 0x14, 0x56, 70}, 8);
    oa_touch_poll(&t, &settings, 20000);
    CHECK(t.sample.valid && t.sample.down);
    CHECK(t.sample.raw_x == 0x321 && t.sample.raw_y == 0x456 && t.sample.z == 70);
    CHECK(f.fifo_count == 0);
    f.pen_down = false;
    f.fifo_count = 1; /* Point ancien après le relâchement. */
    oa_touch_poll(&t, &settings, 40000);
    CHECK(t.sample.valid && !t.sample.down && f.fifo_count == 0);
    f.reject_ctrl = true;
    f.touch_registers[0x40] = 0;
    CHECK(oa_touch_begin(&t, io, OA_TOUCH_STMPE) == OA_TOUCH_AUTO);
    CHECK(t.proof[3] == OA_PROOF_IDENTITY);
    f.reject_ctrl = false;
    scenarios++;
    for (unsigned k = 0; k < 2; k++) {
        f.accel_id = k ? 0x3F : 0x33;
        CHECK(oa_accel_begin(io) == f.accel_id);
        f.registers[0x27] = 8;
        uint16_t u = k ? 16667 : 16000;
        f.registers[0x28] = (uint8_t)u;
        f.registers[0x29] = (uint8_t)(u >> 8);
        int16_t mg[3];
        CHECK(oa_accel_read(io, f.accel_id, mg));
        CHECK(mg[0] >= 999 && mg[0] <= 1001);
        f.registers[0x28] = 0x80;
        f.registers[0x29] = 0xC1;
        CHECK(oa_accel_read(io, f.accel_id, mg));
        CHECK(mg[0] < 0);
    }
    f.accel_id = 0;
    CHECK(oa_accel_begin(io) == 0);
}
static void render_tests(void) {
    scenarios++;
    static uint16_t p[2564];
    oa_game_init(&a);
    p[0] = 0x1234;
    p[2561] = 0x5678;
    for (int y = 0; y < 240; y += 8) {
        oa_render_band(&a, NULL, y, p + 1);
        CHECK(p[0] == 0x1234 && p[2561] == 0x5678);
    }
    CHECK(oa_render_hash(p + 1, 0) != 0);
    CHECK(oa_render_hash(p + 1, 10) == 0);
    for (unsigned n = 0; n < 10; n++) {
        CHECK(oa_game_load(&a, n, false));
        for (int y = 0; y < 240; y += 8)
            oa_render_band(&a, NULL, y, p + 1);
        CHECK(p[2561] == 0x5678);
    }
}
int main(void) {
    physics_tests();
    level_state_tests();
    input_tests();
    gameplay_tests();
    storage_tests();
    driver_tests();
    render_tests();
    printf("test_engine : %u scenarios, %u verifications reussies (moteur, journal, pilotes sur bus "
           "simules)\n",
           scenarios, assertions);
    return 0;
}
