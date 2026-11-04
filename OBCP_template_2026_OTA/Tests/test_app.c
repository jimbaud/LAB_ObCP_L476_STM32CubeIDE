/**
 ******************************************************************************
 * @file    Tests/test_app.c
 * @brief   Simulation sur PC de toute l'application (app.c et ses modules,
 *          usbd_cdc_if.c généré par CubeMX) avec une carte, un PC et un
 *          téléphone simulés : commandes et réponses, messages spontanés,
 *          et cas qui posaient problème dans la version 2024.
 ******************************************************************************
 */
#include <stdint.h>
#include <stdlib.h>
#include "check.h"
#include "fake_board.h"
#include "fake_bluenrg.h"
#include "fake_usb.h"

#include "app.h"
#include "app_config.h"
#include "accelerometer.h"
#include "ble_uart.h"
#include "commands.h"
#include "outputs.h"
#include "ota_switch.h"
#include "timer_1s.h"
#include "usb_console.h"
#include "usbd_cdc_if.h"

/* -------------------------------------------------------------------------- */
/* Outils                                                                     */
/* -------------------------------------------------------------------------- */
static char s_usb[70000];
static char s_ble[70000];

/* n tours de boucle ; le PC lit ce qui arrive et renvoie ses paquets. */
static void loop(int n)
{
  for (int i = 0; i < n; i++)
  {
    app_loop();
    fake_usb_host_read();
    (void)fake_usb_pump();
  }
}

/* Texte reçu par le PC depuis le dernier appel. */
static const char *usb_out(void)
{
  (void)fake_usb_host_take(s_usb, sizeof(s_usb));
  return s_usb;
}

/* Notifications reçues par le téléphone, mises bout à bout, puis oubliées. */
static const char *ble_out(void)
{
  size_t n = 0U;

  for (int i = 0; i < fake_ble_notif_count; i++)
  {
    memcpy(&s_ble[n], fake_ble_notif[i], fake_ble_notif_len[i]);
    n += fake_ble_notif_len[i];
  }
  s_ble[n] = '\0';
  fake_ble_clear_notifications();
  return s_ble;
}

static void usb_cmd(const char *s)
{
  fake_usb_host_send_str(s);
  loop(3);
}

static void ble_cmd(const char *s)
{
  fake_ble_write_str(s);
  loop(3);
}

/* Tours de boucle jusqu'au passage au chargeur (max tours au plus). Rend le
 * nombre de tours, 0 si le passage n'a pas eu lieu. Le PC ne lit plus rien
 * après le passage : usb_out() rend ce qui lui est arrivé AVANT. */
static int loop_until_loader(int max)
{
  int before = fake_ota_enter_count;

  for (int i = 1; i <= max; i++)
  {
    app_loop();
    if (fake_ota_enter_count != before)
    {
      return i;
    }
    fake_usb_host_read();
    (void)fake_usb_pump();
  }
  return 0;
}

static uint32_t ccr_red(void)   { return htim3.Instance->CCR1; }
static uint32_t ccr_green(void) { return htim3.Instance->CCR3; }
static uint32_t ccr_blue(void)  { return htim3.Instance->CCR4; }
static uint32_t ccr_motor(void) { return htim1.Instance->CCR3; }

static const uint8_t PEER[6] = { 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 };

/* -------------------------------------------------------------------------- */
static void test_boot(void)
{
  printf("- démarrage : PWM, accéléromètre, Bluetooth, publicité\n");
  fake_acc_set_chip(FAKE_ACC_LIS3DH);
  fake_acc_set_raw(16, -32, 16384);
  fake_acc_set_temperature_delta(2);
  app_init();

  CHECK(fake_pwm_started[0] == 1 && fake_pwm_started[1] == 1 && fake_pwm_started[2] == 1);
  CHECK(fake_pwm_started[3] == 1);
  CHECK(ccr_red() == 0U && ccr_green() == 0U && ccr_blue() == 0U && ccr_motor() == 0U);
  CHECK(accel_chip() == ACCEL_LIS3DH);
  CHECK(fake_acc_reg(0x20) == 0x27U);                /* CTRL_REG1 : 10 Hz, XYZ */
  CHECK(fake_acc_reg(0x23) == 0x88U);                /* CTRL_REG4 : BDU, ±2 g, HR */
  CHECK(fake_acc_reg(0x1F) == 0xC0U);                /* TEMP_CFG_REG */
  CHECK(fake_gpio_get(GPIOC, GPIO_PIN_5) == GPIO_PIN_SET);   /* CS au repos */
  CHECK(ble_uart_is_ready());
  CHECK(fake_ble_init_count == 1);
  CHECK(fake_exti_disable_count >= 1 && fake_exti_enabled);
  CHECK_STR(fake_ble_device_name, "OBCP-CROC");
  CHECK(memcmp(fake_ble_public_addr, fake_ble_random_addr, 6) == 0);
  CHECK(fake_ble_fixed_pin == 123456U);
  {
    static const uint8_t svc[16] = { 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                                     0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E };
    CHECK(memcmp(fake_ble_service_uuid, svc, 16) == 0);
    CHECK(fake_ble_tx_uuid[12] == 0x02 && fake_ble_rx_uuid[12] == 0x03);
    CHECK(memcmp(fake_ble_tx_uuid, svc, 12) == 0 && memcmp(fake_ble_rx_uuid + 13, svc + 13, 3) == 0);
  }
  CHECK(fake_ble_tx_props == (0x10 | 0x02));         /* NOTIFY | READ */
  CHECK(fake_ble_rx_props == (0x08 | 0x04));         /* WRITE | WRITE_WITHOUT_RESP */
  CHECK(fake_ble_adv_count == 0);
  loop(1);
  CHECK(fake_ble_adv_count == 1 && fake_ble_advertising);
  CHECK_STR(fake_ble_adv_name, "OBCP-CROC");
  CHECK(fake_wfi_count >= 1);                        /* sommeil en fin de boucle */
  CHECK(!usb_console_is_ready());
}

/* -------------------------------------------------------------------------- */
static void test_usb_commands(void)
{
  printf("- USB : commandes et réponses (envoi en paquets, comme les scripts Python)\n");
  fake_usb_connect();
  loop(1);
  CHECK(usb_console_is_ready());
  CHECK_STR(usb_out(), "");

  usb_cmd("r500\n");     CHECK_STR(usb_out(), "Red = 500\n\r");     CHECK(ccr_red() == 500U);
  usb_cmd("v1000\r\n");  CHECK_STR(usb_out(), "Green = 1000\n\r");  CHECK(ccr_green() == 1000U);
  usb_cmd("B65535\n");   CHECK_STR(usb_out(), "Blue = 65535\n\r");  CHECK(ccr_blue() == 65535U);
  usb_cmd("M32768\n");   CHECK_STR(usb_out(), "PWM = 32768\n\r");   CHECK(ccr_motor() == 32768U);
  usb_cmd("R7\r");       CHECK_STR(usb_out(), "Red = 7\n\r");       CHECK(ccr_red() == 7U);
  usb_cmd("V8\r\r");     CHECK_STR(usb_out(), "Green = 8\n\r");     /* OBCP_LED_RVB.py + CR */
  usb_cmd("a\n");        CHECK_STR(usb_out(), "Acceleration : X=0.00 Y=-0.00 Z=1.02\n\r");
  usb_cmd("A\n");        CHECK_STR(usb_out(), "Acceleration : X=0.00 Y=-0.00 Z=1.02\n\r");
  usb_cmd("T\n");        CHECK_STR(usb_out(), "T = 27\n\r");
  usb_cmd("t\n");        CHECK_STR(usb_out(), "T = 27\n\r");
  usb_cmd("?\n");        CHECK_STR(usb_out(), commands_help_text());
  usb_cmd("cls\n");      CHECK_STR(usb_out(), "\x1B[1;1H\x1B[2J");
  usb_cmd("xyz\n");      CHECK_STR(usb_out(), "False command\n\r");
  usb_cmd("\r\n");       CHECK_STR(usb_out(), "");                  /* ligne vide : rien */

  printf("- USB : minuterie, remise à zéro\n");
  usb_cmd("start timer\n"); CHECK_STR(usb_out(), "");  CHECK(timer_1s_is_running() && htim7.running);
  usb_cmd("start timer\n"); CHECK(timer_1s_is_running());
  fake_timer7_fire();
  loop(2);
  CHECK_STR(usb_out(), "");                          /* mesures envoyées par BLE seulement */
  usb_cmd("stop timer\n");  CHECK_STR(usb_out(), "");  CHECK(!timer_1s_is_running() && !htim7.running);
  usb_cmd("r100\nv200\nb300\nm400\n");
  CHECK_STR(usb_out(), "Red = 100\n\rGreen = 200\n\rBlue = 300\n\rPWM = 400\n\r");
  usb_cmd("start timer\n");
  usb_cmd("#rst\n");     CHECK_STR(usb_out(), "");
  CHECK(ccr_red() == 0U && ccr_green() == 0U && ccr_blue() == 0U && ccr_motor() == 0U);
  CHECK(!timer_1s_is_running());

  printf("- USB : valeurs invalides (nouveau : refusées et signalées)\n");
  usb_cmd("r70000\n");   CHECK_STR(usb_out(), "False value (0-65535)\n\r");  CHECK(ccr_red() == 0U);
  usb_cmd("r-5\n");      CHECK_STR(usb_out(), "False value (0-65535)\n\r");  CHECK(ccr_red() == 0U);
  usb_cmd("r\n");        CHECK_STR(usb_out(), "False value (0-65535)\n\r");
  usb_cmd("r010\n");     CHECK_STR(usb_out(), "Red = 10\n\r");      /* décimal, pas octal */
  usb_cmd("red\n");      CHECK_STR(usb_out(), "False command\n\r");
}

/* -------------------------------------------------------------------------- */
static void test_usb_typing(void)
{
  const char *typed;

  printf("- USB : frappe caractère par caractère (terminal)\n");
  for (typed = "r5\r"; *typed != '\0'; typed++)
  {
    fake_usb_host_send(typed, 1U);
    loop(1);
  }
  CHECK_STR(usb_out(), "Red = 5\n\r");
  /* Entrée = « \r\n » : la version 2024 gardait le « \n » en tête de la
   * commande suivante, qui devenait « False command ». */
  for (typed = "v7\r\nb9\r\n"; *typed != '\0'; typed++)
  {
    fake_usb_host_send(typed, 1U);
    loop(1);
  }
  CHECK_STR(usb_out(), "Green = 7\n\rBlue = 9\n\r");
  /* « start timer » tapé : 12 caractères, trop pour le tampon de 10
   * caractères de la version 2024 (débordement). */
  for (typed = "start timer\r"; *typed != '\0'; typed++)
  {
    fake_usb_host_send(typed, 1U);
    loop(1);
  }
  CHECK(timer_1s_is_running());
  usb_cmd("stop timer\n");
  /* Effacement */
  for (typed = "r12\b3\r"; *typed != '\0'; typed++)
  {
    fake_usb_host_send(typed, 1U);
    loop(1);
  }
  CHECK_STR(usb_out(), "Red = 13\n\r");
  /* Ligne trop longue */
  for (int i = 0; i < 80; i++)
  {
    fake_usb_host_send("x", 1U);
    loop(1);
  }
  fake_usb_host_send("\r", 1U);
  loop(2);
  CHECK_STR(usb_out(), "False command\n\r");
  /* Saisie commencée au clavier puis complétée par un paquet : une seule ligne */
  fake_usb_host_send("m", 1U);
  loop(1);
  fake_usb_host_send("12\r", 3U);
  loop(2);
  CHECK_STR(usb_out(), "PWM = 12\n\r");
}

/* -------------------------------------------------------------------------- */
static void test_usb_packets(void)
{
  int rearm;

  printf("- USB : paquets sans fin de ligne (Terminal_port_com.py, option « Aucun »)\n");
  usb_cmd("r65535");     CHECK_STR(usb_out(), "Red = 65535\n\r");
  usb_cmd("r5");         CHECK_STR(usb_out(), "Red = 5\n\r");       /* 2024 : 55535 ! */
  usb_cmd("start timer");
  CHECK(timer_1s_is_running());
  usb_cmd("stop timer");
  usb_cmd("r1\r\nv2\r\nb3");
  CHECK_STR(usb_out(), "Red = 1\n\rGreen = 2\n\rBlue = 3\n\r");

  printf("- USB : contrôle de flux, aucun paquet perdu ni écrasé\n");
  rearm = fake_usb_rearm_count;
  fake_usb_host_send_str("r11\n");
  fake_usb_host_send_str("v22\n");
  fake_usb_host_send_str("b33\n");
  fake_usb_host_send_str("m44\n");
  CHECK(fake_usb_host_pending() == 3U);              /* un seul paquet accepté à la fois */
  loop(10);
  CHECK(fake_usb_host_pending() == 0U);
  CHECK_STR(usb_out(), "Red = 11\n\rGreen = 22\n\rBlue = 33\n\rPWM = 44\n\r");
  CHECK(fake_usb_rearm_count == rearm + 4);

  printf("- USB : PC qui ne lit pas un moment, puis lit (rien de perdu, ordre gardé)\n");
  fake_usb_host_reads = false;
  usb_cmd("?\n");
  usb_cmd("a\n");
  usb_cmd("r3\n");
  CHECK_STR(usb_out(), "");
  fake_usb_host_reads = true;
  loop(10);
  {
    char expected[2048];
    snprintf(expected, sizeof(expected), "%sAcceleration : X=0.00 Y=-0.00 Z=1.02\n\rRed = 3\n\r",
             commands_help_text());
    CHECK_STR(usb_out(), expected);
  }

  printf("- USB : CDC_Transmit_FS avec un tampon local (copié aussitôt)\n");
  {
    char local[16] = "Bonjour\n\r";
    CHECK(CDC_Transmit_FS((uint8_t *)local, (uint16_t)strlen(local)) == USBD_OK);
    strcpy(local, "XXXXXXXXX");
    CHECK(CDC_Transmit_FS((uint8_t *)"OK\n\r", 4U) == USBD_OK);   /* pendant l'envoi */
    loop(4);
    CHECK_STR(usb_out(), "Bonjour\n\rOK\n\r");
  }

  printf("- USB : tampon plein si le PC ne lit plus : messages abandonnés entiers\n");
  fake_usb_host_reads = false;
  for (int i = 0; i < 100; i++)
  {
    (void)usb_console_print("0123456789012345678901234567890123456789\n\r");   /* 42 octets */
    loop(1);
  }
  fake_usb_host_reads = true;
  loop(20);
  {
    const char *out = usb_out();
    size_t len = strlen(out);
    CHECK(len > 0U && (len % 42U) == 0U);            /* que des lignes entières */
    CHECK(len <= 1024U + 256U);
  }

  printf("- USB : débranchement et rebranchement\n");
  fake_usb_disconnect();
  CHECK(!usb_console_is_ready());
  CHECK(!usb_console_print("perdu\n\r"));
  usb_cmd("r9\n");                                   /* le PC n'est plus là */
  fake_usb_connect();
  loop(2);
  CHECK(usb_console_is_ready());
  usb_cmd("r9\n");
  CHECK_STR(usb_out(), "Red = 9\n\r");
  fake_usb_suspend();
  CHECK(!usb_console_is_ready());
  fake_usb_resume();
  CHECK(usb_console_is_ready());
}

/* -------------------------------------------------------------------------- */
static void test_ble(void)
{
  const char *help = commands_help_text();

  printf("- Bluetooth : connexion du téléphone\n");
  (void)usb_out();
  fake_ble_clear_notifications();
  fake_ble_connect(PEER, 0x00);
  fake_ble_subscribe();
  loop(2);
  CHECK(ble_uart_is_connected());
  CHECK_STR(usb_out(), "Generated BLE Address: C1:23:45:67:89:AB\n\r"
                       "Connected to device: 11:22:33:44:55:66\n\r");
  CHECK_STR(ble_out(), "USB Connected\n\r");

  printf("- Bluetooth : commandes du téléphone, copie vers le terminal\n");
  ble_cmd("r200");       CHECK_STR(ble_out(), "Red = 200\n\r");  CHECK_STR(usb_out(), "Red = 200\n\r");
  CHECK(ccr_red() == 200U);
  ble_cmd("v65535");     CHECK_STR(ble_out(), "Green = 65535\n\r");
  ble_cmd("b0");         CHECK_STR(ble_out(), "Blue = 0\n\r");
  ble_cmd("m1234");      CHECK_STR(ble_out(), "PWM = 1234\n\r");  CHECK(ccr_motor() == 1234U);
  CHECK_STR(usb_out(), "Green = 65535\n\rBlue = 0\n\rPWM = 1234\n\r");
  ble_cmd("a");
  CHECK(fake_ble_notif_count == 2);                  /* 38 octets : 20 + 18 */
  CHECK(fake_ble_notif_len[0] == 20U && fake_ble_notif_len[1] == 18U);
  CHECK_STR(ble_out(), "Acceleration : X=0.00 Y=-0.00 Z=1.02\n\r");
  CHECK_STR(usb_out(), "Acceleration : X=0.00 Y=-0.00 Z=1.02\n\r");
  ble_cmd("t");          CHECK_STR(ble_out(), "T = 27\n\r");  (void)usb_out();
  ble_cmd("cls");        CHECK_STR(ble_out(), "False command\n\r");  CHECK_STR(usb_out(), "False command\n\r");
  ble_cmd("r70000");     CHECK_STR(ble_out(), "False value (0-65535)\n\r");  (void)usb_out();

  printf("- Bluetooth : aide en notifications de 20 octets, sans notification vide\n");
  ble_cmd("?");
  loop(40);
  {
    int full = (int)strlen(help) / 20;
    int rest = (int)strlen(help) % 20;
    CHECK(fake_ble_notif_count == full + (rest ? 1 : 0));
    for (int i = 0; i < fake_ble_notif_count; i++)
    {
      CHECK(fake_ble_notif_len[i] > 0U && fake_ble_notif_len[i] <= 20U);
    }
  }
  CHECK_STR(ble_out(), help);
  CHECK_STR(usb_out(), "");                          /* l'aide ne va qu'au demandeur */

  printf("- Bluetooth : commandes non terminées par un zéro (bug 2024) et plusieurs lignes\n");
  ble_cmd("r65535");     CHECK_STR(ble_out(), "Red = 65535\n\r");
  ble_cmd("r5");         CHECK_STR(ble_out(), "Red = 5\n\r");  CHECK(ccr_red() == 5U);
  ble_cmd("v");          CHECK_STR(ble_out(), "False value (0-65535)\n\r");
  ble_cmd("r1\nv2\r\n");  CHECK_STR(ble_out(), "Red = 1\n\rGreen = 2\n\r");
  (void)usb_out();

  printf("- Bluetooth : terminal -> copie vers le téléphone\n");
  usb_cmd("b77\n");      CHECK_STR(usb_out(), "Blue = 77\n\r");  CHECK_STR(ble_out(), "Blue = 77\n\r");
  usb_cmd("?\n");        (void)usb_out();  CHECK_STR(ble_out(), "");   /* aide : PC seulement */
  usb_cmd("cls\n");      (void)usb_out();  CHECK_STR(ble_out(), "");

  printf("- Bluetooth : mesures chaque seconde (deux notifications séparées)\n");
  ble_cmd("start timer");
  CHECK_STR(ble_out(), "");
  fake_timer7_fire();
  loop(3);
  CHECK(fake_ble_notif_count == 2);
  CHECK_STR(fake_ble_notif[0], "X0.00Y-0.00Z1.02\n\r");
  CHECK_STR(fake_ble_notif[1], "T = 27\n\r");
  fake_ble_clear_notifications();
  fake_timer7_fire();
  fake_timer7_fire();                                /* deux tics en retard : une mesure */
  loop(3);
  CHECK(fake_ble_notif_count == 2);
  fake_ble_clear_notifications();
  ble_cmd("stop timer");
  fake_timer7_fire();
  loop(3);
  CHECK(fake_ble_notif_count == 0);
  CHECK_STR(usb_out(), "");

  printf("- Bluetooth : tampons du module pleins, rien n'est perdu\n");
  fake_ble_busy_next = 5;
  ble_cmd("?");
  loop(10);
  fake_ble_tx_pool_available();
  loop(200);
  CHECK_STR(ble_out(), help);

  printf("- bouton : un message par appui, rebonds filtrés\n");
  {
    uint64_t t = fake_now_us;
    uint64_t changes[] = { t + 1000U, t + 2000U, t + 2500U, t + 3500U,      /* rebonds */
                           t + 200000U, t + 201000U, t + 201500U };         /* relâchement */
    fake_button_schedule(changes, sizeof(changes) / sizeof(changes[0]), true);
    fake_time_advance_ms(1U);
    fake_button_exti();
    loop(300);
    CHECK_STR(usb_out(), "Push button pressed\n\r");
    CHECK_STR(ble_out(), "PButton pressed\n\r");
    /* appui trop bref (10 ms) : ignoré */
    t = fake_now_us;
    uint64_t brief[] = { t + 1000U, t + 11000U };
    fake_button_schedule(brief, 2U, true);
    fake_button_exti();
    loop(100);
    CHECK_STR(usb_out(), "");
    CHECK_STR(ble_out(), "");
    fake_button_set(false);
  }

  printf("- Bluetooth : déconnexion -> message, sorties éteintes, publicité relancée\n");
  ble_cmd("r300");
  ble_cmd("start timer");
  (void)usb_out();
  (void)ble_out();
  {
    int adv = fake_ble_adv_count;
    fake_ble_write_str("m999");                      /* commande reçue juste avant */
    fake_ble_disconnect();
    loop(3);
    CHECK(!ble_uart_is_connected());
    CHECK_STR(usb_out(), "PWM = 999\n\rBLE disconnected :p\n\r");
    CHECK(ccr_red() == 0U && ccr_motor() == 0U);     /* éteintes après la commande */
    CHECK(!timer_1s_is_running());
    CHECK(fake_ble_adv_count == adv + 1 && fake_ble_advertising);
  }
  CHECK(!ble_uart_print("personne\n\r"));
}

/* -------------------------------------------------------------------------- */
static void test_ble_robustness(void)
{
  int adv;
  int inits;

  printf("- Bluetooth : connexion ratée -> publicité relancée\n");
  adv = fake_ble_adv_count;
  fake_ble_connect(PEER, 0x3E);
  loop(2);
  CHECK(!ble_uart_is_connected());
  CHECK(fake_ble_adv_count == adv + 1);
  CHECK_STR(usb_out(), "");

  printf("- Bluetooth : publicité refusée -> nouvel essai\n");
  fake_ble_connect(PEER, 0x00);
  loop(2);
  (void)usb_out();
  adv = fake_ble_adv_count;
  fake_ble_adv_fail_next = 1;
  fake_ble_disconnect();
  loop(2);
  CHECK(fake_ble_adv_count == adv);
  loop(250);
  CHECK(fake_ble_adv_count == adv + 1);
  (void)usb_out();

  printf("- Bluetooth : redémarrage inattendu du module -> remis en service\n");
  inits = fake_ble_init_count;
  fake_ble_hal_initialized(1);                       /* redémarrage normal : ignoré */
  loop(2);
  CHECK(fake_ble_init_count == inits);
  fake_ble_hal_initialized(5);                       /* chien de garde : tout refaire */
  loop(3);
  CHECK(fake_ble_init_count == inits + 1);
  CHECK(ble_uart_is_ready() && fake_ble_advertising);

  printf("- Bluetooth : broche IRQ restée à 1 -> lecture relancée\n");
  {
    int kicks = fake_ble_isr_kicks;
    fake_gpio_set_input(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
    loop(1);
    CHECK(fake_ble_isr_kicks == kicks + 1);
    CHECK(fake_exti_enabled);
    loop(1);
    CHECK(fake_ble_isr_kicks == kicks + 1);
  }
}

/* -------------------------------------------------------------------------- */
static void test_ota(void)
{
  int      enter;
  int      terms;
  int      turns;
  uint32_t t0;
  uint32_t elapsed;

  printf("- #ota par USB : réponse reçue, sorties éteintes, puis chargeur (une fois)\n");
  usb_cmd("r500\nm700\nstart timer\n");
  (void)usb_out();
  (void)ble_out();
  CHECK(!ota_switch_is_pending());
  enter = fake_ota_enter_count;
  terms = fake_ble_terminate_count;
  fake_usb_host_send_str("#ota\n#ota\n");            /* deux demandes : un seul passage */
  CHECK(ota_switch_is_pending() == false);           /* rien avant le tour de boucle */
  turns = loop_until_loader(50);
  CHECK(turns >= 2 && turns <= 5);
  CHECK(fake_ota_enter_count == enter + 1);
  CHECK_STR(usb_out(), "OTA loader...\n\rOTA loader...\n\r");   /* reçues avant le redémarrage */
  CHECK(ccr_red() == 0U && ccr_motor() == 0U);
  CHECK(!timer_1s_is_running());
  CHECK(fake_ble_terminate_count == terms);          /* aucun téléphone : rien à fermer */
  CHECK(!ota_switch_is_pending());
  loop(5);
  CHECK(fake_ota_enter_count == enter + 1);

  printf("- #ota par Bluetooth : réponse au téléphone et au PC, connexion fermée par la carte\n");
  fake_ble_connect(PEER, 0x00);
  fake_ble_subscribe();
  loop(2);
  ble_cmd("b900");
  (void)usb_out();
  (void)ble_out();
  enter = fake_ota_enter_count;
  terms = fake_ble_terminate_count;
  fake_ble_write_str("#ota");
  turns = loop_until_loader(50);
  CHECK(turns >= 3 && turns <= 6);
  CHECK(fake_ota_enter_count == enter + 1);
  CHECK_STR(ble_out(), "OTA loader...\n\r");
  CHECK_STR(usb_out(), "OTA loader...\n\rBLE disconnected :p\n\r");
  CHECK(fake_ble_terminate_count == terms + 1);
  CHECK(fake_ble_terminate_handle == 0x0801U);
  CHECK(fake_ble_terminate_reason == 0x13U);         /* « fermée par l'utilisateur » */
  CHECK(!ble_uart_is_connected());
  CHECK(ccr_blue() == 0U);

  printf("- #ota par USB, téléphone connecté : copie au téléphone, puis fermeture\n");
  fake_ble_connect(PEER, 0x00);
  fake_ble_subscribe();
  loop(2);
  (void)usb_out();
  (void)ble_out();
  terms = fake_ble_terminate_count;
  fake_usb_host_send_str("#ota\r\n");
  CHECK(loop_until_loader(50) > 0);
  CHECK_STR(ble_out(), "OTA loader...\n\r");
  CHECK_STR(usb_out(), "OTA loader...\n\rBLE disconnected :p\n\r");
  CHECK(fake_ble_terminate_count == terms + 1);

  printf("- #ota : le téléphone (ou obcp_ota.py) part aussitôt -> chargeur sans attendre\n");
  fake_ble_connect(PEER, 0x00);                      /* sans abonnement aux notifications */
  loop(2);
  (void)usb_out();
  terms = fake_ble_terminate_count;
  fake_ble_write_str("#ota");
  fake_ble_disconnect();
  turns = loop_until_loader(50);
  CHECK(turns >= 2 && turns <= 5);
  CHECK(fake_ble_terminate_count == terms);          /* déjà fermée */
  CHECK_STR(usb_out(), "OTA loader...\n\rBLE disconnected :p\n\r");

  printf("- #ota : PC qui ne lit plus le port COM -> chargeur au bout de 1 s au plus\n");
  fake_usb_host_reads = false;
  enter = fake_ota_enter_count;
  t0 = HAL_GetTick();
  fake_usb_host_send_str("#ota\n");
  turns = loop_until_loader(3000);
  elapsed = HAL_GetTick() - t0;
  CHECK(turns > 0 && fake_ota_enter_count == enter + 1);
  CHECK(elapsed >= OTA_REPLY_TIMEOUT_MS + OTA_DISCONNECT_TIMEOUT_MS);
  CHECK(elapsed <= OTA_REPLY_TIMEOUT_MS + OTA_DISCONNECT_TIMEOUT_MS + 20U);
  fake_usb_host_reads = true;
  loop(5);
  (void)usb_out();

  printf("- #ota : module qui ne confirme pas la déconnexion -> chargeur au bout de 500 ms\n");
  fake_ble_connect(PEER, 0x00);
  fake_ble_subscribe();
  loop(2);
  (void)usb_out();
  (void)ble_out();
  fake_ble_terminate_silent = true;
  terms = fake_ble_terminate_count;
  fake_ble_write_str("#ota");
  t0 = HAL_GetTick();
  turns = loop_until_loader(3000);
  elapsed = HAL_GetTick() - t0;
  CHECK(turns > 0);
  CHECK(fake_ble_terminate_count == terms + 1);
  CHECK(elapsed >= OTA_DISCONNECT_TIMEOUT_MS && elapsed <= OTA_DISCONNECT_TIMEOUT_MS + 20U);
  CHECK(ble_uart_is_connected());                    /* jamais confirmée */
  CHECK_STR(ble_out(), "OTA loader...\n\r");
  fake_ble_terminate_silent = false;
  fake_ble_disconnect();
  loop(3);
  (void)usb_out();
  CHECK(!ble_uart_is_connected() && fake_ble_advertising);
}

/* -------------------------------------------------------------------------- */
static void test_accelerometer(void)
{
  accel_g_t g;
  int t;
  int bursts;

  printf("- accéléromètre : LIS3DH, lecture des 3 axes en un seul échange\n");
  fake_acc_set_chip(FAKE_ACC_LIS3DH);
  CHECK(accel_init(&hspi3) == ACCEL_LIS3DH);
  CHECK_STR(accel_chip_name(), "LIS3DH");
  fake_acc_set_raw(16000, -16000, 0);
  bursts = fake_acc_xyz_burst_reads;
  CHECK(accel_read(&g));
  CHECK(fake_acc_xyz_burst_reads == bursts + 1);
  CHECK(g.x > 0.9999f && g.x < 1.0001f);            /* 16000 x 0,0625 mg = 1000 mg */
  CHECK(g.y < -0.9999f && g.y > -1.0001f);
  CHECK(g.z == 0.0f);
  fake_acc_set_temperature_delta(-5);
  CHECK(accel_read_temperature(&t) && t == 20);

  printf("- accéléromètre : étalonnage\n");
  fake_acc_set_raw(0, 0, 16768);                    /* 1048 mg */
  accel_set_calibration(ACCEL_AXIS_Z, -952.0f, 1048.0f);
  CHECK(accel_read(&g) && g.z > 0.9999f && g.z < 1.0001f);
  fake_acc_set_raw(0, 0, -15232);                   /* -952 mg */
  CHECK(accel_read(&g) && g.z < -0.9999f && g.z > -1.0001f);
  accel_set_calibration(ACCEL_AXIS_Z, 5.0f, 5.0f);  /* incohérent : ignoré */
  accel_set_calibration(ACCEL_AXIS_Z, -1000.0f, 1000.0f);

  printf("- accéléromètre : LIS3DSH (autre circuit, même boîtier)\n");
  fake_acc_set_chip(FAKE_ACC_LIS3DSH);
  CHECK(accel_init(&hspi3) == ACCEL_LIS3DSH);
  CHECK(fake_acc_reg(0x20) == 0x3FU && fake_acc_reg(0x24) == 0x00U && fake_acc_reg(0x25) == 0x10U);
  fake_acc_set_raw(0, 0, 16384);                    /* 16384 x 0,06 = 983 mg */
  CHECK(accel_read(&g) && g.z > 0.983f && g.z < 0.9832f);
  fake_acc_set_temperature_delta(3);
  CHECK(accel_read_temperature(&t) && t == 28);
  usb_cmd("a\n");
  CHECK_STR(usb_out(), "Acceleration : X=0.00 Y=0.00 Z=0.98\n\r");

  printf("- accéléromètre absent ou en panne\n");
  fake_acc_set_chip(FAKE_ACC_NONE);
  CHECK(accel_init(&hspi3) == ACCEL_NONE);
  CHECK_STR(accel_chip_name(), "none");
  CHECK(!accel_read(&g) && !accel_read_temperature(&t));
  usb_cmd("a\n");        CHECK_STR(usb_out(), "Accelerometer error\n\r");
  usb_cmd("t\n");        CHECK_STR(usb_out(), "Accelerometer error\n\r");
  fake_acc_set_chip(FAKE_ACC_LIS3DH);
  fake_spi_fail = true;
  CHECK(accel_init(&hspi3) == ACCEL_NONE);
  fake_spi_fail = false;
  CHECK(accel_init(&hspi3) == ACCEL_LIS3DH);
  fake_acc_set_raw(16, -32, 16384);
}

/* -------------------------------------------------------------------------- */
static void test_no_module(void)
{
  printf("- module Bluetooth absent : le reste fonctionne\n");
  fake_ble_disconnect();
  loop(2);
  (void)usb_out();
  fake_ble_present = false;
  CHECK(ble_uart_init() == BLE_UART_ERR_NO_MODULE);
  CHECK(!ble_uart_is_ready() && !ble_uart_is_connected());
  loop(3);
  usb_cmd("r42\n");      CHECK_STR(usb_out(), "Red = 42\n\r");
  fake_ble_present = true;
  CHECK(ble_uart_init() == BLE_UART_OK);

  printf("- adresse de secours quand le module n'en fournit pas\n");
  fake_ble_random_addr_valid = false;
  CHECK(ble_uart_init() == BLE_UART_OK);
  CHECK((fake_ble_public_addr[5] & 0xC0U) == 0xC0U);
  CHECK(memcmp(fake_ble_public_addr, fake_ble_random_addr, 6) != 0);
  {
    uint8_t first[6];
    memcpy(first, fake_ble_public_addr, 6);
    CHECK(ble_uart_init() == BLE_UART_OK);
    CHECK(memcmp(first, fake_ble_public_addr, 6) == 0);    /* stable */
  }
  fake_ble_random_addr_valid = true;
}

int main(void)
{
  printf("test_app\n");
  test_boot();
  test_usb_commands();
  test_usb_typing();
  test_usb_packets();
  test_ble();
  test_ble_robustness();
  test_ota();
  test_accelerometer();
  test_no_module();
  return check_summary("test_app");
}
