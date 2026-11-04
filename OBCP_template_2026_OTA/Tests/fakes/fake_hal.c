/**
 ******************************************************************************
 * @file    Tests/fakes/fake_hal.c
 * @brief   Simulation sur PC de la HAL et de la carte : temps, broches,
 *          interruptions, minuteries, accéléromètre LIS3DH / LIS3DSH sur SPI.
 ******************************************************************************
 */
#include <string.h>
#include "fake_board.h"
#include "obcp_ota_request.h"       /* le remplaçant, dans ce dossier */

/* Fonctions de rappel écrites par le programme testé. */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);

/* ========================================================================== */
/* Temps                                                                      */
/* ========================================================================== */
uint64_t fake_now_us = 1000000U;      /* on démarre à 1 s */

void fake_time_advance_ms(uint32_t ms)
{
  fake_now_us += (uint64_t)ms * 1000U;
}

/* Chaque lecture du temps fait avancer l'horloge d'une microseconde : une
 * boucle qui attend en lisant HAL_GetTick() finit donc toujours. */
uint32_t HAL_GetTick(void)
{
  fake_now_us += 1U;
  return (uint32_t)(fake_now_us / 1000U);
}

void HAL_Delay(uint32_t ms)
{
  fake_now_us += (uint64_t)(ms + 1U) * 1000U;    /* HAL_Delay attend au moins ms + 1 */
}

/* ========================================================================== */
/* Chargeur Bluetooth (remplace les fonctions de obcp_ota_request.h)          */
/* ========================================================================== */
int fake_ota_enter_count;               /* appels à obcp_ota_enter_loader() */

/* Appelée par main.c seulement, qui n'est pas compilé sur PC. */
void obcp_ota_init_vectors(void)
{
}

/* Sur la carte : mot magique en SRAM2 puis redémarrage, sans retour. Ici,
 * on compte l'appel et on revient : le programme doit le supporter.      */
void obcp_ota_enter_loader(void)
{
  fake_ota_enter_count++;
}

uint32_t HAL_GetUIDw0(void) { return 0x00360023U; }
uint32_t HAL_GetUIDw1(void) { return 0x4D4B5003U; }
uint32_t HAL_GetUIDw2(void) { return 0x20353150U; }
void HAL_DBGMCU_EnableDBGSleepMode(void) { }
void fault_init(void) { }                 /* fault.c n'est pas compilé sur PC */

/* ========================================================================== */
/* Interruptions                                                              */
/* ========================================================================== */
uint32_t fake_primask;
int      fake_wfi_count;
int      fake_exti_disable_count;
bool     fake_exti_enabled = true;

uint32_t __get_PRIMASK(void)            { return fake_primask; }
void     __set_PRIMASK(uint32_t primask) { fake_primask = primask; }
void     __disable_irq(void)            { fake_primask = 1U; }
void     __enable_irq(void)             { fake_primask = 0U; }

void __WFI(void)
{
  fake_wfi_count++;
  fake_time_advance_ms(1U);                      /* réveil par le SysTick */
}

void HAL_NVIC_DisableIRQ(IRQn_Type irq)
{
  if (irq == EXTI15_10_IRQn)
  {
    fake_exti_disable_count++;
    fake_exti_enabled = false;
  }
}

void HAL_NVIC_EnableIRQ(IRQn_Type irq)
{
  if (irq == EXTI15_10_IRQn)
  {
    fake_exti_enabled = true;
  }
}

/* ========================================================================== */
/* Broches                                                                    */
/* ========================================================================== */
GPIO_TypeDef fake_gpioa = { 0 }, fake_gpiob = { 1 }, fake_gpioc = { 2 }, fake_gpioh = { 7 };

static uint16_t s_out[8];             /* niveaux écrits, par port */
static uint16_t s_in[8];              /* niveaux imposés aux entrées, par port */

/* Bouton : instants de changement (appui, relâchement, appui...). */
#define BUTTON_MAX_CHANGES 32
static uint64_t s_button_changes[BUTTON_MAX_CHANGES];
static size_t   s_button_count;
static bool     s_button_manual_down;

static void acc_cs(bool low);

void fake_button_set(bool down)
{
  s_button_count       = 0U;
  s_button_manual_down = down;
}

void fake_button_schedule(const uint64_t *change_times_us, size_t count, bool first_level_down)
{
  (void)first_level_down;
  if (count > BUTTON_MAX_CHANGES)
  {
    count = BUTTON_MAX_CHANGES;
  }
  memcpy(s_button_changes, change_times_us, count * sizeof(uint64_t));
  s_button_count       = count;
  s_button_manual_down = false;
}

static bool button_down_now(void)
{
  bool down = s_button_manual_down;

  for (size_t i = 0U; i < s_button_count; i++)
  {
    if (fake_now_us >= s_button_changes[i])
    {
      down = !down;
    }
  }
  return down;
}

void fake_button_exti(void)
{
  HAL_GPIO_EXTI_Callback(GPIO_PIN_13);
}

void fake_gpio_set_input(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level)
{
  if (level == GPIO_PIN_SET)
  {
    s_in[port->port] |= pin;
  }
  else
  {
    s_in[port->port] &= (uint16_t)~pin;
  }
}

GPIO_PinState fake_gpio_get(GPIO_TypeDef *port, uint16_t pin)
{
  return ((s_out[port->port] & pin) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
  fake_now_us += 1U;                              /* une boucle d'attente avance */
  if ((port == GPIOC) && (pin == GPIO_PIN_13))
  {
    return button_down_now() ? GPIO_PIN_RESET : GPIO_PIN_SET;  /* actif à 0 */
  }
  return ((s_in[port->port] & pin) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
  if (state == GPIO_PIN_SET)
  {
    s_out[port->port] |= pin;
  }
  else
  {
    s_out[port->port] &= (uint16_t)~pin;
  }
  if ((port == GPIOC) && (pin == GPIO_PIN_5))     /* CS de l'accéléromètre */
  {
    acc_cs(state == GPIO_PIN_RESET);
  }
}

/* ========================================================================== */
/* Minuteries                                                                 */
/* ========================================================================== */
TIM_TypeDef fake_tim1, fake_tim3, fake_tim7;
TIM_HandleTypeDef htim1 = { &fake_tim1, 0 };
TIM_HandleTypeDef htim3 = { &fake_tim3, 0 };
TIM_HandleTypeDef htim7 = { &fake_tim7, 0 };
int fake_pwm_started[4];

volatile uint32_t *fake_tim_ccr(TIM_HandleTypeDef *htim, uint32_t channel)
{
  switch (channel)
  {
    case TIM_CHANNEL_1: return &htim->Instance->CCR1;
    case TIM_CHANNEL_2: return &htim->Instance->CCR2;
    case TIM_CHANNEL_3: return &htim->Instance->CCR3;
    default:            return &htim->Instance->CCR4;
  }
}

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim, uint32_t channel)
{
  if (htim == &htim3)
  {
    if (channel == TIM_CHANNEL_1) fake_pwm_started[0]++;
    if (channel == TIM_CHANNEL_3) fake_pwm_started[1]++;
    if (channel == TIM_CHANNEL_4) fake_pwm_started[2]++;
  }
  return HAL_OK;
}

HAL_StatusTypeDef HAL_TIMEx_PWMN_Start(TIM_HandleTypeDef *htim, uint32_t channel)
{
  if ((htim == &htim1) && (channel == TIM_CHANNEL_3))
  {
    fake_pwm_started[3]++;
  }
  return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim)
{
  if (htim->running)
  {
    return HAL_ERROR;                             /* comme la HAL : déjà lancée */
  }
  htim->running = 1;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim)
{
  htim->running = 0;
  return HAL_OK;
}

void fake_timer7_fire(void)
{
  if (htim7.running)
  {
    HAL_TIM_PeriodElapsedCallback(&htim7);
  }
}

/* ========================================================================== */
/* Accéléromètre sur SPI3                                                     */
/* ========================================================================== */
SPI_HandleTypeDef hspi3;
bool fake_spi_fail;
int  fake_acc_transactions;
int  fake_acc_xyz_burst_reads;
bool fake_acc_cs_low;

static fake_acc_chip_t s_chip = FAKE_ACC_LIS3DH;
static uint8_t         s_reg[128];
static int16_t         s_raw[3] = { 16, -32, 16384 };     /* environ 0, 0, 1 g */
static int8_t          s_temp_delta = 2;

/* Échange en cours */
static int     s_byte_index;      /* rang de l'octet dans l'échange */
static bool    s_read;
static bool    s_incr;
static uint8_t s_addr;
static uint8_t s_first_addr;
static int     s_count;

static void acc_reset_regs(void)
{
  memset(s_reg, 0, sizeof(s_reg));
  if (s_chip == FAKE_ACC_LIS3DH)
  {
    s_reg[0x0F] = 0x33U;
    s_reg[0x20] = 0x07U;           /* CTRL_REG1 par défaut */
  }
  else if (s_chip == FAKE_ACC_LIS3DSH)
  {
    s_reg[0x0F] = 0x3FU;
    s_reg[0x20] = 0x07U;           /* CTRL_REG4 par défaut */
    s_reg[0x25] = 0x10U;           /* CTRL_REG6 : ADD_INC = 1 */
  }
}

void fake_acc_set_chip(fake_acc_chip_t chip)
{
  s_chip = chip;
  acc_reset_regs();
}

void fake_acc_set_raw(int16_t x, int16_t y, int16_t z)
{
  s_raw[0] = x;
  s_raw[1] = y;
  s_raw[2] = z;
}

void fake_acc_set_temperature_delta(int8_t delta)
{
  s_temp_delta = delta;
}

uint8_t fake_acc_reg(uint8_t addr)
{
  return s_reg[addr & 0x7FU];
}

static uint8_t acc_read_reg(uint8_t addr)
{
  if ((addr >= 0x28U) && (addr <= 0x2DU))
  {
    int axis = (addr - 0x28) / 2;
    uint16_t v = (uint16_t)s_raw[axis];
    return ((addr & 1U) == 0U) ? (uint8_t)v : (uint8_t)(v >> 8);
  }
  if (s_chip == FAKE_ACC_LIS3DH)
  {
    if (addr == 0x0CU) return 0x00U;                 /* OUT_ADC3_L */
    if (addr == 0x0DU) return (uint8_t)s_temp_delta; /* OUT_ADC3_H */
  }
  if ((s_chip == FAKE_ACC_LIS3DSH) && (addr == 0x0CU))
  {
    return (uint8_t)s_temp_delta;                    /* OUT_T */
  }
  return s_reg[addr & 0x7FU];
}

static void acc_cs(bool low)
{
  if (low && !fake_acc_cs_low)
  {
    s_byte_index = 0;
    s_count      = 0;
    fake_acc_transactions++;
  }
  if (!low && fake_acc_cs_low)
  {
    if (s_read && (s_first_addr == 0x28U) && (s_count >= 6) && s_incr)
    {
      fake_acc_xyz_burst_reads++;
    }
  }
  fake_acc_cs_low = low;
}

static uint8_t acc_xfer(uint8_t mosi)
{
  uint8_t miso = 0x00U;

  if (!fake_acc_cs_low || (s_chip == FAKE_ACC_NONE))
  {
    return 0x00U;                                    /* personne ne répond */
  }
  if (s_byte_index == 0)
  {
    s_read = (mosi & 0x80U) != 0U;
    if (s_chip == FAKE_ACC_LIS3DH)
    {
      s_incr = (mosi & 0x40U) != 0U;
      s_addr = mosi & 0x3FU;
    }
    else
    {
      s_incr = (s_reg[0x25] & 0x10U) != 0U;
      s_addr = mosi & 0x7FU;
    }
    s_first_addr = s_addr;
  }
  else
  {
    if (s_read)
    {
      miso = acc_read_reg(s_addr);
    }
    else
    {
      s_reg[s_addr & 0x7FU] = mosi;
    }
    s_count++;
    if (s_incr)
    {
      s_addr++;
    }
  }
  s_byte_index++;
  return miso;
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi, uint8_t *tx, uint8_t *rx,
                                          uint16_t size, uint32_t timeout)
{
  (void)hspi;
  (void)timeout;
  if (fake_spi_fail)
  {
    return HAL_ERROR;
  }
  for (uint16_t i = 0U; i < size; i++)
  {
    rx[i] = acc_xfer(tx[i]);
  }
  return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *data, uint16_t size,
                                   uint32_t timeout)
{
  (void)hspi;
  (void)timeout;
  if (fake_spi_fail)
  {
    return HAL_ERROR;
  }
  for (uint16_t i = 0U; i < size; i++)
  {
    (void)acc_xfer(data[i]);
  }
  return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *data, uint16_t size,
                                  uint32_t timeout)
{
  (void)hspi;
  (void)timeout;
  if (fake_spi_fail)
  {
    return HAL_ERROR;
  }
  for (uint16_t i = 0U; i < size; i++)
  {
    data[i] = acc_xfer(0x00U);
  }
  return HAL_OK;
}
