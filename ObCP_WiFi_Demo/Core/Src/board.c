/**
 ******************************************************************************
 * @file    board.c
 * @brief   Compléments à l'initialisation générée par STM32CubeMX pour la
 *          démo Wi-Fi 6 sur l'ObCP (voir board.h) :
 *            - passage dans le chargeur USB DFU de ST (commande « dfu ») ;
 *            - démarrage du quartz 32 kHz avec l'amplification qu'il demande ;
 *            - suite de l'initialisation (SPI, PWM, ADC, RTC) ;
 *            - diagnostic des erreurs d'initialisation (LED rouge) ;
 *            - identifiant de la carte, rapport de faute, heure UTC.
 *
 *          Ce fichier n'est pas généré : CubeMX ne le modifie jamais. Il est
 *          appelé depuis les sections « USER CODE » de main.c, dans l'ordre :
 *            board_early_init()  tout début de main()        (USER CODE 1)
 *            board_lse_start()   avant SystemClock_Config()  (USER CODE Init)
 *            board_post_init()   après les MX_xxx_Init()     (USER CODE 2)
 *            board_error_blink() dans Error_Handler()        (USER CODE Error_Handler_Debug)
 *
 *          Registres de sauvegarde de la RTC (BKPxR) : 32 mots de 32 bits
 *          qui gardent leur valeur pendant un reset (domaine de sauvegarde).
 *          Ils servent ici à transmettre une information d'un démarrage au
 *          suivant : demande de passage en DFU, rapport de faute, heure réglée.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "board.h"
#include "cdc_port.h"

/* Registres de sauvegarde de la RTC (conservés pendant un reset) :
 *   BKP0R      : heure réglée par SNTP
 *   BKP1R..7R  : rapport de faute
 *   BKP31R     : demande de démarrage dans le chargeur DFU              */
#define RTC_BKP_MAGIC   0x57363721UL      /* « W67! » : heure réglée par SNTP */
#define FAULT_MAGIC     0xFA017ED0UL
#define DFU_MAGIC       0xDF00B007UL
#define SYSMEM_BASE     0x1FFF0000UL      /* chargeur de ST (AN2606) */

static char s_uid_short[8];               /* identifiant court, calculé par uid_init() */

/* -------------------------------------------------------------------------- */
/* Passage dans le chargeur USB DFU de ST sans toucher à l'interrupteur        */
/* Prog/Run : la commande « dfu » écrit DFU_MAGIC puis redémarre ; au          */
/* démarrage suivant, board_early_init() saute dans la mémoire système avant   */
/* toute initialisation (horloges dans l'état du reset).                       */
/* -------------------------------------------------------------------------- */
/* Écrit un registre de sauvegarde. Le domaine de sauvegarde est protégé en
 * écriture (bit DBP de PWR->CR1) : la protection est levée le temps de
 * l'écriture, puis rétablie si elle était en place.                       */
static void backup_write(volatile uint32_t *reg, uint32_t value)
{
  uint32_t dbp;

  RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
  (void)RCC->APB1ENR1;
  dbp = PWR->CR1 & PWR_CR1_DBP;          /* accès déjà ouvert par SystemClock_Config() ? */
  PWR->CR1 |= PWR_CR1_DBP;
  *reg = value;
  if (dbp == 0U)
  {
    PWR->CR1 &= ~PWR_CR1_DBP;           /* sinon la RTC ne serait plus modifiable */
  }
}

/* Saut dans le chargeur de ST (mémoire système, AN2606) : interruptions
 * coupées, mémoire système placée à l'adresse 0, puis chargement de la
 * pile (MSP) et de l'adresse de départ lues dans sa table des vecteurs.  */
__attribute__((noreturn)) static void jump_to_system_memory(void)
{
  uint32_t sp = *(volatile uint32_t *)SYSMEM_BASE;
  uint32_t pc = *(volatile uint32_t *)(SYSMEM_BASE + 4U);
  uint32_t i;

  __disable_irq();
  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL  = 0U;
  for (i = 0U; i < 8U; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFFUL;
    NVIC->ICPR[i] = 0xFFFFFFFFUL;
  }
  /* Mémoire système visible en 0x00000000 (table des vecteurs du chargeur) */
  RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
  (void)RCC->APB2ENR;
  SYSCFG->MEMRMP = (SYSCFG->MEMRMP & ~SYSCFG_MEMRMP_MEM_MODE) | SYSCFG_MEMRMP_MEM_MODE_0;
  SCB->VTOR = 0U;
  __DSB();
  __ISB();
  __enable_irq();
  __asm volatile ("msr msp, %0 \n"
                  "bx  %1      \n" : : "r" (sp), "r" (pc) : "memory");
  for (;;)
  {
  }
}

/* Voir board.h */
void board_early_init(void)
{
  RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
  (void)RCC->APB1ENR1;
  if (RTC->BKP31R == DFU_MAGIC)
  {
    backup_write(&RTC->BKP31R, 0U);     /* une seule fois : le prochain reset relance l'application */
    jump_to_system_memory();
  }
}

/* Voir board.h : l'indicateur est écrit, puis la carte redémarre */
void board_reboot_to_dfu(void)
{
  backup_write(&RTC->BKP31R, DFU_MAGIC);
  cdc_port_usb_stop();                  /* l'hôte voit le port COM disparaître */
  HAL_Delay(300);
  NVIC_SystemReset();
  for (;;)
  {
  }
}

/* -------------------------------------------------------------------------- */
/* Quartz 32,768 kHz (ABS25, CL = 6 pF) : l'amplification « medium high » est  */
/* nécessaire pour un démarrage sûr. Le registre LSEDRV n'est modifiable que   */
/* tant que l'oscillateur est arrêté (il reste ensuite en marche à travers les */
/* resets) : il est donc réglé ici, avant SystemClock_Config(), quelle que     */
/* soit la valeur relue dans le .ioc (RCC > LSE Drive Capability, même réglage).*/
/* Si le quartz ne démarre pas (5 s), il est arrêté et c'est la configuration  */
/* de CubeMX qui décide : RTC sur LSE, SystemClock_Config() échoue à son tour  */
/* et Error_Handler() fait clignoter la LED rouge (3 éclats) ; RTC passée sur  */
/* LSI dans CubeMX (dépannage d'une carte au quartz défectueux), le programme  */
/* continue sans le quartz.                                                    */
/* -------------------------------------------------------------------------- */
void board_lse_start(void)
{
  RCC_OscInitTypeDef osc = { 0 };

  HAL_PWR_EnableBkUpAccess();
  if ((RCC->BDCR & RCC_BDCR_LSEON) == 0U)
  {
    __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_MEDIUMHIGH);
  }
  osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
  osc.LSEState       = RCC_LSE_ON;
  osc.PLL.PLLState   = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    __HAL_RCC_LSE_CONFIG(RCC_LSE_OFF);
  }
}

/* -------------------------------------------------------------------------- */
/* Erreur d'initialisation : la console USB n'est pas disponible, la LED rouge */
/* (PC6, commandée par TIM3 en temps normal) est pilotée directement.          */
/* -------------------------------------------------------------------------- */
/* Boucle d'attente sans SysTick (les interruptions sont coupées) */
static void delay_cycles(uint32_t n)
{
  while (n-- > 0U)
  {
    __NOP();
  }
}

/* Voir board.h. La cause est déduite des bits d'état des oscillateurs :
 * oscillateur demandé (ON) mais jamais prêt (RDY) = quartz défaillant.   */
void board_error_blink(void)
{
  uint32_t flashes = 0U;                /* 0 : clignotement rapide continu */
  uint32_t pin = (uint32_t)__builtin_ctz(LED_R_PIN);
  uint32_t loops_per_ms;
  uint32_t i;

  __disable_irq();
  if (((RCC->CR & RCC_CR_HSEON) != 0U) && ((RCC->CR & RCC_CR_HSERDY) == 0U))
  {
    flashes = 2U;                       /* quartz 8 MHz */
  }
  else if (((RCC->BDCR & RCC_BDCR_LSEON) != 0U) && ((RCC->BDCR & RCC_BDCR_LSERDY) == 0U))
  {
    flashes = 3U;                       /* quartz 32,768 kHz */
  }
  else
  {
    /* autre erreur */
  }

  RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN;
  (void)RCC->AHB2ENR;
  LED_R_PORT->MODER = (LED_R_PORT->MODER & ~(3UL << (pin * 2U))) | (1UL << (pin * 2U));
  LED_R_PORT->BRR = LED_R_PIN;
  loops_per_ms = (SystemCoreClock / 4000U) + 1U;    /* ~4 cycles par tour de boucle */
  for (;;)
  {
    if (flashes == 0U)
    {
      LED_R_PORT->ODR ^= LED_R_PIN;
      delay_cycles(100U * loops_per_ms);
    }
    else
    {
      for (i = 0U; i < flashes; i++)
      {
        LED_R_PORT->BSRR = LED_R_PIN;
        delay_cycles(200U * loops_per_ms);
        LED_R_PORT->BRR = LED_R_PIN;
        delay_cycles(250U * loops_per_ms);
      }
      delay_cycles(1200U * loops_per_ms);
    }
  }
}

/* -------------------------------------------------------------------------- */
/* Voir board.h : six registres de sauvegarde, puis un mot « magique » qui
 * indique au démarrage suivant qu'un rapport est présent.               */
void board_fault_save(const board_fault_t *f)
{
  backup_write(&RTC->BKP2R, f->kind);
  backup_write(&RTC->BKP3R, f->pc);
  backup_write(&RTC->BKP4R, f->lr);
  backup_write(&RTC->BKP5R, f->cfsr);
  backup_write(&RTC->BKP6R, f->hfsr);
  backup_write(&RTC->BKP7R, f->bfar);
  backup_write(&RTC->BKP1R, FAULT_MAGIC);
}

/* Voir board.h */
bool board_fault_take(board_fault_t *f)
{
  if (RTC->BKP1R != FAULT_MAGIC)
  {
    return false;
  }
  f->kind = RTC->BKP2R;
  f->pc   = RTC->BKP3R;
  f->lr   = RTC->BKP4R;
  f->cfsr = RTC->BKP5R;
  f->hfsr = RTC->BKP6R;
  f->bfar = RTC->BKP7R;
  backup_write(&RTC->BKP1R, 0U);
  return true;
}

/* -------------------------------------------------------------------------- */
/* Date de départ de la RTC à la première mise sous tension */
static void rtc_first_date(void)
{
  if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) != RTC_BKP_MAGIC)
  {
    /* RTC jamais réglée par SNTP (première mise sous tension) : 1er janvier 2026,
     * sauf si le calendrier tourne déjà (heure conservée à travers les resets) */
    dt_t now;

    if (!board_rtc_get(&now) || (now.year < 2026U))
    {
      dt_t            d = { 2026U, 1U, 1U, 0U, 0U, 0U, 0U };
      RTC_TimeTypeDef t = { 0 };
      RTC_DateTypeDef dd = { 0 };

      d.wday           = dt_weekday(d.year, d.mon, d.day);
      t.Hours          = d.hour;
      t.Minutes        = d.min;
      t.Seconds        = d.sec;
      t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
      t.StoreOperation = RTC_STOREOPERATION_RESET;
      dd.Year          = (uint8_t)(d.year - 2000U);
      dd.Month         = d.mon;
      dd.Date          = d.day;
      dd.WeekDay       = d.wday;
      (void)HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);
      (void)HAL_RTC_SetDate(&hrtc, &dd, RTC_FORMAT_BIN);
    }
  }
}

/* Identifiant court : empreinte FNV-1a des 96 bits de l'identifiant unique
 * du STM32, réduite à 24 bits (6 chiffres hexadécimaux).                 */
static void uid_init(void)
{
  uint32_t w[3] = { HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2() };
  uint32_t h = 2166136261UL;            /* FNV-1a 32 bits */
  const uint8_t *p = (const uint8_t *)w;
  size_t i;

  for (i = 0U; i < sizeof(w); i++)
  {
    h ^= p[i];
    h *= 16777619UL;
  }
  (void)snprintf(s_uid_short, sizeof(s_uid_short), "%06lX", (unsigned long)(h & 0xFFFFFFUL));
}

/* Voir board.h */
void board_post_init(void)
{
  /* SPI activés tout de suite : SCK est alors piloté à son niveau de repos
   * avant la première activation d'un CS (sinon la broche reste flottante). */
  __HAL_SPI_ENABLE(&hspi1);
  __HAL_SPI_ENABLE(&hspi3);

  /* LED RVB (TIM3) et sortie PWM du bornier (TIM1_CH3N), rapport cyclique nul */
  if ((HAL_TIM_PWM_Start(&htim3, LED_R_CHANNEL) != HAL_OK) ||
      (HAL_TIM_PWM_Start(&htim3, LED_G_CHANNEL) != HAL_OK) ||
      (HAL_TIM_PWM_Start(&htim3, LED_B_CHANNEL) != HAL_OK) ||
      (HAL_TIMEx_PWMN_Start(&htim1, PWM_OUT_CHANNEL) != HAL_OK))   /* CC3NE + MOE */
  {
    Error_Handler();
  }

  /* ADC : étalonnage (canaux internes, mesures en mode simple) */
  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
  {
    Error_Handler();
  }

  rtc_first_date();
  uid_init();
}

/* Voir board.h : quartz prêt ET choisi comme horloge de la RTC */
bool board_lse_ok(void)
{
  return ((RCC->BDCR & RCC_BDCR_LSERDY) != 0U) &&
         ((RCC->BDCR & RCC_BDCR_RTCSEL) == RCC_RTCCLKSOURCE_LSE);
}

/* Voir board.h : quartz prêt ET source de la PLL */
bool board_hse_ok(void)
{
  return ((RCC->CR & RCC_CR_HSERDY) != 0U) &&
         (__HAL_RCC_GET_PLL_OSCSOURCE() == RCC_PLLSOURCE_HSE);
}

/* Voir board.h */
const char *board_uid_short(void)
{
  return s_uid_short;
}

/* Voir board.h */
void board_uid_full(char *out, size_t len)
{
  (void)snprintf(out, len, "%08lX%08lX%08lX", (unsigned long)HAL_GetUIDw2(),
                 (unsigned long)HAL_GetUIDw1(), (unsigned long)HAL_GetUIDw0());
}

/* Fin du programme en flash : début des valeurs initiales de .data + leur taille
 * (symboles du script de liens généré par CubeMX).                           */
uint32_t board_image_end(void)
{
  extern uint32_t _sidata;
  extern uint32_t _sdata;
  extern uint32_t _edata;

  return (uint32_t)&_sidata + ((uint32_t)&_edata - (uint32_t)&_sdata);
}

/* Voir board.h */
void board_reset(void)
{
  cdc_port_usb_stop();                  /* le port COM disparaît proprement */
  HAL_Delay(200);
  NVIC_SystemReset();
}

/* -------------------------------------------------------------------------- */
/* Voir board.h. La RTC compte en BCD ; la HAL convertit (RTC_FORMAT_BIN). */
bool board_rtc_get(dt_t *dt)
{
  RTC_TimeTypeDef t;
  RTC_DateTypeDef d;

  if ((HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) ||
      (HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK))   /* déverrouille les registres fantômes */
  {
    return false;
  }
  dt->year = (uint16_t)(2000U + d.Year);
  dt->mon  = d.Month;
  dt->day  = d.Date;
  dt->hour = t.Hours;
  dt->min  = t.Minutes;
  dt->sec  = t.Seconds;
  dt->wday = d.WeekDay;
  return true;
}

/* Voir board.h : marque aussi l'heure comme « réglée » (registre BKP0) */
bool board_rtc_set(const dt_t *dt)
{
  RTC_TimeTypeDef t = { 0 };
  RTC_DateTypeDef d = { 0 };

  if ((dt->year < 2000U) || (dt->year > 2099U))
  {
    return false;
  }
  t.Hours          = dt->hour;
  t.Minutes        = dt->min;
  t.Seconds        = dt->sec;
  t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  t.StoreOperation = RTC_STOREOPERATION_RESET;
  d.Year           = (uint8_t)(dt->year - 2000U);
  d.Month          = dt->mon;
  d.Date           = dt->day;
  d.WeekDay        = dt_weekday(dt->year, dt->mon, dt->day);
  if ((HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) ||
      (HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK))
  {
    return false;
  }
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_BKP_MAGIC);
  return true;
}

/* Voir board.h */
bool board_rtc_synced(void)
{
  return HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == RTC_BKP_MAGIC;
}
