/**
 ******************************************************************************
 * @file    Tests/fakes/stm32l4xx_hal.h
 * @brief   Remplaçant PC minimal de la HAL de ST, pour compiler et tester le
 *          code de l'application sur un ordinateur (gcc natif).
 *
 *  Il déclare seulement ce qu'utilisent les modules testés et les en-têtes
 *  générés qu'ils incluent (main.h, tim.h, spi.h, usbd_conf.h...). Les
 *  fonctions sont écrites dans fake_hal.c : elles simulent la carte
 *  (broches, minuteries, accéléromètre sur SPI, temps qui passe).
 *  Placé avant les vrais dossiers d'en-têtes dans les options -I, ce
 *  fichier remplace le vrai stm32l4xx_hal.h.
 ******************************************************************************
 */
#ifndef FAKE_STM32L4XX_HAL_H
#define FAKE_STM32L4XX_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Mots-clés et macros de base (CMSIS) --------------------------------- */
#define __IO              volatile
#define __weak            __attribute__((weak))
#define __PACKED          __attribute__((packed))
#define __STATIC_INLINE   static inline
#define UNUSED(X)         ((void)(X))

#define USE_HAL_SPI_REGISTER_CALLBACKS   0U
#define USE_HAL_PCD_REGISTER_CALLBACKS   0U
#define USE_HAL_TIM_REGISTER_CALLBACKS   0U

typedef enum
{
  HAL_OK      = 0x00U,
  HAL_ERROR   = 0x01U,
  HAL_BUSY    = 0x02U,
  HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;

/* ---- Interruptions -------------------------------------------------------- */
typedef enum
{
  EXTI15_10_IRQn = 40,
  TIM7_IRQn      = 55,
  OTG_FS_IRQn    = 67
} IRQn_Type;

void     HAL_NVIC_DisableIRQ(IRQn_Type irq);
void     HAL_NVIC_EnableIRQ(IRQn_Type irq);
uint32_t __get_PRIMASK(void);
void     __set_PRIMASK(uint32_t primask);
void     __disable_irq(void);
void     __enable_irq(void);
void     __WFI(void);

typedef struct
{
  uint32_t Line;
  void   (*PendingCallback)(void);
} EXTI_HandleTypeDef;

/* ---- GPIO ----------------------------------------------------------------- */
typedef struct
{
  uint32_t port;          /* 0 = A, 1 = B, 2 = C, 7 = H */
} GPIO_TypeDef;

extern GPIO_TypeDef fake_gpioa, fake_gpiob, fake_gpioc, fake_gpioh;
#define GPIOA (&fake_gpioa)
#define GPIOB (&fake_gpiob)
#define GPIOC (&fake_gpioc)
#define GPIOH (&fake_gpioh)

#define GPIO_PIN_0   ((uint16_t)0x0001)
#define GPIO_PIN_1   ((uint16_t)0x0002)
#define GPIO_PIN_2   ((uint16_t)0x0004)
#define GPIO_PIN_3   ((uint16_t)0x0008)
#define GPIO_PIN_4   ((uint16_t)0x0010)
#define GPIO_PIN_5   ((uint16_t)0x0020)
#define GPIO_PIN_6   ((uint16_t)0x0040)
#define GPIO_PIN_7   ((uint16_t)0x0080)
#define GPIO_PIN_8   ((uint16_t)0x0100)
#define GPIO_PIN_9   ((uint16_t)0x0200)
#define GPIO_PIN_10  ((uint16_t)0x0400)
#define GPIO_PIN_11  ((uint16_t)0x0800)
#define GPIO_PIN_12  ((uint16_t)0x1000)
#define GPIO_PIN_13  ((uint16_t)0x2000)
#define GPIO_PIN_14  ((uint16_t)0x4000)
#define GPIO_PIN_15  ((uint16_t)0x8000)

typedef enum
{
  GPIO_PIN_RESET = 0U,
  GPIO_PIN_SET
} GPIO_PinState;

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void          HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);

/* ---- Minuteries ----------------------------------------------------------- */
typedef struct
{
  volatile uint32_t CR1, SR, EGR, CNT, ARR, CCR1, CCR2, CCR3, CCR4;
} TIM_TypeDef;

extern TIM_TypeDef fake_tim1, fake_tim3, fake_tim7;
#define TIM1 (&fake_tim1)
#define TIM3 (&fake_tim3)
#define TIM7 (&fake_tim7)

typedef struct
{
  TIM_TypeDef *Instance;
  int          running;   /* simulation : minuterie lancée */
} TIM_HandleTypeDef;

#define TIM_CHANNEL_1    0x00000000U
#define TIM_CHANNEL_2    0x00000004U
#define TIM_CHANNEL_3    0x00000008U
#define TIM_CHANNEL_4    0x0000000CU
#define TIM_FLAG_UPDATE  0x00000001U

volatile uint32_t *fake_tim_ccr(TIM_HandleTypeDef *htim, uint32_t channel);
#define __HAL_TIM_SET_COMPARE(h, ch, v)   (*fake_tim_ccr((h), (ch)) = (uint32_t)(v))
#define __HAL_TIM_SET_COUNTER(h, v)       ((h)->Instance->CNT = (uint32_t)(v))
#define __HAL_TIM_CLEAR_FLAG(h, f)        ((h)->Instance->SR = ~(uint32_t)(f))

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim, uint32_t channel);
HAL_StatusTypeDef HAL_TIMEx_PWMN_Start(TIM_HandleTypeDef *htim, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim);

/* ---- SPI ------------------------------------------------------------------ */
typedef struct
{
  uint32_t id;
} SPI_TypeDef;

typedef struct
{
  SPI_TypeDef *Instance;
  uint32_t     reserved[8];
} SPI_HandleTypeDef;

HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi, uint8_t *tx, uint8_t *rx,
                                          uint16_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *data, uint16_t size,
                                   uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *data, uint16_t size,
                                  uint32_t timeout);

/* ---- Système -------------------------------------------------------------- */
uint32_t HAL_GetTick(void);
void     HAL_Delay(uint32_t ms);
uint32_t HAL_GetUIDw0(void);
uint32_t HAL_GetUIDw1(void);
uint32_t HAL_GetUIDw2(void);
void     HAL_DBGMCU_EnableDBGSleepMode(void);

#endif /* FAKE_STM32L4XX_HAL_H */
