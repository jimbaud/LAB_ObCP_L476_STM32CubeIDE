/** @file oa_accel.c @brief Registres et unités distincts pour LIS3DH et LIS3DSH.
 * SPDX-License-Identifier: MIT */
#include "oa_devices.h"
static bool read_reg(OaIo io, uint8_t reg, uint8_t *value) {
    uint8_t tx[2] = {(uint8_t)(0x80 | reg), 0}, rx[2];
    if (!io.spi(io.context, OA_DEV_ACCEL, tx, rx, 2, 5000000, 3))
        return false;
    *value = rx[1];
    return true;
}
static bool write_reg(OaIo io, uint8_t reg, uint8_t v) {
    uint8_t tx[2] = {reg, v}, rx[2];
    return io.spi(io.context, OA_DEV_ACCEL, tx, rx, 2, 5000000, 3) && read_reg(io, reg, &rx[0]) &&
           rx[0] == v;
}
uint8_t oa_accel_begin(OaIo io) {
    uint8_t id = 0;
    if (!io.spi || !read_reg(io, 0x0F, &id))
        return 0;
    if (id == 0x33) {
        if (!write_reg(io, 0x20, 0x57) || !write_reg(io, 0x23, 0x88))
            return 0;
    } else if (id == 0x3F) {
        if (!write_reg(io, 0x25, 0x10) || !write_reg(io, 0x20, 0x6F) || !write_reg(io, 0x24, 0))
            return 0;
    } else
        return 0;
    return id;
}
bool oa_accel_read(OaIo io, uint8_t id, int16_t mg[3]) {
    uint8_t status = 0;
    if (id != 0x33 && id != 0x3F)
        return false;
    if (!read_reg(io, 0x27, &status) || (status & 8) == 0)
        return false;
    uint8_t tx[7] = {id == 0x33 ? 0xE8 : 0xA8, 0, 0, 0, 0, 0, 0}, rx[7];
    if (!io.spi(io.context, OA_DEV_ACCEL, tx, rx, 7, 5000000, 3))
        return false;
    for (unsigned i = 0; i < 3; i++) {
        uint32_t u = rx[1 + i * 2] | ((uint32_t)rx[2 + i * 2] << 8);
        int32_t raw = u < 32768 ? (int32_t)u : (int32_t)u - 65536;
        mg[i] = (int16_t)(id == 0x33 ? raw / 16 : raw * 60 / 1000);
    }
    return true;
}
