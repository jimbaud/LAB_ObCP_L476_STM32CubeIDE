/**
 ******************************************************************************
 * @file    Tests/fakes/fake_usb.h
 * @brief   Simulation du PC au bout du câble USB (classe CDC), pour les tests
 *          du programme sur PC : paquets envoyés par le PC, octets reçus.
 ******************************************************************************
 */
#ifndef FAKE_USB_H
#define FAKE_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void fake_usb_connect(void);            /* énumération : CDC_Init_FS() */
void fake_usb_disconnect(void);         /* CDC_DeInit_FS() */
void fake_usb_suspend(void);            /* HAL_PCD_SuspendCallback() */
void fake_usb_resume(void);             /* HAL_PCD_ResumeCallback() */

/* Le PC envoie un paquet (64 octets au plus) : il attend son tour si la
 * réception n'est pas autorisée (contrôle de flux). */
void fake_usb_host_send(const char *data, size_t len);
void fake_usb_host_send_str(const char *text);
/* Distribue le prochain paquet en attente si la carte l'accepte. */
bool fake_usb_pump(void);
size_t fake_usb_host_pending(void);    /* paquets en attente côté PC */

/* Le PC lit ce que la carte envoie (fin du transfert en cours). */
extern bool fake_usb_host_reads;        /* false : le PC ne lit plus (port fermé) */
void fake_usb_host_read(void);

/* Octets reçus par le PC depuis le dernier fake_usb_host_take(). */
size_t fake_usb_host_take(char *out, size_t size);
extern int fake_usb_rearm_count;        /* appels à USBD_CDC_ReceivePacket */
extern int fake_usb_transfers;          /* transferts IN démarrés */

#endif /* FAKE_USB_H */
