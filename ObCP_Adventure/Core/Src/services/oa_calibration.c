/** @file oa_calibration.c @brief Collecte stable, validation indépendante et sorties bornées.
 * SPDX-License-Identifier: MIT */
#include "oa_calibration.h"
#include <string.h>
const OaCalPoint oa_cal_targets[OA_CAL_POINTS] = {
    {28, 72}, {292, 72}, {28, 188}, {292, 188}, {160, 132}};
static void reset_samples(OaCalibration *c) {
    c->count = 0;
    memset(c->sums, 0, sizeof(c->sums));
    memset(c->squares, 0, sizeof(c->squares));
    for (unsigned i = 0; i < 2; i++) {
        c->lo[i] = UINT16_MAX;
        c->hi[i] = 0;
    }
}
void oa_cal_begin(OaCalibration *c, const OaSettings *s, bool tilt, bool available, uint32_t now) {
    memset(c, 0, sizeof(*c));
    c->candidate = *s;
    c->tilt = tilt;
    c->started_ms = now;
    c->point_ms = now;
    c->last_sequence = UINT32_MAX;
    c->status = available ? OA_CAL_RUNNING : OA_CAL_UNAVAILABLE;
    c->wait_release = !tilt;
    c->notice = tilt ? OA_CAL_NOTICE_STILL : OA_CAL_NOTICE_RELEASE;
    reset_samples(c);
}
OaCalStatus oa_cal_watchdog(OaCalibration *c, uint32_t now, bool cancel) {
    if (c->status == OA_CAL_RUNNING) {
        if (cancel)
            c->status = OA_CAL_CANCELLED;
        else if (now - c->point_ms >= OA_CAL_POINT_TIMEOUT_MS ||
                 now - c->started_ms >= OA_CAL_TOTAL_TIMEOUT_MS)
            c->status = OA_CAL_TIMEOUT;
    }
    return c->status;
}
void oa_cal_touch(OaCalibration *c, const OaTouchSample *s, uint32_t now) {
    if (oa_cal_watchdog(c, now, false) != OA_CAL_RUNNING || c->tilt)
        return;
    if (!s->valid) {
        reset_samples(c);
        return;
    }
    if (s->sequence == c->last_sequence)
        return;
    c->last_sequence = s->sequence;
    if (!s->down) {
        bool completed = c->point_complete;
        if (c->point_complete) {
            c->point_complete = false;
            c->point_ms = now;
            if (++c->point == OA_CAL_POINTS) {
                c->status = OA_CAL_DONE;
                return;
            }
        }
        c->wait_release = false;
        reset_samples(c);
        /* L'erreur reste lisible après le relâchement. Le prochain essai
         * remplacera ce message lorsqu'il aura fourni assez de mesures. */
        if (completed || c->notice == OA_CAL_NOTICE_RELEASE)
            c->notice = OA_CAL_NOTICE_PRESS;
        return;
    }
    if (c->wait_release)
        return;
    uint16_t values[2] = {s->raw_x, s->raw_y};
    for (unsigned i = 0; i < 2; i++) {
        if (values[i] < c->lo[i])
            c->lo[i] = values[i];
        if (values[i] > c->hi[i])
            c->hi[i] = values[i];
        c->sums[i] += values[i];
    }
    c->count++;
    if (c->hi[0] - c->lo[0] > OA_CAL_RAW_SPREAD || c->hi[1] - c->lo[1] > OA_CAL_RAW_SPREAD) {
        reset_samples(c);
        c->notice = OA_CAL_NOTICE_UNSTABLE;
        return;
    }
    if (c->count < OA_CAL_SAMPLES)
        return;
    int rx = c->sums[0] / OA_CAL_SAMPLES, ry = c->sums[1] / OA_CAL_SAMPLES;
    c->wait_release = true;
    c->notice = OA_CAL_NOTICE_RELEASE;
    if (c->point < 3) {
        c->raw[c->point] = (OaCalPoint){(int16_t)rx, (int16_t)ry};
        if (c->point == 2 && !oa_calibration_fit(&c->candidate, c->raw, oa_cal_targets)) {
            c->point = 0;
            c->notice = OA_CAL_NOTICE_RESTART;
            return;
        }
    } else {
        int16_t x = 0, y = 0;
        bool valid = oa_calibration_map(&c->candidate, rx, ry, &x, &y);
        int dx = x - oa_cal_targets[c->point].x, dy = y - oa_cal_targets[c->point].y;
        if (!valid || dx < -OA_CAL_MAX_ERROR_PX || dx > OA_CAL_MAX_ERROR_PX ||
            dy < -OA_CAL_MAX_ERROR_PX || dy > OA_CAL_MAX_ERROR_PX) {
            c->notice = OA_CAL_NOTICE_RETRY;
            /* Un point imprécis se retente au même endroit. Trois échecs
             * indiquent probablement un mauvais ajustement initial. */
            if (++c->retries == 3) {
                c->point = 0;
                c->retries = 0;
                c->notice = OA_CAL_NOTICE_RESTART;
            }
            return;
        }
    }
    c->retries = 0;
    c->point_complete = true; /* Chaque cible se valide au relâchement, la dernière aussi. */
}
void oa_cal_tilt(OaCalibration *c, const int16_t mg[3], uint32_t now) {
    if (oa_cal_watchdog(c, now, false) != OA_CAL_RUNNING || !c->tilt)
        return;
    for (unsigned i = 0; i < 3; i++) {
        c->sums[i] += mg[i];
        c->squares[i] += (int64_t)mg[i] * mg[i];
    }
    if (++c->count < OA_CAL_TILT_SAMPLES)
        return;
    bool stable = true;
    for (unsigned i = 0; i < 3; i++) {
        /* N*sum(x²)-sum(x)² = N²*variance. La borne correspond à 30 mg. */
        if (c->squares[i] * OA_CAL_TILT_SAMPLES - (int64_t)c->sums[i] * c->sums[i] >
            INT64_C(9000000))
            stable = false;
    }
    if (!stable) {
        reset_samples(c);
        c->notice = OA_CAL_NOTICE_UNSTABLE;
        return;
    }
    for (unsigned i = 0; i < 3; i++)
        c->candidate.neutral[i] = (int16_t)(c->sums[i] / OA_CAL_TILT_SAMPLES);
    c->status = oa_settings_valid(&c->candidate) ? OA_CAL_DONE : OA_CAL_UNAVAILABLE;
}
