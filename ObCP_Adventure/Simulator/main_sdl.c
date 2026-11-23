/** @file main_sdl.c
 * @brief Simulateur SDL2 : exactement le même moteur et le même rendu par bandes.
 * @details Le framebuffer complet existe seulement sur PC. La souris représente
 * un contact unique ; le clavier sert de référence indépendante de l'ergonomie.
 * SPDX-License-Identifier: MIT
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "oa_engine.h"
#include "oa_ui.h"
#include "oa_input.h"
#include "oa_render.h"
#include "oa_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static OaGame game;
static uint16_t screen[320 * 240];
static uint8_t memory[8192];
static const char *save_path = "obcp_adventure.sav";
static bool nv_read(void *ctx, uint16_t a, uint8_t *p, size_t n) {
    (void)ctx;
    if (a + n > 8192)
        return false;
    memcpy(p, memory + a, n);
    return true;
}
static bool nv_write(void *ctx, uint16_t a, const uint8_t *p, size_t n) {
    (void)ctx;
    if (a + n > 8192 || n > 32 || a / 32 != (a + n - 1) / 32)
        return false;
    memcpy(memory + a, p, n);
    return true;
}
static bool nv_ready(void *ctx) {
    (void)ctx;
    return true;
}
static bool save_file(void) {
    FILE *f = fopen(save_path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(memory, 1, 8192, f) == 8192;
    ok = fclose(f) == 0 && ok;
    return ok;
}
static void ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    (void)fprintf(f, "P6\n320 240\n255\n");
    for (unsigned i = 0; i < 320 * 240; i++) {
        uint16_t v = screen[i];
        uint8_t rgb[3] = {(uint8_t)(((v >> 11) & 31) * 255 / 31),
                          (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31)};
        (void)fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void compose(OaOverlay *o) {
    for (int y = 0; y < 240; y += 8)
        oa_render_band(&game, o, y, screen + y * 320);
}
static OaInput autopilot(void) {
    OaInput in = {.move = 1000, .run = true, .jump_held = true};
    int x = oa_pixel(game.player.body.x);
    bool danger = false;
    for (int d = 24; d <= 50; d += 4)
        if (oa_tile_at(&game, (x + d) / 16, 10) == OA_AIR ||
            oa_tile_at(&game, (x + d) / 16, 9) == OA_SPIKES)
            danger = true;
    uint8_t obstacle = oa_tile_at(&game, (x + 20) / 16,
                                  (oa_pixel(game.player.body.y) + game.player.body.h - 1) / 16);
    if (obstacle == OA_BLOCK || obstacle == OA_BRICK || obstacle == OA_GROUND)
        danger = true;
    for (unsigned i = 0; i < game.enemy_count; i++)
        if (game.enemies[i].alive) {
            int dx = oa_pixel(game.enemies[i].body.x) - x;
            if (dx > 0 && dx < 55)
                danger = true;
        }
    in.jump_pressed = game.player.body.ground && danger;
    if (!game.player.body.ground && game.player.body.vy > 0 && danger &&
        oa_tile_at(&game, x / 16, 10) != OA_AIR)
        in.move = 0;
    return in;
}
static void sound(SDL_AudioDeviceID device, OaSound event) {
    if (!device || event == OA_SOUND_NONE)
        return;
    int16_t samples[2400];
    unsigned hz = event == OA_SOUND_COIN
                      ? 880
                      : (event == OA_SOUND_HURT ? 110 : (event == OA_SOUND_CLEAR ? 660 : 440));
    /* Motif carré et enveloppe créés ici ; aucune mélodie de jeu existant. */
    for (unsigned i = 0; i < 2400; i++) {
        int amplitude = (int)(2400 - i) * 2;
        samples[i] = (int16_t)(((i * hz * 2 / 24000) % 2) ? amplitude : -amplitude);
    }
    if (SDL_GetQueuedAudioSize(device) < 24000)
        (void)SDL_QueueAudio(device, samples, sizeof(samples));
}
int main(int argc, char **argv) {
    bool headless = false, auto_play = false, spawn_boss = false, menu_only = false, level_given = false;
    unsigned ticks_limit = 0, level = 0;
    const char *capture = NULL, *metrics_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless"))
            headless = true;
        else if (!strcmp(argv[i], "--menu"))
            menu_only = true;
        else if (!strcmp(argv[i], "--autoplay"))
            auto_play = true;
        else if (!strcmp(argv[i], "--spawn-boss"))
            spawn_boss = true;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            ticks_limit = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) {
            level = (unsigned)strtoul(argv[++i], NULL, 10) - 1;
            level_given = true;
        }
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc)
            capture = argv[++i];
        else if (!strcmp(argv[i], "--metrics") && i + 1 < argc)
            metrics_path = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc)
            save_path = argv[++i];
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 2;
        }
    }
    if (level >= 10) {
        fputs("Level must be between 1 and 10\n", stderr);
        return 2;
    }
    if (headless && !ticks_limit)
        ticks_limit = 600;
    /* Avec SDL_MAIN_HANDLED, le point d'entrée reste le main C standard,
     * notamment sous Windows : SDL doit être averti explicitement. */
    SDL_SetMainReady();
    if (SDL_Init(headless ? SDL_INIT_TIMER : SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) !=
        0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    memset(memory, 0xFF, sizeof(memory));
    FILE *f = fopen(save_path, "rb");
    if (f) {
        /* Un fichier tronqué ou d'une autre taille est ignoré : EEPROM vierge. */
        if (fread(memory, 1, sizeof(memory), f) != sizeof(memory))
            memset(memory, 0xFF, sizeof(memory));
        fclose(f);
    }
    oa_game_init(&game);
    OaStorage store;
    (void)oa_storage_open(&store, (OaNv){NULL, nv_read, nv_write, nv_ready}, &game);
    /* --level 1 compte aussi : le niveau demandé est chargé directement. */
    if (headless || level_given) {
        (void)oa_game_load(&game, level, false);
    }
    if (menu_only)
        game.state = OA_MENU;
    if (spawn_boss) {
        const OaLevel *l = &oa_levels[level];
        game.player.body.x = OA_FX((l->main_end - 18) * 16);
        game.camera = game.player.body.x - OA_FX(80);
    }
    OaOverlay overlay = {0};
    (void)snprintf(overlay.lines[0], 48, "SIMULATION PC SDL2 - PAS DE CARTE");
    (void)snprintf(overlay.lines[1], 48, "MEME MOTEUR C / PHYSIQUE 60 HZ");
    (void)snprintf(overlay.lines[2], 48, "SOURIS : CONTACT UNIQUE");
    (void)snprintf(overlay.lines[3], 48, "F2 PARAMETRES / F3 DIAGNOSTIC");
    (void)snprintf(overlay.lines[4], 48, "F5 SAUVER / J L : INCLINAISON");
    (void)snprintf(overlay.lines[5], 48, "F7 NIVEAU SUIVANT : DEBUG PC");
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_AudioDeviceID audio = 0;
    if (!headless) {
        window = SDL_CreateWindow("ObCP Adventure v" OA_VERSION " - SIMULATION PC", SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, 960, 720, SDL_WINDOW_RESIZABLE);
        if (window)
            renderer = SDL_CreateRenderer(window, -1,
                                          SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer && window)
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!window || !renderer) {
            fprintf(stderr, "SDL display: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }
        SDL_RenderSetLogicalSize(renderer, 320, 240);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                    320, 240);
        if (!texture) {
            fprintf(stderr, "SDL texture: %s\n", SDL_GetError());
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        SDL_AudioSpec desired = {0};
        desired.freq = 24000;
        desired.format = AUDIO_S16SYS;
        desired.channels = 1;
        desired.samples = 512;
        audio = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, 0);
        if (audio)
            SDL_PauseAudioDevice(audio, 0);
    }
    FILE *metrics = metrics_path ? fopen(metrics_path, "w") : NULL;
    if (metrics)
        fputs("tick,level,render_us,x,y,state\n", metrics);
    OaTouchInput touch_state = {0};
    OaTiltInput tilt = {0};
    uint8_t previous[SDL_NUM_SCANCODES] = {0};
    bool running = true;
    uint64_t frequency = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter(),
             debt = 0;
    unsigned ticks = 0, rendered = 0;
    uint32_t last_fps = SDL_GetTicks(), frame_count = 0;
    while (running && (!ticks_limit || ticks < ticks_limit)) {
        OaInput input = {0};
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                running = false;
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                if (e.key.keysym.sym == SDLK_F2)
                    oa_ui_open_settings(&game);
                if (e.key.keysym.sym == SDLK_F3)
                    game.state = OA_DIAGNOSTIC;
                if (e.key.keysym.sym == SDLK_F5)
                    game.save_requested = true;
                if (e.key.keysym.sym == SDLK_F7)
                    (void)oa_game_load(&game, (game.level + 1) % 10, false);
                if (e.key.keysym.sym == SDLK_w)
                    oa_storage_allow_initialize(&store);
            }
        }
        uint64_t now = SDL_GetPerformanceCounter();
        if (headless)
            debt = frequency;
        else {
            debt += (now - last) * 60;
            last = now;
            if (debt > frequency * 6)
                debt = frequency * 6;
        }
        if (debt < frequency) {
            SDL_Delay(1);
            continue;
        }
        debt -= frequency;
        const uint8_t *keys = SDL_GetKeyboardState(NULL);
        if (auto_play)
            input = autopilot();
        else if (!headless) {
            int mx, my;
            uint32_t buttons = SDL_GetMouseState(&mx, &my);
            float fx, fy;
            SDL_RenderWindowToLogical(renderer, mx, my, &fx, &fy);
            OaTouchSample sample = {.x = (int16_t)fx,
                                    .y = (int16_t)fy,
                                    .valid = true,
                                    .down = (buttons & SDL_BUTTON_LMASK) != 0};
            input = oa_input_touch(&touch_state, &sample, &game.settings);
            int key_axis = (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D] ? 1000 : 0) -
                           (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A] ? 1000 : 0);
            if (key_axis)
                input.move = (int16_t)key_axis;
            input.jump_pressed |= keys[SDL_SCANCODE_SPACE] && !previous[SDL_SCANCODE_SPACE];
            input.jump_held |= keys[SDL_SCANCODE_SPACE] != 0;
            input.run |= keys[SDL_SCANCODE_LSHIFT] != 0;
            input.pause = keys[SDL_SCANCODE_ESCAPE] && !previous[SDL_SCANCODE_ESCAPE];
            input.back = input.pause;
            input.confirm = keys[SDL_SCANCODE_RETURN] && !previous[SDL_SCANCODE_RETURN];
            input.menu_delta =
                (int8_t)((keys[SDL_SCANCODE_DOWN] && !previous[SDL_SCANCODE_DOWN] ? 1 : 0) -
                         (keys[SDL_SCANCODE_UP] && !previous[SDL_SCANCODE_UP] ? 1 : 0));
            input.ui_next_page = keys[SDL_SCANCODE_TAB] && !previous[SDL_SCANCODE_TAB];
            input.ui_adjust =
                (int8_t)((keys[SDL_SCANCODE_RIGHT] && !previous[SDL_SCANCODE_RIGHT] ? 1 : 0) -
                         (keys[SDL_SCANCODE_LEFT] && !previous[SDL_SCANCODE_LEFT] ? 1 : 0));
            if (game.settings.control_mode) {
                int16_t mg[3] = {
                    (int16_t)((keys[SDL_SCANCODE_L] ? 500 : 0) - (keys[SDL_SCANCODE_J] ? 500 : 0)),
                    0, 1000};
                oa_tilt_sample(&tilt, mg, ticks * 1000 / 60);
                input.move = oa_tilt_axis(&tilt, &game.settings, ticks * 1000 / 60);
            }
        }
        memcpy(previous, keys, sizeof(previous));
        oa_game_step(&game, &input);
        ticks++;
        if (game.state == OA_CALIBRATION) {
            /* Il n'y a pas de dalle brute ni de carte physique sur PC. Ne pas
             * fabriquer une calibration matérielle : le simulateur explique ce point. */
            game.state = OA_DIAGNOSTIC;
            (void)snprintf(overlay.lines[6], 48, "ETALONNAGE MATERIEL SUR LA CARTE");
        }
        if (game.save_requested && !store.state) {
            if (oa_storage_request(&store, &game, ticks * 1000 / 60))
                game.save_requested = false;
        }
        bool was_writing = store.state != 0;
        oa_storage_task(&store, ticks * 1000 / 60);
        if (was_writing && !store.state && !store.error)
            (void)save_file();
        sound(audio, game.sound);
        if (ticks % 2 == 0) {
            uint64_t start = SDL_GetPerformanceCounter();
            compose(&overlay);
            uint64_t duration = SDL_GetPerformanceCounter() - start;
            rendered++;
            frame_count++;
            if (metrics)
                fprintf(metrics, "%u,%u,%.2f,%ld,%ld,%u\n", ticks, game.level + 1,
                        (double)duration * 1000000.0 / (double)frequency,
                        (long)oa_pixel(game.player.body.x), (long)oa_pixel(game.player.body.y),
                        game.state);
            if (renderer) {
                SDL_UpdateTexture(texture, NULL, screen, 320 * 2);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                SDL_RenderPresent(renderer);
            }
        }
        if (!headless && SDL_GetTicks() - last_fps >= 1000) {
            overlay.fps10 = (uint16_t)(frame_count * 10);
            frame_count = 0;
            last_fps = SDL_GetTicks();
        }
    }
    compose(&overlay);
    if (capture)
        ppm(capture);
    if (store.state)
        for (unsigned i = 0; i < 30; i++)
            oa_storage_task(&store, ticks * 1000 / 60 + i);
    if (!store.error)
        (void)save_file();
    if (metrics)
        fclose(metrics);
    printf("SIMULATION ONLY ticks=%u rendered=%u level=%u state=%u x=%ld y=%ld score=%lu\n", ticks,
           rendered, game.level + 1, game.state, (long)oa_pixel(game.player.body.x),
           (long)oa_pixel(game.player.body.y), (unsigned long)game.score);
    if (audio)
        SDL_CloseAudioDevice(audio);
    if (texture)
        SDL_DestroyTexture(texture);
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
