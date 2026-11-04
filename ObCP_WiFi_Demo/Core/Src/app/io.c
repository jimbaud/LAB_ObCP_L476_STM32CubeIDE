/**
 ******************************************************************************
 * @file    io.c
 * @brief   Entrées / sorties de la démo sur l'ObCP (voir io.h).
 *
 *  - LED RVB : trois canaux du timer TIM3 en PWM à 1 kHz ; la luminosité de
 *    chaque couleur est le rapport cyclique (temps allumé / période).
 *  - Sortie PWM du bornier : canal complémentaire CH3N du timer TIM1.
 *  - Température et VDDA : ADC1 sur deux canaux internes du STM32
 *    (capteur de température, tension de référence VREFINT).
 *  - Accéléromètre : SPI3, pilote lis3dh.c (indépendant du matériel :
 *    io.c lui fournit la fonction d'échange SPI acc_xfer()).
 *  - Boutons : lecture des broches avec anti-rebond logiciel.
 *  Les périphériques sont initialisés par le code généré (MX_xxx_Init) et
 *  démarrés par board_post_init() ; ce fichier ne fait que les utiliser.
 ******************************************************************************
 */
#include "io.h"
#include "board.h"
#include "color.h"
#include "lis3dh.h"

/* Anti-rebond : un bouton mécanique « rebondit » (plusieurs contacts en
 * quelques ms). Son nouvel état n'est accepté qu'après DEBOUNCE_N lectures
 * identiques espacées de DEBOUNCE_MS.                                    */
#define DEBOUNCE_MS    10U
#define DEBOUNCE_N      3U

/* État d'un bouton */
typedef struct
{
  GPIO_TypeDef *port;
  uint16_t      pin;
  bool          stable;      /* état stable : true = appuyé */
  uint8_t       cnt;
  uint32_t      presses;
} button_t;

static button_t s_btn[2] =
{
  { BTN_USER_PORT, BTN_USER_PIN, false, 0U, 0U },
  { BTN_SW1_PORT,  BTN_SW1_PIN,  false, 0U, 0U },
};
static uint8_t  s_led;                             /* luminosité (%)           */
static uint8_t  s_led_last = 50U;                  /* dernier niveau non nul   */
static uint8_t  s_rgb[3] = { 255U, 255U, 255U };   /* blanc */
static uint8_t  s_pwm;                             /* sortie PWM (%)           */
static uint32_t s_last_sample;                     /* dernière lecture boutons */
static lis3dh_t s_acc;                             /* accéléromètre            */

/* ---- Accéléromètre (SPI3, CS = PC5) --------------------------------------------- */
/* Échange SPI fourni au pilote lis3dh.c : CS à 0, n octets échangés (les
 * octets reçus remplacent ceux envoyés dans buf), CS à 1               */
static int acc_xfer(uint8_t *buf, size_t n, void *ctx)
{
  HAL_StatusTypeDef st;

  (void)ctx;
  HAL_GPIO_WritePin(ACC_CS_PORT, ACC_CS_PIN, GPIO_PIN_RESET);
  st = HAL_SPI_TransmitReceive(&hspi3, buf, buf, (uint16_t)n, 5U);
  HAL_GPIO_WritePin(ACC_CS_PORT, ACC_CS_PIN, GPIO_PIN_SET);
  return (st == HAL_OK) ? 0 : -1;
}

/* Recalcule et écrit les trois rapports cycliques de la LED RVB
 * (registres de comparaison CCR des canaux de TIM3)                     */
static void led_apply(void)
{
  __HAL_TIM_SET_COMPARE(&htim3, LED_R_CHANNEL, color_duty(s_rgb[0], s_led, LED_PWM_PERIOD));
  __HAL_TIM_SET_COMPARE(&htim3, LED_G_CHANNEL, color_duty(s_rgb[1], s_led, LED_PWM_PERIOD));
  __HAL_TIM_SET_COMPARE(&htim3, LED_B_CHANNEL, color_duty(s_rgb[2], s_led, LED_PWM_PERIOD));
}

/* Voir io.h */
void io_init(void)
{
  io_led_set(0U);
  io_pwm_set(0U);
  (void)lis3dh_init(&s_acc, acc_xfer, NULL);
  s_btn[0].stable = (HAL_GPIO_ReadPin(BTN_USER_PORT, BTN_USER_PIN) == GPIO_PIN_RESET);
  s_btn[1].stable = (HAL_GPIO_ReadPin(BTN_SW1_PORT, BTN_SW1_PIN) == GPIO_PIN_RESET);
}

/* Moyenne de 16 conversions d'un canal (ADC1 réglé dans CubeMX : conversion
 * unique déclenchée par logiciel ; le canal est choisi à chaque mesure). */
#define ADC_AVG_N   16U

/* Mesure d'un canal de l'ADC (résultat 0 à 4095 sur 12 bits) */
static uint16_t adc_read(uint32_t channel, uint32_t sampling)
{
  ADC_ChannelConfTypeDef ch = { 0 };
  uint32_t               sum = 0U;
  uint32_t               n = 0U;

  ch.Channel      = channel;
  ch.Rank         = ADC_REGULAR_RANK_1;
  ch.SamplingTime = sampling;
  ch.SingleDiff   = ADC_SINGLE_ENDED;
  ch.OffsetNumber = ADC_OFFSET_NONE;
  ch.Offset       = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &ch) != HAL_OK)
  {
    return 0U;
  }
  while (n < ADC_AVG_N)
  {
    if ((HAL_ADC_Start(&hadc1) != HAL_OK) || (HAL_ADC_PollForConversion(&hadc1, 20U) != HAL_OK))
    {
      break;
    }
    sum += HAL_ADC_GetValue(&hadc1);
    n++;
  }
  (void)HAL_ADC_Stop(&hadc1);
  return (n > 0U) ? (uint16_t)((sum + (n / 2U)) / n) : 0U;
}

/* Voir io.h. VDDA se déduit de VREFINT : tension interne de valeur connue
 * (mesurée en usine, VREFINT_CAL) ; si VDDA baisse, sa mesure augmente :
 * VDDA = 3,0 V x VREFINT_CAL / mesure.                                  */
void io_read(io_sensors_t *s)
{
  uint16_t vref = adc_read(ADC_CHANNEL_VREFINT, ADC_SAMPLETIME_640CYCLES_5);
  uint16_t ts   = adc_read(ADC_CHANNEL_TEMPSENSOR, ADC_SAMPLETIME_640CYCLES_5);
  int32_t  cal1 = (int32_t)*TEMPSENSOR_CAL1_ADDR;
  int32_t  cal2 = (int32_t)*TEMPSENSOR_CAL2_ADDR;
  uint32_t vdda = 3300U;

  if (vref > 0U)
  {
    vdda = ((uint32_t)VREFINT_CAL_VREF * (uint32_t)(*VREFINT_CAL_ADDR)) / vref;
  }
  s->vdda_mv = (uint16_t)vdda;

  /* Température : droite passant par les deux points d'étalonnage d'usine
   * (TS_CAL1 à 30 °C et TS_CAL2 à 110 °C, mesurés sous VDDA = 3,0 V)      */
  if (cal2 != cal1)
  {
    int32_t ts_norm = ((int32_t)ts * (int32_t)vdda) / (int32_t)TEMPSENSOR_CAL_VREFANALOG;
    s->temp_x10 = (int16_t)((((ts_norm - cal1) * (TEMPSENSOR_CAL2_TEMP - TEMPSENSOR_CAL1_TEMP) * 10) /
                             (cal2 - cal1)) + (TEMPSENSOR_CAL1_TEMP * 10));
  }
  else
  {
    s->temp_x10 = 0;
  }

  s->acc_ok = (lis3dh_read_mg(&s_acc, s->acc_mg) == 0);
  if (s->acc_ok)
  {
    lis3dh_tilt(s->acc_mg, &s->pitch_x10, &s->roll_x10);
  }
  else
  {
    s->acc_mg[0] = 0;
    s->acc_mg[1] = 0;
    s->acc_mg[2] = 0;
    s->pitch_x10 = 0;
    s->roll_x10 = 0;
  }
  s->user = (HAL_GPIO_ReadPin(BTN_USER_PORT, BTN_USER_PIN) == GPIO_PIN_RESET);
  s->sw1  = (HAL_GPIO_ReadPin(BTN_SW1_PORT, BTN_SW1_PIN) == GPIO_PIN_RESET);
}

/* Voir io.h */
uint8_t io_acc_whoami(void)
{
  return s_acc.who;
}

/* Voir io.h */
const char *io_acc_name(void)
{
  return lis3dh_name(&s_acc);
}

/* ---- LED RVB ------------------------------------------------------------------ */
/* Voir io.h */
void io_led_set(uint8_t pct)
{
  if (pct > 100U)
  {
    pct = 100U;
  }
  s_led = pct;
  if (pct > 0U)
  {
    s_led_last = pct;
  }
  led_apply();
}

/* Voir io.h */
uint8_t io_led_get(void)
{
  return s_led;
}

/* Voir io.h */
void io_led_toggle(void)
{
  io_led_set((s_led > 0U) ? 0U : s_led_last);
}

/* Voir io.h */
void io_led_color_set(const uint8_t rgb[3])
{
  s_rgb[0] = rgb[0];
  s_rgb[1] = rgb[1];
  s_rgb[2] = rgb[2];
  led_apply();
}

/* Voir io.h */
void io_led_color_get(uint8_t rgb[3])
{
  rgb[0] = s_rgb[0];
  rgb[1] = s_rgb[1];
  rgb[2] = s_rgb[2];
}

/* ---- Sortie PWM ---------------------------------------------------------------- */
/* Voir io.h : rapport cyclique = pct % de la période de TIM1 */
void io_pwm_set(uint8_t pct)
{
  if (pct > 100U)
  {
    pct = 100U;
  }
  s_pwm = pct;
  /* CCR = période (ARR + 1) donne une sortie toujours active (100 %) */
  __HAL_TIM_SET_COMPARE(&htim1, PWM_OUT_CHANNEL, ((uint32_t)pct * OUT_PWM_PERIOD) / 100U);
}

/* Voir io.h */
uint8_t io_pwm_get(void)
{
  return s_pwm;
}

/* ---- Boutons ------------------------------------------------------------------- */
/* Voir io.h (boutons actifs à l'état bas : appuyé = GPIO_PIN_RESET) */
uint8_t io_buttons_poll(void)
{
  uint8_t  events = 0U;
  uint32_t now = HAL_GetTick();
  unsigned i;

  if ((now - s_last_sample) < DEBOUNCE_MS)
  {
    return 0U;
  }
  s_last_sample = now;

  for (i = 0U; i < 2U; i++)
  {
    button_t *b = &s_btn[i];
    bool      raw = (HAL_GPIO_ReadPin(b->port, b->pin) == GPIO_PIN_RESET);

    if (raw == b->stable)
    {
      b->cnt = 0U;
      continue;
    }
    if (++b->cnt >= DEBOUNCE_N)
    {
      b->cnt = 0U;
      b->stable = raw;
      if (raw)
      {
        b->presses++;
        events |= (uint8_t)(1U << i);
      }
    }
  }
  return events;
}

/* Voir io.h */
uint32_t io_button_count(uint8_t btn)
{
  return (btn == IO_BTN_USER) ? s_btn[0].presses : s_btn[1].presses;
}
