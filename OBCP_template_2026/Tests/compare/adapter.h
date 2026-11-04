/**
 ******************************************************************************
 * @file    Tests/compare/adapter.h
 * @brief   Interface commune aux deux versions du programme (2024 et 2026)
 *          pour leur faire jouer le même scénario (scenario.c) et comparer
 *          ce qu'elles envoient au PC et au téléphone.
 ******************************************************************************
 */
#ifndef ADAPTER_H
#define ADAPTER_H

#include <stddef.h>
#include <stdint.h>

extern const char *adapter_name;

void   adapter_init(void);                              /* démarrage (main.c) */
void   adapter_loop(void);                              /* un tour de boucle */
void   adapter_usb_connect(void);                       /* câble USB branché */
void   adapter_usb_send(const char *data, size_t len);  /* un paquet du PC */
size_t adapter_usb_take(char *out, size_t size);        /* octets reçus par le PC */

#endif /* ADAPTER_H */
