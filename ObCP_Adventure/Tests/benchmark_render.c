/** @file benchmark_render.c @brief Mesure PC et comptage exact des pixels modifiés.
 * Les durées SPI sont calculées à partir du trafic ; ce ne sont pas des
 * mesures de carte. Le même rendu et les mêmes empreintes que le STM32 servent
 * au comptage. SPDX-License-Identifier: MIT */
#include "oa_engine.h"
#include "oa_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
static OaGame game;
static uint16_t band[2560];
int main(int argc, char **argv) {
    FILE *out = argc > 1 ? fopen(argv[1], "w") : stdout;
    if (!out)
        return 1;
    fputs("level,frame,cpu_pc_us,dirty_pixels,spi10_min_us\n", out);
    for (unsigned n = 0; n < 10; n++) {
        uint32_t previous[30][10] = {{0}};
        oa_game_init(&game);
        (void)oa_game_load(&game, n, false);
        /* Scènes de rendu déterministes : un défilement de deux pixels par
         * image et des animations. Il ne s'agit pas d'un joueur automatique. */
        for (unsigned f = 0; f < 120; f++) {
            game.camera = OA_FX((int)f * 2);
            game.tick = f * 2;
            game.player.body.x = game.camera + OA_FX(110);
            clock_t begin = clock();
            unsigned pixels = 0;
            for (int y = 0; y < 240; y += 8) {
                oa_render_band(&game, NULL, y, band);
                for (unsigned block = 0; block < 10; block++) {
                    uint32_t h = oa_render_hash(band, block);
                    if (h != previous[y / 8][block])
                        pixels += 32 * 8;
                    previous[y / 8][block] = h;
                }
            }
            double us = (double)(clock() - begin) * 1000000.0 / CLOCKS_PER_SEC;
            fprintf(out, "%u,%u,%.2f,%u,%.2f\n", n + 1, f, us, pixels,
                    (double)pixels * 16.0 / 10.0);
        }
    }
    if (out != stdout)
        fclose(out);
    return 0;
}
