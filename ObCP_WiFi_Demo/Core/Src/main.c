/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * Démonstrateur Wi-Fi 6 : carte ObCP (STM32L476RG) + X-NUCLEO-67W61M1.
  *
  * Quatre démonstrations pilotées depuis un terminal série sur le port COM
  * virtuel USB de l'ObCP (prise micro-USB, aucun réglage de débit) :
  *   1. scanner Wi-Fi et suivi du RSSI (LED RVB)
  *   2. connexion et services réseau (DHCP / IP fixe, DNS, ping, SNTP -> RTC)
  *   3. serveur web en point d'accès (SoftAP)
  *   4. télémétrie MQTT (accéléromètre, LED RVB, sortie PWM)
  * Taper « help » dans le terminal pour la liste des commandes.
  *
  * Fichier généré par STM32CubeMX (ObCP_WiFi_Demo.ioc). Le code de la démo se
  * limite aux sections USER CODE (conservées à chaque régénération) :
  *   USER CODE 1     board_early_init() : saut éventuel dans le chargeur DFU ;
  *   USER CODE Init  board_lse_start()  : quartz 32 kHz (amplification) ;
  *   USER CODE 2     board_post_init(), app_init() ;
  *   USER CODE WHILE app_loop() dans la boucle principale ;
  *   Error_Handler   board_error_blink() : diagnostic par la LED rouge.
  * Le reste de la démo est dans des fichiers que CubeMX ne génère pas
  * (Core/Src/board.c, cdc_port.c, fault_handlers.c, uart_bridge.c,
  * ncp_port_obcp.c, Core/Src/app/, Core/Src/st67w6x/, Core/Src/qrcodegen/).
  *
  * Par où commencer la lecture ? Ce fichier (ordre du démarrage), puis
  * Core/Src/app/app.c (app_init, app_loop : le cœur de l'application),
  * cli.c (les commandes du terminal), une démo (demo_scan.c...), et enfin
  * le pilote du module (Core/Inc/ncp.h présente ses couches). Chaque
  * fonction est décrite dans l'en-tête .h qui la déclare ; carte du
  * programme et liste des fonctions : Doc/Documentation_Programme_ObCP.docx.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "rtc.h"
#include "spi.h"
#include "tim.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "board.h"
#include "app.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  board_early_init();            /* éventuel saut dans le chargeur USB DFU (commande « dfu ») */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  board_lse_start();             /* quartz 32 kHz avec l'amplification qu'il demande */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_SPI3_Init();
  MX_ADC1_Init();
  MX_TIM1_Init();
  MX_TIM3_Init();
  MX_RTC_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
  board_post_init();             /* SPI actifs, PWM démarrées, ADC étalonné, date de la RTC */
  app_init();                    /* configuration, module Wi-Fi, console */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  /* Boucle principale, sans fin : chaque tour lit le terminal et fait avancer
   * les tâches de fond (Wi-Fi, serveur web, MQTT). Aucune fonction ne doit y
   * rester bloquée longtemps : pas de RTOS, tout est « à tour de rôle ».   */
  while (1)
  {
    app_loop();
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_MEDIUMHIGH);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 20;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* La console USB n'est pas forcément disponible : LED rouge clignotante
   * (2 éclats : quartz 8 MHz, 3 éclats : quartz 32 kHz, rapide : autre).  */
  board_error_blink();
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
