/**
 ******************************************************************************
 * @file    Tests/fakes/fake_board.h
 * @brief   Commande de la carte simulée sur PC (fake_hal.c, fake_usb.c,
 *          fake_bluenrg.c) : temps, bouton, accéléromètre, minuterie, port
 *          USB vu du PC, module Bluetooth vu du téléphone.
 ******************************************************************************
 */
#ifndef FAKE_BOARD_H
#define FAKE_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stm32l4xx_hal.h"

/* ---- Temps ---------------------------------------------------------------- */
extern uint64_t fake_now_us;                 /* temps simulé, en µs */
void     fake_time_advance_ms(uint32_t ms);

/* ---- Chargeur Bluetooth (fakes/obcp_ota_request.h) ------------------------ */
extern int      fake_ota_enter_count;        /* appels à obcp_ota_enter_loader() */

/* ---- Interruptions -------------------------------------------------------- */
extern uint32_t fake_primask;                /* 1 : interruptions masquées */
extern int      fake_wfi_count;              /* nombre d'appels à __WFI() */
extern int      fake_exti_disable_count;     /* HAL_NVIC_DisableIRQ(EXTI15_10) */
extern bool     fake_exti_enabled;

/* ---- Bouton (PC13, actif à 0) ---------------------------------------------- */
/* Bouton enfoncé entre press_at et release_at (µs). Une liste de
 * changements permet de simuler des rebonds. */
void fake_button_set(bool down);
void fake_button_schedule(const uint64_t *change_times_us, size_t count, bool first_level_down);
void fake_button_exti(void);                 /* déclenche HAL_GPIO_EXTI_Callback(PC13) */

/* ---- Broches -------------------------------------------------------------- */
GPIO_PinState fake_gpio_get(GPIO_TypeDef *port, uint16_t pin);   /* niveau écrit */
void          fake_gpio_set_input(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level);

/* ---- Minuteries ------------------------------------------------------------ */
extern TIM_HandleTypeDef htim1, htim3, htim7;
extern int fake_pwm_started[4];              /* rouge, vert, bleu, moteur */
void fake_timer7_fire(void);                 /* interruption de TIM7 si lancée */

/* ---- Accéléromètre (SPI3, CS = PC5) --------------------------------------- */
typedef enum { FAKE_ACC_NONE = 0, FAKE_ACC_LIS3DH, FAKE_ACC_LIS3DSH } fake_acc_chip_t;
extern SPI_HandleTypeDef hspi3;
void     fake_acc_set_chip(fake_acc_chip_t chip);
void     fake_acc_set_raw(int16_t x, int16_t y, int16_t z);
void     fake_acc_set_temperature_delta(int8_t delta);   /* valeur brute du capteur */
uint8_t  fake_acc_reg(uint8_t addr);                     /* registre (contrôle) */
extern int  fake_acc_transactions;                       /* nombre d'échanges CS */
extern int  fake_acc_xyz_burst_reads;                    /* lectures X,Y,Z en un échange */
extern bool fake_spi_fail;                               /* forcer HAL_ERROR */
extern bool fake_acc_cs_low;                             /* CS actuellement à 0 */

#endif /* FAKE_BOARD_H */
