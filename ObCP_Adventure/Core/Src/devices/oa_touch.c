/** @file oa_touch.c @brief FT62xx, TSC2007 et STMPE610 derrière un contrat unique.
 * @details Les délais d'initialisation sont bornés et réservés au démarrage.
 * En jeu, les conversions TSC2007 ne bloquent pas la super-boucle.
 * SPDX-License-Identifier: MIT
 */
#include "oa_devices.h"
#include <string.h>
enum {
    STMPE_TSC_CTRL = 0x40,
    STMPE_XYZ_ENABLE = 0x01,
    STMPE_FIFO_STATUS = 0x4B,
    STMPE_FIFO_SIZE = 0x4C,
    STMPE_DATA = 0x57,
    STMPE_MAX_BATCH = 8,
    TSC_SETTLE_US = 2000
};
static bool stm_clear(OaTouch *t);
static bool reg_i2c(OaIo *io, uint8_t addr, uint8_t reg, uint8_t *p, size_t n) {
    return io->i2c_write(io->context, addr, &reg, 1) && io->i2c_read(io->context, addr, p, n);
}
static bool stm_read(OaTouch *t, uint8_t reg, uint8_t *p) {
    OaIo *io = &t->io;
    uint8_t tx[3] = {(uint8_t)(reg | 0x80), 0, 0}, rx[3];
    if (!io->spi(io->context, OA_DEV_TOUCH, tx, rx, 3, 1000000, t->stm_mode))
        return false;
    *p = rx[2];
    return true;
}
static bool stm_write(OaTouch *t, uint8_t reg, uint8_t value) {
    OaIo *io = &t->io;
    uint8_t tx[2] = {reg, value}, rx[2];
    return io->spi(io->context, OA_DEV_TOUCH, tx, rx, 2, 1000000, t->stm_mode);
}
static bool stm_clear(OaTouch *t) {
    return stm_write(t, STMPE_FIFO_STATUS, 1) && stm_write(t, STMPE_FIFO_STATUS, 0);
}
static bool ft_probe(OaTouch *t) {
    uint8_t v = 0, id = 0;
    OaIo *io = &t->io;
    if (!io->i2c_write(io->context, 0x38, NULL, 0))
        return false;
    t->proof[OA_TOUCH_FT] = OA_PROOF_ELECTRICAL;
    if (!reg_i2c(io, 0x38, 0xA8, &v, 1) || !reg_i2c(io, 0x38, 0xA3, &id, 1))
        return false;
    if (v != 0x11 || (id != 0x06 && id != 0x36 && id != 0x64))
        return false;
    t->proof[OA_TOUCH_FT] = OA_PROOF_IDENTITY;
    t->identity = (uint16_t)((v << 8) | id);
    return true;
}
static bool tsc_probe(OaTouch *t) {
    OaIo *io = &t->io;
    uint8_t cmd = 0, r[2];
    if (!io->i2c_write(io->context, 0x48, NULL, 0))
        return false;
    t->proof[OA_TOUCH_TSC] = OA_PROOF_ELECTRICAL;
    for (unsigned i = 0; i < 3; i++) {
        if (!io->i2c_write(io->context, 0x48, &cmd, 1))
            return false;
        io->delay_us(io->context, TSC_SETTLE_US);
        if (!io->i2c_read(io->context, 0x48, r, 2) || (r[1] & 15) != 0)
            return false;
    }
    /* Une réponse compatible n'est pas un identifiant, même répétée trois fois. */
    t->proof[OA_TOUCH_TSC] = OA_PROOF_FUNCTIONAL;
    return true;
}
static bool stm_probe(OaTouch *t) {
    /* Les exemples de référence autorisent les modes 0 et 1. On ne change
     * de mode que pendant la sonde, CS inactif entre chaque transaction. */
    for (unsigned mode = 0; mode < 2; mode++) {
        uint8_t hi = 0, lo = 0;
        t->stm_mode = (uint8_t)mode;
        if (stm_read(t, 0, &hi) && stm_read(t, 1, &lo) && hi == 0x08 && lo == 0x11) {
            t->proof[OA_TOUCH_STMPE] = OA_PROOF_IDENTITY;
            t->identity = 0x0811;
            return true;
        }
    }
    return false;
}
OaTouchKind oa_touch_begin(OaTouch *t, OaIo io, OaTouchKind requested) {
    memset(t, 0, sizeof(*t));
    t->io = io;
    if (!io.spi || !io.i2c_write || !io.i2c_read || !io.delay_us || requested > OA_TOUCH_STMPE)
        return OA_TOUCH_AUTO;
    bool found[4] = {false, false, false, false};
    if (requested == OA_TOUCH_AUTO || requested == OA_TOUCH_FT)
        found[1] = ft_probe(t);
    if (requested == OA_TOUCH_AUTO || requested == OA_TOUCH_TSC)
        found[2] = tsc_probe(t);
    if (requested == OA_TOUCH_AUTO || requested == OA_TOUCH_STMPE)
        found[3] = stm_probe(t);
    unsigned count = 0;
    for (unsigned i = 1; i < 4; i++)
        if (found[i]) {
            t->candidates |= (uint8_t)(1u << i);
            t->kind = (OaTouchKind)i;
            count++;
        }
    if (count != 1) {
        t->ambiguous = count > 1;
        t->kind = OA_TOUCH_AUTO;
        return t->kind;
    }
    bool ok = true;
    if (t->kind == OA_TOUCH_STMPE) {
        ok = stm_write(t, 0x03, 2);
        io.delay_us(io.context, 10000);
        /* XYZ est indispensable au décodage de quatre octets. La valeur 3
         * active XY seul et ne correspond pas au format XYZ lu plus bas. */
        static const uint8_t config[][2] = {
            {0x04, 0}, {STMPE_TSC_CTRL, 0}, {0x09, 0},   {0x0A, 0}, {0x20, 0x68},
            {0x21, 2}, {0x41, 0xA4},        {0x56, 6},   {0x4A, 1}, {0x4B, 1},
            {0x4B, 0}, {0x58, 1},           {0x0B, 0xFF}};
        for (unsigned i = 0; i < sizeof(config) / sizeof(config[0]) && ok; i++)
            ok = stm_write(t, config[i][0], config[i][1]);
        io.delay_us(io.context, TSC_SETTLE_US);
        uint8_t control = 0;
        ok = ok && stm_write(t, STMPE_TSC_CTRL, STMPE_XYZ_ENABLE) &&
             stm_read(t, STMPE_TSC_CTRL, &control) && (control & 0x7F) == STMPE_XYZ_ENABLE;
    }
    if (!ok)
        t->kind = OA_TOUCH_AUTO;
    return t->kind;
}
static void finish(OaTouch *t, const OaSettings *s, uint16_t rx, uint16_t ry, uint16_t z,
                   bool down) {
    OaTouchSample *v = &t->sample;
    v->raw_x = rx;
    v->raw_y = ry;
    v->z = z;
    v->down = down;
    v->valid = true;
    v->raw_valid = true;
    v->raw_down = down;
    v->sequence++;
    if (t->kind == OA_TOUCH_FT) {
        if (rx >= 240 || ry >= 320) {
            v->valid = !down;
            v->down = false;
            v->raw_valid = !down;
            v->raw_down = false;
            return;
        }
        v->x = (int16_t)ry;
        v->y = (int16_t)(239 - rx);
    } else if (s->calibrated) {
        if (!oa_calibration_map(s, rx, ry, &v->x, &v->y)) {
            v->valid = false;
            v->down = false;
        }
    } else {
        v->x = (int16_t)((uint32_t)rx * 319 / 4095);
        v->y = (int16_t)((uint32_t)ry * 239 / 4095);
    }
    t->failures = 0;
}
void oa_touch_poll(OaTouch *t, const OaSettings *s, uint32_t now) {
    OaIo *io = &t->io;
    bool ok = true;
    if (t->kind == OA_TOUCH_AUTO) {
        t->sample.valid = false;
        t->sample.down = false;
        t->sample.raw_valid = false;
        t->sample.raw_down = false;
        return;
    }
    if (t->kind == OA_TOUCH_TSC) {
        static const uint8_t commands[] = {0xE4, 0xF4, 0xC4, 0xD4, 0xC4, 0xD4, 0x00};
        if (t->phase == 0) {
            if (now - t->last_sample < 10000)
                return;
            ok = io->i2c_write(io->context, 0x48, &commands[0], 1);
            t->deadline = now;
            t->phase = 1;
        } else if (now - t->deadline >= TSC_SETTLE_US) {
            uint8_t r[2];
            unsigned k = t->phase - 1;
            ok = io->i2c_read(io->context, 0x48, r, 2) && (r[1] & 15) == 0;
            if (ok)
                t->conversions[k] = (uint16_t)(((uint16_t)r[0] << 4) | (r[1] >> 4));
            if (ok && k < 6) {
                ok = io->i2c_write(io->context, 0x48, &commands[k + 1], 1);
                t->phase++;
                t->deadline = now;
            } else if (ok) {
                int dx = (int)t->conversions[2] - t->conversions[4],
                    dy = (int)t->conversions[3] - t->conversions[5];
                bool down = t->conversions[0] > 30 && t->conversions[1] > t->conversions[0] &&
                            t->conversions[1] < 4095 && dx > -100 && dx < 100 && dy > -100 &&
                            dy < 100;
                finish(t, s, t->conversions[4], t->conversions[5], t->conversions[0], down);
                t->phase = 0;
                t->last_sample = now;
            }
        }
    } else if (now - t->last_sample >= 10000) {
        t->last_sample = now;
        if (t->kind == OA_TOUCH_FT) {
            uint8_t r[16];
            ok = reg_i2c(io, 0x38, 0, r, 16);
            if (ok) {
                unsigned n = r[2] & 15;
                if (n > 2)
                    ok = false;
                else
                    finish(t, s, (uint16_t)(((r[3] & 15) << 8) | r[4]),
                           (uint16_t)(((r[5] & 15) << 8) | r[6]), 1, n != 0);
            }
        } else {
            uint8_t status = 0, size = 0;
            ok = stm_read(t, STMPE_TSC_CTRL, &status) && (status & 0x7F) == STMPE_XYZ_ENABLE &&
                 stm_read(t, STMPE_FIFO_SIZE, &size) && size <= 128;
            if (ok && !(status & 0x80)) {
                /* Un ancien point FIFO n'est jamais publié comme nouvel appui
                 * après le relâchement : le wizard attend ce front pour avancer. */
                ok = !size || stm_clear(t);
                if (ok)
                    finish(t, s, t->sample.raw_x, t->sample.raw_y, 0, false);
            } else if (ok && size) {
                uint8_t r[4] = {0};
                unsigned take = size < STMPE_MAX_BATCH ? size : STMPE_MAX_BATCH;
                for (unsigned point = 0; point < take && ok; point++)
                    for (unsigned i = 0; i < 4 && ok; i++)
                        ok = stm_read(t, STMPE_DATA, &r[i]);
                if (ok && size > take)
                    ok = stm_clear(t);
                if (ok)
                    finish(t, s, (uint16_t)((r[0] << 4) | (r[1] >> 4)),
                           (uint16_t)(((r[1] & 15) << 8) | r[2]), r[3], true);
            }
        }
    }
    if (!ok) {
        t->sample.valid = false;
        t->sample.down = false;
        t->sample.raw_valid = false;
        t->sample.raw_down = false;
        t->sample.sequence++;
        t->phase = 0;
        t->last_sample = now;
        t->failures++;
    }
}
