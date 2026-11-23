/** @file test_app.c
 * @brief Exécute réellement oa_app_init/task avec un BSP PC et un STMPE simulés.
 * @details La façade applicative n'est pas réécrite pour le test : seules les
 * fonctions de Core/Inc/oa_board.h (bus, temps, bouton, LED, transport LCD) et
 * la console USB sont remplacées ici. Ce test n'est pas matériel.
 * SPDX-License-Identifier: MIT
 */
#include "oa_app.h"
#include "oa_board.h"
#include "oa_calibration.h"
#include "oa_ui.h"
#include "usb_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t milliseconds;
static bool button, down;
static uint16_t raw_x, raw_y;
static uint8_t regs[128], data_index;
static uint16_t pixels[320 * 240];
static char log_line[512];
static unsigned assertions, led_calls;
static uint32_t led_on_ms, led_off_ms;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        assertions++;                                                                              \
        if (!(x)) {                                                                                \
            fprintf(stderr, "ECHEC APP ligne %d : %s\n", __LINE__, #x);                            \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
/* ---- Temps, bouton et liaison série simulés (oa_board.h) ---- */
void oa_board_start_timebase(void) {
}
uint32_t oa_board_ms(void) {
    return milliseconds;
}
void oa_board_delay_ms(uint32_t ms) {
    milliseconds += ms;
}
bool oa_board_button_raw(void) {
    return false;
}
int oa_board_uart_getchar(void) {
    return -1;
}
void oa_board_fatal(void) {
    fprintf(stderr, "ECHEC APP : oa_board_fatal()\n");
    exit(1);
}
static bool spi(void *ctx, OaDevice d, const uint8_t *tx, uint8_t *rx, size_t n, uint32_t hz,
                unsigned mode) {
    (void)ctx;
    (void)hz;
    (void)mode;
    memset(rx, 0xFF, n);
    if (d != OA_DEV_TOUCH)
        return false;
    unsigned r = tx[0] & 0x7F;
    if (n == 2) {
        regs[r] = tx[1];
        if (r == 0x4B)
            data_index = 0;
        return true;
    }
    if (n != 3)
        return false;
    rx[2] = r == 0 ? 8 : (r == 1 ? 0x11 : regs[r]);
    if (r == 0x40)
        rx[2] |= down ? 0x80 : 0;
    if (r == 0x4C)
        rx[2] = down ? 1 : 0;
    if (r == 0x57) {
        uint8_t packet[4] = {(uint8_t)(raw_x >> 4), (uint8_t)((raw_x << 4) | (raw_y >> 8)),
                             (uint8_t)raw_y, 64};
        rx[2] = packet[data_index++ % 4];
    }
    return true;
}
static bool iw(void *c, uint8_t a, const uint8_t *p, size_t n) {
    (void)c;
    (void)a;
    (void)p;
    (void)n;
    return false;
}
static bool ir(void *c, uint8_t a, uint8_t *p, size_t n) {
    (void)c;
    (void)a;
    (void)p;
    (void)n;
    return false;
}
static void delay(void *c, uint32_t us) {
    (void)c;
    milliseconds += (us + 999) / 1000;
}
OaIo oa_board_io(void) {
    return (OaIo){NULL, spi, iw, ir, delay};
}
uint32_t oa_board_us(void) {
    return milliseconds * 1000;
}
bool oa_board_display_busy(void) {
    return false;
}
bool oa_board_display_error(void) {
    return false;
}
void oa_board_lcd_fast(bool v) {
    (void)v;
}
bool oa_lcd_init(void) {
    return true;
}
bool oa_board_lcd_dma(unsigned x, unsigned y, unsigned w, unsigned h, const uint8_t *p) {
    CHECK(x + w <= 320 && y + h <= 240 && w * h * 2 <= 5120);
    for (unsigned j = 0; j < h; j++)
        for (unsigned i = 0; i < w; i++) {
            unsigned at = (j * w + i) * 2;
            pixels[(y + j) * 320 + x + i] = (uint16_t)(((uint16_t)p[at] << 8) | p[at + 1]);
        }
    return true;
}
bool oa_board_button(bool *p) {
    *p = button;
    button = false;
    return *p;
}
void oa_board_led(bool r, bool g, bool b) {
    led_calls++;
    if (g && !r && !b)
        led_on_ms = milliseconds;
    else if (!r && !g && !b)
        led_off_ms = milliseconds;
    else
        CHECK(false);
}
static const char *console_input = "";
int usb_console_getchar(void) {
    return *console_input ? (unsigned char)*console_input++ : -1;
}
bool usb_console_write(const uint8_t *p, uint32_t n) {
    if (n >= sizeof(log_line))
        n = sizeof(log_line) - 1;
    memcpy(log_line, p, n);
    log_line[n] = '\0';
    return true;
}
static void run(unsigned duration) {
    for (unsigned i = 0; i < duration; i++) {
        milliseconds++;
        oa_app_task();
    }
}
static void contact(unsigned x, unsigned y, bool pressed, unsigned duration) {
    raw_x = (uint16_t)(x * 10 + 100);
    raw_y = (uint16_t)(y * 10 + 100);
    down = pressed;
    run(duration);
}
static void long_touch(void) {
    contact(160, 120, true, 1600);
    down = false;
    run(50);
}
static void tap(unsigned x, unsigned y) {
    contact(x, y, true, 50);
    down = false;
    run(50);
}
static void capture(const char *name) {
    FILE *f = fopen(name, "wb");
    CHECK(f != NULL);
    fprintf(f, "P6\n320 240\n255\n");
    for (unsigned i = 0; i < 320 * 240; i++) {
        uint16_t v = pixels[i];
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
    oa_app_init();
    run(100);
    CHECK(oa_app_test_game()->state == OA_MENU);
    CHECK(!oa_app_test_game()->settings.calibrated);
    CHECK(led_calls == 2 && led_off_ms - led_on_ms == 60);
    long_touch();
    CHECK(oa_app_test_game()->state == OA_CALIBRATION);
    if (argc > 1)
        capture(argv[1]);
    long_touch();
    CHECK(oa_app_test_game()->state == OA_MENU);
    CHECK(!oa_app_test_game()->settings.calibrated);
    long_touch();
    for (unsigned i = 0; i < 5; i++) {
        down = false;
        run(40);
        contact((unsigned)oa_cal_targets[i].x, (unsigned)oa_cal_targets[i].y, true, 100);
        CHECK(!oa_app_test_game()->settings.calibrated);
        down = false;
        run(40);
    }
    CHECK(oa_app_test_game()->state == OA_MENU);
    CHECK(oa_app_test_game()->settings.calibrated);
    OaSettings before = oa_app_test_game()->settings;
    long_touch();
    CHECK(oa_app_test_game()->state == OA_CALIBRATION);
    run(30100);
    CHECK(oa_app_test_game()->state == OA_MENU);
    CHECK(oa_app_test_game()->ui_notice == OA_UI_NOTICE_CAL_TIMEOUT);
    CHECK(memcmp(&before, &oa_app_test_game()->settings, sizeof(before)) == 0);
    CHECK(strstr(log_line, "raw=") != NULL);
    tap(100, 80);
    CHECK(oa_app_test_game()->state == OA_PLAY);
    /* Le tap qui ouvre la pause est posé à l'endroit de REPRENDRE : sa
     * libération ne doit pas traverser la nouvelle fenêtre. */
    tap(140, 110);
    CHECK(oa_app_test_game()->state == OA_PAUSE);
    int32_t x = oa_app_test_game()->player.body.x, y = oa_app_test_game()->player.body.y;
    uint32_t tick = oa_app_test_game()->tick;
    run(200);
    CHECK(oa_app_test_game()->tick == tick);
    CHECK(oa_app_test_game()->player.body.x == x && oa_app_test_game()->player.body.y == y);
    tap(160, 160);
    CHECK(oa_app_test_game()->state == OA_MENU);
    CHECK(oa_app_test_game()->resume_available);
    tap(160, 120);
    CHECK(oa_app_test_game()->state == OA_PLAY);
    CHECK(oa_app_test_game()->player.body.x == x);
    CHECK(led_calls == 2); /* Aucun nouvel éclair durant menu, calibration ou jeu. */
    /* Commande « ? » de la console : l'aide part au tour suivant. */
    log_line[0] = '\0';
    console_input = "?";
    run(2);
    CHECK(strstr(log_line, "ObCP Adventure v" OA_VERSION) != NULL);
    CHECK(strstr(log_line, "etalonnage de l'ecran tactile") != NULL);
    printf("test_app : ordonnanceur complet, %u verifications reussies (parcours tactile, "
           "etalonnage, pause/menu/reprise, eclair de 60 ms)\n",
           assertions);
    return 0;
}
