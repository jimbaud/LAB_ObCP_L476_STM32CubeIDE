/** @file oa_ui.c @brief Navigation tactile, brouillon de réglages et pause explicite.
 * SPDX-License-Identifier: MIT */
#include "oa_ui.h"
#include <stdio.h>
bool oa_ui_contains(OaRect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
static void notice(OaGame *g, OaUiNotice n) {
    g->ui_notice = (uint8_t)n;
    g->ui_notice_ticks = 180;
}
static void reset_pointer(OaGame *g) {
    g->ui_pressed = 0;
    g->ui_blocked = false;
}
bool oa_ui_control_rect(const OaSettings *s, unsigned n, OaRect *r) {
    if (n < 1 || n > 3)
        return false;
    if (s->control_mode) {
        if (n != 3)
            return false;
        *r = (OaRect){164, 196, 153, 39};
        return true;
    }
    int split = s->touch_split;
    *r = n == 1
             ? (OaRect){3, 196, (int16_t)(split - 6), 39}
             : (n == 2 ? (OaRect){(int16_t)(split + 3), 196, (int16_t)(split - 6), 39}
                       : (OaRect){(int16_t)(split * 2 + 3), 196, (int16_t)(317 - split * 2), 39});
    return true;
}
unsigned oa_ui_control_at(const OaSettings *s, int x, int y) {
    for (unsigned i = 1; i <= 3; i++) {
        OaRect r;
        if (oa_ui_control_rect(s, i, &r) && oa_ui_contains(r, x, y))
            return i;
    }
    return 0;
}
bool oa_ui_rect(const OaGame *g, OaUiId id, OaRect *r) {
    if ((g->state == OA_GAMEOVER || g->state == OA_VICTORY) && id == OA_UI_MAIN) {
        *r = (OaRect){48, 184, 224, 42};
        return true;
    }
    if (g->state == OA_MENU && id >= OA_UI_NEW && id <= OA_UI_DIAGNOSTIC) {
        *r = (OaRect){48, (int16_t)(62 + (id - OA_UI_NEW) * 42), 224, 34};
        return true;
    }
    if (g->state == OA_PAUSE && (id == OA_UI_RESUME || id == OA_UI_MAIN)) {
        *r = (OaRect){62, id == OA_UI_RESUME ? 96 : 146, 196, 36};
        return true;
    }
    if (g->state == OA_DIAGNOSTIC) {
        if (id == OA_UI_BACK) {
            *r = (OaRect){8, 204, 144, 32};
            return true;
        }
        if (id == OA_UI_CAL_TOUCH) {
            *r = (OaRect){168, 204, 144, 32};
            return true;
        }
    }
    if (g->state == OA_CALIBRATION && id == OA_UI_CAL_CANCEL) {
        *r = (OaRect){8, 204, 304, 32};
        return true;
    }
    if (g->state != OA_SETTINGS)
        return false;
    if (id >= OA_UI_TAB_MODE && id <= OA_UI_TAB_TOUCH) {
        *r = (OaRect){(int16_t)(8 + (id - OA_UI_TAB_MODE) * 104), 28, 96, 32};
        return true;
    }
    if (id == OA_UI_BACK || id == OA_UI_APPLY) {
        *r = (OaRect){id == OA_UI_BACK ? 8 : 168, 204, 144, 32};
        return true;
    }
    if (g->settings_page == 0) {
        if (id == OA_UI_MODE_TOUCH || id == OA_UI_MODE_TILT) {
            *r = (OaRect){id == OA_UI_MODE_TOUCH ? 8 : 168, 80, 144, 34};
            return true;
        }
        if (id == OA_UI_CAL_TOUCH || id == OA_UI_CAL_TILT) {
            *r = (OaRect){id == OA_UI_CAL_TOUCH ? 8 : 168, 139, 144, 36};
            return true;
        }
    } else {
        if (id >= OA_UI_DEC0 && id <= OA_UI_INC2) {
            unsigned row = (unsigned)(id - OA_UI_DEC0) / 2;
            if (g->settings_page == 2 && row == 2)
                return false;
            *r = (OaRect){(id - OA_UI_DEC0) % 2 ? 272 : 8, (int16_t)(78 + row * 45), 40, 30};
            return true;
        }
        if (g->settings_page == 2 && id == OA_UI_CAL_TOUCH) {
            *r = (OaRect){56, 168, 208, 30};
            return true;
        }
    }
    return false;
}
void oa_ui_open_settings(OaGame *g) {
    g->settings_draft = g->settings;
    g->settings_page = 0;
    g->setting_item = 0;
    g->state = OA_SETTINGS;
    reset_pointer(g);
}
void oa_ui_open_menu(OaGame *g) {
    if (g->state == OA_GAMEOVER || g->state == OA_VICTORY) {
        g->resume_available = false;
        g->lives = 3;
    }
    if (g->state == OA_PLAY || g->state == OA_PAUSE) {
        g->resume_available = true;
        g->save_requested = true;
    }
    g->state = OA_MENU;
    reset_pointer(g);
}
void oa_ui_calibrate(OaGame *g, bool tilt, OaState back) {
    g->calibration_tilt = tilt;
    g->calibration_return = back;
    g->state = OA_CALIBRATION;
    reset_pointer(g);
}
void oa_ui_end_calibration(OaGame *g, const OaSettings *s, OaUiNotice n) {
    OaState back = g->calibration_return;
    if (back != OA_SETTINGS && back != OA_DIAGNOSTIC)
        back = OA_MENU;
    if (s) {
        if (back == OA_SETTINGS)
            g->settings_draft = *s;
        else {
            g->settings = *s;
            g->save_requested = true;
        }
    }
    g->state = back;
    if (back == OA_SETTINGS)
        g->settings_page = 0;
    notice(g, n);
    reset_pointer(g);
}
const char *oa_ui_main_label(const OaGame *g, unsigned i) {
    static const char *labels[] = {"NOUVELLE PARTIE", "CONTINUER", "REGLAGES",
                                   "ECRAN / DIAGNOSTIC"};
    return i == 1 && g->resume_available ? "REPRENDRE LA PARTIE" : labels[i < 4 ? i : 0];
}
static unsigned nearest(const uint16_t *values, unsigned count, unsigned value) {
    unsigned best = 0, distance = UINT16_MAX;
    for (unsigned i = 0; i < count; i++) {
        unsigned d = value > values[i] ? value - values[i] : values[i] - value;
        if (d < distance) {
            distance = d;
            best = i;
        }
    }
    return best;
}
static void adjust(OaGame *g, unsigned row, int direction) {
    OaSettings *s = &g->settings_draft;
    static const uint16_t sensitivity[] = {600, 350, 200}, deadzone[] = {25, 50, 100},
                          size[] = {64, 96, 112}, gesture[] = {12, 18, 30};
    if (g->settings_page == 1) {
        if (row == 0)
            s->tilt_full_mg =
                sensitivity[(nearest(sensitivity, 3, s->tilt_full_mg) + 3 + direction) % 3];
        if (row == 1)
            s->tilt_dead_mg = deadzone[(nearest(deadzone, 3, s->tilt_dead_mg) + 3 + direction) % 3];
        if (row == 2) {
            unsigned axis = (unsigned)((s->tilt_axis * 2 + s->invert + 6 + direction) % 6);
            s->tilt_axis = (uint8_t)(axis / 2);
            s->invert = (uint8_t)(axis % 2);
        }
    } else if (g->settings_page == 2) {
        if (row == 0)
            s->touch_split = (uint8_t)size[(nearest(size, 3, s->touch_split) + 3 + direction) % 3];
        if (row == 1)
            s->gesture_px =
                (uint8_t)gesture[(nearest(gesture, 3, s->gesture_px) + 3 + direction) % 3];
    }
    if (s->tilt_full_mg <= s->tilt_dead_mg + 50)
        s->tilt_full_mg = (uint16_t)(s->tilt_dead_mg + 100);
}
void oa_ui_setting_value(const OaGame *g, unsigned row, char *out, size_t cap) {
    const OaSettings *s = &g->settings_draft;
    const char *label = "";
    if (g->settings_page == 1) {
        if (row == 0)
            label =
                s->tilt_full_mg >= 500 ? "DOUCE" : (s->tilt_full_mg <= 250 ? "VIVE" : "NORMALE");
        if (row == 1) {
            (void)snprintf(out, cap, "%s (%u MG)",
                           s->tilt_dead_mg < 40 ? "FAIBLE"
                                                : (s->tilt_dead_mg < 80 ? "NORMALE" : "FORTE"),
                           s->tilt_dead_mg);
            return;
        }
        if (row == 2) {
            (void)snprintf(out, cap, "%c / %s", 'X' + s->tilt_axis,
                           s->invert ? "INVERSE" : "NORMAL");
            return;
        }
    } else {
        if (row == 0)
            label =
                s->touch_split < 80 ? "PETITES" : (s->touch_split < 105 ? "MOYENNES" : "GRANDES");
        if (row == 1)
            label = s->gesture_px < 16 ? "COURT" : (s->gesture_px < 25 ? "NORMAL" : "LONG");
    }
    (void)snprintf(out, cap, "%s", label);
}
static void activate(OaGame *g, OaUiId id) {
    if (id >= OA_UI_TAB_MODE && id <= OA_UI_TAB_TOUCH) {
        g->settings_page = (uint8_t)(id - OA_UI_TAB_MODE);
        g->setting_item = 0;
        return;
    }
    if (id >= OA_UI_DEC0 && id <= OA_UI_INC2) {
        g->setting_item = (uint8_t)((id - OA_UI_DEC0) / 2);
        adjust(g, g->setting_item, (id - OA_UI_DEC0) % 2 ? 1 : -1);
        return;
    }
    switch (id) {
    case OA_UI_NEW:
        g->lives = 3;
        g->score = 0;
        g->coins = 0;
        (void)oa_game_load(g, 0, false);
        g->resume_available = true;
        break;
    case OA_UI_CONTINUE:
        if (g->resume_available)
            g->state = OA_PLAY;
        else {
            g->lives = g->lives ? g->lives : 3;
            (void)oa_game_load(g, g->level, true);
            g->resume_available = true;
        }
        break;
    case OA_UI_SETTINGS:
        oa_ui_open_settings(g);
        break;
    case OA_UI_DIAGNOSTIC:
        g->state = OA_DIAGNOSTIC;
        break;
    case OA_UI_BACK:
        if (g->state == OA_SETTINGS)
            notice(g, OA_UI_NOTICE_CANCELLED);
        oa_ui_open_menu(g);
        break;
    case OA_UI_APPLY:
        if (oa_settings_valid(&g->settings_draft)) {
            g->settings = g->settings_draft;
            g->save_requested = true;
            oa_ui_open_menu(g);
            notice(g, OA_UI_NOTICE_SAVED);
        }
        break;
    case OA_UI_MODE_TOUCH:
        g->settings_draft.control_mode = 0;
        break;
    case OA_UI_MODE_TILT:
        g->settings_draft.control_mode = 1;
        break;
    case OA_UI_CAL_TOUCH:
        oa_ui_calibrate(g, false, g->state);
        break;
    case OA_UI_CAL_TILT:
        oa_ui_calibrate(g, true, g->state);
        break;
    case OA_UI_RESUME:
        g->state = OA_PLAY;
        break;
    case OA_UI_MAIN:
        oa_ui_open_menu(g);
        break;
    case OA_UI_CAL_CANCEL:
        oa_ui_end_calibration(g, NULL, OA_UI_NOTICE_CAL_CANCELLED);
        break;
    default:
        break;
    }
    reset_pointer(g);
}
bool oa_ui_step(OaGame *g, const OaInput *in) {
    if (g->ui_notice_ticks && --g->ui_notice_ticks == 0)
        g->ui_notice = OA_UI_NOTICE_NONE;
    if (g->state == OA_PLAY) {
        if (in->pause ||
            (in->pointer_pressed && oa_ui_control_at(&g->settings, in->x, in->y) == 0)) {
            g->state = OA_PAUSE;
            g->menu_item = 0;
            g->resume_available = true;
            reset_pointer(g);
            g->ui_blocked = in->pointer_down;
            return true;
        }
        return false;
    }
    if (g->state != OA_MENU && g->state != OA_SETTINGS && g->state != OA_PAUSE &&
        g->state != OA_DIAGNOSTIC && g->state != OA_CALIBRATION && g->state != OA_GAMEOVER &&
        g->state != OA_VICTORY)
        return false;
    if (in->pointer_long) {
        if (g->state == OA_MENU || g->state == OA_DIAGNOSTIC) {
            oa_ui_calibrate(g, false, g->state);
            return true;
        }
        if (g->state == OA_CALIBRATION) {
            activate(g, OA_UI_CAL_CANCEL);
            return true;
        }
    }
    if (in->back || in->pause) {
        if (g->state == OA_PAUSE)
            activate(g, OA_UI_RESUME);
        else if (g->state == OA_CALIBRATION)
            activate(g, OA_UI_CAL_CANCEL);
        else if (g->state == OA_GAMEOVER || g->state == OA_VICTORY)
            activate(g, OA_UI_MAIN);
        else if (g->state != OA_MENU)
            activate(g, OA_UI_BACK);
        return true;
    }
    if (g->ui_blocked) {
        if (!in->pointer_down)
            g->ui_blocked = false;
        return true;
    }
    if (in->ui_next_page && g->state == OA_SETTINGS) {
        g->settings_page = (uint8_t)((g->settings_page + 1) % 3);
        g->setting_item = 0;
    }
    if (in->menu_delta) {
        if (g->state == OA_MENU)
            g->menu_item = (uint8_t)((g->menu_item + 4 + in->menu_delta) % 4);
        if (g->state == OA_PAUSE)
            g->menu_item ^= 1;
        if (g->state == OA_SETTINGS)
            g->setting_item = (uint8_t)((g->setting_item + 3 + in->menu_delta) % 3);
    }
    if (g->state == OA_SETTINGS && in->ui_adjust)
        adjust(g, g->setting_item, in->ui_adjust);
    if (in->confirm) {
        if (g->state == OA_MENU)
            activate(g, (OaUiId)(OA_UI_NEW + g->menu_item));
        else if (g->state == OA_PAUSE)
            activate(g, g->menu_item ? OA_UI_MAIN : OA_UI_RESUME);
        else if (g->state == OA_DIAGNOSTIC)
            activate(g, OA_UI_BACK);
        else if (g->state == OA_GAMEOVER || g->state == OA_VICTORY)
            activate(g, OA_UI_MAIN);
        else if (g->state == OA_SETTINGS && g->settings_page)
            adjust(g, g->setting_item, 1);
        return true;
    }
    if (in->pointer_cancelled) {
        g->ui_pressed = 0;
        return true;
    }
    if (in->pointer_pressed) {
        g->ui_pressed = 0;
        for (unsigned id = 1; id <= OA_UI_CAL_CANCEL; id++) {
            OaRect r;
            if (g->state == OA_CALIBRATION && !g->calibration_tilt)
                break; /* Coordonnées non fiables : appui long. */
            if (oa_ui_rect(g, (OaUiId)id, &r) && oa_ui_contains(r, in->x, in->y)) {
                g->ui_pressed = (uint8_t)id;
                break;
            }
        }
    }
    if (in->pointer_released && g->ui_pressed) {
        OaUiId id = (OaUiId)g->ui_pressed;
        OaRect r;
        g->ui_pressed = 0;
        if (oa_ui_rect(g, id, &r) && oa_ui_contains(r, in->x, in->y))
            activate(g, id);
    }
    return true;
}
