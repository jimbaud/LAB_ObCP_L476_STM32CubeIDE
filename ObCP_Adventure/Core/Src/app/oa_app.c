/** @file oa_app.c
 * @brief Ordonnanceur coopératif : entrées 100 Hz, physique 60 Hz, rendu partiel.
 * @details Aucune attente de DMA dans la boucle. Deux buffers de 5 120 octets
 * séparent la composition RGB565 et le flux SPI MSB d'abord. La copie de jeu
 * fige une scène cohérente pendant toute l'image, malgré les pas de physique.
 * SPDX-License-Identifier: MIT
 */
#include "oa_app.h"
#include "oa_board.h"
#include "oa_render.h"
#include "oa_ui.h"
#include "oa_calibration.h"
#include "usb_console.h"
#include <stdio.h>
#include <string.h>
#ifndef OA_TOUCH_FORCE
#define OA_TOUCH_FORCE 0
#endif
static OaGame game, view;
static OaOverlay overlay, view_overlay;
static OaTouch touch;
static OaTouchInput touch_input;
static OaTiltInput tilt;
static OaHoldInput raw_hold;
static OaStorage storage;
static OaIo io;
static uint8_t accel_id, profile;
static uint16_t band[320 * 8];
static uint8_t transmit[320 * 8 * 2];
static uint32_t hashes[30][10], new_hashes[10], pending_hashes[10];
static uint16_t dirty;
static int band_y, pending_y;
static unsigned pending_first, pending_end;
static bool frame_active, band_ready, inflight, lcd_ok, button_event;
static uint32_t last_loop, last_accel, last_frame, last_stats, frames, frame_count, bytes_sent,
    overruns;
static uint64_t debt;
static OaCalibration calibration;
static bool calibration_active, calibration_native;
static OaCalStatus last_cal_status;
static void diagnostic(void) {
    overlay.touch_needs_calibration =
        touch.kind != OA_TOUCH_AUTO && touch.kind != OA_TOUCH_FT && !game.settings.calibrated;
    overlay.save_error = storage.error || !storage.available || !storage.writable;
    if (game.state == OA_CALIBRATION)
        return;
    const char *kind[] = {"NON OPERATIONNEL", "FT62XX IDENTIFIE", "TSC2007 COMPATIBLE",
                          "STMPE610 IDENTIFIE"};
    (void)snprintf(overlay.lines[0], 48, "TACTILE : %s", kind[touch.kind]);
    if (touch.ambiguous)
        (void)snprintf(overlay.lines[0], 48, "PLUSIEURS TACTILES : CHOISIR 1/2/3");
    (void)snprintf(overlay.lines[1], 48, "PREUVES FT:%u TSC:%u STM:%u", touch.proof[1],
                   touch.proof[2], touch.proof[3]);
    (void)snprintf(overlay.lines[2], 48, "ACCEL ID:%02X / LCD:%s", accel_id,
                   lcd_ok ? "COMMANDES OK" : "ERREUR BUS");
    (void)snprintf(overlay.lines[3], 48, "EEPROM:%s / JOURNAL:%s",
                   storage.available ? "REPONSE" : "INDISPONIBLE",
                   storage.writable ? "OUVERT" : "VERROUILLE");
    (void)snprintf(overlay.lines[4], 48, "LCD %s / 2 BANDES DE 8 LIGNES",
                   game.settings.lcd_fast ? "40M EXPERIMENTAL" : "10M");
    (void)snprintf(overlay.lines[5], 48, "1 FT  2 TSC  3 STM  A AUTO");
    (void)snprintf(overlay.lines[6], 48, "C CALIB  N NEUTRE  F VITESSE");
    (void)snprintf(overlay.lines[7], 48, "W INIT JOURNAL / M MENU / P PAUSE");
    if (last_cal_status == OA_CAL_TIMEOUT)
        (void)snprintf(overlay.lines[7], 48, "CALIB : DELAI DEPASSE / C OU N REESSAYER");
    else if (last_cal_status == OA_CAL_UNAVAILABLE)
        (void)snprintf(overlay.lines[7], 48, "CALIB : CAPTEUR INDISPONIBLE / BUS");
    else if (calibration_native)
        (void)snprintf(overlay.lines[7], 48, "FT : COORDONNEES NATIVES / M MENU");
}
static void calibration_task(void) {
    uint32_t now = oa_board_ms();
    if (game.state != OA_CALIBRATION) {
        if (calibration_active)
            (void)oa_cal_watchdog(&calibration, now, true);
        calibration_active = false;
        return;
    }
    if (!calibration_active) {
        calibration_native = !game.calibration_tilt && touch.kind == OA_TOUCH_FT;
        if (calibration_native) {
            oa_ui_end_calibration(&game, NULL, OA_UI_NOTICE_NATIVE);
            last_cal_status = OA_CAL_DONE;
            diagnostic();
            return;
        }
        bool available = game.calibration_tilt ? accel_id != 0 : touch.kind != OA_TOUCH_AUTO;
        const OaSettings *source =
            game.calibration_return == OA_SETTINGS ? &game.settings_draft : &game.settings;
        oa_cal_begin(&calibration, source, game.calibration_tilt, available, now);
        calibration_active = true;
    }
    if (!calibration.tilt)
        oa_cal_touch(&calibration, &touch.sample, now);
    (void)oa_cal_watchdog(&calibration, now, false);
    if (calibration.status != OA_CAL_RUNNING) {
        last_cal_status = calibration.status;
        if (calibration.status == OA_CAL_DONE) {
            /* Publication atomique au niveau applicatif : les cinq points ou
             * les cent mesures sont validés avant de remplacer les réglages. */
            oa_ui_end_calibration(&game, &calibration.candidate,
                                  game.calibration_return == OA_SETTINGS ? OA_UI_NOTICE_CAL_READY
                                                                         : OA_UI_NOTICE_CAL_SAVED);
        } else {
            oa_ui_end_calibration(&game, NULL,
                                  calibration.status == OA_CAL_CANCELLED
                                      ? OA_UI_NOTICE_CAL_CANCELLED
                                      : (calibration.status == OA_CAL_TIMEOUT
                                             ? OA_UI_NOTICE_CAL_TIMEOUT
                                             : OA_UI_NOTICE_CAL_NO_SENSOR));
        }
        calibration_active = false;
        diagnostic();
        return;
    }
    static const char *messages[] = {"RELACHER LE DOIGT",
                                     "MAINTENIR SUR LA CROIX 0.2 S",
                                     "APPUI INSTABLE : GARDER IMMOBILE",
                                     "POINT IMPRECIS : REESSAYER",
                                     "POINTS INVALIDES : RECOMMENCER",
                                     "GARDER LA CARTE IMMOBILE"};
    (void)snprintf(overlay.lines[0], 48, "%s", messages[calibration.notice]);
    if (calibration.tilt) {
        overlay.cross_x = -30;
        overlay.cross_y = -30;
        (void)snprintf(overlay.lines[1], 48, "MESURES : %u / %u", calibration.count,
                       OA_CAL_TILT_SAMPLES);
    } else {
        overlay.cross_x = oa_cal_targets[calibration.point].x;
        overlay.cross_y = oa_cal_targets[calibration.point].y;
        (void)snprintf(overlay.lines[1], 48, "POINT %u/5 : MESURES %u/4", calibration.point + 1,
                       calibration.count);
    }
    (void)snprintf(overlay.lines[2], 48, "RAW %u,%u APPUI:%u VALIDE:%u", touch.sample.raw_x,
                   touch.sample.raw_y, touch.sample.down, touch.sample.valid);
}
/* Aide de la console (commande « ? »), envoyée dès que l'USB est libre. */
static const char help_text[] =
    "\r\nObCP Adventure v" OA_VERSION " - commandes d'une lettre (USB ou USART2) :\r\n"
    "  1 / 2 / 3  profil tactile FT62xx / TSC2007 / STMPE610, A : detection auto\r\n"
    "  C  etalonnage de l'ecran tactile    N  neutre de l'inclinaison\r\n"
    "  M  menu    P  pause    D  diagnostic    F  ecran 10 / 40 MHz (essai)\r\n"
    "  W  autoriser la sauvegarde dans une zone EEPROM deja ecrite\r\n"
    "  ?  cette aide ; une ligne de mesures s'affiche chaque seconde\r\n";
static bool help_pending;
static void command(int ch) {
    if (ch == '?' || ch == 'h' || ch == 'H')
        help_pending = true;
    if (ch == 'm' || ch == 'M')
        oa_ui_open_menu(&game);
    if (ch == 'p' || ch == 'P')
        button_event = true;
    if (ch == 'd' || ch == 'D')
        game.state = OA_DIAGNOSTIC;
    if (ch == 'c' || ch == 'C') {
        oa_ui_calibrate(&game, false, OA_MENU);
        calibration_active = false;
    }
    if (ch == 'n' || ch == 'N') {
        oa_ui_calibrate(&game, true, OA_MENU);
        calibration_active = false;
    }
    if (ch == 'w' || ch == 'W') {
        oa_storage_allow_initialize(&storage);
        game.save_requested = true;
    }
    if (ch == 'f' || ch == 'F') {
        game.settings.lcd_fast ^= 1;
        oa_board_lcd_fast(game.settings.lcd_fast);
        game.save_requested = true;
    }
    if (ch == 'a' || ch == 'A' || (ch >= '1' && ch <= '3')) {
        game.settings.touch_profile = (uint8_t)(ch >= '1' && ch <= '3' ? ch - '0' : 0);
        profile = 255;
        game.settings.calibrated = 0;
        game.state = OA_DIAGNOSTIC;
    }
}
static void render_task(uint32_t now) {
    bool busy = oa_board_display_busy();
    if (oa_board_display_error()) {
        memset(hashes, 0, sizeof(hashes));
        frame_active = false;
        inflight = false;
        band_ready = false;
        return;
    }
    if (inflight && !busy) {
        for (unsigned i = pending_first; i < pending_end; i++)
            hashes[pending_y / 8][i] = pending_hashes[i];
        inflight = false;
    }
    if (frame_active && band_y >= 240 && !inflight) {
        frame_active = false;
        frames++;
        frame_count++;
    }
    if (!frame_active) {
        if (busy || now - last_frame < 33333)
            return;
        last_frame = now;
        view = game;
        view_overlay = overlay;
        frame_active = true;
        band_y = 0;
        band_ready = false;
    }
    if (!band_ready && band_y < 240) {
        oa_render_band(&view, &view_overlay, band_y, band);
        dirty = 0;
        for (unsigned i = 0; i < 10; i++) {
            new_hashes[i] = oa_render_hash(band, i);
            if (new_hashes[i] != hashes[band_y / 8][i])
                dirty |= (uint16_t)(1u << i);
        }
        band_ready = true;
    }
    if (busy || inflight || band_y >= 240)
        return;
    if (!dirty) {
        band_y += 8;
        band_ready = false;
        return;
    }
    unsigned first = 0;
    while (!(dirty & (1u << first)))
        first++;
    unsigned end = first + 1;
    while (end < 10 && (dirty & (1u << end)))
        end++;
    unsigned width = (end - first) * 32;
    for (unsigned y = 0; y < 8; y++)
        for (unsigned x = 0; x < width; x++) {
            uint16_t p = band[y * 320 + first * 32 + x];
            unsigned at = (y * width + x) * 2;
            transmit[at] = (uint8_t)(p >> 8);
            transmit[at + 1] = (uint8_t)p;
        }
    if (oa_board_lcd_dma(first * 32, (unsigned)band_y, width, 8, transmit)) {
        pending_first = first;
        pending_end = end;
        pending_y = band_y;
        memcpy(pending_hashes, new_hashes, sizeof(pending_hashes));
        inflight = true;
        bytes_sent += width * 8 * 2;
        for (unsigned i = first; i < end; i++)
            dirty &= (uint16_t)~(1u << i);
        if (!dirty) {
            band_y += 8;
            band_ready = false;
        }
    }
}
void oa_app_init(void) {
    /* CubeMX initialise TIM2 mais ne le démarre pas ; cet appel reste ici
     * pour que la base de temps fonctionne après une régénération. */
    oa_board_start_timebase();
    /* Un seul éclair vert de 60 ms au démarrage, aucune animation périodique. */
    oa_board_led(false, true, false);
    oa_board_delay_ms(60);
    oa_board_led(false, false, false);
    oa_game_init(&game);
    io = oa_board_io();
    (void)oa_storage_open(&storage, oa_eeprom_interface(&io), &game);
    if (OA_TOUCH_FORCE)
        game.settings.touch_profile = OA_TOUCH_FORCE;
    /* Maintenir USER pendant un reset permet de passer au profil suivant,
     * même si l'écran tactile est absent ou impossible à calibrer. */
    if (oa_board_button_raw()) {
        game.settings.touch_profile = (uint8_t)((game.settings.touch_profile + 1) % 4);
        game.save_requested = true;
    }
    profile = game.settings.touch_profile;
    (void)oa_touch_begin(&touch, io, (OaTouchKind)profile);
    accel_id = oa_accel_begin(io);
    lcd_ok = oa_lcd_init();
    oa_board_lcd_fast(game.settings.lcd_fast);
    if (touch.kind == OA_TOUCH_AUTO || !lcd_ok)
        game.state = OA_DIAGNOSTIC;
    last_loop = oa_board_us();
    last_stats = last_loop;
    diagnostic();
}
void oa_app_task(void) {
    uint32_t now = oa_board_us(), ms = oa_board_ms(), elapsed = now - last_loop;
    last_loop = now;
    debt += (uint64_t)elapsed * 60;
    bool press = false;
    bool held = oa_board_button(&press);
    (void)held;
    if (press && game.state == OA_MENU) {
        /* Raccourci matériel facultatif ; tous les parcours ont une solution tactile. */
        oa_ui_calibrate(&game, game.settings.control_mode != 0, OA_MENU);
        calibration_active = false;
    } else if (press)
        button_event = true;
    int ch = usb_console_getchar();
    if (ch >= 0)
        command(ch);
    ch = oa_board_uart_getchar();
    if (ch >= 0)
        command(ch);
    if (profile != game.settings.touch_profile && !oa_board_display_busy()) {
        profile = game.settings.touch_profile;
        (void)oa_touch_begin(&touch, io, (OaTouchKind)profile);
        memset(&touch_input, 0, sizeof(touch_input));
    }
    OaSettings cfg = game.settings;
    if (game.state == OA_CALIBRATION && !game.calibration_tilt)
        cfg.calibrated = 0;
    if (touch.kind != OA_TOUCH_STMPE || !oa_board_display_busy())
        oa_touch_poll(&touch, &cfg, now);
    if (now - last_accel >= 10000) {
        last_accel = now;
        int16_t mg[3];
        if (oa_accel_read(io, accel_id, mg)) {
            oa_tilt_sample(&tilt, mg, ms);
            if (game.state == OA_CALIBRATION && calibration_active && calibration.tilt)
                oa_cal_tilt(&calibration, mg, ms);
        }
    }
    unsigned count = 0;
    while (debt >= 1000000 && count < 4) {
        debt -= 1000000;
        count++;
        OaInput in = oa_input_touch(&touch_input, &touch.sample, &game.settings);
        /* La sortie par appui long reste possible même avec une ancienne
         * transformation écran erronée : elle utilise le contact brut valide. */
        in.pointer_long |=
            oa_input_long_press(&raw_hold, touch.sample.raw_valid, touch.sample.raw_down);
        if (game.settings.control_mode) {
            in.move = oa_tilt_axis(&tilt, &game.settings, ms);
            in.run = in.move > 700 || in.move < -700;
        }
        if (button_event) {
            in.pause = true;
            in.back = game.state == OA_SETTINGS || game.state == OA_DIAGNOSTIC ||
                      game.state == OA_CALIBRATION;
            button_event = false;
        }
        oa_game_step(&game, &in);
    }
    if (debt >= 4000000) {
        overruns++;
        debt = 0;
        if (game.state == OA_PLAY) {
            game.state = OA_PAUSE;
            game.menu_item = 0;
            game.ui_pressed = 0;
            game.ui_blocked = false;
            game.resume_available = true;
        }
    }
    overlay.long_hold_progress = (uint8_t)(raw_hold.ticks > 90 ? 90 : raw_hold.ticks);
    calibration_task();
    if (game.save_requested && !storage.state) {
        if (oa_storage_request(&storage, &game, ms))
            game.save_requested = false;
        else if (!storage.writable || !storage.available)
            game.save_requested = false;
    }
    oa_storage_task(&storage, ms);
    overlay.save_error = storage.error || !storage.available || !storage.writable;
    overlay.save_pending = game.save_requested || storage.state != 0;
    if (help_pending &&
        usb_console_write((const uint8_t *)help_text, (uint32_t)(sizeof(help_text) - 1)))
        help_pending = false;
    if (now - last_stats >= 1000000) {
        overlay.fps10 = (uint16_t)(frame_count * 10);
        frame_count = 0;
        last_stats = now;
        diagnostic();
        char line[320];
        int n =
            snprintf(line, sizeof(line),
                     "ObCP Adventure v" OA_VERSION " tick=%lu level=%u state=%u fps10=%u bytes=%lu "
                     "overrun=%lu touch=%u proof=%u accel=%02X save=%u "
                     "raw=%u,%u down=%u valid=%u seq=%lu cal=%u/%u\r\n",
                     (unsigned long)game.tick, game.level + 1, game.state, overlay.fps10,
                     (unsigned long)bytes_sent, (unsigned long)overruns, touch.kind,
                     touch.proof[touch.kind], accel_id, storage.error, touch.sample.raw_x,
                     touch.sample.raw_y, touch.sample.down, touch.sample.valid,
                     (unsigned long)touch.sample.sequence, calibration.point, calibration.count);
        if (n > 0)
            (void)usb_console_write(
                (const uint8_t *)line,
                (uint32_t)((size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1));
    }
    if (lcd_ok)
        render_task(now);
}
#ifdef OA_APP_TEST
const OaGame *oa_app_test_game(void) {
    return &game;
}
#endif
