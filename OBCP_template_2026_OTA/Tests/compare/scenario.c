/**
 ******************************************************************************
 * @file    Tests/compare/scenario.c
 * @brief   Même scénario joué par les versions 2024 et 2026 du programme
 *          (compilé deux fois, avec adapter_2024.c ou adapter_2026.c). Écrit
 *          le « journal » de ce que la carte envoie au PC (USB) et au
 *          téléphone (BLE) à chaque étape ; compare.py compare les deux
 *          journaux.
 *
 *  Partie « commun » : usages normaux (outils existants, terminal) ; les deux
 *  versions doivent répondre exactement pareil, aux valeurs mesurées près
 *  (la version 2024 avait une sensibilité fausse de 4 %).
 *  Partie « differences » : cas qui révélaient des défauts de la version
 *  2024 ; les réponses diffèrent, et compare.py les affiche côte à côte.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "adapter.h"
#include "fake_board.h"
#include "fake_bluenrg.h"

static const uint8_t PEER[6] = { 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 };

static void print_escaped(const char *s, size_t n)
{
  for (size_t i = 0U; i < n; i++)
  {
    unsigned char c = (unsigned char)s[i];
    if (c == '\n')       fputs("\\n", stdout);
    else if (c == '\r')  fputs("\\r", stdout);
    else if (c == 27)    fputs("\\e", stdout);
    else if (c == '\\')  fputs("\\\\", stdout);
    else if (c < 32)     printf("\\x%02X", c);
    else                 putchar((int)c);
  }
}

static void run(int n)
{
  for (int i = 0; i < n; i++)
  {
    adapter_loop();
  }
}

/* Écrit ce qui a été envoyé depuis l'étape précédente. */
static void dump(const char *name)
{
  static char usb[65536];
  size_t n = adapter_usb_take(usb, sizeof(usb));

  printf("== %s\nUSB: ", name);
  print_escaped(usb, n);
  printf("\nBLE:");
  for (int i = 0; i < fake_ble_notif_count; i++)
  {
    printf(" [");
    print_escaped(fake_ble_notif[i], fake_ble_notif_len[i]);
    printf("]");
  }
  fake_ble_clear_notifications();
  printf("\nPWM: r=%lu g=%lu b=%lu m=%lu timer=%d\n",
         (unsigned long)htim3.Instance->CCR1, (unsigned long)htim3.Instance->CCR3,
         (unsigned long)htim3.Instance->CCR4, (unsigned long)htim1.Instance->CCR3,
         htim7.running);
}

static void usb(const char *name, const char *data)
{
  adapter_usb_send(data, strlen(data));
  run(5);
  dump(name);
}

static void usb_typed(const char *name, const char *data)
{
  for (; *data != '\0'; data++)
  {
    adapter_usb_send(data, 1U);
    run(2);
  }
  run(3);
  dump(name);
}

static void ble(const char *name, const char *data)
{
  fake_ble_write_str(data);
  run(5);
  dump(name);
}

static void timer_tick(const char *name)
{
  fake_timer7_fire();
  run(5);
  dump(name);
}

static void button(const char *name)
{
  uint64_t t = fake_now_us;
  uint64_t changes[] = { t + 1000U, t + 120000U };

  fake_button_schedule(changes, 2U, true);
  fake_time_advance_ms(2U);
  fake_button_exti();
  run(300);
  fake_button_set(false);
  dump(name);
}

int main(void)
{
  fake_acc_set_chip(FAKE_ACC_LIS3DH);
  fake_acc_set_raw(160, -320, 16000);     /* carte à plat : Z = 1 g */
  fake_acc_set_temperature_delta(2);
  fake_ble_clean_buffers = true;        /* cas favorable à 2024 (voir differences) */

  printf("# version %s\n", adapter_name);
  printf("# section commun\n");
  adapter_init();
  run(3);
  dump("demarrage");
  adapter_usb_connect();
  run(3);
  dump("USB branche");

  /* Commandes envoyées en paquets (scripts Python, VI LabVIEW) */
  usb("USB r500", "r500\n");
  usb("USB v1000", "v1000\n");
  usb("USB b65535", "b65535\n");
  usb("USB m32768", "m32768\n");
  usb("USB R10", "R10\n");
  usb("USB V20 CRLF", "V20\r\n");
  usb("USB B30 CR", "B30\r");
  usb("USB M40", "M40\n");
  usb("USB a", "a\n");
  usb("USB A", "A\n");
  usb("USB t", "t\n");
  usb("USB T", "T\n");
  usb("USB aide", "?\n");
  usb("USB cls", "cls\n");
  usb("USB start timer", "start timer\n");
  usb("USB stop timer", "stop timer\n");
  usb("USB start timer 2", "start timer\n");
  usb("USB #rst", "#rst\n");
  usb("USB xyz", "xyz\n");
  usb("USB hello world", "hello world\n");
  usb("USB r123 sans fin de ligne", "r123");
  usb("USB r 45 avec espace", "r 45\n");
  usb("USB r500 CR (OBCP_LED_RVB.py)", "r500\r");
  /* Frappe au clavier */
  usb_typed("USB tape r5", "r5\r");
  usb_typed("USB tape a", "a\r");
  usb_typed("USB tape #rst", "#rst\r");

  /* Téléphone */
  fake_ble_connect(PEER, 0x00);
  fake_ble_subscribe();
  run(5);
  dump("BLE connexion");
  ble("BLE r200", "r200");
  ble("BLE v300", "v300");
  ble("BLE b400", "b400");
  ble("BLE m500", "m500");
  ble("BLE R65535", "R65535");
  ble("BLE a", "a");
  ble("BLE A", "A");
  ble("BLE t", "t");
  ble("BLE T", "T");
  fake_ble_write_str("?");
  run(80);
  dump("BLE aide");
  ble("BLE cls", "cls");
  ble("BLE xyz", "xyz");
  ble("BLE start timer", "start timer");
  timer_tick("BLE seconde 1");
  timer_tick("BLE seconde 2");
  ble("BLE stop timer", "stop timer");
  timer_tick("BLE seconde apres arret");
  ble("BLE #rst", "#rst");
  /* Terminal pendant que le téléphone est connecté */
  usb("USB r9 (copie BLE)", "r9\n");
  usb("USB a (copie BLE)", "a\n");
  fake_ble_clear_notifications();
  adapter_usb_send("?\n", 2U);
  run(10);
  dump("USB aide (pas de copie)");
  usb("USB start timer (BLE connecte)", "start timer\n");
  timer_tick("USB seconde 1 (mesures par BLE)");
  usb("USB stop timer (BLE connecte)", "stop timer\n");
  button("bouton");
  usb("USB r1000 avant deconnexion", "r1000\n");
  usb("USB m2000 avant deconnexion", "m2000\n");
  usb("USB start timer avant deconnexion", "start timer\n");
  fake_ble_disconnect();
  run(5);
  dump("BLE deconnexion");
  fake_ble_connect(PEER, 0x00);
  fake_ble_subscribe();
  run(5);
  dump("BLE reconnexion");
  ble("BLE r7 apres reconnexion", "r7");
  button("bouton 2");

  printf("# section differences\n");
  usb("USB r65535 sans fin de ligne (preparation)", "r65535");
  usb("USB r5 sans fin de ligne (apres r65535)", "r5");
  usb_typed("USB tape v7 puis Entree CRLF", "v7\r\n");
  usb_typed("USB tape b9 puis Entree CRLF", "b9\r\n");
  usb("USB r010", "r010\n");
  usb("USB r-1", "r-1\n");
  usb("USB r70000", "r70000\n");
  usb("USB r seul", "r\n");
  usb("USB ligne vide", "\r\n");
  usb("USB blue", "blue\n");
  usb("USB r50x", "r50x\n");
  usb("USB a mesure (sensibilite)", "a\n");
  usb("USB #rst", "#rst\n");
  /* La bibliothèque BlueNRG range chaque évènement reçu dans l'un de ses
   * 5 tampons, utilisés à tour de rôle et jamais effacés. La 6e écriture
   * retombe donc dans le tampon de la 1re, qui contient encore « r65535 ». */
  fake_ble_clean_buffers = false;
  ble("BLE r65535 (tampon 1, preparation)", "r65535");
  ble("BLE v1 (tampon 2)", "v1");
  ble("BLE v2 (tampon 3)", "v2");
  ble("BLE v3 (tampon 4)", "v3");
  ble("BLE v4 (tampon 5)", "v4");
  ble("BLE r5 (tampon 1 reutilise)", "r5");
  ble("BLE start timer", "start timer");
  ble("BLE #rst", "#rst");
  return 0;
}
