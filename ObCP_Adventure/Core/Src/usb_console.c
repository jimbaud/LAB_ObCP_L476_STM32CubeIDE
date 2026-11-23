/** @file usb_console.c @brief File SPSC de réception et copie d'émission CDC.
 * @details L'IRQ ne fait ni printf ni interprétation de commandes. Le tampon
 * envoyé à USB est statique et reste détenu jusqu'à la fin du transfert.
 * SPDX-License-Identifier: MIT */
#include "usb_console.h"
#include "usbd_cdc.h"
#include "usb_device.h"
#include "stm32l4xx_hal.h"
#include <string.h>
extern USBD_HandleTypeDef hUsbDeviceFS;
static uint8_t rx[128], tx[512];
static volatile uint16_t head, tail;
static volatile bool active;
void usb_console_on_init(void) {
    active = true;
}
void usb_console_on_deinit(void) {
    active = false;
}
void usb_console_on_suspend(void) {
    active = false;
}
void usb_console_on_resume(void) {
    active = true;
}
void usb_console_on_receive(const uint8_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        uint16_t next = (uint16_t)((head + 1) % sizeof(rx));
        if (next == tail)
            break;
        rx[head] = p[i];
        __DMB();
        head = next;
    }
    /* Réarmement immédiat : les octets ont été copiés, aucun pointeur USB ne
     * reste exposé au moteur. Une saturation abandonne les octets excédentaires. */
    (void)USBD_CDC_ReceivePacket(&hUsbDeviceFS);
}
int usb_console_getchar(void) {
    if (head == tail)
        return -1;
    int c = rx[tail];
    __DMB();
    tail = (uint16_t)((tail + 1) % sizeof(rx));
    return c;
}
bool usb_console_write(const uint8_t *p, uint32_t n) {
    if (!active || n > sizeof(tx) || hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED ||
        !hUsbDeviceFS.pClassData)
        return false;
    USBD_CDC_HandleTypeDef *cdc = hUsbDeviceFS.pClassData;
    if (cdc->TxState)
        return false;
    memcpy(tx, p, n);
    (void)USBD_CDC_SetTxBuffer(&hUsbDeviceFS, tx, n);
    return USBD_CDC_TransmitPacket(&hUsbDeviceFS) == USBD_OK;
}
