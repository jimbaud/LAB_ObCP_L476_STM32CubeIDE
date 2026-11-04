/**
 ******************************************************************************
 * @file    button.c
 * @brief   Bouton utilisateur avec anti-rebond non bloquant (voir button.h).
 *
 *  Machine à états : la variable s_state dit où l'on en est, et chaque appel
 *  de button_poll_pressed() regarde l'état du bouton et le temps écoulé pour
 *  décider s'il faut changer d'état. Aucune attente : la fonction rend la
 *  main tout de suite.
 ******************************************************************************
 */
#include "button.h"

#include <stdint.h>
#include "main.h"           /* HAL, étiquettes USER_BUTTON_Pin / _GPIO_Port */
#include "app_config.h"     /* BUTTON_DEBOUNCE_MS */

/* États de la machine à états (voir le schéma dans button.h). */
typedef enum
{
  BTN_RELEASED = 0,     /* bouton relâché, on attend un appui */
  BTN_CONFIRM_PRESS,    /* le bouton vient d'être enfoncé : est-ce un vrai appui ? */
  BTN_HELD,             /* appui signalé, on attend le relâchement */
  BTN_CONFIRM_RELEASE   /* le bouton vient d'être relâché : est-ce définitif ? */
} button_state_t;

static volatile bool  s_edge;      /* front descendant vu par l'interruption */
static button_state_t s_state;     /* état courant */
static uint32_t       s_since;     /* instant (ms) d'entrée dans l'état courant */

/* Voir button.h */
void button_init(void)
{
  s_state = BTN_RELEASED;
  s_edge  = false;
}

/* Voir button.h : appelée dans l'interruption, donc la plus courte possible. */
void button_on_interrupt(void)
{
  s_edge = true;
}

/* Voir button.h. GPIO_PIN_RESET = niveau 0 = bouton enfoncé (rappel vers 3,3 V). */
bool button_is_down(void)
{
  return HAL_GPIO_ReadPin(USER_BUTTON_GPIO_Port, USER_BUTTON_Pin) == GPIO_PIN_RESET;
}

/* Voir button.h */
bool button_poll_pressed(void)
{
  uint32_t now     = HAL_GetTick();     /* millisecondes depuis le démarrage */
  bool     down    = button_is_down();
  bool     pressed = false;

  switch (s_state)
  {
    case BTN_RELEASED:
      if (s_edge || down)              /* interruption reçue ou bouton déjà bas */
      {
        s_edge  = false;
        s_since = now;
        s_state = BTN_CONFIRM_PRESS;
      }
      break;

    case BTN_CONFIRM_PRESS:
      if (!down)
      {
        s_state = BTN_RELEASED;        /* simple rebond ou contact trop bref */
      }
      else if ((now - s_since) >= BUTTON_DEBOUNCE_MS)
      {
        pressed = true;                /* resté appuyé assez longtemps : appui ! */
        s_state = BTN_HELD;
      }
      break;

    case BTN_HELD:
      if (!down)
      {
        s_since = now;
        s_state = BTN_CONFIRM_RELEASE;
      }
      break;

    case BTN_CONFIRM_RELEASE:
    default:
      if (down)
      {
        s_state = BTN_HELD;            /* rebond au relâchement */
      }
      else if ((now - s_since) >= BUTTON_DEBOUNCE_MS)
      {
        s_edge  = false;               /* fronts dus aux rebonds : oubliés */
        s_state = BTN_RELEASED;
      }
      break;
  }
  return pressed;
}
