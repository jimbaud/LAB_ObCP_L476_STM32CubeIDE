/** @file usb_console.h @brief Console USB CDC sans attente, avec tampons statiques.
 * Les fonctions on_* sont appelées par le code USB du modèle ObCP.
 * SPDX-License-Identifier: MIT */
#ifndef USB_CONSOLE_H
#define USB_CONSOLE_H
#include <stdint.h>
#include <stdbool.h>
void usb_console_on_init(void);
void usb_console_on_deinit(void);
void usb_console_on_suspend(void);
void usb_console_on_resume(void);
void usb_console_on_receive(const uint8_t *p, uint32_t n);
bool usb_console_write(const uint8_t *p, uint32_t n);
int usb_console_getchar(void);
#endif
