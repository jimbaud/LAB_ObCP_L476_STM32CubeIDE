/** @file oa_input.c @brief Gestes mono-contact et filtre d'inclinaison.
 * SPDX-License-Identifier: MIT */
#include "oa_input.h"
#include "oa_ui.h"
#include <string.h>
OaInput oa_input_touch(OaTouchInput *s, const OaTouchSample *t, const OaSettings *cfg) {
    OaInput in = {0};
    in.x = t->x;
    in.y = t->y;
    if (!t->valid || !t->down) {
        in.pointer_released = s->previous && t->valid && !t->down;
        in.pointer_cancelled = s->previous && !t->valid;
        in.x = s->last_x;
        in.y = s->last_y;
        memset(s, 0, sizeof(*s));
        return in;
    }
    in.pointer_down = true;
    in.pointer_pressed = !s->previous;
    s->last_x = t->x;
    s->last_y = t->y;
    if (!s->previous) {
        s->origin_y = t->y;
        s->armed = true;
        s->lane = (uint8_t)oa_ui_control_at(cfg, t->x, t->y);
    }
    if (s->hold < UINT16_MAX)
        s->hold++;
    in.pointer_long = s->hold == 90;
    if (s->lane == 1 || s->lane == 2) {
        /* La capture reste active hors de la bande. Le seuil horizontal possède
         * une hystérésis pour éviter les inversions au voisinage d'une frontière. */
        if (t->x < cfg->touch_split - 10)
            s->lane = 1;
        if (t->x > cfg->touch_split + 10 && t->x < cfg->touch_split * 2)
            s->lane = 2;
        in.move = s->lane == 1 ? -1000 : 1000;
        in.run = s->hold > 24;
        if (s->armed && s->origin_y - t->y >= cfg->gesture_px) {
            s->gesture = true;
            s->armed = false;
            in.jump_pressed = true;
        }
        if (s->origin_y - t->y < cfg->gesture_px / 2) {
            s->gesture = false;
            s->armed = true;
        }
        in.jump_held = s->gesture;
    } else if (s->lane == 3) {
        in.jump_pressed = !s->previous;
        in.jump_held = true;
    }
    s->previous = true;
    return in;
}
bool oa_input_long_press(OaHoldInput *s, bool valid, bool down) {
    if (!valid || !down) {
        s->ticks = 0;
        return false;
    }
    if (s->ticks < UINT16_MAX)
        s->ticks++;
    return s->ticks == 90;
}
void oa_tilt_sample(OaTiltInput *s, const int16_t mg[3], uint32_t now) {
    for (unsigned i = 0; i < 3; i++)
        s->filtered[i] =
            s->initialized ? s->filtered[i] + ((int32_t)mg[i] - s->filtered[i]) / 4 : mg[i];
    s->initialized = true;
    s->last_ms = now;
}
int16_t oa_tilt_axis(const OaTiltInput *s, const OaSettings *cfg, uint32_t now) {
    if (!s->initialized || now - s->last_ms > 50 || cfg->tilt_axis > 2 ||
        cfg->tilt_full_mg <= cfg->tilt_dead_mg)
        return 0;
    int32_t v = s->filtered[cfg->tilt_axis] - cfg->neutral[cfg->tilt_axis];
    if (cfg->invert)
        v = -v;
    int sign = v < 0 ? -1 : 1;
    if (v < 0)
        v = -v;
    if (v <= cfg->tilt_dead_mg)
        return 0;
    v = (v - cfg->tilt_dead_mg) * 1000 / (cfg->tilt_full_mg - cfg->tilt_dead_mg);
    if (v > 1000)
        v = 1000;
    return (int16_t)(v * sign);
}
bool oa_calibration_fit(OaSettings *s, const OaCalPoint r[3], const OaCalPoint p[3]) {
    int64_t x0 = r[0].x, y0 = r[0].y, x1 = r[1].x, y1 = r[1].y, x2 = r[2].x, y2 = r[2].y;
    int64_t d = x0 * (y1 - y2) + x1 * (y2 - y0) + x2 * (y0 - y1);
    if (d > -1000 && d < 1000)
        return false;
    OaSettings temp = *s;
    for (unsigned k = 0; k < 2; k++) {
        int64_t z0 = (k ? p[0].y : p[0].x), z1 = (k ? p[1].y : p[1].x), z2 = (k ? p[2].y : p[2].x);
        int64_t a = (z0 * (y1 - y2) + z1 * (y2 - y0) + z2 * (y0 - y1)) * OA_ONE / d;
        int64_t b = (z0 * (x2 - x1) + z1 * (x0 - x2) + z2 * (x1 - x0)) * OA_ONE / d;
        int64_t c =
            (z0 * (x1 * y2 - x2 * y1) + z1 * (x2 * y0 - x0 * y2) + z2 * (x0 * y1 - x1 * y0)) *
            OA_ONE / d;
        if (a < INT32_MIN || a > INT32_MAX || b < INT32_MIN || b > INT32_MAX || c < INT32_MIN ||
            c > INT32_MAX)
            return false;
        temp.affine[k * 3] = (int32_t)a;
        temp.affine[k * 3 + 1] = (int32_t)b;
        temp.affine[k * 3 + 2] = (int32_t)c;
    }
    temp.calibrated = 1;
    if (!oa_settings_valid(&temp))
        return false;
    *s = temp;
    return true;
}
bool oa_calibration_map(const OaSettings *s, int rx, int ry, int16_t *x, int16_t *y) {
    int64_t X = ((int64_t)s->affine[0] * rx + (int64_t)s->affine[1] * ry + s->affine[2]) / OA_ONE;
    int64_t Y = ((int64_t)s->affine[3] * rx + (int64_t)s->affine[4] * ry + s->affine[5]) / OA_ONE;
    if (X < -16 || X >= 336 || Y < -16 || Y >= 256)
        return false;
    if (X < 0)
        X = 0;
    if (X > 319)
        X = 319;
    if (Y < 0)
        Y = 0;
    if (Y > 239)
        Y = 239;
    *x = (int16_t)X;
    *y = (int16_t)Y;
    return true;
}
