/**
 ******************************************************************************
 * @file    led_fx.c
 * @brief   Effets de la LED tricolore (voir led_fx.h).
 *
 *  Principe : chaque effet garde sa couleur et son instant de fin ;
 *  led_fx_process() choisit, à chaque tour de boucle, l'effet actif le plus
 *  prioritaire et n'écrit dans les registres PWM que si la couleur change.
 *  Les fonctions led_fx_xxx() ne font que noter la demande : elles
 *  rendent la main aussitôt.
 ******************************************************************************
 */
#include "led_fx.h"

#include "app_config.h"
#include "main.h"                 /* HAL_GetTick */
#include "outputs.h"

/* Un effet temporaire : couleur et instant de fin. */
typedef struct
{
  bool        active;
  led_color_t color;
  uint32_t    end_ms;
} timed_effect_t;

static led_color_t    s_base;           /* fond */
static bool           s_base_timed;     /* fond à durée limitée */
static uint32_t       s_base_end_ms;
static timed_effect_t s_flash;          /* éclair d'activité */
static timed_effect_t s_show;           /* affichage temporaire (vague) */
static timed_effect_t s_blink;          /* clignotement */
static uint16_t       s_blink_period_ms;
static uint32_t       s_blink_start_ms;
static bool           s_activity = true;
static led_color_t    s_shown;          /* couleur actuellement affichée */
static bool           s_shown_valid;

/* Battement de cœur : un éclat vert très faible. */
static const led_color_t k_heartbeat = { 0U, 24U, 0U };
#define HEARTBEAT_ON_MS   40U

/* Vrai si l'instant t est atteint ou dépassé. */
static bool time_reached(uint32_t now, uint32_t t)
{
  return (int32_t)(now - t) >= 0;
}

/* Vrai si la couleur est le noir (LED éteinte). */
static bool is_black(led_color_t c)
{
  return (c.r == 0U) && (c.g == 0U) && (c.b == 0U);
}

/* Valeur PWM d'une composante : v² (0 à 65025). */
static uint16_t pwm_level(uint8_t v)
{
  return (uint16_t)((uint16_t)v * (uint16_t)v);
}

/* Affiche une couleur sur la LED (rien à faire si elle est déjà affichée). */
static void apply(led_color_t c)
{
  if (s_shown_valid && (c.r == s_shown.r) && (c.g == s_shown.g) && (c.b == s_shown.b))
  {
    return;                              /* rien n'a changé */
  }
  outputs_set(OUTPUT_RED, pwm_level(c.r));
  outputs_set(OUTPUT_GREEN, pwm_level(c.g));
  outputs_set(OUTPUT_BLUE, pwm_level(c.b));
  s_shown       = c;
  s_shown_valid = true;
}

/* Voir led_fx.h */
void led_fx_init(void)
{
  led_fx_off();
  s_activity    = true;
  s_shown_valid = false;
  apply(LED_BLACK);
}

/* Voir led_fx.h */
void led_fx_set_color(led_color_t c, uint32_t duration_ms)
{
  s_base        = c;
  s_base_timed  = (duration_ms > 0U);
  s_base_end_ms = HAL_GetTick() + duration_ms;
}

/* Voir led_fx.h */
led_color_t led_fx_color(void)
{
  return s_base;
}

/* Voir led_fx.h */
void led_fx_flash(led_color_t c, uint16_t ms)
{
  if (!s_activity || s_show.active || s_blink.active)
  {
    return;
  }
  s_flash.active = true;
  s_flash.color  = c;
  s_flash.end_ms = HAL_GetTick() + ms;
}

/* Voir led_fx.h */
void led_fx_show(led_color_t c, uint32_t ms)
{
  s_show.active = true;
  s_show.color  = c;
  s_show.end_ms = HAL_GetTick() + ms;
}

/* Voir led_fx.h */
void led_fx_blink(led_color_t c, uint32_t duration_ms, uint16_t period_ms)
{
  s_blink.active    = true;
  s_blink.color     = c;
  s_blink_start_ms  = HAL_GetTick();
  s_blink.end_ms    = s_blink_start_ms + duration_ms;
  s_blink_period_ms = (period_ms < 2U) ? 2U : period_ms;
}

/* Voir led_fx.h */
void led_fx_off(void)
{
  s_base         = LED_BLACK;
  s_base_timed   = false;
  s_flash.active = false;
  s_show.active  = false;
  s_blink.active = false;
}

/* Voir led_fx.h */
void led_fx_set_activity(bool on)
{
  s_activity = on;
  if (!on)
  {
    s_flash.active = false;
  }
}

/* Voir led_fx.h */
bool led_fx_activity(void)
{
  return s_activity;
}

/* Couleurs de la vague, de 0 à 7 relais et plus. */
static const led_color_t k_hops_colors[8] =
{
  { 255U, 0U, 0U },      /* 0 : rouge (reçue en direct) */
  { 255U, 80U, 0U },     /* 1 : orange */
  { 255U, 200U, 0U },    /* 2 : jaune */
  { 0U, 255U, 0U },      /* 3 : vert */
  { 0U, 200U, 255U },    /* 4 : cyan */
  { 0U, 0U, 255U },      /* 5 : bleu */
  { 160U, 0U, 255U },    /* 6 : violet */
  { 255U, 40U, 110U }    /* 7 et plus : rose */
};
static const char *const k_hops_names[8] =
{
  "rouge", "orange", "jaune", "verte", "cyan", "bleue", "violette", "rose"
};

/* Voir led_fx.h */
led_color_t led_fx_hops_color(uint8_t relays)
{
  return k_hops_colors[(relays > 7U) ? 7U : relays];
}

/* Voir led_fx.h */
const char *led_fx_hops_color_name(uint8_t relays)
{
  return k_hops_names[(relays > 7U) ? 7U : relays];
}

/* Voir led_fx.h */
void led_fx_process(uint32_t now)
{
  /* Fin des effets temporaires */
  if (s_blink.active && time_reached(now, s_blink.end_ms))
  {
    s_blink.active = false;
  }
  if (s_show.active && time_reached(now, s_show.end_ms))
  {
    s_show.active = false;
  }
  if (s_flash.active && time_reached(now, s_flash.end_ms))
  {
    s_flash.active = false;
  }
  if (s_base_timed && time_reached(now, s_base_end_ms))
  {
    s_base       = LED_BLACK;
    s_base_timed = false;
  }

  /* L'effet le plus prioritaire s'affiche */
  if (s_blink.active)
  {
    bool on = (((now - s_blink_start_ms) % s_blink_period_ms) < (s_blink_period_ms / 2U));
    apply(on ? s_blink.color : LED_BLACK);
  }
  else if (s_show.active)
  {
    apply(s_show.color);
  }
  else if (s_flash.active)
  {
    apply(s_flash.color);
  }
  else if (!is_black(s_base))
  {
    apply(s_base);
  }
#if (LED_HEARTBEAT_MS > 0)
  else if ((now % LED_HEARTBEAT_MS) < HEARTBEAT_ON_MS)
  {
    apply(k_heartbeat);
  }
#endif
  else
  {
    apply(LED_BLACK);
  }
}
