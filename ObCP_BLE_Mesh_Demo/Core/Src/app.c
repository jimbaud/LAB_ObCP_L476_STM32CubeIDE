/**
 ******************************************************************************
 * @file    app.c
 * @brief   Démonstration du réseau maillé Bluetooth : démarrage et boucle
 *          principale (voir app.h).
 *
 *  Pour suivre le programme, partir de app_init() puis de app_loop() :
 *
 *    main()  (Core/Src/main.c, généré par CubeMX)
 *     ├─ obcp_ota_init_vectors(), HAL_Init(), SystemClock_Config() : 64 MHz
 *     ├─ MX_GPIO_Init(), MX_USART2_UART_Init(), MX_SPI3_Init(),
 *     │  MX_TIM1_Init(), MX_TIM3_Init(), MX_TIM7_Init(), MX_USB_DEVICE_Init()
 *     ├─ app_init() : fautes détaillées, sorties PWM, accéléromètre, bouton,
 *     │               module Bluetooth (mesh_radio), nœud (node), signal
 *     │               de démarrage par la LED, minuterie d'une seconde
 *     └─ while (1) app_loop() :
 *          1. mesh_radio_process()  évènements du module : trames reçues,
 *                                   fin des émissions, relance de l'écoute
 *          2. node_process()        trames reçues -> réseau maillé (relais,
 *                                   doublons...), émission de la suivante,
 *                                   réponses, délais d'attente
 *          3. commandes du PC       commands_execute()
 *          4. câble USB branché     présentation dans le terminal
 *          5. bouton SW1            vague
 *          6. seconde écoulée       envoi automatique des mesures
 *          7. votre code
 *          8. led_fx_process()      effets de la LED
 *          9. usb_console_process() envoi vers le PC
 *         10. #ota demandé ?        redémarrage dans le chargeur
 *         11. __WFI()               sommeil jusqu'à la prochaine interruption
 *
 *  Règle d'or de la super boucle : aucune étape n'attend. Une attente
 *  (HAL_Delay, boucle qui guette un évènement) bloque toutes les autres :
 *  les trames reçues pendant ce temps s'accumulent, et les relais partent
 *  en retard.
 ******************************************************************************
 */
#include "app.h"

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "accelerometer.h"
#include "button.h"
#include "commands.h"
#include "fault.h"
#include "led_fx.h"
#include "main.h"
#include "mesh_radio.h"
#include "node.h"
#include "obcp_ota_request.h"
#include "outputs.h"
#include "report.h"
#include "spi.h"            /* hspi3 : bus SPI de l'accéléromètre */
#include "timer_1s.h"
#include "usb_console.h"

/* Signal de démarrage (voir signal_startup). */
#define STARTUP_FLASH_LEVEL   16384U   /* 25 % de la pleine luminosité */
#define STARTUP_FLASH_MS      150U

/* Délai entre « #ota » et le redémarrage, en ms. */
#define OTA_DELAY_MS          300U

static accel_chip_t   s_accel_chip;    /* accéléromètre trouvé au démarrage */
static radio_status_t s_radio_status;  /* résultat du démarrage du module Bluetooth */
static bool           s_ota_requested;
static uint32_t       s_ota_at;

/* -------------------------------------------------------------------------- */
/* Démarrage                                                                  */
/* -------------------------------------------------------------------------- */

/* count éclats de la couleur led (attente active : seulement au démarrage). */
static void flash(output_t led, int count)
{
  for (int i = 0; i < count; i++)
  {
    outputs_set(led, STARTUP_FLASH_LEVEL);
    HAL_Delay(STARTUP_FLASH_MS);
    outputs_set(led, 0U);
    HAL_Delay(STARTUP_FLASH_MS);
  }
}

/* Signal de démarrage par la LED tricolore, comme le programme modèle :
 *   1 éclat vert            tout va bien ;
 *   2 éclats rouges         accéléromètre absent ou muet ;
 *   3 éclats rouges         module Bluetooth absent ou muet (pas de réseau).
 * (Les deux erreurs ensemble : 2 éclats, une pause, puis 3 éclats.)      */
static void signal_startup(void)
{
  bool accel_ok = (s_accel_chip != ACCEL_NONE);
  bool radio_ok = (s_radio_status == RADIO_OK);

  if (accel_ok && radio_ok)
  {
    flash(OUTPUT_GREEN, 1);
    return;
  }
  if (!accel_ok)
  {
    flash(OUTPUT_RED, 2);
    HAL_Delay(400U);
  }
  if (!radio_ok)
  {
    flash(OUTPUT_RED, 3);
  }
}

/* Voir app.h */
void app_init(void)
{
  fault_init();                          /* fautes détaillées (voir fault.h) */
  outputs_init();                        /* PWM démarrées, tout éteint */
  s_accel_chip   = accel_init(&hspi3);   /* LIS3DH (ou LIS3DSH) sur SPI3 */
  button_init();
  s_radio_status = mesh_radio_init();    /* environ 150 ms */
  node_init(s_radio_status);             /* réseau maillé, réglages enregistrés */
  signal_startup();
  led_fx_init();
  timer_1s_start();                      /* une interruption par seconde (TIM7) */

#ifdef DEBUG
  /* Version « Debug » : garde la sonde ST-LINK (connecteur J17) connectée
   * pendant le sommeil de __WFI().                                       */
  HAL_DBGMCU_EnableDBGSleepMode();
#endif
}

/* Voir app.h */
void app_request_ota(void)
{
  s_ota_requested = true;
  s_ota_at        = HAL_GetTick() + OTA_DELAY_MS;
}

/* Voir app.h */
bool app_address_is_ota(uintptr_t address)
{
  return (address >= APP_OTA_FLASH_START) && (address < APP_OTA_FLASH_END);
}

/* Voir app.h. g_pfnVectors est déclarée dans obcp_ota_request.h. */
bool app_is_ota(void)
{
  return app_address_is_ota((uintptr_t)g_pfnVectors);
}

/* -------------------------------------------------------------------------- */
/* Boucle principale                                                          */
/* -------------------------------------------------------------------------- */

/* Voir app.h */
void app_loop(void)
{
  char          line[CMD_LINE_MAX + 1U];
  line_status_t status;

  /* 1. Module Bluetooth : trames reçues, fin des émissions, écoute. */
  mesh_radio_process();

  /* 2. Réseau maillé et réactions de la carte. */
  node_process();

  /* 3. Commandes reçues du PC. */
  while ((status = usb_console_read_line(line, sizeof(line))) != LINE_NONE)
  {
    if (status == LINE_READY)
    {
      commands_execute(line);
    }
    else
    {
      commands_reject();                 /* ligne trop longue */
    }
  }

  /* 4. Câble USB branché (ou PC réveillé) : la carte se présente. */
  if (usb_console_take_link_up_event())
  {
    report_banner(node_mesh());
  }

  /* 5. Bouton utilisateur SW1 : une vague. */
  if (button_poll_pressed())
  {
    node_on_button();
  }

  /* 6. Une seconde s'est écoulée (TIM7). */
  if (timer_1s_take_tick())
  {
    node_on_second();
  }

  /* 7. ---- Votre code ici : traitements à chaque tour de boucle ---------
   *    Pas d'attente (HAL_Delay) ici. Pour agir toutes les N ms, comparer
   *    HAL_GetTick() à l'instant de la dernière action. Exemple : envoyer
   *    à tous un texte quand la carte est retournée (Z < -0,5 g), au plus
   *    une fois toutes les 2 s :
   *
   *      static uint32_t last_ms;
   *      if ((HAL_GetTick() - last_ms) >= 2000U)
   *      {
   *        accel_g_t g;
   *        if (accel_read(&g) && (g.z < -0.5f))
   *        {
   *          last_ms = HAL_GetTick();
   *          (void)node_send_text(MESH_ADDR_ALL, "retournée !");
   *        }
   *      }
   * ------------------------------------------------------------------------ */

  /* 8. Effets de la LED. */
  led_fx_process(HAL_GetTick());

  /* 9. Envoi vers le PC de ce qui attend dans le tampon USB. */
  usb_console_process();

  /* 10. « #ota » : redémarrage dans le chargeur, une fois la réponse partie. */
  if (s_ota_requested && ((int32_t)(HAL_GetTick() - s_ota_at) >= 0))
  {
    s_ota_requested = false;
    obcp_ota_enter_loader();             /* ne revient pas */
  }

  /* 11. Sommeil jusqu'à la prochaine interruption (SysTick : 1 ms au plus,
   *     USB, module Bluetooth, bouton, minuterie).                       */
#if (APP_SLEEP_WHEN_IDLE != 0)
  __WFI();
#endif
}
