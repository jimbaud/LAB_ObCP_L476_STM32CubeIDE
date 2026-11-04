/**
 ******************************************************************************
 * @file    bridge_ctl.c
 * @brief   Fin de la passerelle « module boot » (voir bridge_ctl.h).
 *
 *  Logique pure (aucun accès au matériel) : elle ne reçoit que des valeurs
 *  (temps, DTR, octets relayés, bouton) et rend une décision. Elle peut donc
 *  être testée sur PC (Tests/test_app.c) en simulant toutes les situations.
 ******************************************************************************
 */
#include "bridge_ctl.h"

/* Voir bridge_ctl.h. Terminal encore ouvert : on attend d'abord sa fermeture */
void bridge_ctl_init(bridge_ctl_t *c, bool dtr, uint32_t now)
{
  c->phase = dtr ? BRIDGE_WAIT_CLOSE : BRIDGE_WAIT_OPEN;
  c->dtr_prev = dtr;
  c->rise_after_traffic = false;
  c->t_activity = now;
  c->t_btn = 0U;
  c->to_module = 0U;
  c->from_module = 0U;
}

/* Voir bridge_ctl.h */
bridge_end_t bridge_ctl_step(bridge_ctl_t *c, uint32_t now, bool dtr, uint32_t in, uint32_t out,
                             bool button)
{
  bool     rise = dtr && !c->dtr_prev;
  bool     exchanged;
  uint32_t quiet;

  c->dtr_prev = dtr;
  c->to_module += in;
  c->from_module += out;
  if ((in + out) > 0U)
  {
    c->t_activity = now;
    c->rise_after_traffic = false;
  }
  else if (rise)
  {
    c->rise_after_traffic = true;
  }
  quiet = now - c->t_activity;

  /* Bouton maintenu : arrêt dans toutes les phases */
  if (button)
  {
    if (c->t_btn == 0U)
    {
      c->t_btn = now | 1U;
    }
    else if ((now - c->t_btn) >= BRIDGE_BUTTON_MS)
    {
      return BRIDGE_END_BUTTON;
    }
  }
  else
  {
    c->t_btn = 0U;
  }

  switch (c->phase)
  {
    case BRIDGE_WAIT_CLOSE:
      if (!dtr)
      {
        c->phase = BRIDGE_WAIT_OPEN;
      }
      break;

    case BRIDGE_WAIT_OPEN:
      if (rise || (in > 0U))
      {
        c->phase = BRIDGE_ACTIVE;
      }
      break;

    default:
      break;
  }

  /* Le module a répondu : la programmation a eu lieu (ou a été tentée) */
  exchanged = (c->to_module > 0U) && (c->from_module > 0U);
  if (exchanged && (c->phase == BRIDGE_ACTIVE))
  {
    if (c->rise_after_traffic && dtr && (quiet >= BRIDGE_REOPEN_QUIET_MS))
    {
      return BRIDGE_END_REOPENED;
    }
    if (quiet >= BRIDGE_IDLE_ACTIVE_MS)
    {
      return BRIDGE_END_IDLE;
    }
  }
  else if (quiet >= BRIDGE_IDLE_WAIT_MS)
  {
    return BRIDGE_END_IDLE;
  }
  return BRIDGE_END_NONE;
}
