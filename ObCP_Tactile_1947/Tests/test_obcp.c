/** @file test_obcp.c
 * @brief Banc pédagogique sur PC avec faux matériel et assertions.
 * Une assertion exprime le résultat attendu. SAN active ASan/UBSan dans Tests/Makefile.
 */
#include "obcp_app.h"
#include "obcp_config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Flash en RAM avec injection d’une coupure à une opération choisie. */
typedef struct {
    uint8_t bytes[OBCP_STORAGE_BYTES]; /**< État de tous les octets simulés, 0xFF après effacement. */
    int operations; /**< Nombre d’opérations d’effacement ou de programmation commencées. */
    int fail_at; /**< Indice d’opération à interrompre, -1 pour aucune coupure. */
    bool fault_read; /**< true pour refuser toutes les lectures. */
} MockFlash;

/* Ces objets statiques évitent de placer les grandes images sur la pile du PC.
 * screen reçoit les bandes du transport, expected reçoit le rendu complet de référence.
 * draw_a/draw_b servent à vérifier deux contenus persistants distincts. */
static MockFlash flash;
static App app;
static Renderer renderer;
static uint8_t screen[LCD_WIDTH * LCD_HEIGHT * 2];
static uint8_t expected[LCD_WIDTH * LCD_HEIGHT * 2];
static uint8_t draw_a[PAINT_BYTES], draw_b[PAINT_BYTES];
static void tap(int16_t x, int16_t y);

/**
 * @brief Lire la flash simulée en vérifiant les bornes et en permettant une panne de lecture injectée.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in,out] ctx Contexte du faux port mémoire ou d’affichage transmis par le module testé.
 * @param[in] off Offset en octets dans le tableau de flash simulée.
 * @param[out] dst Tampon de destination valide pour n octets.
 * @param[in] n Nombre d’octets de l’opération.
 * @return true si l’opération simulée réussit.
 */
static bool mem_read(void *ctx, uint32_t off, void *dst, size_t n)
{
    MockFlash *f = ctx;
    assert(off + n <= sizeof(f->bytes));
    if (f->fault_read) {
        return false;
    }
    memcpy(dst, f->bytes + off, n);
    return true;
}

/**
 * @brief Simuler un effacement par pages, avec une coupure éventuellement placée à mi-opération.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in,out] ctx Contexte du faux port mémoire ou d’affichage transmis par le module testé.
 * @param[in] off Offset en octets dans le tableau de flash simulée.
 * @param[in] n Nombre d’octets de l’opération.
 * @return true si l’opération simulée réussit.
 */
static bool mem_erase(void *ctx, uint32_t off, uint32_t n)
{
    MockFlash *f = ctx;
    assert(off % OBCP_FLASH_PAGE == 0U && n % OBCP_FLASH_PAGE == 0U);
    assert(off + n <= sizeof(f->bytes));
    if (f->operations++ == f->fail_at) {
        memset(f->bytes + off, 0xFF, n / 2U);
        return false;
    }
    memset(f->bytes + off, 0xFF, n);
    return true;
}

/**
 * @brief Simuler la programmation d’un double mot vierge, avec écriture partielle de quatre octets lors d’une coupure.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in,out] ctx Contexte du faux port mémoire ou d’affichage transmis par le module testé.
 * @param[in] off Offset en octets dans le tableau de flash simulée.
 * @param[in] word Double mot de huit octets à programmer.
 * @return true si l’opération simulée réussit.
 */
static bool mem_program(void *ctx, uint32_t off, const uint8_t word[8])
{
    MockFlash *f = ctx;
    assert(off % 8U == 0U && off + 8U <= sizeof(f->bytes));
    bool cut = f->operations++ == f->fail_at;
    unsigned n = cut ? 4U : 8U;
    for (unsigned i = 0; i < n; ++i) {
        assert(f->bytes[off + i] == 0xFF);
        f->bytes[off + i] = word[i];
    }
    return !cut;
}

/**
 * @brief Recréer une mémoire effacée et brancher les trois callbacks du port simulé.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @return Port mémoire raccordé à la flash simulée.
 */
static Storage storage_init(void)
{
    memset(flash.bytes, 0xFF, sizeof(flash.bytes));
    flash.operations = 0;
    flash.fail_at = -1;
    flash.fault_read = false;
    return (Storage){&flash, mem_read, mem_erase, mem_program};
}

/**
 * @brief Vérifier CRC, alternance A/B, erreurs de lecture et conservation de l’ancienne copie à chaque coupure injectée.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 */
static void test_storage(void)
{
    Storage s = storage_init();
    uint8_t a[73], b[73], actual[73];
    memset(a, 0x2F, sizeof(a));
    memset(b, 0x71, sizeof(b));
    assert(storage_crc32("123456789", 9) == UINT32_C(0xCBF43926));
    assert(storage_read(&s, SAVE_MUSIC, actual, sizeof(actual)) == STORE_EMPTY);
    unsigned tested = 0;
    for (int cut = 0; cut < 15; ++cut) {
        s = storage_init();
        assert(storage_save(&s, SAVE_MUSIC, a, sizeof(a)) == STORE_OK);
        flash.operations = 0;
        flash.fail_at = cut;
        assert(storage_save(&s, SAVE_MUSIC, b, sizeof(b)) == STORE_IO);
        assert(storage_read(&s, SAVE_MUSIC, actual, sizeof(actual)) == STORE_OK);
        assert(memcmp(actual, a, sizeof(a)) == 0);
        flash.fail_at = -1;
        assert(storage_save(&s, SAVE_MUSIC, b, sizeof(b)) == STORE_OK);
        assert(storage_read(&s, SAVE_MUSIC, actual, sizeof(actual)) == STORE_OK);
        assert(memcmp(actual, b, sizeof(b)) == 0);
        ++tested;
    }
    flash.fault_read = true;
    int before_error = flash.operations;
    assert(storage_save(&s, SAVE_MUSIC, a, sizeof(a)) == STORE_IO);
    assert(flash.operations == before_error);
    flash.fault_read = false;
    /* La copie B est plus récente. Sa corruption doit ramener à A. */
    flash.bytes[OBCP_STORAGE_BYTES - OBCP_FLASH_PAGE + 32U] ^= 1U;
    assert(storage_read(&s, SAVE_MUSIC, actual, sizeof(actual)) == STORE_OK);
    assert(memcmp(actual, a, sizeof(a)) == 0);

    memset(draw_a, 0x13, sizeof(draw_a));
    memset(draw_b, 0x74, sizeof(draw_b));
    assert(storage_save(&s, SAVE_DRAW_1, draw_a, sizeof(draw_a)) == STORE_OK);
    assert(storage_save(&s, SAVE_DRAW_2, draw_b, sizeof(draw_b)) == STORE_OK);
    assert(storage_read(&s, SAVE_DRAW_1, expected, sizeof(draw_a)) == STORE_OK);
    assert(memcmp(draw_a, expected, sizeof(draw_a)) == 0);
    assert(storage_read(&s, SAVE_DRAW_2, expected, sizeof(draw_b)) == STORE_OK);
    assert(memcmp(draw_b, expected, sizeof(draw_b)) == 0);
    printf("FLASH 2 dessins de %u octets, CRC et %u interruptions injectees\n", PAINT_BYTES, tested);
}

/**
 * @brief Vérifier la rotation tactile, le neutre, la zone morte et les huit orientations des axes.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 */
static void test_inputs(void)
{
    int16_t x, y;
    assert(touch_landscape(0, 0, false, &x, &y) && x == 319 && y == 0);
    assert(touch_landscape(239, 319, false, &x, &y) && x == 0 && y == 239);
    assert(touch_landscape(0, 0, true, &x, &y) && x == 0 && y == 239);
    assert(!touch_landscape(240, 0, false, &x, &y));
    TouchState state = {0};
    assert(touch_update(&state, true, 20, 30).kind == TOUCH_DOWN);
    assert(touch_update(&state, true, 20, 30).kind == TOUCH_NONE);
    assert(touch_update(&state, true, 21, 31).kind == TOUCH_MOVE);
    assert(touch_update(&state, false, 0, 0).kind == TOUCH_UP);
    ResistiveFilter filter = {0};
    RawTouch raw = {.point = {800, 600}, .down = true};
    assert(!touch_filter(&filter, raw).down);
    assert(touch_filter(&filter, raw).down);
    ++raw.point.x;
    assert(touch_filter(&filter, raw).point.x == 801U);
    raw.point.x = 4000;
    assert(touch_filter(&filter, raw).point.x == 801U);
    raw.down = false;
    assert(touch_filter(&filter, raw).down);
    assert(!touch_filter(&filter, raw).down);
    Tilt tilt;
    tilt_init(&tilt);
    for (unsigned i = 0; i < 32; ++i) {
        tilt_sample(&tilt, true, 0.01f, 0.02f, 1.0f);
    }
    assert(tilt.calibrated && tilt.x == 0 && tilt.y == 0);
    for (unsigned i = 0; i < 30; ++i) {
        tilt_sample(&tilt, true, -0.30f, -0.40f, 0.87f);
    }
    assert(tilt.x < -0.8f && tilt.y < -0.5f);
    tilt_sample(&tilt, false, 0, 0, 0);
    assert(!tilt.valid && tilt.x == 0 && tilt.y == 0);
    puts("ENTREES transformation tactile, transitions et filtre a 100 Hz");
}

/**
 * @brief Alimenter l’assistant d’axes avec seize observations stables d’une pose choisie.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in,out] guide Assistant d’orientation à faire progresser.
 * @param[in,out] tilt État de l’inclinaison simulée.
 * @param[in] x Accélération X en g.
 * @param[in] y Accélération Y en g.
 */
static void feed_tilt(TiltGuide *guide, Tilt *tilt, float x, float y)
{
    for (unsigned i = 0; i < 42U; ++i) {
        tilt_sample(tilt, true, x, y, 0.93f);
        (void)tilt_guide_sample(guide, tilt);
    }
}

/**
 * @brief Vérifier les poses droite/à plat/bas, le rejet des diagonales et la réutilisation du repère retenu.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 */
static void test_tilt_guide(void)
{
    for (uint8_t axes = 0; axes <= TILT_AXES_MASK; ++axes) {
        Tilt tilt;
        TiltGuide guide;
        tilt_init(&tilt);
        tilt_guide_start(&guide, &tilt);
        feed_tilt(&guide, &tilt, 0, 0);
        assert(guide.phase == TILT_GUIDE_RIGHT);
        feed_tilt(&guide, &tilt, 0.30f, 0.30f);
        assert(guide.phase == TILT_GUIDE_RIGHT);
        float right = (axes & TILT_INVERT_X) ? -0.30f : 0.30f;
        float down = (axes & TILT_INVERT_Y) ? -0.30f : 0.30f;
        bool swap = (axes & TILT_SWAP_XY) != 0U;
        feed_tilt(&guide, &tilt, swap ? 0 : right, swap ? right : 0);
        assert(guide.phase == TILT_GUIDE_FLAT);
        feed_tilt(&guide, &tilt, 0, 0);
        assert(guide.phase == TILT_GUIDE_DOWN);
        feed_tilt(&guide, &tilt, swap ? 0 : right, swap ? right : 0);
        assert(guide.phase == TILT_GUIDE_DOWN);
        feed_tilt(&guide, &tilt, swap ? down : 0, swap ? 0 : down);
        assert(guide.phase == TILT_GUIDE_PREVIEW && guide.axes == axes);
        float x, y;
        tilt_project(&tilt, guide.axes, &x, &y);
        assert(x == 0.0f && y > 0.6f);
        feed_tilt(&guide, &tilt, swap ? 0 : right, swap ? right : 0);
        tilt_project(&tilt, guide.axes, &x, &y);
        assert(x > 0.6f && y == 0.0f);
    }

    Storage s = storage_init();
    app_init(&app);
    app.diag.accel_ok = true;
    tap(185, 180); /* Paramètres */
    tap(235, 90); /* Axes jeux */
    assert(app.screen == SCREEN_TILT);
    tap(246, 169); /* Mesurer */
    feed_tilt(&app.tilt_guide, &app.tilt, 0, 0);
    feed_tilt(&app.tilt_guide, &app.tilt, -0.30f, 0);
    feed_tilt(&app.tilt_guide, &app.tilt, 0, 0);
    feed_tilt(&app.tilt_guide, &app.tilt, 0, -0.30f);
    assert(app.tilt_guide.phase == TILT_GUIDE_PREVIEW);
    tap(246, 169); /* Enregistrer */
    assert(app.job == JOB_TILT_SAVE && app_store_task(&app, &s) == STORE_OK);
    assert(app.screen == SCREEN_SETTINGS);
    app_init(&app);
    app_read_axes(&app, &s);
    assert(app.tilt.axes == (TILT_INVERT_X | TILT_INVERT_Y));
    tilt_recenter(&app.tilt);
    assert(app.tilt.axes == (TILT_INVERT_X | TILT_INVERT_Y));
    puts("AXES assistant tactile, 8 reperes, refus diagonale et reprise flash");
}

/**
 * @brief Tester le seuil et la consommation de l’appui tactile de secours au démarrage.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 */
static void test_touch_boot(void)
{
    TouchBoot boot = {.armed = true};
    assert(touch_boot_sample(&boot, false, 10) == BOOT_TOUCH_NONE);
    assert(touch_boot_sample(&boot, true, 20) == BOOT_TOUCH_NONE);
    boot = (TouchBoot){.armed = true};
    assert(touch_boot_sample(&boot, true, 10) == BOOT_TOUCH_USED);
    assert(touch_boot_sample(&boot, false, 20) == BOOT_TOUCH_USED);
    assert(touch_boot_sample(&boot, true, 2000) == BOOT_TOUCH_NONE);
    boot = (TouchBoot){.armed = true};
    uint32_t start = UINT32_MAX - 10U;
    assert(touch_boot_sample(&boot, true, start) == BOOT_TOUCH_USED);
    assert(touch_boot_sample(&boot, true, start + TOUCH_BOOT_HOLD_MS - 1U) == BOOT_TOUCH_USED);
    assert(touch_boot_sample(&boot, true, start + TOUCH_BOOT_HOLD_MS) == BOOT_TOUCH_CALIBRATE);
    assert(touch_boot_sample(&boot, true, start + TOUCH_BOOT_HOLD_MS + 1U) == BOOT_TOUCH_NONE);
    puts("SECOURS TACTILE contact au demarrage, appui bref filtre et seuil long");
}

static const RawPoint cal_points[4] = {
    {3500, 500}, {3500, 3500}, {500, 500}, {500, 3500}
};

/**
 * @brief Fournir six mesures d’une cible puis un relâchement pour faire avancer le calibrage.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in,out] s Session de calibrage à alimenter.
 * @param[in] p Point brut de la cible simulée.
 * @return Événement de progression du calibrage.
 */
static TouchCalEvent hold_target(TouchCalSession *s, RawPoint p)
{
    RawTouch raw = {.point = p, .down = true};
    for (unsigned i = 0; i < CAL_STABLE_SAMPLES; ++i) {
        assert(touch_cal_sample(s, raw) == (i + 1U == CAL_STABLE_SAMPLES ? CAL_HELD : CAL_UNCHANGED));
    }
    return touch_cal_sample(s, (RawTouch){0});
}

/**
 * @brief Vérifier l’ajustement affine, les mesures instables, la quatrième cible et les profils persistants avec coupures.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 */
static void test_calibration(void)
{
    TouchCalibration rotated, inverted;
    const RawPoint reversed[3] = {{3600, 600}, {600, 600}, {3600, 3500}};
    const RawPoint collinear[3] = {{0, 0}, {700, 700}, {1500, 1500}};
    assert(touch_cal_fit(&rotated, cal_points));
    assert(touch_cal_fit(&inverted, reversed));
    assert(!touch_cal_fit(&inverted, collinear));
    int16_t x, y;
    assert(touch_cal_map(&rotated, cal_points[3], &x, &y));
    assert(x == TOUCH_CAL_TARGETS[3].x && y == TOUCH_CAL_TARGETS[3].y);
    assert(touch_cal_map(&inverted, reversed[1], &x, &y));
    assert(x == TOUCH_CAL_TARGETS[1].x && y == TOUCH_CAL_TARGETS[1].y);
    assert(touch_cal_map(&rotated, (RawPoint){4095, 4095}, &x, &y) && x == 319 && y == 30);
    assert(!touch_cal_map(&rotated, (RawPoint){4096, 0}, &x, &y));

    TouchCalSession session;
    touch_cal_start(&session);
    RawTouch raw = {.point = cal_points[0], .down = true};
    assert(touch_cal_sample(&session, raw) == CAL_UNCHANGED && session.samples == 0U);
    assert(touch_cal_sample(&session, (RawTouch){0}) == CAL_READY);
    assert(touch_cal_sample(&session, raw) == CAL_UNCHANGED && session.samples == 1U);
    touch_cal_interrupt(&session);
    assert(touch_cal_sample(&session, (RawTouch){0}) == CAL_READY && session.target == 0U);
    for (unsigned i = 0; i < CAL_FIT_COUNT; ++i) {
        assert(hold_target(&session, cal_points[i]) == CAL_ADVANCED);
    }
    assert(hold_target(&session, cal_points[0]) == CAL_RETRY && session.target == 0U);
    for (unsigned i = 0; i < CAL_FIT_COUNT; ++i) {
        assert(hold_target(&session, cal_points[i]) == CAL_ADVANCED);
    }
    assert(hold_target(&session, cal_points[3]) == CAL_COMPLETE);

    Storage s = storage_init();
    TouchCalBook book = {0}, stored;
    touch_cal_set(&book, TOUCH_CTRL_TSC2007, &rotated);
    touch_cal_set(&book, TOUCH_CTRL_STMPE610, &inverted);
    assert(touch_cal_save(&book, &s, OBCP_LCD_MADCTL) == STORE_OK);
    assert(touch_cal_load(&stored, &s, OBCP_LCD_MADCTL ^ 1U) == STORE_INVALID);
    touch_cal_set(&book, TOUCH_CTRL_TSC2007, &inverted);
    for (int cut = 0; cut < 9; ++cut) {
        /* Une tentative est interrompue à chaque effacement ou double mot. */
        flash.fail_at = cut;
        flash.operations = 0;
        assert(touch_cal_save(&book, &s, OBCP_LCD_MADCTL) == STORE_IO);
        assert(touch_cal_load(&stored, &s, OBCP_LCD_MADCTL) == STORE_OK);
        const TouchCalibration *tsc = touch_cal_get(&stored, TOUCH_CTRL_TSC2007);
        const TouchCalibration *stm = touch_cal_get(&stored, TOUCH_CTRL_STMPE610);
        assert(tsc != NULL && tsc->points[0].x == cal_points[0].x);
        assert(stm != NULL && stm->points[0].x == reversed[0].x);
    }
    flash.fail_at = -1;
    assert(touch_cal_save(&book, &s, OBCP_LCD_MADCTL) == STORE_OK);
    assert(touch_cal_load(&stored, &s, OBCP_LCD_MADCTL) == STORE_OK);
    const TouchCalibration *saved = touch_cal_get(&stored, TOUCH_CTRL_TSC2007);
    assert(saved != NULL && saved->points[0].x == reversed[0].x);

    app_init(&app);
    app.diag.touch_ok = true;
    app.diag.touch_controller = TOUCH_CTRL_TSC2007;
    tap(185, 180);
    tap(235, 162);
    assert(app.screen == SCREEN_TOUCH_CAL);
    app_touch(&app, (TouchEvent){TOUCH_DOWN, 20, 15});
    assert(app.screen == SCREEN_TOUCH_CAL);
    app_calibration_sample(&app, (RawTouch){0});
    for (unsigned i = 0; i < CAL_TARGET_COUNT; ++i) {
        raw.point = cal_points[i];
        for (unsigned j = 0; j < CAL_STABLE_SAMPLES; ++j) {
            app_calibration_sample(&app, raw);
        }
        app_calibration_sample(&app, (RawTouch){0});
    }
    assert(app.job == JOB_TOUCH_CAL_SAVE);
    app_store_task(&app, &s);
    assert(app.job == JOB_NONE && app.screen == SCREEN_SETTINGS);
    app_init(&app);
    app_read_calibration(&app, &s);
    assert(touch_cal_get(&app.touch_cal, TOUCH_CTRL_TSC2007) != NULL);
    app.diag.touch_ok = true;
    app.diag.touch_controller = TOUCH_CTRL_TSC2007;
    const RawPoint panel[3] = {{800, 1000}, {3400, 1000}, {800, 3400}};
    TouchCalibration can_cancel;
    assert(touch_cal_fit(&can_cancel, panel));
    touch_cal_set(&app.touch_cal, TOUCH_CTRL_TSC2007, &can_cancel);
    tap(185, 180);
    tap(235, 162);
    assert(app.cal_can_cancel && app.screen == SCREEN_TOUCH_CAL);
    app_calibration_sample(&app, (RawTouch){0});
    app_calibration_sample(&app, (RawTouch){.point = {3230, 310}, .down = true});
    assert(app.screen == SCREEN_SETTINGS);
    app_force_touch_calibration(&app);
    assert(!app.cal_can_cancel && app.screen == SCREEN_TOUCH_CAL);
    puts("CALIBRAGE axes, appui, controle independant, deux profils et 9 coupures simulees");
}

/**
 * @brief Tester les appuis courts et longs de SW1, les rebonds et le débordement de la base de temps.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 */
static void test_button(void)
{
    ButtonState button;
    button_init(&button, false, 0);

    /* Un rebond isolé ne valide aucun front. */
    assert(button_update(&button, true, 10) == BUTTON_NONE);
    assert(button_update(&button, false, 15) == BUTTON_NONE);
    assert(button_update(&button, false, 60) == BUTTON_NONE);

    assert(button_update(&button, true, 100) == BUTTON_NONE);
    assert(button_update(&button, true, 100 + BUTTON_DEBOUNCE_MS) == BUTTON_NONE);
    assert(button_update(&button, false, 180) == BUTTON_NONE);
    assert(button_update(&button, false, 180 + BUTTON_DEBOUNCE_MS - 1) == BUTTON_NONE);
    assert(button_update(&button, false, 180 + BUTTON_DEBOUNCE_MS) == BUTTON_SHORT);
    assert(button_update(&button, false, 230) == BUTTON_NONE);

    /* Un appui long produit un seul événement, y compris au relâchement. */
    assert(button_update(&button, true, 300) == BUTTON_NONE);
    assert(button_update(&button, true, 300 + BUTTON_DEBOUNCE_MS) == BUTTON_NONE);
    assert(button_update(&button, true, 300 + BUTTON_HOLD_MS - 1) == BUTTON_NONE);
    assert(button_update(&button, true, 300 + BUTTON_HOLD_MS) == BUTTON_LONG);
    assert(button_update(&button, true, 1300) == BUTTON_NONE);
    assert(button_update(&button, false, 1320) == BUTTON_NONE);
    assert(button_update(&button, false, 1320 + BUTTON_DEBOUNCE_MS) == BUTTON_NONE);

    button_init(&button, true, 0);
    assert(button_update(&button, true, BUTTON_HOLD_MS + 1) == BUTTON_NONE);
    assert(button_update(&button, false, 1000) == BUTTON_NONE);
    assert(button_update(&button, false, 1000 + BUTTON_DEBOUNCE_MS) == BUTTON_NONE);

    /* Le compteur de ticks passe par zéro entre le front et sa validation. */
    uint32_t start = UINT32_MAX - 10U;
    button_init(&button, false, start - 1U);
    assert(button_update(&button, true, start) == BUTTON_NONE);
    assert(button_update(&button, true, start + BUTTON_DEBOUNCE_MS) == BUTTON_NONE);
    assert(button_update(&button, false, start + 60U) == BUTTON_NONE);
    assert(button_update(&button, false, start + 60U + BUTTON_DEBOUNCE_MS) == BUTTON_SHORT);
    puts("SW1 rebond, appuis court et long, reset et debordement du tick");
}

/**
 * @brief Mesurer par recherche en largeur le plus court chemin de chaque carte et vérifier son accessibilité.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in] maze Carte dont le chemin est mesuré.
 * @return Longueur du plus court chemin, en déplacements de case.
 */
static unsigned maze_route(const Maze *maze)
{
    enum { COLS = 17, ROWS = 11 };
    static const int dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1};
    unsigned distance[ROWS][COLS] = {0};
    unsigned queue[COLS * ROWS] = {COLS + 1U};
    unsigned tail = 1;
    distance[1][1] = 1;
    for (unsigned head = 0; head < tail; ++head) {
        int x = (int)(queue[head] % COLS), y = (int)(queue[head] / COLS);
        if (x == COLS - 2 && y == ROWS - 2) {
            return distance[y][x] - 1U;
        }
        for (unsigned i = 0; i < 4U; ++i) {
            int nx = x + dx[i], ny = y + dy[i];
            if (nx <= 0 || nx >= COLS - 1 || ny <= 0 || ny >= ROWS - 1) {
                continue;
            }
            if (distance[ny][nx] == 0U && !maze_solid(maze, 32.0f + 16.0f * nx, 54.0f + 16.0f * ny)) {
                distance[ny][nx] = distance[y][x] + 1U;
                queue[tail++] = (unsigned)(ny * COLS + nx);
            }
        }
    }
    return 0;
}

/**
 * @brief Vérifier les dix niveaux, les collisions, la commande de raquette et les familles de véhicules.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 */
static void test_games(void)
{
    Damage d = {0};
    Maze m;
    unsigned previous_route = 0, previous_limit = 100;
    for (unsigned level = 0; level < GAME_LEVELS; ++level) {
        maze_init(&m, level);
        assert(!maze_solid(&m, m.x, m.y));
        unsigned route = maze_route(&m);
        assert(route > previous_route && maze_seconds_left(&m) < previous_limit);
        previous_route = route;
        previous_limit = maze_seconds_left(&m);
        m.elapsed_ms = maze_seconds_left(&m) * 1000U - GAME_STEP_MS;
        m.phase = GAME_RUN;
        maze_step(&m, 0, 0, &d);
        assert(m.phase == GAME_LOST && maze_seconds_left(&m) == 0U);
    }
    maze_init(&m, 0);
    m.phase = GAME_RUN;
    float start = m.x;
    for (unsigned i = 0; i < 100; ++i) {
        maze_step(&m, -1.0f, 0.0f, &d);
        assert(!maze_solid(&m, m.x, m.y));
    }
    assert(m.x < start && m.vx == 0);
    Rect finish = maze_finish();
    m.x = finish.x + 8;
    m.y = finish.y + 8;
    maze_step(&m, 0, 0, &d);
    assert(m.phase == GAME_WON);

    Breakout b;
    float previous_ball_speed = 0, previous_paddle_limit = 100.0f;
    for (unsigned level = 0; level < GAME_LEVELS; ++level) {
        breakout_init(&b, level);
        assert(-b.vy > previous_ball_speed);
        previous_ball_speed = -b.vy;
        assert(b.bricks != 0U && (b.hard_bricks & ~b.bricks) == 0U);
        b.paddle = 10;
        b.phase = GAME_RUN;
        breakout_step(&b, -1, &d);
        for (unsigned i = 0; i < 12; ++i) {
            breakout_step(&b, -1, &d);
        }
        assert(b.paddle < previous_paddle_limit);
        previous_paddle_limit = b.paddle;
    }
    breakout_init(&b, 0);
    b.phase = GAME_RUN;
    b.bricks = 1;
    Rect target = breakout_brick(0);
    b.x = target.x + 12;
    b.y = target.y + target.h + 6;
    b.vx = 0;
    b.vy = -260;
    breakout_step(&b, 0, &d);
    assert(b.bricks == 0 && b.score == 10 && b.vy > 0 && b.phase == GAME_WON);
    breakout_init(&b, 0);
    b.phase = GAME_RUN;
    b.y = 244.0f;
    b.vy = 140.0f;
    breakout_step(&b, 0, &d);
    assert(b.phase == GAME_LOST);
    breakout_init(&b, 9);
    b.phase = GAME_RUN;
    for (unsigned i = 0; i < 3; ++i) {
        breakout_step(&b, 1, &d);
    }
    assert(b.paddle > 294.0f); /* Arriver à moins de 3 px du bord en 60 ms. */
    b.bricks = b.hard_bricks = 1;
    b.x = target.x + 12;
    b.y = target.y + target.h + 6;
    b.vx = 0;
    b.vy = -360;
    breakout_step(&b, 0, &d);
    assert(b.bricks == 1U && b.hard_bricks == 0U && b.score == 5U);

    Race race;
    float previous_traffic_step = 0;
    for (unsigned level = 0; level < GAME_LEVELS; ++level) {
        race_init(&race, level);
        assert(race_goal(&race) >= 50U);
        race.phase = GAME_RUN;
        race_step(&race, 0, &d);
        assert(race.scroll > previous_traffic_step);
        previous_traffic_step = race.scroll;
        unsigned kinds = 0;
        for (unsigned i = 0; i < 128; ++i) {
            memset(race.rival, 0, sizeof(race.rival));
            race.spawn_ms = 0;
            race_step(&race, 0, &d);
            assert(race.rival[0].active);
            kinds |= 1U << race.rival[0].kind;
        }
        unsigned expected_kinds = level >= 6U ? 15U : (level >= 4U ? 7U : (level >= 2U ? 3U : 1U));
        assert(kinds == expected_kinds);
    }
    race_init(&race, 0);
    race.phase = GAME_RUN;
    for (unsigned i = 0; i < 20U; ++i) {
        race_step(&race, -1.0f, &d);
    }
    assert(race.player_x >= 71.0f && race.player_x < 100.0f);
    race.rival[0] = (Rival){.y = 193, .lane = 0, .active = true};
    race_step(&race, 0.0f, &d);
    assert(race.phase == GAME_LOST);
    assert(race.crash_ms == 900U);
    for (unsigned i = 0; i < 45; ++i) {
        race_step(&race, 0, &d);
    }
    assert(race.crash_ms == 0U && race.phase == GAME_LOST);
    race_init(&race, 6);
    race.player_x = 186;
    race.rival[0] = (Rival){.y = 193, .lane = 1, .active = true, .kind = VEHICLE_BIKE};
    race.rival[1].active = false;
    race.phase = GAME_RUN;
    race_step(&race, 0, &d);
    assert(race.phase == GAME_RUN);
    race.rival[0].kind = VEHICLE_CAR;
    race_step(&race, 0, &d);
    assert(race.phase == GAME_LOST);
    puts("JEUX 10 cartes resolubles, progression, commande rapide, briques renforcees, vehicules et BOUM");
}

/**
 * @brief Injecter un appui puis un relâchement aux coordonnées écran indiquées.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in] x Abscisse écran, en pixels.
 * @param[in] y Ordonnée écran, en pixels.
 */
static void tap(int16_t x, int16_t y)
{
    app_touch(&app, (TouchEvent){TOUCH_DOWN, x, y});
    app_touch(&app, (TouchEvent){TOUCH_UP, x, y});
}

/**
 * @brief Vérifier la durée et la destination de l’accueil ainsi que les réglages X/Y et leur persistance.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 */
static void test_preferences_and_splash(void)
{
    Storage s = storage_init();
    app_init(&app);
    app_start_splash(&app);
    tap(25, 40);
    for (unsigned i = 0; i < 159U; ++i) {
        app_step(&app);
    }
    assert(app.screen == SCREEN_SPLASH);
    app_step(&app);
    assert(app.screen == SCREEN_MENU);
    tap(185, 180);
    assert(app.screen == SCREEN_SETTINGS);
    tap(85, 90);
    assert(app.screen == SCREEN_RESPONSE);
    tap(275, 85);
    tap(45, 142);
    assert(app.tilt.response_x == 7U && app.tilt.response_y == 5U);
    tap(225, 208);
    assert(app.job == JOB_PREFERENCES_SAVE && app_store_task(&app, &s) == STORE_OK);
    app_init(&app);
    app_read_preferences(&app, &s);
    assert(app.tilt.response_x == 7U && app.tilt.response_y == 5U);
    tilt_recenter(&app.tilt);
    assert(app.tilt.response_x == 7U && app.tilt.response_y == 5U);

    Tilt slow, quick;
    tilt_init(&slow);
    tilt_init(&quick);
    slow.axes = quick.axes = 0;
    slow.response_x = 1;
    quick.response_x = 10;
    for (unsigned i = 0; i < 32; ++i) {
        tilt_sample(&slow, true, 0, 0, 1);
        tilt_sample(&quick, true, 0, 0, 1);
    }
    tilt_sample(&slow, true, 0.20f, 0, 0.98f);
    tilt_sample(&quick, true, 0.20f, 0, 0.98f);
    assert(slow.x == 0.0f && quick.x > 0.5f && slow.y == 0.0f && quick.y == 0.0f);
    uint8_t invalid[3] = {1, 0, 11};
    assert(storage_save(&s, SAVE_PREFERENCES, invalid, sizeof(invalid)) == STORE_OK);
    app_init(&app);
    app_read_preferences(&app, &s);
    assert(app.tilt.response_x == TILT_RESPONSE_DEFAULT && app.tilt.response_y == TILT_RESPONSE_DEFAULT);
    app_open(&app, SCREEN_TOUCH_CAL);
    app_start_splash(&app);
    for (unsigned i = 0; i < 160; ++i) {
        app_step(&app);
    }
    assert(app.screen == SCREEN_TOUCH_CAL && app.touch_cal_session.wait_release);
    puts("PARAMETRES accueil 3,2 s, commandes X/Y distinctes et persistance de la reactivite");
}

/**
 * @brief Vérifier les diamètres, l’édition RVB, les palettes et la migration des dessins enregistrés.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 */
static void test_paint_tools(void)
{
    Storage s = storage_init();
    app_init(&app);
    tap(25, 40);
    tap(115, 218);
    assert(app.modal == MODAL_BRUSH);
    tap(245, 150);
    assert(app.modal == MODAL_NONE && PAINT_DIAMETERS[app.paint.brush] == 19U);
    tap(0, 0);
    assert(paint_get(&app.paint, 9, 0) == 8U && paint_get(&app.paint, 10, 0) == 0U);
    tap(70, 218);
    assert(app.modal == MODAL_COLORS);
    tap(60, 135);
    assert(app.paint.ink == 8U && app.modal == MODAL_COLORS);
    tap(90, 208);
    assert(app.modal == MODAL_RGB);
    tap(219, 73);
    tap(90, 116);
    tap(90, 159);
    tap(90, 208);
    assert(app.modal == MODAL_NONE && app.paint.palette[8] == RGB565(255,0,0));
    assert(paint_get(&app.paint, 0, 0) == 8U);
    assert(paint_save(&app.paint, &s, SAVE_DRAW_1) == STORE_OK);
    paint_init(&app.paint);
    assert(paint_load(&app.paint, &s, SAVE_DRAW_1) == STORE_OK);
    assert(app.paint.brush == 5U && app.paint.palette[8] == RGB565(255,0,0));
    assert(paint_get(&app.paint, 9, 0) == 8U);

    /* Passer du dessin de 32 000 octets au dessin avec palette ne doit
     * effacer sa seule copie valide à aucune des phases d'une sauvegarde. */
    static const int cuts[] = {0, 1, 3, 4, 100, 4003, 4008, 4009};
    for (unsigned i = 0; i < sizeof(cuts) / sizeof(cuts[0]); ++i) {
        s = storage_init();
        memset(draw_a, 0x88, sizeof(draw_a));
        assert(storage_save(&s, SAVE_DRAW_1, draw_a, sizeof(draw_a)) == STORE_OK);
        paint_init(&app.paint);
        assert(paint_load(&app.paint, &s, SAVE_DRAW_1) == STORE_OK);
        paint_set_color(&app.paint, RGB565(0,255,0));
        app.paint.brush = 5;
        flash.operations = 0;
        flash.fail_at = cuts[i];
        assert(paint_save(&app.paint, &s, SAVE_DRAW_1) == STORE_IO);
        assert(paint_load(&app.paint, &s, SAVE_DRAW_1) == STORE_OK);
        assert(memcmp(app.paint.image.pixels, draw_a, sizeof(draw_a)) == 0);
        assert(memcmp(app.paint.palette, PAINT_PALETTE, sizeof(app.paint.palette)) == 0);
        flash.fail_at = -1;
        paint_set_color(&app.paint, RGB565(0,255,0));
        assert(paint_save(&app.paint, &s, SAVE_DRAW_1) == STORE_OK);
        paint_init(&app.paint);
        assert(paint_load(&app.paint, &s, SAVE_DRAW_1) == STORE_OK);
        assert(app.paint.palette[8] == RGB565(0,255,0));
    }
    puts("DESSIN taille 1 a 19 px, edition RVB, palette par dessin et 8 coupures de migration");
}

/**
 * @brief Vérifier les cibles, les aplats, les commandes de test et la navigation des paramètres.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 */
static void test_diagnostic(void)
{
    static const uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    uint8_t pixel[2];
    Gfx view = {{160, 200, 1, 1}, pixel};

    app_init(&app);
    app_button(&app, BUTTON_SHORT);
    assert(app.screen == SCREEN_SETTINGS);
    tap(85, 162);
    assert(app.screen == SCREEN_DIAG && app.test_color == 0U);

    /* Aucun événement tactile pendant le premier parcours des mires. */
    for (unsigned i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
        app_button(&app, BUTTON_SHORT);
        assert(app.screen == SCREEN_DIAG && app.test_color == i + 1U);
        app_render(&app, &view);
        assert(pixel[0] == colors[i] >> 8 && pixel[1] == (colors[i] & 0xFFU));
    }
    app_button(&app, BUTTON_SHORT);
    assert(app.test_color == 0U);
    tap(93, 16); /* Aplats */
    assert(app.test_color == 1U);
    app_button(&app, BUTTON_SHORT);
    assert(app.test_color == 2U);
    app_button(&app, BUTTON_LONG);
    assert(app.screen == SCREEN_MENU && app.test_color == 0U);
    tap(185, 180);
    assert(app.screen == SCREEN_SETTINGS);
    tap(85, 162);
    assert(app.screen == SCREEN_DIAG);
    for (unsigned i = 0; i < 4U; ++i) {
        tap(93, 16);
        assert(app.test_color == i + 1U);
    }
    tap(93, 16);
    assert(app.test_color == 0U);
    tap(25, 16);
    assert(app.screen == SCREEN_SETTINGS);
    tap(25, 16);
    assert(app.screen == SCREEN_MENU);
    puts("DIAGNOSTIC quatre mires RGB565, ouverture et retour via le tactile");
}

/**
 * @brief Vérifier les commandes des applications et les demandes de stockage différées.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 */
static void test_app(void)
{
    Storage s = storage_init();
    app_init(&app);
    tap(25, 40);
    assert(app.screen == SCREEN_PAINT);
    app_touch(&app, (TouchEvent){TOUCH_DOWN, 0, 0});
    app_touch(&app, (TouchEvent){TOUCH_MOVE, 319, 199});
    app_touch(&app, (TouchEvent){TOUCH_UP, 319, 199});
    assert(paint_get(&app.paint, 0, 0) == 8 && paint_get(&app.paint, 319, 199) == 8);
    assert(paint_get(&app.paint, 160, 100) == 8);
    tap(203, 218);
    tap(69, 107);
    assert(app.job == JOB_DRAW_SAVE);
    app_store_task(&app, &s);

    app_init(&app);
    tap(25, 40);
    tap(248, 218);
    tap(69, 107);
    app_store_task(&app, &s);
    assert(paint_get(&app.paint, 160, 100) == 8 && !app.paint.modified);
    tap(18, 220);
    assert(app.screen == SCREEN_MENU);
    tap(185, 40);
    assert(app.screen == SCREEN_GROOVE);
    tap(111, 215); /* tempo + */
    assert(app.audio.pattern.bpm == 114U);
    tap(150, 215); /* timbre piste 1 */
    assert(app.audio.pattern.timbre[0] == 1U);
    tap(192, 215); /* sauver */
    tap(188, 148); /* motif 4 */
    assert(app.job == JOB_MUSIC_SAVE && app.slot == 3U);
    app_store_task(&app, &s);
    app_init(&app);
    app_read_book(&app, &s);
    tap(185, 40);
    tap(239, 215);
    tap(188, 148);
    app_store_task(&app, &s);
    assert(app.audio.pattern.bpm == 114U && app.audio.pattern.timbre[0] == 1U);

    tap(24, 15);
    tap(25, 110);
    assert(app.screen == SCREEN_MAZE);
    app.tilt.calibrated = true;
    app.tilt.valid = true;
    tap(150, 140);
    assert(app.maze.phase == GAME_RUN);
    tap(287, 15);
    assert(app.maze.phase == GAME_PAUSE);
    tap(150, 140);
    assert(app.maze.phase == GAME_RUN);
    tap(24, 15);
    tap(185, 110);
    assert(app.screen == SCREEN_BREAKOUT);
    tap(24, 15);
    tap(25, 180);
    assert(app.screen == SCREEN_RACE);
    tap(24, 15);
    tap(185, 180);
    assert(app.screen == SCREEN_SETTINGS);
    tap(85, 162);
    assert(app.screen == SCREEN_DIAG);
    tap(16, 50);
    assert(app.diag.hits == 1U);
    puts("APPLICATIONS navigation, pause, peinture, sauvegarde et rappel du motif 4");
}

/**
 * @brief Placer le modèle du jeu courant dans un état terminal pour tester sa navigation.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in] win true pour simuler une victoire, false pour une défaite.
 */
static void finish_game(bool win)
{
    if (app.screen == SCREEN_MAZE) {
        if (win) {
            Rect goal = maze_finish();
            app.maze.x = goal.x + goal.w / 2;
            app.maze.y = goal.y + goal.h / 2;
        } else {
            app.maze.elapsed_ms = 90000U;
        }
    } else if (app.screen == SCREEN_BREAKOUT) {
        if (win) {
            app.breakout.bricks = 0;
        } else {
            app.breakout.y = 243;
            app.breakout.vy = 145;
        }
    } else if (win) {
        app.race.score = race_goal(&app.race) - 1U;
        app.race.rival[0] = (Rival){.y = 239, .lane = 0, .active = true};
    } else {
        app.race.rival[0] = (Rival){.y = 193, .lane = 1, .active = true};
    }
    app_step(&app);
    while (app.screen == SCREEN_RACE && app.race.crash_ms > 0U) {
        app_step(&app);
    }
}

/**
 * @brief Vérifier victoire, niveau suivant, défaite, rejeu, pause et retour au menu pour les trois jeux.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 */
static void test_game_flow(void)
{
    const Screen screens[] = {SCREEN_MAZE, SCREEN_BREAKOUT, SCREEN_RACE};
    const int16_t cards[3][2] = {{25, 110}, {185, 110}, {25, 180}};
    GamePhase *const phase[] = {&app.maze.phase, &app.breakout.phase, &app.race.phase};
    const uint8_t *const level[] = {&app.maze.level, &app.breakout.level, &app.race.level};
    for (unsigned i = 0; i < 3U; ++i) {
        app_init(&app);
        app.tilt.valid = app.tilt.calibrated = true;
        tap(cards[i][0], cards[i][1]);
        assert(app.screen == screens[i]);
        tap(160, 130);
        assert(*phase[i] == GAME_RUN);
        finish_game(false);
        assert(*phase[i] == GAME_LOST);
        app.tilt.valid = false;
        tap(160, 170); /* Menu principal, même si le capteur est indisponible. */
        assert(app.screen == SCREEN_MENU);

        app.tilt.valid = true;
        tap(cards[i][0], cards[i][1]);
        tap(160, 130);
        for (unsigned n = 0; n < GAME_LEVELS; ++n) {
            assert(*level[i] == n && *phase[i] == GAME_RUN);
            finish_game(false);
            assert(*phase[i] == GAME_LOST);
            tap(160, 130); /* Rejouer le niveau courant. */
            assert(*level[i] == n && *phase[i] == GAME_RUN);
            finish_game(true);
            assert(*phase[i] == GAME_WON);
            tap(160, 130); /* Niveau suivant, puis nouvelle série. */
        }
        assert(*level[i] == 0 && *phase[i] == GAME_RUN);
        tap(287, 15);
        assert(*phase[i] == GAME_PAUSE);
        tap(160, 170);
        assert(app.screen == SCREEN_MENU);
    }
    puts("PARCOURS jeux, retour apres defaite, reprise du niveau et progression 1 a 10");
}

/**
 * @brief Compter les passages montants de la note de 440 Hz et les pas du séquenceur à 120 BPM.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 */
static void test_audio(void)
{
    Audio a;
    audio_init(&a);
    a.pattern.bpm = 120;
    uint16_t block[256];
    audio_start(&a);
    for (unsigned i = 0; i < 61; ++i) {
        audio_fill(&a, block, 256);
        for (unsigned j = 0; j < 256; ++j) {
            assert(block[j] < AUDIO_MAX_DUTY && block[j] > 0U);
        }
    }
    audio_fill(&a, block, 9);
    assert(a.step == 8); /* 120 BPM -> huit doubles croches en une seconde. */
    audio_stop(&a);
    a.test_tone = true;
    unsigned rising = 0;
    uint16_t previous = AUDIO_CENTER;
    for (unsigned left = AUDIO_HZ; left > 0U;) {
        unsigned count = left < 256U ? left : 256U;
        audio_fill(&a, block, count);
        for (unsigned i = 0; i < count; ++i) {
            if (previous < AUDIO_CENTER && block[i] >= AUDIO_CENTER) {
                ++rising;
            }
            previous = block[i];
        }
        left -= count;
    }
    assert(rising >= 439U && rising <= 441U);
    printf("AUDIO 120 BPM et %u passages montants pour la note en une seconde\n", rising);
}

/** @brief Transport d’affichage simulé qui garde la propriété du tampon entre send et poll. */
typedef struct {
    Rect rect; /**< Bande actuellement en vol dans le repère écran. */
    const uint8_t *data; /**< Adresse du tampon emprunté au renderer, sans copie immédiate. */
    uint32_t crc; /**< CRC permettant de détecter une modification du tampon pendant le transfert. */
    bool waiting; /**< true tant que poll n’a pas terminé la copie vers screen. */
} MockDisplay;
static MockDisplay transport;

/**
 * @brief Enregistrer la bande transférée et maintenir le transport simulé occupé pendant plusieurs sondages.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in,out] ctx Contexte du faux port mémoire ou d’affichage transmis par le module testé.
 * @param[in] r Rectangle écran de la bande transférée.
 * @param[in] data Octets RGB565 à conserver jusqu’à la fin du transfert simulé.
 * @param[in] n Nombre d’octets de l’opération.
 * @return true si l’opération simulée réussit.
 */
static bool send(void *ctx, Rect r, const uint8_t *data, uint16_t n)
{
    MockDisplay *d = ctx;
    assert(n == 2 * r.w * r.h && !d->waiting);
    *d = (MockDisplay){r, data, storage_crc32(data, n), true};
    return true;
}

/**
 * @brief Faire progresser le transfert simulé puis copier ses octets dans l’image écran.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in,out] ctx Contexte du faux port mémoire ou d’affichage transmis par le module testé.
 * @return État du transfert simulé.
 */
static TransferState poll(void *ctx)
{
    MockDisplay *d = ctx;
    assert(d->waiting && storage_crc32(d->data, (size_t)(2 * d->rect.w * d->rect.h)) == d->crc);
    for (int y = 0; y < d->rect.h; ++y) {
        memcpy(&screen[2 * ((d->rect.y + y) * LCD_WIDTH + d->rect.x)],
               &d->data[2 * y * d->rect.w], (size_t)(2 * d->rect.w));
    }
    d->waiting = false;
    return TRANSFER_DONE;
}

/**
 * @brief Comparer pixel par pixel un rendu par bandes au rendu complet de la même scène.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 */
static void render_and_check(void)
{
    unsigned attempts = 0;
    while (!renderer_idle(&renderer, &app.dirty)) {
        renderer_task(&renderer, &app.dirty, app_render, &app);
        assert(++attempts < 500U);
    }
    Gfx reference = {LCD_RECT, expected};
    app_render(&app, &reference);
    assert(memcmp(expected, screen, sizeof(screen)) == 0);
}

/**
 * @brief Vérifier clipping, dommages, reprise sur erreur et équivalence des scènes animées.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 */
static void test_display(void)
{
    renderer.port = (DisplayPort){&transport, send, poll};
    app_init(&app);
    render_and_check();
    assert(renderer.pixels == LCD_WIDTH * LCD_HEIGHT && renderer.bands == 15U);
    app_open(&app, SCREEN_PAINT);
    render_and_check();
    uint32_t before = renderer.pixels;
    app_touch(&app, (TouchEvent){TOUCH_DOWN, 15, 15});
    renderer_task(&renderer, &app.dirty, app_render, &app);
    app_touch(&app, (TouchEvent){TOUCH_MOVE, 32, 22});
    app_touch(&app, (TouchEvent){TOUCH_UP, 32, 22});
    render_and_check();
    assert(renderer.pixels - before < 600U);

    app_open(&app, SCREEN_RACE);
    app.race.phase = GAME_RUN;
    render_and_check();
    before = renderer.pixels;
    for (unsigned i = 0; i < 25; ++i) {
        app_step(&app);
        app_step(&app);
        render_and_check();
    }
    printf("RENDU 25 lots course sur PC, moyenne %lu pixels par lot, equivalence par bandes\n",
           (unsigned long)((renderer.pixels - before) / 25U));
    app_open(&app, SCREEN_SPLASH);
    render_and_check();
    app_open(&app, SCREEN_SETTINGS);
    render_and_check();
    app_open(&app, SCREEN_RESPONSE);
    render_and_check();
    app_open(&app, SCREEN_PAINT);
    app.modal = MODAL_RGB;
    app.rgb[0] = 173;
    render_and_check();
    app_open(&app, SCREEN_RACE);
    race_init(&app.race, 6);
    app.race.phase = GAME_RUN;
    app.race.rival[0] = (Rival){.y = 193, .lane = 1, .active = true, .kind = VEHICLE_TRUCK};
    app_step(&app);
    assert(app.race.crash_ms > 0U);
    render_and_check();
    for (unsigned i = 0; i < 45U; ++i) {
        app_step(&app);
        render_and_check();
    }
    puts("RENDU accueil, parametres, RVB et animation BOUM identiques en bandes et en image complete");
}

/**
 * @brief Écrire une image PPM RGB sur PC à partir du rendu RGB565 réellement utilisé par le firmware.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in] dir Dossier où écrire les images PPM.
 * @param[in] name Nom de la capture sans extension.
 */
static void screenshot(const char *dir, const char *name)
{
    Gfx g = {LCD_RECT, expected};
    app_render(&app, &g);
    char file[300];
    int n = snprintf(file, sizeof(file), "%s/%s.ppm", dir, name);
    assert(n > 0 && (size_t)n < sizeof(file));
    FILE *out = fopen(file, "wb");
    assert(out != NULL);
    fprintf(out, "P6\n320 240\n255\n");
    for (size_t i = 0; i < sizeof(expected); i += 2) {
        unsigned color = (unsigned)expected[i] << 8 | expected[i + 1];
        fputc((int)((color >> 11) * 255U / 31U), out);
        fputc((int)(((color >> 5) & 63U) * 255U / 63U), out);
        fputc((int)((color & 31U) * 255U / 31U), out);
    }
    assert(fclose(out) == 0);
}

/**
 * @brief Préparer les scènes de démonstration puis les capturer avec le moteur graphique du projet.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in] dir Dossier où écrire les images PPM.
 */
static void captures(const char *dir)
{
    app_init(&app);
    screenshot(dir, "menu");
    app_start_splash(&app);
    screenshot(dir, "accueil");
    app_open(&app, SCREEN_SETTINGS);
    screenshot(dir, "parametres");
    app_open(&app, SCREEN_RESPONSE);
    app.tilt.response_x = 8;
    app.tilt.response_y = 6;
    screenshot(dir, "reactivite");
    app_open(&app, SCREEN_PAINT);
    app.paint.ink = 8;
    paint_segment(&app.paint, 25, 150, 118, 44);
    paint_segment(&app.paint, 118, 44, 208, 147);
    paint_segment(&app.paint, 208, 147, 285, 69);
    app.paint.ink = 2;
    paint_segment(&app.paint, 25, 170, 284, 170);
    app.paint.ink = 6;
    paint_segment(&app.paint, 75, 125, 161, 124);
    screenshot(dir, "paint");
    app.modal = MODAL_BRUSH;
    screenshot(dir, "taille-crayon");
    app.modal = MODAL_RGB;
    app.rgb[0] = 210;
    app.rgb[1] = 85;
    app.rgb[2] = 175;
    screenshot(dir, "couleur-rvb");
    app.modal = MODAL_COLORS;
    screenshot(dir, "palette");
    app_open(&app, SCREEN_GROOVE);
    audio_start(&app.audio);
    screenshot(dir, "groovebox");
    app_open(&app, SCREEN_MAZE);
    app.maze.phase = GAME_RUN;
    screenshot(dir, "labyrinthe");
    maze_init(&app.maze, 9);
    app.maze.phase = GAME_RUN;
    screenshot(dir, "labyrinthe-n10");
    app_open(&app, SCREEN_BREAKOUT);
    app.breakout.phase = GAME_RUN;
    screenshot(dir, "casse-briques");
    breakout_init(&app.breakout, 9);
    app.breakout.phase = GAME_RUN;
    screenshot(dir, "casse-briques-n10");
    app_open(&app, SCREEN_RACE);
    race_init(&app.race, 6);
    app.race.phase = GAME_RUN;
    app.race.rival[0] = (Rival){.y = 89, .lane = 0, .active = true, .kind = VEHICLE_BIKE};
    app.race.rival[1] = (Rival){.y = 51, .lane = 1, .active = true, .kind = VEHICLE_VAN};
    app.race.rival[2] = (Rival){.y = 119, .lane = 2, .active = true, .kind = VEHICLE_TRUCK};
    screenshot(dir, "course-auto");
    app.race.rival[0] = (Rival){.y = 193, .lane = 1, .active = true};
    race_step(&app.race, 0, &app.dirty);
    screenshot(dir, "collision-boum");
    race_init(&app.race, 1);
    app.race.phase = GAME_LOST;
    screenshot(dir, "defaite");
    app_open(&app, SCREEN_DIAG);
    app.diag.tft_id = 0x9341;
    app.diag.touch_controller = TOUCH_CTRL_TSC2007;
    app.diag.touch_ok = true;
    app.diag.accel_ok = true;
    app.diag.accel_name = "LIS3DSH";
    app.diag.flash_ok = true;
    TouchCalibration cal;
    assert(touch_cal_fit(&cal, cal_points));
    touch_cal_set(&app.touch_cal, TOUCH_CTRL_TSC2007, &cal);
    screenshot(dir, "diagnostic");
    app_open(&app, SCREEN_TOUCH_CAL);
    app_calibration_sample(&app, (RawTouch){0});
    screenshot(dir, "calibrage");
    app_open(&app, SCREEN_TILT);
    app.diag.accel_ok = true;
    app.tilt.valid = app.tilt.calibrated = true;
    app.tilt_guide.phase = TILT_GUIDE_PREVIEW;
    app.tilt_guide.axes = TILT_SWAP_XY;
    app.tilt.filtered_y = 0.30f;
    screenshot(dir, "axes");
}

/**
 * @brief Exécuter les assertions du banc PC, puis produire les captures si un dossier est fourni.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in] argc Nombre d’arguments transmis par le système.
 * @param[in] argv Arguments dont le dossier de captures optionnel.
 * @return Zéro si tous les scénarios atteignent leur fin.
 */
int main(int argc, char **argv)
{
    test_inputs();
    test_tilt_guide();
    test_touch_boot();
    test_button();
    test_storage();
    test_preferences_and_splash();
    test_paint_tools();
    test_calibration();
    test_games();
    test_diagnostic();
    test_app();
    test_game_flow();
    test_audio();
    test_display();
    if (argc == 2) {
        captures(argv[1]);
    }
    puts("Recette PC terminee.");
    return 0;
}
