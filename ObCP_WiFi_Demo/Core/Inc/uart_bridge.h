/**
 ******************************************************************************
 * @file    uart_bridge.h
 * @brief   Passerelle port COM virtuel (USB) <-> UART du module ST67W611M1
 *          (USART2, PA2/PA3 = D1/D0), pour la première programmation du
 *          module avec l'outil QConn_Flash de ST, sans adaptateur USB-UART.
 *
 *          Sur l'ObCP, les broches D0/D1 du connecteur Arduino sont reliées
 *          directement à PA3/PA2 : l'UART du module (UART_TXD sur D0,
 *          UART_RXD sur D1) arrive donc sur l'USART2 du STM32. Le débit suit
 *          celui que le programme du PC choisit pour le port COM (500 kbit/s
 *          puis 2 Mbit/s pour QConn_Flash) ; l'échange se fait par DMA.
 ******************************************************************************
 */
#ifndef UART_BRIDGE_H
#define UART_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include "bridge_ctl.h"              /* bridge_end_t : causes de fin de session */

#ifdef __cplusplus
extern "C" {
#endif

/** Bilan d'une session de passerelle (affiché à la fin de « module boot »). */
typedef struct
{
  uint32_t     to_module;    /* octets PC -> module */
  uint32_t     from_module;  /* octets module -> PC */
  uint32_t     baud;         /* dernier débit demandé par le PC */
  uint32_t     seconds;      /* durée de la session */
  bridge_end_t end;
} bridge_result_t;

/**
 * @brief  Relaie les octets entre le port COM virtuel et l'UART du module
 *         jusqu'à la fin de la session (bouton, terminal rouvert, 2 min sans
 *         échange : voir bridge_ctl.h). Bloquante. Le module doit avoir été
 *         placé en mode téléchargement par l'appelant (cmd_module()).
 * @param  r  reçoit le bilan de la session.
 */
void uart_bridge_run(bridge_result_t *r);

#ifdef __cplusplus
}
#endif

#endif /* UART_BRIDGE_H */
