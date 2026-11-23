/** @file test_calibration.c @brief Régressions de l'assistant sans carte et sans HAL.
 * SPDX-License-Identifier: MIT */
#include "oa_calibration.h"
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
static OaSettings settings;
static OaCalibration cal;
static uint32_t sequence, time_ms;
static void sample(bool down, unsigned x, unsigned y) {
    OaTouchSample s = {.raw_x = (uint16_t)x,
                       .raw_y = (uint16_t)y,
                       .down = down,
                       .valid = true,
                       .sequence = ++sequence};
    time_ms += 20;
    oa_cal_touch(&cal, &s, time_ms);
}
static void point(unsigned index, bool wrong) {
    sample(false, 0, 0);
    /* Dalle synthétique : raw_x = écran_x*10+100, raw_y = écran_y*10+100. */
    unsigned x = (unsigned)oa_cal_targets[index].x * 10 + 100;
    unsigned y = (unsigned)oa_cal_targets[index].y * 10 + 100;
    for (unsigned n = 0; n < OA_CAL_SAMPLES; n++)
        sample(true, x + (wrong ? 400 : 0), y);
    sample(false, 0, 0);
}
static void begin(bool tilt) {
    time_ms = 0;
    sequence = 0;
    oa_settings_default(&settings);
    oa_cal_begin(&cal, &settings, tilt, true, time_ms);
}
int main(void) {
    scenarios++;
    begin(false);
    OaSettings before = settings;
    for (unsigned i = 0; i < 5; i++) {
        point(i, false);
        CHECK(memcmp(&settings, &before, sizeof(settings)) == 0);
    }
    CHECK(cal.status == OA_CAL_DONE);
    CHECK(cal.candidate.calibrated == 1);
    int16_t x = 0, y = 0;
    CHECK(oa_calibration_map(&cal.candidate, 1700, 1420, &x, &y));
    CHECK(x >= 159 && x <= 160 && y >= 131 && y <= 132);

    scenarios++;
    begin(false);
    for (unsigned i = 0; i < 10; i++)
        sample(true, 380, 820);
    CHECK(cal.point == 0 && cal.count == 0 && cal.wait_release);
    sample(false, 0, 0);
    OaTouchSample same = {
        .raw_x = 380, .raw_y = 820, .down = true, .valid = true, .sequence = 1000};
    for (unsigned i = 0; i < 100; i++)
        oa_cal_touch(&cal, &same, ++time_ms);
    CHECK(cal.count == 1 && cal.point == 0); /* Une mesure répétée n'est pas quatre mesures. */

    scenarios++;
    begin(false);
    point(0, false);
    point(1, false);
    point(2, false);
    CHECK(cal.point == 3);
    CHECK(oa_cal_watchdog(&cal, time_ms, true) == OA_CAL_CANCELLED);
    CHECK(settings.calibrated == 0);
    CHECK(settings.affine[0] == OA_ONE); /* Aucun ajustement partiel publié. */

    scenarios++;
    begin(false);
    CHECK(oa_cal_watchdog(&cal, OA_CAL_POINT_TIMEOUT_MS, false) == OA_CAL_TIMEOUT);
    begin(true);
    CHECK(oa_cal_watchdog(&cal, OA_CAL_POINT_TIMEOUT_MS, false) == OA_CAL_TIMEOUT);
    oa_cal_begin(&cal, &settings, false, false, 0);
    CHECK(cal.status == OA_CAL_UNAVAILABLE);
    oa_cal_begin(&cal, &settings, true, false, 0);
    CHECK(cal.status == OA_CAL_UNAVAILABLE);

    scenarios++;
    begin(false);
    sample(false, 0, 0);
    sample(true, 1000, 1000);
    sample(true, 1400, 1000);
    CHECK(cal.point == 0 && cal.notice == OA_CAL_NOTICE_UNSTABLE);
    CHECK(cal.count == 0);
    begin(false);
    for (unsigned i = 0; i < 3; i++)
        point(i, false);
    point(3, true);
    CHECK(cal.point == 3 && cal.notice == OA_CAL_NOTICE_RETRY);
    point(3, false);
    point(4, false);
    CHECK(cal.status == OA_CAL_DONE);

    scenarios++;
    begin(false);
    for (unsigned i = 0; i < 3; i++)
        point(i, false);
    for (unsigned i = 0; i < 3; i++)
        point(3, true);
    CHECK(cal.point == 0 && cal.notice == OA_CAL_NOTICE_RESTART);

    scenarios++;
    begin(true);
    int16_t mg[3] = {120, -30, 980};
    for (unsigned i = 0; i < 100; i++)
        oa_cal_tilt(&cal, mg, i * 10);
    CHECK(cal.status == OA_CAL_DONE);
    CHECK(cal.candidate.neutral[0] == 120 && cal.candidate.neutral[1] == -30);
    CHECK(settings.neutral[0] == 0);

    scenarios++;
    begin(true);
    for (unsigned i = 0; i < 100; i++) {
        mg[0] = (int16_t)(i % 2 ? 500 : -500);
        oa_cal_tilt(&cal, mg, i * 10);
    }
    CHECK(cal.status == OA_CAL_RUNNING && cal.notice == OA_CAL_NOTICE_UNSTABLE);
    CHECK(cal.count == 0);
    CHECK(oa_cal_watchdog(&cal, 30000, false) == OA_CAL_TIMEOUT);

    scenarios++;
    oa_cal_begin(&cal, &settings, false, true, UINT32_MAX - 100);
    CHECK(oa_cal_watchdog(&cal, 100, false) == OA_CAL_RUNNING);
    CHECK(oa_cal_watchdog(&cal, 30000, false) == OA_CAL_TIMEOUT);
    printf("test_calibration : %u scenarios, %u verifications reussies (etalonnage, entrees simulees)\n",
           scenarios, assertions);
    return 0;
}
