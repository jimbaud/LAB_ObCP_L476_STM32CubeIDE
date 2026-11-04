/*
 * Remplaçants PC des modules matériels (board, io, config, passerelle UART)
 * pour test_sim : l'application complète (Core/Src/app) tourne sur PC face au
 * module simulé.
 */
#include <stdio.h>
#include <string.h>
#include "stm32l4xx_hal.h"
#include "board.h"
#include "io.h"
#include "lis3dh.h"
#include "uart_bridge.h"
#include "config.h"
#include "app_config.h"
#include "ncp_port.h"
#include "host.h"

GPIO_TypeDef fake_gpioa, fake_gpiob, fake_gpioc;
SPI_HandleTypeDef  hspi1;
SPI_HandleTypeDef  hspi3;
ADC_HandleTypeDef  hadc1;
TIM_HandleTypeDef  htim1;
TIM_HandleTypeDef  htim3;
RTC_HandleTypeDef  hrtc;

/* ---- HAL ------------------------------------------------------------------ */
uint32_t HAL_GetTick(void)
{
  return ncp_port_tick();          /* le temps simulé avance à chaque lecture */
}

void HAL_Delay(uint32_t ms)
{
  ncp_port_delay(ms);
}

static bool s_user_held;

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
  if ((port == BTN_USER_PORT) && (pin == BTN_USER_PIN) && s_user_held)
  {
    return GPIO_PIN_RESET;
  }
  return GPIO_PIN_SET;             /* boutons relâchés */
}

uint32_t HAL_RCC_GetSysClockFreq(void)
{
  return 80000000UL;
}

void host_hold_user(bool held)
{
  s_user_held = held;
}

/* ---- board ------------------------------------------------------------------ */
static int64_t  s_rtc_base = 1767225600LL;   /* 2026-01-01 00:00:00 UTC */
static uint32_t s_rtc_tick;
static bool     s_rtc_synced;
int             g_host_resets;

bool board_lse_ok(void)
{
  return true;
}

bool board_hse_ok(void)
{
  return true;
}

int  g_host_dfu;
bool g_host_fault_pending;

void board_reboot_to_dfu(void)
{
  g_host_dfu++;
}

bool board_fault_take(board_fault_t *f)
{
  if (!g_host_fault_pending)
  {
    return false;
  }
  g_host_fault_pending = false;
  f->kind = 2U;                        /* BusFault */
  f->pc   = 0x08001234UL;
  f->lr   = 0x08005679UL;
  f->cfsr = 0x00008200UL;
  f->hfsr = 0x40000000UL;
  f->bfar = 0x20030000UL;
  return true;
}

void board_fault_save(const board_fault_t *f)
{
  (void)f;
}

const char *board_uid_short(void)
{
  return "A1B2C3";
}

void board_uid_full(char *out, size_t len)
{
  (void)snprintf(out, len, "%s", "0032001A3138510B39383538");
}

void board_reset(void)
{
  g_host_resets++;
}

bool board_rtc_get(dt_t *dt)
{
  int64_t t = s_rtc_base + (int64_t)((ncp_port_tick() - s_rtc_tick) / 1000U);
  epoch_to_dt(t, dt);
  return true;
}

bool board_rtc_set(const dt_t *dt)
{
  if ((dt->year < 2000U) || (dt->year > 2099U))
  {
    return false;
  }
  s_rtc_base = dt_to_epoch(dt);
  s_rtc_tick = ncp_port_tick();
  s_rtc_synced = true;
  return true;
}

bool board_rtc_synced(void)
{
  return s_rtc_synced;
}

/* ---- io (LED RVB, PWM, accéléromètre simulés) ------------------------------------ */
static uint8_t  s_led;
static uint8_t  s_led_last = 50U;
static uint8_t  s_rgb[3] = { 255U, 255U, 255U };
static uint8_t  s_pwm;
static uint8_t  s_btn_events;
static uint32_t s_count[2];
int16_t         g_host_acc[3] = { 12, -25, 998 };     /* carte presque à plat */

void io_init(void)
{
  s_led = 0U;
  s_pwm = 0U;
}

void io_read(io_sensors_t *s)
{
  s->temp_x10 = 245;
  s->vdda_mv  = 3297U;
  s->acc_ok   = true;
  s->acc_mg[0] = g_host_acc[0];
  s->acc_mg[1] = g_host_acc[1];
  s->acc_mg[2] = g_host_acc[2];
  lis3dh_tilt(s->acc_mg, &s->pitch_x10, &s->roll_x10);
  s->user = false;
  s->sw1  = false;
}

uint8_t io_acc_whoami(void)
{
  return 0x33U;
}

const char *io_acc_name(void)
{
  return "LIS3DH";
}

void io_led_set(uint8_t pct)
{
  s_led = (pct > 100U) ? 100U : pct;
  if (s_led > 0U)
  {
    s_led_last = s_led;
  }
}

uint8_t io_led_get(void)
{
  return s_led;
}

void io_led_toggle(void)
{
  io_led_set((s_led > 0U) ? 0U : s_led_last);
}

void io_led_color_set(const uint8_t rgb[3])
{
  memcpy(s_rgb, rgb, 3U);
}

void io_led_color_get(uint8_t rgb[3])
{
  memcpy(rgb, s_rgb, 3U);
}

void io_pwm_set(uint8_t pct)
{
  s_pwm = (pct > 100U) ? 100U : pct;
}

uint8_t io_pwm_get(void)
{
  return s_pwm;
}

uint8_t io_buttons_poll(void)
{
  uint8_t e = s_btn_events;
  s_btn_events = 0U;
  if ((e & IO_BTN_USER) != 0U)
  {
    s_count[0]++;
  }
  if ((e & IO_BTN_SW1) != 0U)
  {
    s_count[1]++;
  }
  return e;
}

uint32_t io_button_count(uint8_t btn)
{
  return (btn == IO_BTN_USER) ? s_count[0] : s_count[1];
}

void host_press(uint8_t btn)
{
  s_btn_events |= btn;
}

/* ---- passerelle UART (module boot) : session simulée --------------------------------- */
int g_host_bridge_runs;

void uart_bridge_run(bridge_result_t *r)
{
  g_host_bridge_runs++;
  memset(r, 0, sizeof(*r));
  r->to_module   = 1489536U;           /* ordre de grandeur d'une programmation complète */
  r->from_module = 4096U;
  r->baud        = 2000000U;
  r->seconds     = 95U;
  r->end         = BRIDGE_END_REOPENED;
  ncp_port_delay(95000U);
}

/* ---- config (flash simulée en RAM) ----------------------------------------- */
cfg_t          g_cfg;
static uint8_t s_flash[2048];
static bool    s_flash_init;
int            g_host_flash_writes;

static void flash_init(void)
{
  if (!s_flash_init)
  {
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_flash_init = true;
  }
}

int config_load(void)
{
  int st;
  flash_init();
  st = cfg_unpack(&g_cfg, s_flash, sizeof(s_flash));
  if (st != CFG_UNPACK_OK)
  {
    cfg_defaults(&g_cfg);
  }
  return st;
}

int config_save(void)
{
  uint8_t blob[CFG_BLOB_MAX];
  size_t  n = cfg_pack(&g_cfg, blob, sizeof(blob));
  flash_init();
  if (n == 0U)
  {
    return -1;
  }
  memset(s_flash, 0xFF, sizeof(s_flash));
  memcpy(s_flash, blob, n);
  g_host_flash_writes++;
  return 0;
}

int config_erase(void)
{
  flash_init();
  memset(s_flash, 0xFF, sizeof(s_flash));
  return 0;
}

bool config_stored(void)
{
  cfg_t tmp;
  flash_init();
  return cfg_unpack(&tmp, s_flash, sizeof(s_flash)) == CFG_UNPACK_OK;
}

bool config_ecc_nmi(void)
{
  return false;
}

/* Copie conforme des fonctions de nommage de Core/Src/app/config.c */
const char *config_ap_ssid(void)
{
  static char s[33];
  if (g_cfg.ap_ssid[0] != '\0')
  {
    return g_cfg.ap_ssid;
  }
  (void)snprintf(s, sizeof(s), AP_SSID_PREFIX "%s", board_uid_short());
  return s;
}

const char *config_hostname(void)
{
  static char s[33];
  if (g_cfg.hostname[0] != '\0')
  {
    return g_cfg.hostname;
  }
  (void)snprintf(s, sizeof(s), HOSTNAME_PREFIX "%s", board_uid_short());
  return s;
}

const char *config_mqtt_root(void)
{
  static char s[64];
  size_t      n;
  if (g_cfg.mqtt_topic[0] != '\0')
  {
    (void)snprintf(s, sizeof(s), "%s", g_cfg.mqtt_topic);
    n = strlen(s);
    while ((n > 0U) && (s[n - 1U] == '/'))
    {
      s[--n] = '\0';
    }
    return s;
  }
  (void)snprintf(s, sizeof(s), MQTT_TOPIC_PREFIX "%s", board_uid_short());
  return s;
}

const char *config_mqtt_client_id(void)
{
  static char s[32];
  (void)snprintf(s, sizeof(s), HOSTNAME_PREFIX "%s", board_uid_short());
  return s;
}
