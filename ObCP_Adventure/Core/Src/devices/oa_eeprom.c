/** @file oa_eeprom.c @brief M95640 : WREN, pages alignées et scrutation WIP.
 * SPDX-License-Identifier: MIT */
#include "oa_devices.h"
static bool read_nv(void *ctx, uint16_t a, uint8_t *p, size_t n) {
    OaIo *io = ctx;
    if (n > 256 || a > 8192 || n > (size_t)(8192 - a))
        return false;
    uint8_t tx[259] = {3, (uint8_t)(a >> 8), (uint8_t)a}, rx[259];
    if (!io->spi(io->context, OA_DEV_EEPROM, tx, rx, n + 3, 2500000, 0))
        return false;
    for (size_t i = 0; i < n; i++)
        p[i] = rx[i + 3];
    return true;
}
static bool ready_nv(void *ctx) {
    OaIo *io = ctx;
    uint8_t tx[2] = {5, 0}, rx[2];
    return io->spi(io->context, OA_DEV_EEPROM, tx, rx, 2, 2500000, 0) && (rx[1] & 0x71) == 0;
}
static bool write_nv(void *ctx, uint16_t a, const uint8_t *p, size_t n) {
    OaIo *io = ctx;
    if (!n || n > 32 || a >= 8192 || n > (size_t)(8192 - a) || (a % 32) + n > 32)
        return false;
    uint8_t en = 6, reply = 0;
    if (!io->spi(io->context, OA_DEV_EEPROM, &en, &reply, 1, 2500000, 0))
        return false;
    uint8_t tx[35] = {2, (uint8_t)(a >> 8), (uint8_t)a}, rx[35];
    for (size_t i = 0; i < n; i++)
        tx[i + 3] = p[i];
    return io->spi(io->context, OA_DEV_EEPROM, tx, rx, n + 3, 2500000, 0);
}
OaNv oa_eeprom_interface(OaIo *io) {
    return (OaNv){io, read_nv, write_nv, ready_nv};
}
