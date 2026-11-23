/** @file oa_lcd.c @brief Séquence ILI9341, RGB565 et paysage (MADCTL 0x28).
 * SPDX-License-Identifier: MIT */
#include "oa_board.h"
bool oa_lcd_init(void) {
    if (!oa_board_lcd_command(0x01, NULL, 0))
        return false;
    oa_board_delay_ms(150);
    static const uint8_t init[][17] = {{0xEF, 3, 0x03, 0x80, 0x02},
                                       {0xCF, 3, 0, 0xC1, 0x30},
                                       {0xED, 4, 0x64, 0x03, 0x12, 0x81},
                                       {0xE8, 3, 0x85, 0, 0x78},
                                       {0xCB, 5, 0x39, 0x2C, 0, 0x34, 0x02},
                                       {0xF7, 1, 0x20},
                                       {0xEA, 2, 0, 0},
                                       {0xC0, 1, 0x23},
                                       {0xC1, 1, 0x10},
                                       {0xC5, 2, 0x3E, 0x28},
                                       {0xC7, 1, 0x86},
                                       {0x36, 1, 0x28},
                                       {0x3A, 1, 0x55},
                                       {0xB1, 2, 0, 0x18},
                                       {0xB6, 3, 0x08, 0x82, 0x27},
                                       {0xF2, 1, 0},
                                       {0x26, 1, 1}};
    for (unsigned i = 0; i < sizeof(init) / sizeof(init[0]); i++)
        if (!oa_board_lcd_command(init[i][0], init[i] + 2, init[i][1]))
            return false;
    if (!oa_board_lcd_command(0x11, NULL, 0))
        return false;
    oa_board_delay_ms(120);
    return oa_board_lcd_command(0x29, NULL, 0);
}
