/** @file test_ui.c @brief Régressions des parcours tactiles et du centrage réel RGB565.
 * SPDX-License-Identifier: MIT */
#include "oa_ui.h"
#include "oa_input.h"
#include "oa_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static OaGame g;
static OaTouchInput pointer;
static unsigned checks, scenarios;
static uint16_t image[320 * 240];
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "ECHEC UI ligne %d : %s\n", __LINE__, #x);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static void event(int x, int y, bool down, bool valid) {
    OaTouchSample t = {.x = (int16_t)x, .y = (int16_t)y, .down = down, .valid = valid};
    OaInput in = oa_input_touch(&pointer, &t, &g.settings);
    oa_game_step(&g, &in);
}
static void tap(int x, int y) {
    event(x, y, true, true);
    event(0, 0, false, true);
}
static void click(OaUiId id) {
    OaRect r;
    CHECK(oa_ui_rect(&g, id, &r));
    tap(r.x + r.w / 2, r.y + r.h / 2);
}
static void reset(void) {
    oa_game_init(&g);
    memset(&pointer, 0, sizeof(pointer));
}
static void save_capture(const char *folder, const char *name) {
    if (!folder)
        return;
    OaOverlay o = {0};
    if (g.state == OA_CALIBRATION) {
        o.cross_x = 28;
        o.cross_y = 72;
        strcpy(o.lines[0], "MAINTENIR SUR LA CROIX 0.2 S");
        strcpy(o.lines[1], "POINT 1/5 : MESURES 0/4");
    }
    for (int y = 0; y < 240; y += 8)
        oa_render_band(&g, &o, y, image + y * 320);
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%s.ppm", folder, name);
    CHECK(n > 0 && (size_t)n < sizeof(path));
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    fprintf(f, "P6\n320 240\n255\n");
    for (unsigned i = 0; i < 320 * 240; i++) {
        uint16_t v = image[i];
        uint8_t rgb[3] = {(uint8_t)(((v >> 11) & 31) * 255 / 31),
                          (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31)};
        if (fwrite(rgb, 1, 3, f) != 3) {
            fclose(f);
            exit(1);
        }
    }
    CHECK(fclose(f) == 0);
}
int main(int argc, char **argv) {
    const char *captures = argc > 1 ? argv[1] : NULL;
    scenarios++;
    reset();
    save_capture(captures, "ui_menu");
    event(100, 80, true, true);
    CHECK(g.state == OA_MENU); /* Validation au relâchement. */
    event(0, 0, false, true);
    CHECK(g.state == OA_PLAY);

    scenarios++;
    reset();
    event(100, 80, true, true);
    event(10, 10, true, true);
    event(0, 0, false, true);
    CHECK(g.state == OA_MENU);
    event(100, 80, true, true);
    event(0, 0, false, false);
    CHECK(g.state == OA_MENU);

    scenarios++;
    reset();
    click(OA_UI_SETTINGS);
    CHECK(g.state == OA_SETTINGS);
    OaSettings before = g.settings;
    click(OA_UI_MODE_TILT);
    CHECK(g.settings.control_mode == 0 && g.settings_draft.control_mode == 1);
    save_capture(captures, "ui_reglages_commandes");
    click(OA_UI_BACK);
    CHECK(g.state == OA_MENU);
    CHECK(memcmp(&g.settings, &before, sizeof(before)) == 0);
    CHECK(!g.save_requested);

    scenarios++;
    click(OA_UI_SETTINGS);
    click(OA_UI_MODE_TILT);
    click(OA_UI_TAB_TILT);
    uint16_t old = g.settings_draft.tilt_full_mg;
    click(OA_UI_INC0);
    CHECK(g.settings_draft.tilt_full_mg < old);
    click(OA_UI_DEC0);
    CHECK(g.settings_draft.tilt_full_mg == old);
    click(OA_UI_INC2);
    CHECK(g.settings_draft.invert == 1);
    save_capture(captures, "ui_reglages_inclinaison");
    click(OA_UI_APPLY);
    CHECK(g.state == OA_MENU && g.settings.control_mode == 1 && g.settings.invert == 1);
    CHECK(g.save_requested);

    scenarios++;
    click(OA_UI_SETTINGS);
    click(OA_UI_TAB_TOUCH);
    click(OA_UI_INC0);
    click(OA_UI_INC1);
    CHECK(g.settings_draft.touch_split == 112 && g.settings_draft.gesture_px == 30);
    save_capture(captures, "ui_reglages_tactile");
    click(OA_UI_CAL_TOUCH);
    CHECK(g.state == OA_CALIBRATION && !g.calibration_tilt && g.calibration_return == OA_SETTINGS);
    save_capture(captures, "ui_etalonnage");
    OaSettings calibrated = g.settings_draft;
    calibrated.calibrated = 1;
    calibrated.affine[0] = OA_ONE / 10;
    oa_ui_end_calibration(&g, &calibrated, OA_UI_NOTICE_CAL_READY);
    CHECK(g.state == OA_SETTINGS && g.settings.calibrated == 0 && g.settings_draft.calibrated == 1);
    click(OA_UI_BACK);
    CHECK(g.settings.calibrated == 0); /* Annuler annule aussi le calibrage du brouillon. */

    scenarios++;
    reset();
    click(OA_UI_NEW);
    for (unsigned i = 0; i < 4; i++)
        oa_game_step(&g, &(OaInput){0});
    event(140, 110, true, true);
    CHECK(g.state == OA_PAUSE);
    int32_t x = g.player.body.x, y = g.player.body.y;
    uint32_t tick = g.tick;
    uint16_t enemy = g.enemies[0].clock;
    event(0, 0, false, true);
    CHECK(g.state == OA_PAUSE); /* Pas de clic traversant vers REPRENDRE. */
    for (unsigned i = 0; i < 120; i++)
        oa_game_step(&g, &(OaInput){0});
    CHECK(g.tick == tick && g.player.body.x == x && g.player.body.y == y &&
          g.enemies[0].clock == enemy);
    save_capture(captures, "ui_pause");
    click(OA_UI_RESUME);
    CHECK(g.state == OA_PLAY);

    scenarios++;
    event(140, 110, true, true);
    event(0, 0, false, true);
    click(OA_UI_MAIN);
    CHECK(g.state == OA_MENU && g.resume_available);
    x = g.player.body.x;
    click(OA_UI_CONTINUE);
    CHECK(g.state == OA_PLAY && g.player.body.x == x);

    scenarios++;
    reset();
    click(OA_UI_NEW);
    oa_game_step(&g, &(OaInput){0});
    event(140, 215, true, true);
    CHECK(g.state == OA_PLAY);
    event(140, 175, true, true);
    CHECK(g.state == OA_PLAY && g.player.body.vy < 0); /* Geste de saut hors bouton capturé. */
    event(0, 0, false, true);
    event(0, 215, true, true);
    CHECK(g.state == OA_PAUSE); /* Marge hors des boutons. */

    scenarios++;
    reset();
    g.settings.control_mode = 1;
    click(OA_UI_NEW);
    event(180, 215, true, true);
    CHECK(g.state == OA_PLAY);
    event(0, 0, false, true);
    event(30, 215, true, true);
    CHECK(g.state == OA_PAUSE); /* Zone inclinaison sans bouton. */

    scenarios++;
    reset();
    for (unsigned i = 0; i < 90; i++)
        event(15, 15, true, true);
    CHECK(g.state == OA_CALIBRATION && g.calibration_return == OA_MENU);
    event(0, 0, false, true);
    for (unsigned i = 0; i < 90; i++)
        event(15, 15, true, true);
    CHECK(g.state == OA_MENU && g.ui_notice == OA_UI_NOTICE_CAL_CANCELLED);

    scenarios++;
    reset();
    click(OA_UI_DIAGNOSTIC);
    click(OA_UI_BACK);
    CHECK(g.state == OA_MENU);
    click(OA_UI_SETTINGS);
    click(OA_UI_CAL_TILT);
    CHECK(g.state == OA_CALIBRATION && g.calibration_tilt);
    click(OA_UI_CAL_CANCEL);
    CHECK(g.state == OA_SETTINGS);

    scenarios++;
    reset();
    g.state = OA_GAMEOVER;
    click(OA_UI_MAIN);
    CHECK(g.state == OA_MENU && !g.resume_available);
    g.state = OA_VICTORY;
    click(OA_UI_MAIN);
    CHECK(g.state == OA_MENU && !g.resume_available);

    scenarios++;
    reset();
    g.state = OA_PLAY;
    g.player.invulnerable = 0;
    uint8_t splits[] = {64, 96, 112};
    for (unsigned mode = 0; mode < 2; mode++)
        for (unsigned j = 0; j < 3; j++) {
            g.settings.control_mode = (uint8_t)mode;
            g.settings.touch_split = splits[j];
            OaRect r;
            CHECK(oa_ui_control_rect(&g.settings, 3, &r));
            for (int yy = 0; yy < 240; yy += 8)
                oa_render_band(&g, NULL, yy, image + yy * 320);
            int minx = 320, miny = 240, maxx = -1, maxy = -1;
            for (int yy = r.y; yy < r.y + r.h; yy++)
                for (int xx = r.x; xx < r.x + r.w; xx++)
                    if (image[yy * 320 + xx] == 0xFEAB) {
                        if (xx < minx)
                            minx = xx;
                        if (xx > maxx)
                            maxx = xx;
                        if (yy < miny)
                            miny = yy;
                        if (yy > maxy)
                            maxy = yy;
                    }
            CHECK(maxx >= minx && maxy >= miny);
            CHECK(abs((minx + maxx) - (r.x * 2 + r.w - 1)) <= 1);
            CHECK(abs((miny + maxy) - (r.y * 2 + r.h - 1)) <= 1);
        }
    g.settings.control_mode = 0;
    g.settings.touch_split = 96;
    save_capture(captures, "ui_saut_centre");
    printf("test_ui : %u scenarios, %u verifications reussies (navigation tactile, brouillon des "
           "reglages, pause, appui long, centrage RGB565)\n",
           scenarios, checks);
    return 0;
}
