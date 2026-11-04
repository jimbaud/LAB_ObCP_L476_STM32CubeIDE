/**
 ******************************************************************************
 * @file    board.h
 * @brief   Carte ObCP (STM32L476RG, SUPMICROTECH-ENSMM, version 2.3) +
 *          X-NUCLEO-67W61M1 enfichée sur ses connecteurs Arduino Uno.
 *
 *  Les broches, horloges et périphériques sont réglés dans STM32CubeMX
 *  (ObCP_WiFi_Demo.ioc) ; ce fichier (qui n'est pas généré) donne aux
 *  modules de la démo des noms indépendants de CubeMX, construits à partir
 *  des étiquettes (« User Label ») des broches dans le .ioc. Déplacer une
 *  broche dans CubeMX en gardant son étiquette suffit donc à la démo.
 *
 *  Signal X-NUCLEO      Arduino   STM32L476   Étiquette CubeMX
 *  ------------------   -------   ---------   -----------------------------
 *  SPI_CLK              D13       PA5         NCP_SCK   (SPI1)
 *  SPI_MISO             D12       PA6         NCP_MISO  (SPI1)
 *  SPI_MOSI             D11       PA7         NCP_MOSI  (SPI1)
 *  SPI_CS (réveil)      D10       PB6         NCP_CS    sortie, actif HAUT
 *  SPI_RDY (+ LED1)     D3        PB3         NCP_RDY   entrée
 *  CHIP_EN              D5        PB4         NCP_CHIP_EN
 *  BOOT                 D6        PB10        NCP_BOOT  0 = mode mission
 *  Bouton SW1           D8        PA9         NCP_SW1   entrée, actif bas
 *  UART du module       D0/D1     PA3/PA2     NCP_UART_RX / NCP_UART_TX :
 *                                            entrées au repos ; l'USART2 ne
 *                                            les pilote que pendant
 *                                            « module boot » (uart_bridge.c).
 *
 *  Composants de l'ObCP utilisés par la démo :
 *  Console (USB device, CDC)  PA11/PA12       port COM virtuel « Périphérique série USB »
 *  LED RVB (via 2N2222)       PC6/PC8/PC9     Led_Red/Led_Green/Led_Blue : TIM3 CH1, CH3, CH4
 *  Sortie PWM (bornier J13)   PB1             PWM_Motor : TIM1_CH3N, MOSFET (charge sur 5 V)
 *  Accéléromètre LIS3DH/LIS3DSH SPI3 PC10/11/12, ACC_CS = PC5 (modèle reconnu au démarrage)
 *  Bouton User1 (SW1 ObCP)    PC13            User1, actif bas (rappel externe R1)
 *  Quartz 8 MHz (HSE)         PH0/PH1         horloge système (80 MHz) et USB (48 MHz)
 *  Quartz 32,768 kHz (LSE)    PC14/PC15       RTC
 *  Module BLE (BlueNRG-M0)    PC2             BLE_Reset : maintenu en reset (non utilisé)
 *  Interrupteur Prog/Run SW4  BOOT0           Prog = chargeur USB DFU de ST
 ******************************************************************************
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include "main.h"            /* étiquettes des broches générées par CubeMX */
#include "timeutil.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- X-NUCLEO-67W61M1 ---------------------------------------------------------- */
#define NCP_CS_PORT             NCP_CS_GPIO_Port
#define NCP_CS_PIN              NCP_CS_Pin
#define NCP_RDY_PORT            NCP_RDY_GPIO_Port
#define NCP_RDY_PIN             NCP_RDY_Pin
#define NCP_EN_PORT             NCP_CHIP_EN_GPIO_Port
#define NCP_EN_PIN              NCP_CHIP_EN_Pin
#define NCP_BOOT_PORT           NCP_BOOT_GPIO_Port
#define NCP_BOOT_PIN            NCP_BOOT_Pin
/* UART du module (D0 = PA3 = RX du STM32, D1 = PA2 = TX du STM32), même port */
#define NCP_UART_PORT           NCP_UART_TX_GPIO_Port
#define NCP_UART_TX_PIN         NCP_UART_TX_Pin
#define NCP_UART_RX_PIN         NCP_UART_RX_Pin

/* ---- Interface utilisateur de l'ObCP ------------------------------------------------ */
#define BTN_USER_PORT           User1_GPIO_Port      /* bouton User1 de l'ObCP */
#define BTN_USER_PIN            User1_Pin
#define BTN_SW1_PORT            NCP_SW1_GPIO_Port    /* bouton SW1 de la X-NUCLEO */
#define BTN_SW1_PIN             NCP_SW1_Pin
#define LED_R_PORT              Led_Red_GPIO_Port
#define LED_R_PIN               Led_Red_Pin
#define LED_R_CHANNEL           TIM_CHANNEL_1
#define LED_G_PORT              Led_Green_GPIO_Port
#define LED_G_PIN               Led_Green_Pin
#define LED_G_CHANNEL           TIM_CHANNEL_3
#define LED_B_PORT              Led_Blue_GPIO_Port
#define LED_B_PIN               Led_Blue_Pin
#define LED_B_CHANNEL           TIM_CHANNEL_4
#define PWM_OUT_PORT            PWM_Motor_GPIO_Port
#define PWM_OUT_PIN             PWM_Motor_Pin
#define PWM_OUT_CHANNEL         TIM_CHANNEL_3        /* sortie complémentaire CH3N */
#define ACC_CS_PORT             ACC_CS_GPIO_Port
#define ACC_CS_PIN              ACC_CS_Pin
#define BLE_RST_PORT            BLE_Reset_GPIO_Port
#define BLE_RST_PIN             BLE_Reset_Pin

/* Périodes des PWM (registre ARR + 1, réglé dans CubeMX : 1000 pour 1 kHz) */
#define LED_PWM_PERIOD          (__HAL_TIM_GET_AUTORELOAD(&htim3) + 1U)
#define OUT_PWM_PERIOD          (__HAL_TIM_GET_AUTORELOAD(&htim1) + 1U)

/* Périphériques initialisés par le code généré (spi.c, adc.c, tim.c, rtc.c) */
extern SPI_HandleTypeDef  hspi1;     /* module Wi-Fi */
extern SPI_HandleTypeDef  hspi3;     /* accéléromètre */
extern ADC_HandleTypeDef  hadc1;     /* température interne, VREFINT */
extern TIM_HandleTypeDef  htim1;     /* sortie PWM du bornier J13 */
extern TIM_HandleTypeDef  htim3;     /* LED RVB */
extern RTC_HandleTypeDef  hrtc;      /* heure UTC */

/**
 * @brief  À appeler en tout premier dans main() (USER CODE 1) : saute dans le
 *         chargeur USB DFU de ST si la commande « dfu » l'a demandé avant le
 *         redémarrage (indicateur conservé dans un registre de la RTC).
 */
void        board_early_init(void);

/**
 * @brief  Avant SystemClock_Config() (USER CODE Init) : démarre le quartz
 *         32 kHz avec l'amplification « medium high » qu'il demande. Si le
 *         quartz ne démarre pas, il est arrêté et la configuration de CubeMX
 *         décide de la suite (voir board.c).
 */
void        board_lse_start(void);

/**
 * @brief  Après les MX_xxx_Init() (USER CODE 2) : active les SPI, démarre les
 *         PWM, étalonne l'ADC, règle la date de la RTC à la première mise
 *         sous tension et calcule l'identifiant de la carte.
 */
void        board_post_init(void);

/**
 * @brief  Appelée par Error_Handler() : LED rouge clignotante, ne revient pas.
 *         2 éclats : quartz 8 MHz (HSE) ; 3 éclats : quartz 32 kHz (LSE) ;
 *         clignotement rapide continu : autre erreur d'initialisation.
 */
void        board_error_blink(void);

/** @brief  Vrai si la RTC est cadencée par le quartz 32 kHz (LSE). */
bool        board_lse_ok(void);

/** @brief  Vrai si la PLL (horloge système) est alimentée par le quartz 8 MHz (HSE). */
bool        board_hse_ok(void);

/**
 * @brief  Identifiant court de la carte : 6 caractères hexadécimaux tirés de
 *         l'identifiant unique du STM32 (« A1B2C3 »). Sert à nommer le point
 *         d'accès (ObCP-WiFi-A1B2C3) et les topics MQTT (obcp/A1B2C3/...).
 */
const char *board_uid_short(void);

/** @brief  Identifiant unique complet du STM32 (96 bits) en hexadécimal. */
void        board_uid_full(char *out, size_t len);

/** @brief  Adresse de fin du programme en flash (contrôle avant d'effacer la page de configuration). */
uint32_t    board_image_end(void);

/** @brief  Redémarre la carte (l'USB est d'abord arrêté proprement). */
void        board_reset(void);

/**
 * @brief  Redémarre dans le chargeur USB DFU (programmation par
 *         STM32CubeProgrammer). Ne revient pas (sur la carte).
 */
void        board_reboot_to_dfu(void);

/* Rapport de faute (HardFault...) conservé dans les registres de sauvegarde
 * de la RTC pendant le redémarrage, puis affiché au démarrage suivant.     */
typedef struct
{
  uint32_t kind;                         /* 0..3 : HardFault, MemManage, BusFault, UsageFault ; 4 : NMI */
  uint32_t pc;                           /* adresse de l'instruction fautive    */
  uint32_t lr;                           /* adresse de retour de la fonction    */
  uint32_t cfsr;                         /* registres d'état des fautes du      */
  uint32_t hfsr;                         /* Cortex-M4 (cause détaillée)         */
  uint32_t bfar;                         /* adresse en cause d'un BusFault      */
} board_fault_t;

/** @brief  Enregistre un rapport de faute dans les registres de sauvegarde (fault_handlers.c). */
void        board_fault_save(const board_fault_t *f);

/**
 * @brief  Lit et efface le rapport de faute du démarrage précédent.
 * @retval true (une seule fois) si une faute a eu lieu avant ce démarrage.
 */
bool        board_fault_take(board_fault_t *f);

/* ---- RTC en temps universel (UTC) -------------------------------------------------- */
/** @brief  Lit la date et l'heure UTC de la RTC. @retval false si la lecture échoue. */
bool        board_rtc_get(dt_t *dt);

/** @brief  Règle la date et l'heure UTC de la RTC (après une synchronisation SNTP). */
bool        board_rtc_set(const dt_t *dt);

/** @brief  Vrai si l'heure a été réglée par SNTP depuis la mise sous tension de la RTC. */
bool        board_rtc_synced(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
