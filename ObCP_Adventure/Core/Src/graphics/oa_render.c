/** @file oa_render.c
 * @brief Composition CPU : ciel, tuiles, objets, sprites puis interface.
 * @details Toutes les primitives sont découpées par rapport à une bande de
 * huit lignes. L'adresse de destination ne peut jamais sortir du tampon.
 * SPDX-License-Identifier: MIT
 */
#include "oa_render.h"
#include "oa_assets.h"
#include "oa_ui.h"
#include <stdio.h>
#include <string.h>
typedef struct {
    uint16_t *p;
    int y;
    uint16_t pal[16];
} Canvas;
static uint16_t rgb(unsigned r, unsigned g, unsigned b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static void palette(Canvas *c, unsigned biome) {
    static const uint32_t colors[16] = {0,        0x172A38, 0xFFF1CD, 0x5F8CAE, 0x297F86, 0x77BF59,
                                        0xB46B3F, 0xFFD65C, 0xF26464, 0x9371C8, 0x82DED0, 0xB7C6CF,
                                        0xF1A3AE, 0x30445F, 0x71808C, 0x080F20};
    for (unsigned i = 0; i < 16; i++)
        c->pal[i] = rgb(colors[i] >> 16, (colors[i] >> 8) & 255, colors[i] & 255);
    if (biome == 1) {
        c->pal[5] = rgb(185, 141, 79);
        c->pal[6] = rgb(141, 80, 54);
    }
    if (biome == 2) {
        c->pal[5] = rgb(84, 180, 149);
        c->pal[6] = rgb(71, 106, 117);
    }
    if (biome == 3) {
        c->pal[5] = rgb(235, 139, 77);
        c->pal[6] = rgb(103, 72, 87);
    }
    if (biome == 4) {
        c->pal[5] = rgb(137, 148, 202);
        c->pal[6] = rgb(65, 76, 117);
    }
}
static void rect(Canvas *c, int x, int y, int w, int h, uint16_t color) {
    int x0 = x < 0 ? 0 : x, x1 = x + w > 320 ? 320 : x + w, y0 = y < c->y ? c->y : y,
        y1 = y + h > c->y + 8 ? c->y + 8 : y + h;
    for (int Y = y0; Y < y1; Y++)
        for (int X = x0; X < x1; X++)
            c->p[(Y - c->y) * 320 + X] = color;
}
static void text(Canvas *c, int x, int y, const char *s, int scale, uint16_t color) {
    for (; *s; s++, x += 6 * scale) {
        unsigned a = (unsigned char)*s;
        if (a >= 'a' && a <= 'z')
            a -= 32;
        if (a < 32 || a > 95)
            a = '?';
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (oa_font[a - 32][row] & (1u << (4 - col)))
                    rect(c, x + col * scale, y + row * scale, scale, scale, color);
    }
}
static void sprite(Canvas *c, unsigned id, int x, int y, bool flip) {
    const OaSprite *s = &oa_sprites[id];
    int y0 = y < c->y ? c->y - y : 0, y1 = y + s->h > c->y + 8 ? c->y + 8 - y : s->h;
    for (int Y = y0; Y < y1; Y++)
        for (int X = 0; X < s->w; X++) {
            int dx = x + X;
            if (dx < 0 || dx >= 320)
                continue;
            unsigned index = (unsigned)(Y * s->w + (flip ? s->w - 1 - X : X));
            uint8_t p = s->data[index / 2];
            unsigned color = index % 2 ? p & 15 : p >> 4;
            if (color)
                c->p[(y + Y - c->y) * 320 + dx] = c->pal[color];
        }
}
static bool collected(const OaGame *g, unsigned i) {
    return (g->collected[i / 8] & (1u << (i % 8))) != 0;
}
static void world(Canvas *c, const OaGame *g) {
    const OaLevel *l = &oa_levels[g->level];
    int cam = oa_pixel(g->camera);
    uint16_t sky = l->biome == 4
                       ? rgb(24, 30, 65)
                       : rgb(130 - l->biome * 14, 185 - l->biome * 15, 205 - l->biome * 7);
    rect(c, 0, 16, 320, 176, sky);
    /* Quelques silhouettes à parallaxe lente gardent un ciel lisible et bornent
     * le trafic. Les coordonnées sont déterministes ; aucun hasard au rendu. */
    for (int i = 0; i < 7; i++) {
        int x = i * 83 - (cam / 4) % 83 - 45;
        rect(c, x, 52 + (i % 3) * 18, 36, 7, l->biome == 4 ? rgb(50, 61, 94) : rgb(201, 223, 219));
        rect(c, x + 8, 46 + (i % 3) * 18, 21, 7,
             l->biome == 4 ? rgb(50, 61, 94) : rgb(201, 223, 219));
    }
    /* Silhouettes originales propres aux cinq environnements. Ces formes
     * de couleur unie gardent un coût mémoire nul et se recomposent comme
     * n'importe quel fond : elles ne participent jamais aux collisions. */
    for (int i = 0; i < 5; i++) {
        int x = i * 105 - (cam / 3) % 105 - 35;
        uint16_t far = l->biome == 4
                           ? rgb(42, 47, 86)
                           : rgb(84 - l->biome * 5, 145 - l->biome * 8, 153 - l->biome * 4);
        if (l->biome == 0 || l->biome == 2) {
            for (int row = 0; row < 8; row++)
                rect(c, x + row * 4, 148 - row * 5, 100 - row * 8, 6, far);
            rect(c, x + 45, 135, 5, 42, c->pal[4]);
            rect(c, x + 29, 130, 37, 9, c->pal[5]);
            rect(c, x + 35, 121, 25, 10, c->pal[5]);
        } else if (l->biome == 1) {
            rect(c, x + 17, 28, 17, 28, far);
            rect(c, x + 22, 54, 7, 17, far);
            rect(c, x + 64, 134, 25, 44, far);
            rect(c, x + 70, 120, 13, 20, far);
        } else if (l->biome == 3) {
            rect(c, x + 15, 105, 38, 73, far);
            rect(c, x + 22, 78, 9, 30, far);
            rect(c, x + 39, 93, 8, 17, far);
            rect(c, x + 24, 116, 7, 7, c->pal[7]);
        } else {
            rect(c, x + 25, 142, 34, 35, far);
            rect(c, x + 31, 135, 22, 9, far);
            rect(c, x + 36, 125, 12, 12, far);
            rect(c, x + 40, 115, 4, 12, c->pal[11]);
            rect(c, x + 58, 40 + i * 9, 2, 2, c->pal[2]);
        }
    }
    int start = cam / 16;
    if (start < 0)
        start = 0;
    for (int row = 0; row < OA_ROWS; row++) {
        int y = 16 + row * 16;
        if (y + 16 <= c->y || y >= c->y + 8)
            continue;
        for (int col = start; col < start + 21 && col < l->width; col++) {
            unsigned t = g->tiles[row * l->width + col];
            if (t)
                sprite(c, OA_ASSET_TILE_0 + t, col * 16 - cam, y, false);
        }
    }
    for (unsigned i = 0; i < g->platform_count; i++) {
        const OaPlatform *p = &g->platforms[i];
        int x = oa_pixel(p->x) - cam, y = 16 + oa_pixel(p->y);
        rect(c, x, y, 32, 5, c->pal[11]);
        rect(c, x + 2, y + 5, 28, 3, c->pal[4]);
    }
    for (unsigned i = 0; i < l->count; i++) {
        const OaObject *o = &l->objects[i];
        if (o->kind >= OA_PLATFORM || (collected(g, i) && o->kind != OA_CHECKPOINT))
            continue;
        int x = o->x - cam, y = 16 + o->y;
        if (o->kind == OA_COIN) {
            int w = (g->tick / 8) % 2 ? 7 : 3;
            rect(c, x + 7 - w / 2, y + 2, w, 11, c->pal[7]);
            rect(c, x + 7 - w / 2, y + 3, 1, 7, c->pal[2]);
        } else if (o->kind == OA_CHECKPOINT) {
            rect(c, x + 2, y - 14, 2, 30, c->pal[11]);
            rect(c, x + 4, y - 14, 12, 9, c->pal[collected(g, i) ? 7 : 10]);
        } else if (o->kind == OA_PORTAL || o->kind == OA_RETURN) {
            rect(c, x, y - 8, 16, 24, c->pal[13]);
            rect(c, x + 4, y - 4, 8, 20, c->pal[9]);
            rect(c, x + 6, y + 1, 4, 9, c->pal[10]);
        } else {
            rect(c, x + 2, y + 2, 12, 12, c->pal[o->kind == OA_HEART ? 8 : 10]);
            text(c, x + 5, y + 5, o->kind == OA_BOOTS ? "B" : (o->kind == OA_HEART ? "+" : "S"), 1,
                 c->pal[1]);
        }
    }
    for (unsigned i = 0; i < g->enemy_count; i++) {
        const OaEnemy *e = &g->enemies[i];
        if (!e->alive || (e->phase && (g->tick / 4) % 2))
            continue;
        unsigned id = OA_ASSET_WALKER_0 + (e->kind - OA_WALKER) * 2 + (g->tick / 15) % 2;
        if (e->kind == OA_BOSS)
            id = OA_ASSET_BOSS_0 + (e->clock % 240 >= 120 && e->clock % 240 < 150);
        sprite(c, id, oa_pixel(e->body.x) - cam, 16 + oa_pixel(e->body.y), e->direction > 0);
        if (e->kind == OA_BOSS) {
            rect(c, 95, 20, 130, 5, c->pal[1]);
            rect(c, 96, 21, e->hp * 20, 3, c->pal[8]);
        }
    }
    for (unsigned i = 0; i < OA_MAX_SHOTS; i++)
        if (g->shots[i].life)
            rect(c, oa_pixel(g->shots[i].x) - cam, 16 + oa_pixel(g->shots[i].y), 6, 6, c->pal[8]);
    const OaPlayer *p = &g->player;
    int px = oa_pixel(p->body.x) - cam, py = 16 + oa_pixel(p->body.y);
    if (p->shield) {
        rect(c, px - 3, py - 3, 18, 2, c->pal[10]);
        rect(c, px - 3, py + 23, 18, 2, c->pal[10]);
    }
    if (!p->invulnerable || (g->tick / 5) % 2) {
        unsigned f = !p->body.ground ? 4 : (p->body.vx ? (g->tick / 6) % 4 : 0);
        sprite(c, OA_ASSET_ROBOT_0 + f, px - 2, py - 2, p->facing < 0);
    }
    int ex = l->exit_x - cam;
    rect(c, ex, 114, 3, 62, c->pal[11]);
    rect(c, ex + 3, 114, 18, 12, c->pal[7]);
}
static void centered(Canvas *c, OaRect r, const char *label, int scale, uint16_t color) {
    int width = (*label ? (int)strlen(label) * 6 - 1 : 0) * scale;
    text(c, r.x + (r.w - width) / 2, r.y + (r.h - 7 * scale) / 2, label, scale, color);
}
static void button(Canvas *c, const OaGame *g, OaUiId id, const char *label, int scale,
                   bool selected) {
    OaRect r;
    if (!oa_ui_rect(g, id, &r))
        return;
    uint16_t fill = selected || g->ui_pressed == id ? rgb(53, 101, 115) : rgb(34, 60, 77);
    rect(c, r.x, r.y, r.w, r.h, fill);
    if (g->ui_pressed == id)
        rect(c, r.x, r.y + r.h - 3, r.w, 3, c->pal[7]);
    centered(c, r, label, scale, c->pal[2]);
}
static const char *notice_text(const OaGame *g) {
    switch (g->ui_notice) {
    case OA_UI_NOTICE_SAVED:
        return "REGLAGES ENREGISTRES";
    case OA_UI_NOTICE_CANCELLED:
        return "MODIFICATIONS ANNULEES";
    case OA_UI_NOTICE_CAL_READY:
        return "CALIBRAGE PRET : ENREGISTRER";
    case OA_UI_NOTICE_CAL_SAVED:
        return "CALIBRAGE ENREGISTRE";
    case OA_UI_NOTICE_CAL_CANCELLED:
        return "ETALONNAGE ANNULE";
    case OA_UI_NOTICE_NATIVE:
        return "ECRAN CAPACITIF : DEJA ETALONNE";
    case OA_UI_NOTICE_CAL_ERROR:
        return "ECHEC : ANCIENS REGLAGES CONSERVES";
    case OA_UI_NOTICE_CAL_TIMEOUT:
        return "DELAI DEPASSE : REESSAYER";
    case OA_UI_NOTICE_CAL_NO_SENSOR:
        return "CAPTEUR INDISPONIBLE";
    default:
        return "";
    }
}
static void menu(Canvas *c, const OaGame *g, const OaOverlay *o) {
    rect(c, 0, 0, 320, 240, rgb(15, 29, 45));
    centered(c, (OaRect){0, 6, 320, 32}, "OBCP ADVENTURE", 3, c->pal[7]);
    centered(c, (OaRect){0, 39, 320, 18}, "L ATELIER DES CAPTEURS", 1, c->pal[10]);
    for (unsigned i = 0; i < 4; i++)
        button(c, g, (OaUiId)(OA_UI_NEW + i), oa_ui_main_label(g, i), 2, g->menu_item == i);
    const char *footer = notice_text(g);
    if (g->ui_notice == OA_UI_NOTICE_SAVED && o) {
        if (o->save_error)
            footer = "APPLIQUES / SAUVEGARDE INDISPONIBLE";
        else if (o->save_pending)
            footer = "APPLIQUES / SAUVEGARDE EN COURS";
    }
    if (!*footer)
        footer = o && o->touch_needs_calibration
                     ? "APPUI LONG 1.5 S : CALIBRER ECRAN"
                     : (g->resume_available ? "PARTIE EN PAUSE / PROGRESSION CONSERVEE"
                                            : "APPUI LONG 1.5 S : REGLER ECRAN");
    centered(c, (OaRect){0, 226, 320, 14}, footer, 1, c->pal[10]);
}
static void settings(Canvas *c, const OaGame *g) {
    rect(c, 0, 0, 320, 240, rgb(15, 29, 45));
    centered(c, (OaRect){0, 0, 320, 26}, "REGLAGES", 2, c->pal[7]);
    button(c, g, OA_UI_TAB_MODE, "COMMANDES", 1, g->settings_page == 0);
    button(c, g, OA_UI_TAB_TILT, "INCLINAISON", 1, g->settings_page == 1);
    button(c, g, OA_UI_TAB_TOUCH, "TACTILE", 1, g->settings_page == 2);
    if (g->settings_page == 0) {
        text(c, 8, 66, "CHOISIR LE MODE DE JEU", 1, c->pal[11]);
        button(c, g, OA_UI_MODE_TOUCH, "TACTILE", 2, g->settings_draft.control_mode == 0);
        button(c, g, OA_UI_MODE_TILT, "INCLINAISON", 2, g->settings_draft.control_mode == 1);
        text(c, 8, 123, "ETALONNER SI NECESSAIRE", 1, c->pal[11]);
        button(c, g, OA_UI_CAL_TOUCH, "ECRAN", 2, false);
        button(c, g, OA_UI_CAL_TILT, "NEUTRE", 2, false);
        centered(c, (OaRect){0, 179, 320, 20},
                 *notice_text(g) ? notice_text(g) : "ENREGISTRER APPLIQUE LES CHANGEMENTS", 1,
                 c->pal[10]);
    } else {
        static const char *tilt_labels[] = {"SENSIBILITE", "ZONE NEUTRE (ANTI-DERIVE)",
                                            "AXE ET SENS DU MOUVEMENT"};
        static const char *touch_labels[] = {"TAILLE DES FLECHES", "GESTE VERTICAL POUR SAUTER",
                                             "PRECISION DE L ECRAN"};
        for (unsigned row = 0; row < 3; row++) {
            text(c, 8, 65 + (int)row * 45,
                 g->settings_page == 1 ? tilt_labels[row] : touch_labels[row], 1, c->pal[11]);
            if (g->settings_page == 2 && row == 2) {
                button(c, g, OA_UI_CAL_TOUCH, "CALIBRER L ECRAN", 1, false);
                continue;
            }
            char value[32];
            oa_ui_setting_value(g, row, value, sizeof(value));
            button(c, g, (OaUiId)(OA_UI_DEC0 + row * 2), "-", 2, false);
            button(c, g, (OaUiId)(OA_UI_INC0 + row * 2), "+", 2, false);
            OaRect area = {56, (int16_t)(78 + row * 45), 208, 30};
            rect(c, area.x, area.y, area.w, area.h,
                 g->setting_item == row ? rgb(30, 63, 78) : rgb(22, 43, 58));
            centered(c, area, value, 1, c->pal[10]);
        }
    }
    button(c, g, OA_UI_BACK, "ANNULER", 2, false);
    button(c, g, OA_UI_APPLY, "ENREGISTRER", 2, false);
}
void oa_render_band(const OaGame *g, const OaOverlay *o, int y, uint16_t pixels[320 * 8]) {
    Canvas c = {pixels, y, {0}};
    palette(&c, oa_levels[g->level].biome);
    rect(&c, 0, y, 320, 8, 0);
    if (g->state == OA_MENU) {
        menu(&c, g, o);
        return;
    }
    if (g->state == OA_SETTINGS) {
        settings(&c, g);
        return;
    }
    if (g->state == OA_CALIBRATION) {
        rect(&c, 0, 0, 320, 240, rgb(15, 29, 45));
        centered(&c, (OaRect){0, 0, 320, 28}, "ETALONNAGE", 2, c.pal[7]);
        if (o) {
            text(&c, 8, 34, o->lines[0], 1, c.pal[2]);
            text(&c, 8, 47, o->lines[1], 1, c.pal[10]);
            rect(&c, o->cross_x - 10, o->cross_y - 1, 21, 3, c.pal[8]);
            rect(&c, o->cross_x - 1, o->cross_y - 10, 3, 21, c.pal[8]);
        }
        button(&c, g, OA_UI_CAL_CANCEL,
               g->calibration_tilt ? "ANNULER" : "ANNULER : APPUI LONG 1.5 S", 1, false);
        if (o && o->long_hold_progress) {
            unsigned progress = o->long_hold_progress > 90 ? 90 : o->long_hold_progress;
            rect(&c, 8, 233, (int)(304 * progress / 90), 3, c.pal[8]);
        }
        if (!g->calibration_tilt)
            centered(&c, (OaRect){45, 187, 230, 13}, "APPUI LONG POSSIBLE PARTOUT", 1, c.pal[11]);
        return;
    }
    if (g->state == OA_DIAGNOSTIC) {
        rect(&c, 0, 0, 320, 240, rgb(15, 29, 45));
        centered(&c, (OaRect){0, 4, 320, 30}, "ECRAN / DIAGNOSTIC", 2, c.pal[7]);
        if (o)
            for (int i = 0; i < 8; i++)
                text(&c, 8, 41 + i * 17, o->lines[i], 1, c.pal[2]);
        button(&c, g, OA_UI_BACK, "MENU", 2, false);
        button(&c, g, OA_UI_CAL_TOUCH, "CALIBRER", 2, false);
        return;
    }
    world(&c, g);
    rect(&c, 0, 0, 320, 16, rgb(10, 20, 33));
    char b[64];
    (void)snprintf(b, sizeof(b), "N%02u V%u P%03u S%06lu", g->level + 1, g->lives, g->coins,
                   (unsigned long)g->score);
    text(&c, 4, 4, b, 1, c.pal[2]);
    text(&c, 283, 4, "PAUSE", 1, c.pal[7]);
    rect(&c, 0, 192, 320, 48, rgb(10, 20, 33));
    bool controls_visible =
        g->state != OA_PAUSE && g->state != OA_GAMEOVER && g->state != OA_VICTORY;
    for (unsigned control = 1; controls_visible && control <= 3; control++) {
        OaRect r;
        if (!oa_ui_control_rect(&g->settings, control, &r))
            continue;
        rect(&c, r.x, r.y, r.w, r.h, control == 3 ? rgb(42, 90, 100) : rgb(26, 55, 72));
        /* Largeur réelle du texte 5x7, intercaractères exclus au dernier bord. */
        centered(&c, r, control == 1 ? "<" : (control == 2 ? ">" : "SAUT"), control == 3 ? 2 : 3,
                 control == 3 ? c.pal[7] : c.pal[2]);
    }
    if (controls_visible && g->settings.control_mode)
        centered(&c, (OaRect){4, 196, 156, 39}, "INCLINAISON", 1, c.pal[10]);
    /* Cadence mesurée (images par seconde) : affichée seulement une fois
     * connue, c'est-à-dire après la première seconde de jeu. */
    if (controls_visible && o && (o->fps10 || o->save_error)) {
        if (o->fps10)
            (void)snprintf(b, sizeof(b), "%u.%u IPS%s", o->fps10 / 10, o->fps10 % 10,
                           o->save_error ? " SAVE!" : "");
        else
            (void)snprintf(b, sizeof(b), "SAVE!");
        text(&c, 220, 183, b, 1, c.pal[2]);
    }
    if (g->state == OA_PAUSE) {
        rect(&c, 32, 42, 256, 154, rgb(14, 31, 47));
        centered(&c, (OaRect){32, 48, 256, 34}, "PAUSE", 2, c.pal[7]);
        button(&c, g, OA_UI_RESUME, "REPRENDRE", 2, g->menu_item == 0);
        button(&c, g, OA_UI_MAIN, "MENU PRINCIPAL", 2, g->menu_item == 1);
        centered(&c, (OaRect){0, 204, 320, 28}, "CHOISIR REPRENDRE OU MENU", 1, c.pal[10]);
        return;
    }
    const char *caption = NULL;
    if (g->state == OA_DEAD)
        caption = "REESSAIE !";
    if (g->state == OA_CLEAR)
        caption = "NIVEAU TERMINE";
    if (g->state == OA_GAMEOVER)
        caption = "PARTIE TERMINEE";
    if (g->state == OA_VICTORY)
        caption = "ATELIER LIBERE !";
    if (caption) {
        rect(&c, 32, 76, 256, 57, rgb(14, 31, 47));
        centered(&c, (OaRect){32, 76, 256, 57}, caption, 2, c.pal[7]);
    }
    if (g->state == OA_GAMEOVER || g->state == OA_VICTORY)
        button(&c, g, OA_UI_MAIN, "MENU PRINCIPAL", 2, false);
}
uint32_t oa_render_hash(const uint16_t *p, unsigned block) {
    uint32_t hash = UINT32_C(2166136261);
    if (block >= 10)
        return 0;
    for (unsigned y = 0; y < 8; y++)
        for (unsigned x = 0; x < 32; x++) {
            hash ^= p[y * 320 + block * 32 + x];
            hash *= UINT32_C(16777619);
        }
    return hash;
}
