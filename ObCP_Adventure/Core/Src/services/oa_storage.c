/** @file oa_storage.c @brief Sérialisation explicite et commit écrit en dernier.
 * SPDX-License-Identifier: MIT */
#include "oa_storage.h"
#include <string.h>
/** @brief Phases de l'écriture. L'état zéro reste le service au repos. */
enum { NV_IDLE, NV_INVALIDATE, NV_WRITE_PAYLOAD, NV_VERIFY_PAYLOAD, NV_COMMIT, NV_VERIFY_COMMIT };
static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}
static uint16_t get16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t get32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int32_t signed32(uint32_t v) {
    return v <= INT32_MAX ? (int32_t)v : (int32_t)((int64_t)v - INT64_C(4294967296));
}
static int16_t signed16(uint16_t v) {
    return v <= INT16_MAX ? (int16_t)v : (int16_t)((int32_t)v - 65536);
}
uint32_t oa_crc32(const uint8_t *p, size_t n) {
    uint32_t c = UINT32_MAX;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (unsigned b = 0; b < 8; b++)
            c = (c >> 1) ^ ((c & 1) ? UINT32_C(0xEDB88320) : 0);
    }
    return ~c;
}
static void encode(uint8_t *p, const OaGame *g) {
    memset(p, 0, 192);
    const OaSettings *s = &g->settings;
    p[0] = s->control_mode;
    p[1] = s->tilt_axis;
    p[2] = s->invert;
    p[3] = s->gesture_px;
    p[4] = s->touch_split;
    p[5] = s->touch_profile;
    p[6] = s->calibrated;
    p[7] = s->lcd_fast;
    put16(p + 8, s->tilt_dead_mg);
    put16(p + 10, s->tilt_full_mg);
    for (unsigned i = 0; i < 3; i++)
        put16(p + 12 + 2 * i, (uint16_t)s->neutral[i]);
    for (unsigned i = 0; i < 6; i++)
        put32(p + 18 + 4 * i, (uint32_t)s->affine[i]);
    p[42] = g->level;
    p[43] = g->unlocked;
    p[44] = g->lives;
    put32(p + 45, g->score);
    put16(p + 49, g->coins);
    put16(p + 51, g->checkpoint_x);
    put16(p + 53, g->checkpoint_y);
    memcpy(p + 55, g->collected, 16);
    const OaLevel *l = &oa_levels[g->level];
    unsigned changes = 0;
    for (unsigned i = 0; i < l->width * OA_ROWS && changes < OA_MAX_BLOCK_CHANGES; i++)
        if (l->tiles[i] == OA_BLOCK && g->tiles[i] == OA_BRICK) {
            put16(p + 72 + 2 * changes, (uint16_t)i);
            changes++;
        }
    p[71] = (uint8_t)changes;
}
static bool decode(const uint8_t *p, OaGame *g) {
    OaSettings s;
    oa_settings_default(&s);
    s.control_mode = p[0];
    s.tilt_axis = p[1];
    s.invert = p[2];
    s.gesture_px = p[3];
    s.touch_split = p[4];
    s.touch_profile = p[5];
    s.calibrated = p[6];
    s.lcd_fast = p[7];
    s.tilt_dead_mg = get16(p + 8);
    s.tilt_full_mg = get16(p + 10);
    for (unsigned i = 0; i < 3; i++)
        s.neutral[i] = signed16(get16(p + 12 + 2 * i));
    for (unsigned i = 0; i < 6; i++)
        s.affine[i] = signed32(get32(p + 18 + 4 * i));
    unsigned n = p[42];
    if (!oa_settings_valid(&s) || n >= OA_LEVEL_COUNT || p[43] >= OA_LEVEL_COUNT || p[44] > 9 ||
        p[71] > OA_MAX_BLOCK_CHANGES)
        return false;
    const OaLevel *l = &oa_levels[n];
    unsigned cx = get16(p + 51), cy = get16(p + 53);
    if (cx >= l->width * 16 || cy >= OA_WORLD_H)
        return false;
    for (unsigned i = 0; i < p[71]; i++) {
        unsigned j = get16(p + 72 + 2 * i);
        if (j >= l->width * OA_ROWS || l->tiles[j] != OA_BLOCK)
            return false;
    }
    /* Toute la validation précède la mutation du jeu : un enregistrement rejeté
     * ne doit pas laisser la moitié de ses paramètres dans l'application. */
    (void)oa_game_load(g, n, false);
    g->settings = s;
    g->unlocked = p[43];
    g->lives = p[44];
    g->score = get32(p + 45);
    g->coins = get16(p + 49);
    g->checkpoint_x = (uint16_t)cx;
    g->checkpoint_y = (uint16_t)cy;
    memcpy(g->collected, p + 55, 16);
    for (unsigned i = 0; i < p[71]; i++)
        g->tiles[get16(p + 72 + 2 * i)] = OA_BRICK;
    g->player.body.x = OA_FX(cx);
    g->player.body.y = OA_FX(cy);
    g->state = OA_MENU;
    return true;
}
static bool valid(const uint8_t *r) {
    return memcmp(r, "OACP", 4) == 0 && get16(r + 4) == 1 && get16(r + 6) == 192 &&
           get32(r + 224) == UINT32_C(0xC04D17ED) && get32(r + 228) == get32(r + 8) &&
           get32(r + 12) == oa_crc32(r + 32, 192) && get32(r + 232) == oa_crc32(r, 224);
}
bool oa_storage_open(OaStorage *s, OaNv nv, OaGame *g) {
    memset(s, 0, sizeof(*s));
    s->nv = nv;
    uint8_t r[256];
    bool found = false, blank = true;
    if (!nv.read || !nv.write_page || !nv.ready || !nv.ready(nv.context))
        return false;
    s->available = true;
    for (unsigned i = 0; i < OA_SAVE_SLOTS; i++) {
        if (!nv.read(nv.context, (uint16_t)(OA_SAVE_BASE + i * 256), r, 256)) {
            s->available = false;
            return false;
        }
        for (unsigned k = 0; k < 256; k++)
            if (r[k] != 0xFF)
                blank = false;
        uint32_t seq = get32(r + 8), delta = seq - s->sequence;
        if (valid(r) && (!found || (delta != 0 && delta < UINT32_C(0x80000000)))) {
            if (decode(r + 32, g)) {
                found = true;
                s->sequence = seq;
                s->slot = (uint8_t)i;
                memcpy(s->record, r, 256);
            }
        }
    }
    s->writable = found || blank;
    return found;
}
void oa_storage_allow_initialize(OaStorage *s) {
    if (s->available)
        s->writable = true;
}
bool oa_storage_request(OaStorage *s, const OaGame *g, uint32_t now) {
    if (!s->available || !s->writable || s->state || !oa_settings_valid(&g->settings))
        return false;
    memset(s->record, 0, sizeof(s->record));
    uint8_t *r = s->record;
    memcpy(r, "OACP", 4);
    put16(r + 4, 1);
    put16(r + 6, 192);
    put32(r + 8, s->sequence + 1);
    encode(r + 32, g);
    put32(r + 12, oa_crc32(r + 32, 192));
    put32(r + 224, UINT32_C(0xC04D17ED));
    put32(r + 228, s->sequence + 1);
    put32(r + 232, oa_crc32(r, 224));
    s->slot = (uint8_t)((s->slot + 1) % OA_SAVE_SLOTS);
    s->page = 0;
    s->state = NV_INVALIDATE;
    s->error = false;
    s->pending = true;
    s->deadline = now;
    return true;
}
void oa_storage_task(OaStorage *s, uint32_t now) {
    if (!s->state)
        return;
    if (!s->nv.ready(s->nv.context)) {
        if (now - s->deadline > 20) {
            s->state = NV_IDLE;
            s->error = true;
            s->pending = false;
        }
        return;
    }
    uint16_t base = (uint16_t)(OA_SAVE_BASE + s->slot * 256);
    bool ok = true;
    /* Chaque appel ne lance qu'une opération, puis le prochain appel attend
     * WIP=0. Aucune boucle d'attente ne monopolise le jeu pendant une écriture. */
    if (s->state == NV_INVALIDATE) {
        uint8_t invalid[32] = {0};
        ok = s->nv.write_page(s->nv.context, (uint16_t)(base + 224), invalid, 32);
        s->state = NV_WRITE_PAYLOAD;
    } else if (s->state == NV_WRITE_PAYLOAD) {
        ok = s->nv.write_page(s->nv.context, (uint16_t)(base + s->page * 32),
                              s->record + s->page * 32, 32);
        if (++s->page == 7)
            s->state = NV_VERIFY_PAYLOAD;
    } else if (s->state == NV_VERIFY_PAYLOAD) {
        uint8_t verify[224];
        ok = s->nv.read(s->nv.context, base, verify, 224) && memcmp(verify, s->record, 224) == 0;
        s->state = NV_COMMIT;
    } else if (s->state == NV_COMMIT) {
        ok = s->nv.write_page(s->nv.context, (uint16_t)(base + 224), s->record + 224, 32);
        s->state = NV_VERIFY_COMMIT;
    } else {
        uint8_t verify[256];
        ok = s->nv.read(s->nv.context, base, verify, 256) && valid(verify);
        if (ok)
            s->sequence++;
        s->state = NV_IDLE;
        s->pending = false;
    }
    s->deadline = now;
    if (!ok) {
        s->error = true;
        s->state = NV_IDLE;
        s->pending = false;
    }
}
