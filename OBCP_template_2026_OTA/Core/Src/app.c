/**
 ******************************************************************************
 * @file    app.c
 * @brief   Application ObCP : démarrage et boucle principale (voir app.h).
 *
 *  Pour suivre le programme, partir de app_init() puis de app_loop() :
 *
 *    main()  (Core/Src/main.c, généré par CubeMX)
 *     ├─ HAL_Init(), SystemClock_Config() : horloge à 64 MHz
 *     ├─ MX_GPIO_Init(), MX_USART2_UART_Init(), MX_SPI3_Init(),
 *     │  MX_TIM1_Init(), MX_TIM3_Init(), MX_TIM7_Init(), MX_USB_DEVICE_Init()
 *     ├─ app_init() : fautes détaillées, sorties PWM, accéléromètre,
 *     │               bouton, Bluetooth, signal de démarrage par la LED
 *     └─ while (1) app_loop() :
 *          1. ble_uart_process()     évènements Bluetooth, publicité, envois
 *          2. connexions Bluetooth   messages vers le terminal
 *          3. commandes du téléphone commands_execute(..., CMD_SRC_BLE)
 *          4. commandes du PC        commands_execute(..., CMD_SRC_USB)
 *          5. déconnexion Bluetooth  sorties éteintes, minuterie arrêtée
 *          6. « USB Connected »      vers le téléphone
 *          7. bouton                 « Push button pressed »
 *          8. minuterie 1 s          mesures vers le téléphone
 *          9. votre code
 *         10. usb_console_process()  envoi vers le PC
 *         11. __WFI()                sommeil jusqu'à la prochaine interruption
 *
 *  Règle d'or de la super boucle : aucune étape n'attend. Une attente
 *  (HAL_Delay, boucle qui guette un évènement) bloque toutes les autres :
 *  le Bluetooth et l'USB ne sont plus servis pendant ce temps.
 *
 *  Messages spontanés (identiques à la version 2024) :
 *    terminal USB : « Push button pressed », « Generated BLE Address: ... »,
 *                   « Connected to device: ... », « BLE disconnected :p » ;
 *    téléphone    : « PButton pressed », « USB Connected », et chaque
 *                   seconde après « start timer » : « X0.01Y-0.02Z1.00 »
 *                   puis « T = 25 ».
 ******************************************************************************
 */
#include "app.h"

#include <stdint.h>

#include "app_config.h"
#include "accelerometer.h"
#include "ble_uart.h"
#include "button.h"
#include "commands.h"
#include "fault.h"
#include "main.h"
#include "ota_switch.h"
#include "outputs.h"
#include "spi.h"            /* hspi3 : bus SPI de l'accéléromètre */
#include "timer_1s.h"
#include "usb_console.h"

/* Signal de démarrage (voir signal_startup). */
#define STARTUP_FLASH_LEVEL   16384U   /* 25 % de la pleine luminosité */
#define STARTUP_FLASH_MS      150U

static accel_chip_t      s_accel_chip;   /* accéléromètre trouvé au démarrage */
static ble_uart_status_t s_ble_status;   /* résultat du démarrage du Bluetooth */

/* -------------------------------------------------------------------------- */
/* Démarrage                                                                  */
/* -------------------------------------------------------------------------- */

/* count éclats de la couleur led. */
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

/* Signal de démarrage par la LED tricolore, pour une carte sans terminal :
 *   1 éclat vert            tout va bien ;
 *   2 éclats rouges         accéléromètre absent ou muet ;
 *   3 éclats rouges         module Bluetooth absent ou muet.
 * (Les deux erreurs ensemble : 2 éclats, une pause, puis 3 éclats.)      */
static void signal_startup(void)
{
  bool accel_ok = (s_accel_chip != ACCEL_NONE);
  bool ble_ok   = (s_ble_status == BLE_UART_OK);

  if (accel_ok && ble_ok)
  {
    flash(OUTPUT_GREEN, 1);
    return;
  }
  if (!accel_ok)
  {
    flash(OUTPUT_RED, 2);
    HAL_Delay(400U);
  }
  if (!ble_ok)
  {
    flash(OUTPUT_RED, 3);
  }
}

/* Voir app.h */
void app_init(void)
{
  fault_init();                          /* fautes détaillées (voir fault.h) */
  outputs_init();                        /* PWM démarrées, tout éteint */
  s_accel_chip = accel_init(&hspi3);     /* LIS3DH (ou LIS3DSH) sur SPI3 */
  button_init();
  s_ble_status = ble_uart_init();        /* environ 150 ms */
  signal_startup();

#ifdef DEBUG
  /* Version « Debug » : garde la sonde ST-LINK (connecteur J17) connectée
   * pendant le sommeil de __WFI().                                       */
  HAL_DBGMCU_EnableDBGSleepMode();
#endif
}

/* -------------------------------------------------------------------------- */
/* Évènements                                                                 */
/* -------------------------------------------------------------------------- */

/* Exécute une ligne reçue, ou la rejette si elle était trop longue. */
static void execute_line(line_status_t status, const char *line, cmd_source_t src)
{
  if (status == LINE_READY)
  {
    commands_execute(line, src);
  }
  else if (status == LINE_TOO_LONG)
  {
    commands_reject(src);
  }
}

/* Un téléphone vient de se connecter : on l'écrit dans le terminal. Les
 * adresses s'affichent octet de poids fort en premier (forme habituelle). */
static void on_ble_connected(const uint8_t peer[6])
{
  uint8_t own[6];

  if (!usb_console_is_ready())
  {
    return;
  }
  ble_uart_get_address(own);
  (void)usb_console_printf("Generated BLE Address: %02X:%02X:%02X:%02X:%02X:%02X\n\r",
                           own[5], own[4], own[3], own[2], own[1], own[0]);
  (void)usb_console_printf("Connected to device: %02X:%02X:%02X:%02X:%02X:%02X\n\r",
                           peer[5], peer[4], peer[3], peer[2], peer[1], peer[0]);
}

/* Le téléphone s'est déconnecté : message, puis tout s'arrête (comme en
 * 2024) pour ne pas laisser une LED ou le moteur allumés sans contrôle. */
static void on_ble_disconnected(void)
{
  if (usb_console_is_ready())
  {
    (void)usb_console_print("BLE disconnected :p\n\r");
  }
  outputs_all_off();
  timer_1s_stop();
}

/* Chaque seconde après « start timer » : accélération puis température,
 * envoyées au téléphone seulement, en deux messages séparés (deux
 * notifications), au format attendu par l'application Android.          */
static void send_measurements(void)
{
  accel_g_t g;
  int       celsius;

  if (!ble_uart_is_connected())
  {
    return;
  }
  if (accel_read(&g))
  {
    (void)ble_uart_printf("X%.2fY%.2fZ%.2f\n\r", (double)g.x, (double)g.y, (double)g.z);
  }
  if (accel_read_temperature(&celsius))
  {
    (void)ble_uart_printf("T = %i\n\r", celsius);
  }
}

/* -------------------------------------------------------------------------- */
/* Boucle principale                                                          */
/* -------------------------------------------------------------------------- */

/* Voir app.h */
void app_loop(void)
{
  char             line[CMD_LINE_MAX + 1U];
  line_status_t    status;
  ble_uart_event_t event;
  uint8_t          peer[6];
  bool             disconnected = false;

  /* 1. Bluetooth : évènements du module, publicité, notifications. */
  ble_uart_process();

  /* 2. Connexions. Une déconnexion est traitée après les commandes reçues
   *    juste avant elle (étape 5) : les sorties finissent éteintes.       */
  while ((event = ble_uart_get_event(peer)) != BLE_EVENT_NONE)
  {
    if (event == BLE_EVENT_CONNECTED)
    {
      on_ble_connected(peer);
    }
    else
    {
      disconnected = true;
    }
  }

  /* 3. Commandes reçues du téléphone. */
  while ((status = ble_uart_read_line(line, sizeof(line))) != LINE_NONE)
  {
    execute_line(status, line, CMD_SRC_BLE);
  }

  /* 4. Commandes reçues du PC. */
  while ((status = usb_console_read_line(line, sizeof(line))) != LINE_NONE)
  {
    execute_line(status, line, CMD_SRC_USB);
  }

  /* 5. Déconnexion du téléphone. */
  if (disconnected)
  {
    on_ble_disconnected();
  }

  /* 6. L'USB vient d'être (re)connecté alors qu'un téléphone l'est aussi. */
  if (ble_uart_is_connected() && usb_console_take_link_up_event())
  {
    (void)ble_uart_print("USB Connected\n\r");
  }

  /* 7. Bouton utilisateur : message vers chaque interface active (le texte
   *    BLE est plus court : 20 caractères au plus par notification).       */
  if (button_poll_pressed())
  {
    if (usb_console_is_ready())
    {
      (void)usb_console_print("Push button pressed\n\r");
    }
    if (ble_uart_is_connected())
    {
      (void)ble_uart_print("PButton pressed\n\r");
    }
  }

  /* 8. Une seconde s'est écoulée depuis « start timer ». */
  if (timer_1s_take_tick())
  {
    send_measurements();
  }

  /* 9. ---- Votre code ici : traitements à chaque tour de boucle ---------
   *    Pas d'attente (HAL_Delay) ici : pour agir toutes les N ms, on
   *    compare HAL_GetTick() (millisecondes depuis le démarrage) à
   *    l'instant de la dernière action.
   *    Exemple : toutes les 100 ms, LED bleue allumée si la carte est
   *    retournée (Z proche de -1 g).
   *
   *      static uint32_t last_ms;                     // dernière action
   *      if ((HAL_GetTick() - last_ms) >= 100U)       // 100 ms écoulées ?
   *      {
   *        accel_g_t g;
   *        last_ms = HAL_GetTick();
   *        if (accel_read(&g))
   *        {
   *          outputs_set(OUTPUT_BLUE, (g.z < -0.5f) ? 20000U : 0U);
   *        }
   *      }
   *
   *    La soustraction « maintenant - dernière fois » reste juste quand le
   *    compteur de HAL_GetTick() repasse par 0 (au bout de 49 jours).
   * ------------------------------------------------------------------------ */

  /* 10. Envoi vers le PC de ce qui attend dans le tampon USB. */
  usb_console_process();

  /* 11. Commande « #ota » reçue : passage au chargeur Bluetooth, une fois
   *     la réponse partie (voir ota_switch.h). Rien à faire sinon.        */
  ota_switch_process();

  /* 12. Sommeil jusqu'à la prochaine interruption (SysTick : 1 ms au plus,
   *     USB, Bluetooth, bouton, minuterie). Le processeur s'arrête, les
   *     périphériques continuent : la consommation baisse.               */
#if (APP_SLEEP_WHEN_IDLE != 0)
  __WFI();
#endif
}
