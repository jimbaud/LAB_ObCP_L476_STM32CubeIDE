/**
 * @file board.c
 * @brief Ordonnancement coopératif et liaison avec la carte réelle.
 * @ingroup obcp_board
 *
 * Ce module possède App, Renderer et les états d’entrée. Il cadence les tâches sans RTOS,
 * arbitre l’affichage, prépare les demandes flash et expose obcp_live au débogueur. Les
 * callbacks d’interruption se limitent à leurs pilotes.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
/** @var app
 * @brief État applicatif unique de la carte, conservé entre toutes les itérations.
 */
/** @var renderer
 * @brief Tampon et progression du moteur d'affichage.
 */
/** @var touch
 * @brief Dernier contact converti pour produire les événements tactiles.
 */
/** @var touch_device
 * @brief Type et identifiant du contrôleur présent.
 */
/** @var touch_filter_state
 * @brief Historique de médiane et de confirmation du résistif.
 */
/** @var button
 * @brief État d'anti-rebond du bouton PC13.
 */
/** @var boot_touch
 * @brief Mémoire du geste résistif de secours au démarrage.
 */
/** @var flash
 * @brief Callbacks du port de flash interne validé au démarrage.
 */
/** @var last_sample
 * @brief Tick du dernier échantillonnage de l'accéléromètre.
 */
/** @var last_game
 * @brief Base de temps du prochain pas fixe de simulation.
 */
/** @var last_diag
 * @brief Tick du dernier rafraîchissement des valeurs de diagnostic.
 */
/** @var last_rates
 * @brief Tick du début de la fenêtre de calcul des débits.
 */
/** @var last_flash_job
 * @brief Tick de début d'attente du message avant une opération flash.
 */
/** @var last_frames
 * @brief Nombre de lots au début de la fenêtre de débit.
 */
/** @var last_pixels
 * @brief Nombre de pixels au début de la fenêtre de débit.
 */
/** @var last_touch
 * @brief Tick du dernier départ de lecture tactile.
 */
/** @var last_touch_probe
 * @brief Tick de la dernière tentative de reconnaissance tactile.
 */
/** @var touch_failures
 * @brief Nombre d'erreurs tactiles consécutives, seuil de reprise à trois.
 */
/** @var lcd_bus_ok
 * @brief true si les commandes d'initialisation TFT ont été transmises.
 */
/** @var touch_sampling
 * @brief true quand une acquisition tactile doit continuer avant le prochain départ périodique.
 */
/** @var touch_wait_release
 * @brief true si le geste de calibrage ou d'accueil doit être relâché avant l'usage normal.
 */
/** @var led_since
 * @brief Tick de début de l'impulsion de LED courante.
 */
/** @var led_duration
 * @brief Durée de l'impulsion en millisecondes. Zéro lorsque la LED est au repos.
 */
/** @var obcp_live
 * @brief Vue volatile du diagnostic pour Live Expressions dans CubeIDE.
 */

#include "obcp_board.h"
#include "obcp_app.h"
#include "obcp_config.h"
#include "obcp_flash.h"
#include "obcp_panel.h"
#include "obcp_pwm.h"

#include "accelerometer.h"
#include "main.h"
#include "spi.h"

static App app;
static Renderer renderer;
static TouchState touch;
static TouchDevice touch_device;
static ResistiveFilter touch_filter_state;
static ButtonState button;
static TouchBoot boot_touch;
static Storage flash;
static uint32_t last_sample, last_game, last_diag, last_rates, last_flash_job;
static uint32_t last_frames, last_pixels, last_touch, last_touch_probe;
static uint8_t touch_failures;
static bool lcd_bus_ok, touch_sampling, touch_wait_release;
static uint32_t led_since;
static uint16_t led_duration;

/* Valeurs prévues pour Live Expressions dans CubeIDE. */
volatile struct {
    uint32_t frames_per_second; /**< Lots de rectangles achevés par seconde, hors compteur d'images complet. */
    uint32_t pixels_per_second; /**< Pixels acheminés vers le TFT par seconde. */
    uint32_t lcd_errors; /**< Nombre d'erreurs signalées par le moteur de transport TFT. */
    uint32_t audio_missed; /**< Nombre cumulé d'échéances ou erreurs audio. */
    uint32_t last_flash_ms; /**< Durée de la dernière opération de stockage en millisecondes. */
    uint32_t accel_reads; /**< Nombre de lectures d’accéléromètre réussies. */
    uint32_t touch_errors; /**< Nombre d’erreurs de lecture tactile. */
    uint16_t lcd_id; /**< Identifiant ILI9341 lu, normalement 0x9341. */
    uint16_t touch_id; /**< Identifiant du contrôleur s'il en possède un. Zéro pour TSC2007. */
    uint16_t touch_raw_x; /**< Dernière coordonnée X native du doigt. */
    uint16_t touch_raw_y; /**< Dernière coordonnée Y native du doigt. */
    uint16_t touch_z1; /**< Dernière mesure Z1 du tactile. */
    uint16_t touch_z2; /**< Dernière mesure Z2 du tactile. */
    int16_t touch_x; /**< Dernière abscisse écran valide du doigt. */
    int16_t touch_y; /**< Dernière ordonnée écran valide du doigt. */
    uint8_t touch_hits; /**< Masque des cinq cibles tactiles validées. */
    uint8_t flash_ok; /**< true quand le port de stockage physique a été obtenu. */
    uint8_t lcd_io_ok; /**< Initialisation TFT réussie côté HAL. */
    uint8_t touch_ok; /**< true quand le tactile reconnu est disponible. */
    uint8_t touch_down; /**< Dernier état de contact converti accepté. */
    uint8_t diag_pattern; /**< Mire de diagnostic de zéro à quatre. */
    uint8_t touch_controller; /**< Type de tactile détecté et choix du protocole à utiliser. */
    uint8_t touch_calibrated; /**< Profil résistif présent en RAM pour le contrôleur actif. */
    uint8_t tilt_axes; /**< Trois bits de permutation et de signes appliqués aux jeux. */
} obcp_live;

/**
 * @brief Démarrer une impulsion de LED d'état.
 *
 * Sélectionne vert pendant 300 ms pour un succès ou rouge pendant 500 ms pour un échec. Le
 * début et la durée sont enregistrés pour une extinction ultérieure sans attente active.
 * @param[in] ok true pour un état réussi, false pour signaler un échec.
 * @param[in] now Tick courant en millisecondes non signées.
 */
static void led_signal(bool ok, uint32_t now)
{
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, ok ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, ok ? GPIO_PIN_RESET : GPIO_PIN_SET);
    led_since = now;
    led_duration = ok ? 300U : 500U;
}

/**
 * @brief Éteindre les sorties de LED à l'échéance.
 *
 * La soustraction de ticks non signés conserve le délai lors du débordement du compteur.
 * Une durée nulle indique qu'aucune impulsion n'est active.
 * @param[in] now Tick courant en millisecondes non signées.
 */
static void led_update(uint32_t now)
{
    if (led_duration != 0U && now - led_since >= led_duration) {
        HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET);
        led_duration = 0;
    }
}

/**
 * @brief Actualiser la reconnaissance du tactile.
 *
 * Le sondage peut être différé par SPI1 occupé. Un nouveau contrôleur réinitialise le
 * filtre, met à jour le diagnostic et peut ouvrir un calibrage si son profil manque.
 * @param[in] auto_calibrate true pour proposer le calibrage d’un nouveau contrôleur résistif sans
 *   profil.
 * @return true si le sondage a été effectué, même si aucun contrôleur n'a été reconnu.
 */
static bool probe_touch(bool auto_calibrate)
{
    TouchDevice detected;
    if (!touch_probe(&detected)) {
        return false;
    }
    bool ready = detected.controller != TOUCH_CTRL_NONE;
    bool changed = ready && detected.controller != touch_device.controller;
    if (changed || app.diag.touch_ok != ready) {
        damage_full(&app.dirty);
    }
    app.diag.touch_ok = ready;
    if (ready) {
        touch_device = detected;
        app.diag.touch_controller = detected.controller;
        app.diag.touch_id = detected.id;
        if (changed) {
            app.diag.hits = 0;
            touch_filter_state = (ResistiveFilter){0};
            if (app.screen == SCREEN_TOUCH_CAL) {
                app_open(&app, touch_resistive(detected.controller) ? SCREEN_TOUCH_CAL : SCREEN_DIAG);
            } else if (auto_calibrate && (app.screen == SCREEN_MENU || app.screen == SCREEN_SPLASH) &&
                touch_resistive(detected.controller) &&
                touch_cal_get(&app.touch_cal, detected.controller) == NULL) {
                if (app.screen == SCREEN_SPLASH) {
                    app.splash_target = SCREEN_TOUCH_CAL;
                } else {
                    app_open(&app, SCREEN_TOUCH_CAL);
                }
            }
        }
    }
    touch_failures = 0;
    return true;
}

/*
 * Initialiser l'application après les périphériques CubeMX.
 *
 * Charge les objets persistants, initialise TFT et capteur, sonde le tactile, prépare
 * l'accueil et les bases de temps. À appeler une fois depuis le bloc USER CODE 2 de
 * main().
 */
void obcp_start(void)
{
    bool boot_pressed = HAL_GPIO_ReadPin(USER_BUTTON_GPIO_Port, USER_BUTTON_Pin) == GPIO_PIN_RESET;
    app_init(&app);
    pwm_audio_init();
    flash = obcp_flash_storage();
    app.diag.flash_ok = flash.read != NULL;
    app_read_book(&app, &flash);
    app_read_calibration(&app, &flash);
    app_read_axes(&app, &flash);
    app_read_preferences(&app, &flash);

    lcd_bus_ok = lcd_init(&app.diag.tft_id);
    renderer.port = lcd_port();
    (void)probe_touch(!boot_pressed);
    app.diag.accel_ok = accel_init(&hspi3) != ACCEL_NONE;
    app.diag.accel_name = accel_chip_name();

    if (boot_pressed) {
        app_open(&app, SCREEN_DIAG);
    }
    uint32_t now = HAL_GetTick();
    button_init(&button, boot_pressed, now);
    boot_touch.armed = app.screen == SCREEN_MENU && touch_resistive(touch_device.controller);
    app_start_splash(&app);
    led_signal(lcd_bus_ok && app.diag.touch_ok && app.diag.accel_ok, now);
    last_sample = now;
    last_game = now;
    last_rates = now;
    last_touch = now;
    last_touch_probe = now;
}

/**
 * @brief Faire avancer une acquisition tactile et distribuer son résultat.
 *
 * WAIT poursuit une conversion ou attend le bus. EMPTY garde le dernier contact. Une
 * erreur répétée relance la reconnaissance. Les entrées de secours, le calibrage brut et
 * les coordonnées écran sont traités dans cet ordre.
 * @param[in] now Tick courant en millisecondes non signées.
 */
static void sample_touch(uint32_t now)
{
    if (!touch_sampling) {
        if (now - last_touch < 10U) {
            return;
        }
        last_touch = now;
    }
    RawTouch raw;
    TouchRead result = touch_read(&touch_device, &raw);
    /* WAIT annonce une conversion en cours ou un bus occupé. Il faut revenir sans inventer un relâchement du doigt. */
    touch_sampling = result == TOUCH_READ_WAIT;
    if (touch_sampling) {
        return;
    }
    if (result == TOUCH_READ_ERROR) {
        ++obcp_live.touch_errors;
        if (++touch_failures >= 3U) {
            app.diag.touch_ok = false;
            boot_touch.armed = false;
            last_touch_probe = now;
            touch_filter_state = (ResistiveFilter){0};
            app_touch(&app, touch_update(&touch, false, 0, 0));
            if (app.screen == SCREEN_TOUCH_CAL) {
                touch_cal_interrupt(&app.touch_cal_session);
                damage_full(&app.dirty);
            }
        }
        return;
    }
    touch_failures = 0;
    /* EMPTY signifie absence de nouvel échantillon dans la FIFO. Le dernier état tactile reste utilisable. */
    if (result == TOUCH_READ_EMPTY) {
        return;
    }
    app.diag.z1 = raw.z1;
    app.diag.z2 = raw.z2;
    if (raw.down) {
        app.diag.raw_x = raw.point.x;
        app.diag.raw_y = raw.point.y;
    }
    if (app.screen != SCREEN_MENU && app.screen != SCREEN_SPLASH) {
        boot_touch.armed = false;
    }
    BootTouchEvent boot_event = touch_boot_sample(&boot_touch, raw.down, now);
    if (boot_event == BOOT_TOUCH_CALIBRATE) {
        app_force_touch_calibration(&app);
    }
    if (boot_event != BOOT_TOUCH_NONE) {
        return;
    }
    if (app.screen == SCREEN_SPLASH) {
        touch_wait_release = raw.down;
        return;
    }
    if (app.screen == SCREEN_TOUCH_CAL) {
        /* Le calibrage travaille dans le repère brut et consomme ce geste. */
        touch = (TouchState){0};
        touch_filter_state = (ResistiveFilter){0};
        touch_wait_release = raw.down;
        app_calibration_sample(&app, raw);
        return;
    }
    /* Consommer le relâchement du geste ayant traversé l’accueil ou le calibrage évite un clic involontaire sur la nouvelle page. */
    if (touch_wait_release) {
        touch_wait_release = raw.down;
        return;
    }

    bool resistive = touch_resistive(touch_device.controller);
    if (resistive) {
        raw = touch_filter(&touch_filter_state, raw);
    }
    bool pressed = raw.down;
    int16_t x = 0, y = 0;
    if (pressed) {
        if (resistive) {
            const TouchCalibration *cal = touch_cal_get(&app.touch_cal, touch_device.controller);
            pressed = touch_cal_map(cal, raw.point, &x, &y);
        } else {
            pressed = touch_landscape(raw.point.x, raw.point.y, OBCP_TOUCH_FLIPPED, &x, &y);
        }
    }
    if (pressed) {
        app.diag.x = x;
        app.diag.y = y;
    }
    app_touch(&app, touch_update(&touch, pressed, x, y));
}

/**
 * @brief Servir le tactile et échantillonner l'accéléromètre.
 *
 * Vise un départ toutes les 10 ms pour le capteur. Met à jour le filtre et fait avancer
 * l'assistant d'axes quand sa page est ouverte. Les transitions de disponibilité
 * invalident la scène.
 * @param[in] now Tick courant en millisecondes non signées.
 */
static void sample_inputs(uint32_t now)
{
    if (app.diag.touch_ok) {
        sample_touch(now);
    } else if (now - last_touch_probe >= 500U && probe_touch(true)) {
        last_touch_probe = now;
    }
    if (now - last_sample < 10U) {
        return;
    }
    last_sample = now;
    if (app.diag.accel_ok) {
        bool ready = app.tilt.calibrated && app.tilt.valid;
        accel_g_t g = {0};
        bool ok = accel_read(&g);
        if (ok) {
            ++obcp_live.accel_reads;
        }
        tilt_sample(&app.tilt, ok, g.x, g.y, g.z);
        bool guide_changed = app.screen == SCREEN_TILT && tilt_guide_sample(&app.tilt_guide, &app.tilt);
        if (guide_changed || ready != (app.tilt.calibrated && app.tilt.valid)) {
            damage_full(&app.dirty);
        }
    }
}

/**
 * @brief Lire PC13 et transmettre un événement débouncé.
 *
 * La lecture matérielle est active à zéro. button_update() transforme ce niveau en appui
 * court, long ou aucun événement avant l'appel de l'application.
 * @param[in] now Tick courant en millisecondes non signées.
 */
static void sample_button(uint32_t now)
{
    bool pressed = HAL_GPIO_ReadPin(USER_BUTTON_GPIO_Port, USER_BUTTON_Pin) == GPIO_PIN_RESET;
    app_button(&app, button_update(&button, pressed, now));
}

/**
 * @brief Publier les mesures et les compteurs de diagnostic.
 *
 * Les débits sont calculés sur un intervalle d'au moins une seconde. Les autres champs
 * copient l'état du dernier contact, des pilotes et du stockage vers obcp_live.
 * @param[in] now Tick courant en millisecondes non signées.
 */
static void update_rates(uint32_t now)
{
    /* La soustraction de ticks uint32_t reste correcte au rebouclage, tant que les intervalles observés restent courts devant 2^32 ms. */
    uint32_t elapsed = now - last_rates;
    if (elapsed >= 1000U) {
        app.diag.frames_per_second = (renderer.batches - last_frames) * 1000U / elapsed;
        app.diag.pixels_per_second = (renderer.pixels - last_pixels) * 1000U / elapsed;
        last_frames = renderer.batches;
        last_pixels = renderer.pixels;
        last_rates = now;
        obcp_live.frames_per_second = app.diag.frames_per_second;
        obcp_live.pixels_per_second = app.diag.pixels_per_second;
    }
    app.diag.audio_missed = pwm_audio_missed();
    app.diag.lcd_errors = renderer.errors;
    obcp_live.audio_missed = app.diag.audio_missed;
    obcp_live.lcd_errors = app.diag.lcd_errors;
    obcp_live.last_flash_ms = app.diag.last_save_ms;
    obcp_live.lcd_id = app.diag.tft_id;
    obcp_live.touch_id = app.diag.touch_id;
    obcp_live.touch_hits = app.diag.hits;
    obcp_live.flash_ok = app.diag.flash_ok;
    obcp_live.lcd_io_ok = lcd_bus_ok;
    obcp_live.touch_ok = app.diag.touch_ok;
    obcp_live.touch_down = touch.down;
    obcp_live.touch_raw_x = app.diag.raw_x;
    obcp_live.touch_raw_y = app.diag.raw_y;
    obcp_live.touch_z1 = app.diag.z1;
    obcp_live.touch_z2 = app.diag.z2;
    obcp_live.touch_x = app.diag.x;
    obcp_live.touch_y = app.diag.y;
    obcp_live.touch_controller = (uint8_t)app.diag.touch_controller;
    obcp_live.touch_calibrated = touch_cal_get(&app.touch_cal, app.diag.touch_controller) != NULL;
    obcp_live.tilt_axes = app.tilt.axes;
    obcp_live.diag_pattern = app.test_color;
}

/*
 * Servir une itération de toutes les tâches de l'application.
 *
 * Priorise l'audio, les entrées et au plus quatre pas de simulation de rattrapage. Sert le
 * rendu puis un travail flash lorsque son message a pu être affiché. À appeler
 * continuellement depuis USER CODE 3.
 */
void obcp_task(void)
{
    uint32_t now = HAL_GetTick();
    pwm_audio_task(&app.audio, app_wants_audio(&app));
    sample_button(now);
    sample_inputs(now);
    unsigned steps = 0;
    /* Le pas physique reste fixé à 20 ms. Au plus quatre rattrapages évitent qu’un retard bloque indéfiniment le service audio et tactile. */
    while (now - last_game >= GAME_STEP_MS && steps < 4U) {
        last_game += GAME_STEP_MS;
        app_step(&app);
        ++steps;
    }
    if (now - last_game >= GAME_STEP_MS) {
        last_game = now;
    }
    update_rates(now);
    if (now - last_diag >= 160U) {
        last_diag = now;
        app_diag_refresh(&app);
    }
    if (lcd_bus_ok) {
        renderer_task(&renderer, &app.dirty, app_render, &app);
    }
    if (app.job == JOB_NONE) {
        last_flash_job = now;
    } else if (renderer_idle(&renderer, &app.dirty) || now - last_flash_job >= 450U) {
        /* Laisser passer le bandeau avant l'effacement. Arrêter l'audio
         * explicitement avant de bloquer la boucle par l'écriture flash. */
        pwm_audio_task(&app.audio, false);
        uint32_t start = HAL_GetTick();
        StoreResult result = app_store_task(&app, &flash);
        uint32_t end = HAL_GetTick();
        app.diag.last_save_ms = end - start;
        last_game = end;
        led_signal(result == STORE_OK, end);
    }
    led_update(HAL_GetTick());
}
