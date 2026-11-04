/**
 ******************************************************************************
 * @file    bridge_ctl.h
 * @brief   Décision de fin de la passerelle « module boot » (port COM virtuel
 *          <-> UART du module), indépendante du matériel pour être testée sur
 *          PC. uart_bridge.c relaie les octets et appelle bridge_ctl_step() à
 *          chaque tour de boucle.
 *
 *  La fin ne dépend pas de l'état du signal DTR seul : QConn_Flash (option
 *  do_reset) manipule DTR et RTS pour tenter de redémarrer la puce et peut
 *  les laisser à 0 pendant toute la programmation. Fins retenues :
 *    - bouton SW1 (X-NUCLEO) ou User1 (ObCP) maintenu 50 ms ;
 *    - terminal rouvert : une fois que le PC et le module ont échangé des
 *      octets, front montant de DTR survenu après le dernier échange, suivi
 *      d'au moins 2 s de silence ;
 *    - silence de 2 min après les premiers échanges (effacement complet de
 *      la flash du module compris), ou de 15 min sans aucun échange.
 ******************************************************************************
 */
#ifndef BRIDGE_CTL_H
#define BRIDGE_CTL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRIDGE_REOPEN_QUIET_MS  2000U                    /* silence après réouverture  */
#define BRIDGE_IDLE_ACTIVE_MS   120000U                  /* silence après des échanges */
#define BRIDGE_IDLE_WAIT_MS     (15UL * 60UL * 1000UL)   /* aucun échange du tout      */
#define BRIDGE_BUTTON_MS        50U                      /* appui minimal (anti-rebond) */

/** Cause de fin de session. */
typedef enum
{
  BRIDGE_END_BUTTON = 0,     /* bouton SW1 (X-NUCLEO) ou User1 (ObCP) */
  BRIDGE_END_REOPENED,       /* terminal rouvert après la programmation */
  BRIDGE_END_IDLE,           /* plus aucun échange depuis longtemps */
  BRIDGE_END_NONE            /* la session continue */
} bridge_end_t;

/** Phase de la session. */
typedef enum
{
  BRIDGE_WAIT_CLOSE = 0,     /* le terminal qui a lancé « module boot » est encore ouvert */
  BRIDGE_WAIT_OPEN,          /* en attente de QConn_Flash */
  BRIDGE_ACTIVE
} bridge_phase_t;

/** État de la décision (une variable par session, initialisée par bridge_ctl_init()). */
typedef struct
{
  bridge_phase_t phase;
  bool           dtr_prev;
  bool           rise_after_traffic;   /* front montant de DTR depuis le dernier échange */
  uint32_t       t_activity;           /* dernier octet relayé (dans un sens ou l'autre) */
  uint32_t       t_btn;                /* début de l'appui (0 = relâché) */
  uint32_t       to_module;
  uint32_t       from_module;
} bridge_ctl_t;

/**
 * @brief  Début de session.
 * @param  c    état à initialiser.
 * @param  dtr  état du signal DTR (terminal ouvert).
 * @param  now  temps courant (ms).
 */
void         bridge_ctl_init(bridge_ctl_t *c, bool dtr, uint32_t now);

/**
 * @brief  Un tour de boucle de la passerelle : faut-il terminer la session ?
 * @param  c       état de la session.
 * @param  now     temps courant (ms).
 * @param  dtr     état du signal DTR.
 * @param  in      octets relayés PC -> module depuis le tour précédent.
 * @param  out     octets relayés module -> PC depuis le tour précédent.
 * @param  button  un bouton (User1 ou SW1) est appuyé.
 * @retval BRIDGE_END_NONE tant que la session continue, sinon sa cause de fin.
 */
bridge_end_t bridge_ctl_step(bridge_ctl_t *c, uint32_t now, bool dtr, uint32_t in, uint32_t out,
                             bool button);

#ifdef __cplusplus
}
#endif

#endif /* BRIDGE_CTL_H */
