/**
 ******************************************************************************
 * @file    Tests/test_app.c
 * @brief   Simulation sur PC de tout le programme (app.c et ses modules,
 *          usbd_cdc_if.c généré par CubeMX) avec une carte, un PC et un
 *          module Bluetooth simulés : démarrage, commandes, réception et
 *          relais des trames, réponses, LED, réglages à distance, EEPROM,
 *          repli de la radio en mode alterné, redémarrage du module.
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
#include "bluenrg_def.h"            /* codes de refus du module (0x0C, 0x12) */
#include "commands.h"
#include "mesh.h"
#include "mesh_radio.h"
#include "node.h"
#include "outputs.h"
#include "report.h"
#include "usb_console.h"

/* -------------------------------------------------------------------------- */
/* Outils                                                                     */
/* -------------------------------------------------------------------------- */
static char    s_usb[70000];
static uint8_t s_me;              /* numéro de la carte simulée */

/* n tours de boucle (1 ms chacun : __WFI avance le temps d'une ms) ; le PC
 * lit ce qui arrive et renvoie ses paquets. */
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

static void usb_cmd(const char *s)
{
  (void)usb_out();                       /* oublie ce qui précède */
  fake_usb_host_send_str(s);
  loop(5);
}

static uint32_t ccr_red(void)   { return htim3.Instance->CCR1; }
static uint32_t ccr_green(void) { return htim3.Instance->CCR3; }
static uint32_t ccr_blue(void)  { return htim3.Instance->CCR4; }

static mesh_frame_t frame(uint8_t src, uint8_t dst, uint8_t seq, uint8_t ttl, uint8_t hops, uint8_t last,
                          uint8_t type, const void *data, uint8_t len)
{
  mesh_frame_t f;
  memset(&f, 0, sizeof(f));
  f.net = 0U; f.src = src; f.dst = dst; f.seq = seq; f.ttl = ttl; f.hops = hops; f.last = last;
  f.type = type; f.len = len;
  if (len > 0U)
  {
    memcpy(f.payload, data, len);
  }
  return f;
}

/* Dernière trame diffusée par la carte d'un type et d'une origine donnés
 * (origine 0 : n'importe laquelle), parmi celles diffusées depuis l'indice
 * from ; NULL si aucune. */
/* Trame TEXTE envoyée par cette carte depuis first, et si l'écoute tournait
 * pendant son émission (défini plus bas). */
static bool sent_text_during_scan(int first, bool *during_scan);

static const mesh_frame_t *find_sent(uint8_t type, uint8_t src, int from)
{
  const mesh_frame_t *found = NULL;
  for (int i = from; i < fake_ble_sent_count; i++)
  {
    if ((fake_ble_sent[i].type == type) && ((src == 0U) || (fake_ble_sent[i].src == src)))
    {
      found = &fake_ble_sent[i];
    }
  }
  return found;
}

/* -------------------------------------------------------------------------- */
static void test_boot(void)
{
  printf("- démarrage : module, rôles, adresse, puissance, écoute, EEPROM vide\n");
  fake_eeprom_reset();
  fake_acc_set_chip(FAKE_ACC_LIS3DH);
  fake_acc_set_raw(16, -32, 16384);
  fake_acc_set_temperature_delta(2);
  app_init();

  CHECK(fake_ble_init_count == 1);
  CHECK(node_radio_status() == RADIO_OK);
  CHECK(fake_ble_stack_mode == 4U);                  /* pile 7.2c : mode 4 */
  CHECK(fake_ble_late_config_writes == 0);           /* configuration avant toute autre commande */
  CHECK(mesh_radio_stack() == RADIO_STACK_MODE4);
  CHECK(mesh_radio_mode() == RADIO_MODE_ALTERNATE && !mesh_radio_mode_forced());   /* mode par défaut */
  CHECK(mesh_radio_fw_version() == 0x0723U && mesh_radio_hw_version() == 0x31U);
  CHECK(fake_ble_gap_roles == 0x0AU);                /* diffuseur + observateur */
  CHECK(memcmp(fake_ble_public_addr, fake_ble_random_addr, 6) == 0);
  CHECK((fake_ble_tx_high == 1U) && (fake_ble_tx_pa == 4U));   /* -2 dBm */
  CHECK(fake_ble_rand_calls == 1);
  CHECK(node_load_status() == STORE_EMPTY);
  CHECK(fake_eeprom_irq_violations == 0);
  CHECK(fake_exti_enabled);
  CHECK(htim7.running);                              /* minuterie d'une seconde */
  CHECK(!fake_ble_observing);
  loop(1);
  CHECK(fake_ble_observing && (fake_ble_observe_starts == 1));
  CHECK((fake_ble_scan_interval == RADIO_SCAN_INTERVAL) && (fake_ble_scan_window == RADIO_SCAN_WINDOW));
  CHECK((fake_ble_scan_type == 0U) && (fake_ble_scan_filter_dup == 0U));   /* passif, sans filtre */
  s_me = mesh_id(node_mesh());
  CHECK(mesh_addr_is_node(s_me));
  CHECK(!mesh_id_is_fixed(node_mesh()));
  printf("  numéro automatique de la carte simulée : %u\n", (unsigned)s_me);
}

static void test_first_hello(void)
{
  const mesh_frame_t *h;

  printf("- premier HELLO peu après le démarrage, diffusé en publicité non connectable\n");
  loop(2600);
  h = find_sent(MESH_T_HELLO, s_me, 0);
  CHECK(h != NULL);
  if (h != NULL)
  {
    CHECK(!fake_ble_sent_during_scan[h - fake_ble_sent]);  /* mode alterné : écoute arrêtée */
    CHECK(h->dst == MESH_ADDR_ALL);
    CHECK(h->ttl == MESH_TTL_DEFAULT);
    CHECK(h->hops == 0U && h->last == s_me);
    CHECK(mesh_get_u32(h->payload) == mesh_tag(node_mesh()));
  }
  CHECK(fake_ble_adv_interval == RADIO_ADV_INTERVAL);
  CHECK(fake_ble_adv_type == 0x03U);                 /* ADV_NONCONN_IND */
  CHECK(!fake_ble_sent_during_scan[0]);              /* l'écoute s'arrête pendant l'émission */
  loop((int)RADIO_TX_TIME_MS + 5);
  CHECK(!fake_ble_broadcasting && (fake_ble_broadcast_stops >= 1));
  loop(5);
  CHECK(fake_ble_observing && (fake_ble_scan_window == RADIO_SCAN_WINDOW));   /* écoute continue reprise */
  CHECK(fake_ble_time_base_refusals == 0);
}

static void test_usb_console(void)
{
  const char *out;

  printf("- câble USB : présentation, info, aide, écho des touches, erreurs\n");
  fake_usb_connect();
  loop(3);
  out = usb_out();
  CHECK(strstr(out, APP_NAME " " APP_VERSION " (version USB)") != NULL);
  CHECK(strstr(out, "Tapez « aide »") != NULL);

  usb_cmd("info\r");
  out = usb_out();
  CHECK(strstr(out, "réseau     0") != NULL);
  CHECK(strstr(out, "(automatique)") != NULL);
  CHECK(strstr(out, "écoute arrêtée pendant l'émission (automatique)") != NULL);
  CHECK(strstr(out, "pile 7.2c, mode 4 accepté (« radio simultane » possible)") != NULL);
  CHECK(strstr(out, "ttl 4, relais on, suppr 3, rssimin off") != NULL);

  usb_cmd("aide\r");
  loop(30);                                          /* l'aide fait 2 ko : 256 octets par transfert USB */
  CHECK(strstr(usb_out(), "msg dest texte") != NULL);
  CHECK(strstr(s_usb, "#ota") != NULL);
  {
    const char *typed = "stats\r";
    for (const char *c = typed; *c != '\0'; c++)
    {
      fake_usb_host_send(c, 1U);                     /* une touche par paquet */
      loop(2);
    }
  }
  loop(30);
  out = usb_out();
  CHECK(strncmp(out, "stats\r\n", 7) == 0);           /* écho, puis la réponse */
  CHECK(strstr(out, "Réseau maillé") != NULL);

  usb_cmd("blabla\r");
  CHECK(strstr(usb_out(), "Commande inconnue") != NULL);
  usb_cmd("msg 17\r");
  CHECK(strstr(usb_out(), "Usage : msg dest texte") != NULL);
  usb_cmd("ping 300\r");
  CHECK(strstr(usb_out(), "Destination invalide") != NULL);
}

static void test_receive_and_relay(void)
{
  mesh_frame_t        f = frame(5U, MESH_ADDR_ALL, 10U, 3U, 0U, 5U, MESH_T_TEXT, "Bonjour", 7U);
  const mesh_frame_t *r;
  int                 first = fake_ble_sent_count;
  int                 stops = fake_ble_broadcast_stops;

  printf("- trame reçue : affichée, puis relayée (TTL - 1, relais + 1, émetteur = nous)\n");
  (void)usb_out();
  fake_ble_mesh_frame(&f, -60);
  loop(2);
  CHECK(strstr(usb_out(), "Message de 5 à tous (direct, -60 dBm) : Bonjour") != NULL);
  fake_ble_mesh_frame(&f, -61);                      /* répétition : ignorée */
  loop((int)MESH_RELAY_DELAY_MAX_MS + 20);
  r = find_sent(MESH_T_TEXT, 5U, first);
  CHECK(r != NULL);
  if (r != NULL)
  {
    CHECK(r->seq == 10U && r->ttl == 2U && r->hops == 1U && r->last == s_me);
    CHECK(memcmp(r->payload, "Bonjour", 7) == 0);
  }
  loop((int)RADIO_TX_TIME_MS + 5);
  CHECK(fake_ble_broadcast_stops > stops);
  CHECK(mesh_stats(node_mesh())->rx_repeat >= 1U);
  CHECK(mesh_stats(node_mesh())->tx_relay >= 1U);

  printf("- TTL 0 : remise mais pas relayée ; trame pour un autre nœud : relayée sans affichage\n");
  first = fake_ble_sent_count;
  f = frame(5U, MESH_ADDR_ALL, 11U, 0U, 2U, 9U, MESH_T_TEXT, "fin", 3U);
  fake_ble_mesh_frame(&f, -70);
  f = frame(5U, 99U, 12U, 2U, 0U, 5U, MESH_T_TEXT, "pour99", 6U);
  fake_ble_mesh_frame(&f, -70);
  loop(800);
  CHECK(strstr(usb_out(), "Message de 5 à tous (2 relais, -70 dBm) : fin") != NULL);
  CHECK(find_sent(MESH_T_TEXT, 5U, first) != NULL);
  CHECK(find_sent(MESH_T_TEXT, 5U, first)->seq == 12U);
  CHECK(mesh_stats(node_mesh())->relay_ttl_end >= 1U);
}

static void test_suppression(void)
{
  mesh_frame_t f;
  int          first = fake_ble_sent_count;
  uint32_t     before = mesh_stats(node_mesh())->relay_suppressed;

  printf("- suppression : un relais entendu pendant l'attente annule le nôtre (suppr 1)\n");
  usb_cmd("suppr 1\r");
  CHECK(strstr(usb_out(), "suppr = 1") != NULL);
  f = frame(6U, MESH_ADDR_ALL, 20U, 3U, 0U, 6U, MESH_T_WAVE, NULL, 0U);
  fake_ble_mesh_frame(&f, -55);
  app_loop();                                        /* relais programmé */
  f.last = 7U; f.hops = 1U; f.ttl = 2U;              /* le même, relayé par le nœud 7 */
  fake_ble_mesh_frame(&f, -58);
  loop(700);
  CHECK(find_sent(MESH_T_WAVE, 6U, first) == NULL);
  CHECK(mesh_stats(node_mesh())->relay_suppressed == before + 1U);
  usb_cmd("suppr 3\r");
}

static void test_groups_and_led(void)
{
  mesh_frame_t  f;
  const uint8_t red[4]   = { 255U, 0U, 0U, 0U };
  const uint8_t green[4] = { 0U, 255U, 0U, 0U };

  printf("- groupes et LED : couleur pour le groupe 3 appliquée, pour le groupe 4 ignorée\n");
  usb_cmd("groupe +3\r");
  CHECK(strstr(usb_out(), "Groupe 3 rejoint") != NULL);
  f = frame(5U, MESH_GROUP_ADDR(3U), 30U, 2U, 0U, 5U, MESH_T_LED, red, 4U);
  fake_ble_mesh_frame(&f, -50);
  loop(3);
  CHECK(strstr(usb_out(), "Couleur de 5 au groupe 3") != NULL);
  CHECK(ccr_red() == 65025U && ccr_green() == 0U && ccr_blue() == 0U);
  f = frame(5U, MESH_GROUP_ADDR(4U), 31U, 2U, 0U, 5U, MESH_T_LED, green, 4U);
  fake_ble_mesh_frame(&f, -50);
  loop(3);
  CHECK(ccr_red() == 65025U && ccr_green() == 0U);   /* pas notre groupe */

  printf("- vague reçue après 2 relais : LED jaune 1,5 s, puis retour à la couleur\n");
  f = frame(8U, MESH_ADDR_ALL, 32U, 2U, 2U, 9U, MESH_T_WAVE, NULL, 0U);
  fake_ble_mesh_frame(&f, -65);
  loop(3);
  CHECK(strstr(usb_out(), "Vague lancée par 8 : reçue après 2 relais, LED jaune") != NULL);
  CHECK(ccr_red() == 65025U && ccr_green() == 40000U && ccr_blue() == 0U);
  loop((int)LED_WAVE_MS + 10);
  CHECK(ccr_red() == 65025U && ccr_green() == 0U);

  printf("- identification : LED blanche clignotante, puis retour\n");
  {
    const uint8_t two = 2U;
    bool          seen_white = false;
    f = frame(5U, s_me, 33U, 2U, 0U, 5U, MESH_T_IDENT, &two, 1U);
    fake_ble_mesh_frame(&f, -50);
    for (int i = 0; i < 400; i++)
    {
      loop(1);
      seen_white = seen_white || (ccr_red() == 65025U && ccr_green() == 65025U && ccr_blue() == 65025U);
    }
    CHECK(seen_white);
    CHECK(strstr(usb_out(), "Identification demandée de 5 (direct, -50 dBm) : LED blanche clignotante pendant 2 s")
          != NULL);
    loop(2000);
    CHECK(ccr_red() == 65025U && ccr_green() == 0U && ccr_blue() == 0U);
  }
  f = frame(5U, s_me, 34U, 2U, 0U, 5U, MESH_T_LED, (const uint8_t[4]){ 0U, 0U, 0U, 0U }, 4U);
  fake_ble_mesh_frame(&f, -50);
  loop(3);
  CHECK(strstr(usb_out(), "LED éteinte") != NULL);
  usb_cmd("groupe -3\r");

  printf("- commande led : couleur envoyée, durée rappelée\n");
  {
    int                 first = fake_ble_sent_count;
    const mesh_frame_t *p;
    usb_cmd("led g2 0 0 255 10\r");
    CHECK(strstr(usb_out(), "Couleur 0 0 255 envoyée à g2 pour 10 s\r\n") != NULL);
    loop(400);
    p = find_sent(MESH_T_LED, s_me, first);
    CHECK(p != NULL && p->dst == MESH_GROUP_ADDR(2U) && p->len == 4U && p->payload[2] == 255U && p->payload[3] == 10U);
    usb_cmd("led 9 rouge\r");
    CHECK(strstr(usb_out(), "Couleur 255 0 0 envoyée à 9\r\n") != NULL);
    loop(400);
  }
}

static void test_ping_and_trace(void)
{
  const mesh_frame_t *p;
  mesh_frame_t        f;
  int                 first = fake_ble_sent_count;
  uint8_t             id;

  printf("- ping : envoi, réponse, temps d'aller-retour ; délai écoulé sans réponse\n");
  usb_cmd("ping 5\r");
  CHECK(strstr(usb_out(), "envoyé à 5 (ttl 4)") != NULL);
  loop(400);
  p = find_sent(MESH_T_PING, s_me, first);
  CHECK(p != NULL && p->dst == 5U && p->len == 1U);
  id = (p != NULL) ? p->payload[0] : 0U;
  {
    const uint8_t data[3] = { id, 2U, (uint8_t)(int8_t)-70 };
    f = frame(5U, s_me, 40U, 3U, 1U, 9U, MESH_T_PONG, data, 3U);
  }
  fake_ble_mesh_frame(&f, -64);
  loop(3);
  CHECK(strstr(usb_out(), "Réponse de 5 au ping") != NULL);
  CHECK(strstr(s_usb, "aller (2 relais, -70 dBm), retour (1 relais, -64 dBm)") != NULL);

  usb_cmd("ping 77\r");
  loop((int)NODE_REPLY_TIMEOUT_MS + 50);
  CHECK(strstr(usb_out(), "Pas de réponse au ping") != NULL);

  printf("- ping reçu : PONG après la fin de l'émission du demandeur (pour nous), ou plus tard (pour tous)\n");
  first = fake_ble_sent_count;
  f = frame(5U, s_me, 41U, 3U, 1U, 4U, MESH_T_PING, (const uint8_t[1]){ 9U }, 1U);
  fake_ble_mesh_frame(&f, -66);
  loop(3);
  CHECK(strstr(usb_out(), "-> réponse dans 370 ms") != NULL);   /* mode alterné : NODE_REPLY_MIN_ALT_MS */
  loop((int)NODE_REPLY_MIN_ALT_MS + 100);
  p = find_sent(MESH_T_PONG, s_me, first);
  CHECK(p != NULL && p->dst == 5U && p->payload[0] == 9U && p->payload[1] == 1U && (int8_t)p->payload[2] == -66);
  first = fake_ble_sent_count;
  f = frame(5U, MESH_ADDR_ALL, 42U, 3U, 0U, 5U, MESH_T_PING, (const uint8_t[1]){ 10U }, 1U);
  fake_ble_mesh_frame(&f, -50);
  loop(3);
  CHECK(strstr(usb_out(), "-> réponse dans") != NULL);
  loop((int)NODE_REPLY_SPREAD_MS + 800);
  CHECK(find_sent(MESH_T_PONG, s_me, first) != NULL);

  printf("- trace : route aller et retour ; trace reçue : réponse avec la route\n");
  first = fake_ble_sent_count;
  usb_cmd("trace 9\r");
  loop(400);
  p = find_sent(MESH_T_TRACE, s_me, first);
  CHECK(p != NULL && p->dst == 9U && p->len == 1U);
  id = (p != NULL) ? p->payload[0] : 0U;
  {
    const uint8_t data[6] = { id, 2U, 3U, 4U, 8U, 0U };
    f = frame(9U, s_me, 43U, 3U, 1U, 8U, MESH_T_TRACE_BACK, data, 5U);
  }
  fake_ble_mesh_frame(&f, -60);
  loop(3);
  {
    char aller[64];
    char retour[64];
    snprintf(aller, sizeof(aller), "aller  : %u > 3 > 4 > 9 (2 relais)", (unsigned)s_me);
    snprintf(retour, sizeof(retour), "retour : 9 > 8 > %u (1 relais)", (unsigned)s_me);
    const char *out = usb_out();
    CHECK(strstr(out, "Route vers 9") != NULL);
    CHECK(strstr(out, aller) != NULL);
    CHECK(strstr(out, retour) != NULL);
  }
  printf("- trace vers un voisin direct : « (direct) » à l'aller et au retour\n");
  first = fake_ble_sent_count;
  usb_cmd("trace 5\r");
  loop(400);
  p = find_sent(MESH_T_TRACE, s_me, first);
  id = (p != NULL) ? p->payload[0] : 0U;
  {
    const uint8_t data[2] = { id, 0U };
    f = frame(5U, s_me, 45U, 4U, 0U, 5U, MESH_T_TRACE_BACK, data, 2U);
  }
  fake_ble_mesh_frame(&f, -52);
  loop(3);
  {
    char aller[64];
    char retour[64];
    snprintf(aller, sizeof(aller), "aller  : %u > 5 (direct)", (unsigned)s_me);
    snprintf(retour, sizeof(retour), "retour : 5 > %u (direct)", (unsigned)s_me);
    const char *out = usb_out();
    CHECK(strstr(out, aller) != NULL);
    CHECK(strstr(out, retour) != NULL);
  }
  first = fake_ble_sent_count;
  f = frame(5U, s_me, 44U, 2U, 2U, 4U, MESH_T_TRACE, (const uint8_t[3]){ 7U, 3U, 4U }, 3U);
  fake_ble_mesh_frame(&f, -60);
  loop(400);
  p = find_sent(MESH_T_TRACE_BACK, s_me, first);
  CHECK(p != NULL && p->dst == 5U && p->len == 4U);
  CHECK(p != NULL && p->payload[0] == 7U && p->payload[1] == 2U && p->payload[2] == 3U && p->payload[3] == 4U);
  usb_cmd("trace g2\r");
  CHECK(strstr(usb_out(), "trace vise un seul nœud") != NULL);
}

static void test_measures(void)
{
  const mesh_frame_t *p;
  mesh_frame_t        f;
  int                 first = fake_ble_sent_count;

  printf("- mesures : locales, demandées par un autre nœud, envoi automatique\n");
  usb_cmd("mesure\r");
  CHECK(strstr(usb_out(), "Mesures de cette carte : 27 °C, X = 0.00 g, Y = 0.00 g, Z = 1.02 g") != NULL);
  f = frame(5U, s_me, 50U, 3U, 0U, 5U, MESH_T_MEASURE_REQ, NULL, 0U);
  fake_ble_mesh_frame(&f, -60);
  loop(400);
  p = find_sent(MESH_T_MEASURE, s_me, first);
  CHECK(p != NULL && p->dst == 5U && p->len == MESH_MEASURE_LEN);
  if (p != NULL)
  {
    int16_t az = (int16_t)mesh_get_u16(&p->payload[6]);
    CHECK(p->payload[0] == (MESH_MEASURE_ACCEL_OK | MESH_MEASURE_TEMP_OK));
    CHECK((int8_t)p->payload[1] == 27);
    CHECK(az > 950 && az < 1050);
  }
  {
    const uint8_t m[8] = { 3U, 24U, 10U, 0U, 0xF6U, 0xFFU, 0xE8U, 0x03U };   /* 24 °C, 10, -10, 1000 mg */
    f = frame(7U, s_me, 51U, 3U, 1U, 4U, MESH_T_MEASURE, m, 8U);
  }
  fake_ble_mesh_frame(&f, -60);
  loop(3);
  CHECK(strstr(usb_out(), "Mesures de 7 (1 relais, -60 dBm) : 24 °C, X = 0.01 g, Y = -0.01 g, Z = 1.00 g") != NULL);
  {
    /* arrondi au centième, pas de « -0.00 » : -4, 1995, -1006 mg ; température absente */
    const uint8_t m[8] = { 1U, 0U, 0xFCU, 0xFFU, 0xCBU, 0x07U, 0x12U, 0xFCU };
    f = frame(7U, s_me, 52U, 3U, 1U, 4U, MESH_T_MEASURE, m, 8U);
  }
  fake_ble_mesh_frame(&f, -60);
  loop(3);
  CHECK(strstr(usb_out(), "Mesures de 7 (1 relais, -60 dBm) : X = 0.00 g, Y = 2.00 g, Z = -1.01 g") != NULL);

  first = fake_ble_sent_count;
  usb_cmd("auto 2\r");
  CHECK(strstr(usb_out(), "auto = 2 s") != NULL);
  for (int i = 0; i < 2; i++)
  {
    fake_timer7_fire();
    loop(5);
  }
  loop(400);
  p = find_sent(MESH_T_MEASURE, s_me, first);
  CHECK(p != NULL && p->dst == MESH_ADDR_ALL);
  usb_cmd("auto 0\r");
}

static void test_settings(void)
{
  const mesh_frame_t *p;
  int                 first;

  printf("- réglages locaux : valeurs, limites, puissance au plus proche\n");
  usb_cmd("ttl 7\r");
  CHECK(strstr(usb_out(), "ttl = 7") != NULL);
  usb_cmd("ttl\r");
  CHECK(strstr(usb_out(), "ttl = 7   (valeurs : 0 à 15)") != NULL);
  usb_cmd("ttl 99\r");
  CHECK(strstr(usb_out(), "Valeur refusée pour ttl (valeurs : 0 à 15)") != NULL);
  CHECK(mesh_settings(node_mesh())->ttl == 7U);
  usb_cmd("rssimin -70\r");
  CHECK(strstr(usb_out(), "rssimin = -70 dBm") != NULL);
  usb_cmd("rssimin off\r");
  CHECK(mesh_settings(node_mesh())->rssi_min == MESH_RSSI_MIN_OFF);
  usb_cmd("puissance -10\r");
  CHECK(strstr(usb_out(), "puissance = -11 dBm") != NULL);
  CHECK(fake_ble_tx_high == 0U && fake_ble_tx_pa == 2U);
  usb_cmd("puissance -2\r");
  usb_cmd("flash off\r");
  CHECK(strstr(usb_out(), "flash = off") != NULL);
  usb_cmd("flash on\r");
  usb_cmd("ttl 4\r");

  printf("- réglage à distance envoyé : trame REGLAGE, appliqué aussi ici pour « tous »\n");
  first = fake_ble_sent_count;
  usb_cmd("regle tous hello 30\r");
  CHECK(strstr(usb_out(), "Réglage envoyé à tous : hello = 30 s (appliqué aussi sur cette carte)") != NULL);
  loop(400);
  p = find_sent(MESH_T_CONFIG, s_me, first);
  CHECK(p != NULL && p->payload[0] == SETTING_HELLO && mesh_get_u16(&p->payload[1]) == 30U);
  CHECK(mesh_settings(node_mesh())->hello_period_s == 30U);
  usb_cmd("regle 5 ttl 99\r");
  CHECK(strstr(usb_out(), "Valeur refusée pour ttl (valeurs : 0 à 15)") != NULL);
  usb_cmd("regle 5 truc 1\r");
  CHECK(strstr(usb_out(), "Usage : regle dest réglage valeur") != NULL);
  usb_cmd("hello 20\r");
}

static void test_remote_settings(void)
{
  mesh_frame_t f;
  int          first = fake_ble_sent_count;
  uint8_t      data[3];

  printf("- réglage reçu : appliqué après le relais de la trame qui le porte\n");
  data[0] = SETTING_TTL;
  mesh_put_u16(&data[1], 2U);
  f = frame(5U, MESH_ADDR_ALL, 60U, 3U, 0U, 5U, MESH_T_CONFIG, data, 3U);
  fake_ble_mesh_frame(&f, -60);
  loop(50);
  CHECK(mesh_settings(node_mesh())->ttl == 4U);      /* pas encore */
  loop(1000);
  CHECK(mesh_settings(node_mesh())->ttl == 2U);
  CHECK(strstr(usb_out(), "Réglage changé à distance par 5 : ttl = 2") != NULL);

  printf("- « relais off » à distance : la trame qui le demande est quand même relayée\n");
  data[0] = SETTING_RELAY;
  mesh_put_u16(&data[1], 0U);
  f = frame(5U, MESH_ADDR_ALL, 61U, 3U, 0U, 5U, MESH_T_CONFIG, data, 3U);
  first = fake_ble_sent_count;
  fake_ble_mesh_frame(&f, -60);
  loop(1200);
  CHECK(find_sent(MESH_T_CONFIG, 5U, first) != NULL);
  CHECK((find_sent(MESH_T_CONFIG, 5U, first) != NULL) && (find_sent(MESH_T_CONFIG, 5U, first)->seq == 61U));
  CHECK(!mesh_settings(node_mesh())->relay);
  first = fake_ble_sent_count;
  f = frame(6U, MESH_ADDR_ALL, 62U, 3U, 0U, 6U, MESH_T_TEXT, "x", 1U);
  fake_ble_mesh_frame(&f, -60);
  loop(800);
  CHECK(find_sent(MESH_T_TEXT, 6U, first) == NULL);   /* plus de relais */

  data[0] = SETTING_TTL;
  mesh_put_u16(&data[1], 99U);
  f = frame(5U, MESH_ADDR_ALL, 63U, 3U, 0U, 5U, MESH_T_CONFIG, data, 3U);
  fake_ble_mesh_frame(&f, -60);
  loop(5);
  CHECK(strstr(usb_out(), "Réglage refusé (demandé par 5) : ttl = 99, hors limites") != NULL);
  usb_cmd("relais on\r");
  usb_cmd("ttl 4\r");
}

static void test_text_and_json(void)
{
  const mesh_frame_t *p;
  mesh_frame_t        f;
  int                 first = fake_ble_sent_count;
  const char         *out;

  printf("- texte coupé à 14 octets sans couper un caractère accentué\n");
  usb_cmd("msg tous éééééééééé\r");
  CHECK(strstr(usb_out(), "coupé à 14 octets") != NULL);
  loop(400);
  p = find_sent(MESH_T_TEXT, s_me, first);
  CHECK(p != NULL && p->len == 14U);

  printf("- mode PC : lignes JSON, plus d'écho des touches\n");
  usb_cmd("pc on\r");
  out = usb_out();
  CHECK(strstr(out, "{\"ev\":\"info\",") != NULL);
  CHECK(strstr(out, "\"ttl\":4,\"relais\":1,\"suppr\":3") != NULL);
  f = frame(5U, MESH_ADDR_ALL, 70U, 3U, 1U, 9U, MESH_T_TEXT, "a\"b", 3U);
  fake_ble_mesh_frame(&f, -61);
  loop(3);
  out = usb_out();
  CHECK(strstr(out, "{\"ev\":\"rx\",\"t\":") != NULL);
  CHECK(strstr(out, "\"src\":5,\"dst\":255,\"seq\":70,\"ttl\":3,\"hops\":1,\"last\":9,\"type\":\"TEXTE\",\"rssi\":-61,\"text\":\"a\\\"b\",\"data\":\"612262\"}") != NULL);
  CHECK(strstr(out, "Message de 5") == NULL);        /* pas de phrase en mode PC */
  fake_usb_host_send("x", 1U);
  loop(2);
  CHECK(strcmp(usb_out(), "") == 0);                 /* pas d'écho */
  fake_usb_host_send("\r", 1U);
  loop(2);
  usb_cmd("pc off\r");
  CHECK(strstr(usb_out(), "Mode texte") != NULL);

  printf("- journal : chaque trame reçue et relayée a sa ligne\n");
  usb_cmd("journal on\r");
  f = frame(5U, MESH_ADDR_ALL, 71U, 3U, 0U, 5U, MESH_T_WAVE, NULL, 0U);
  fake_ble_mesh_frame(&f, -60);
  {
    /* Le relais part après 20 à 300 ms d'attente, plus 320 ms si la radio
     * émet encore la trame précédente : on attend la ligne (1 s au plus). */
    static char journal[4096];
    journal[0] = '\0';
    for (int i = 0; (i < 20) && (strstr(journal, "-> relayée VAGUE") == NULL); i++)
    {
      loop(50);
      strncat(journal, usb_out(), sizeof(journal) - strlen(journal) - 1U);
    }
    CHECK(strstr(journal, "<- reçue VAGUE 5>tous n°71 ttl 3 de 5 (direct, -60 dBm)") != NULL);
    CHECK(strstr(journal, "-> relayée VAGUE 5>tous n°71 ttl 2 (0 relais avant)") != NULL);
  }
  usb_cmd("journal off\r");
}

static void test_radio_fallback(void)
{
  int  refused = (int)mesh_radio_stats()->tx_refused;
  int  first   = fake_ble_sent_count;
  int  tb      = fake_ble_time_base_refusals;
  bool during  = false;

  printf("- radio simultane : écoute relancée avec des temps admis par la base de temps du module\n");
  loop((int)RADIO_TX_TIME_MS + 400);                 /* radio libre */
  usb_cmd("radio simultane\r");
  CHECK(strstr(usb_out(), "Radio : écoute maintenue pendant l'émission (imposé)") != NULL);
  loop(5);
  CHECK(fake_ble_observing && (fake_ble_scan_interval == RADIO_SIM_SCAN_INTERVAL)
        && (fake_ble_scan_window == RADIO_SIM_SCAN_WINDOW));
  usb_cmd("msg tous simult\r");
  loop(400);
  CHECK(sent_text_during_scan(first, &during) && during);   /* émise sans arrêter l'écoute */
  CHECK(fake_ble_time_base_refusals == tb);
  CHECK((int)mesh_radio_stats()->tx_refused == refused);
  usb_cmd("radio\r");
  CHECK(strstr(usb_out(), "Base de temps du module : période 105,0 ms, plus grand créneau libre 23,7 ms") != NULL);

  printf("- refus d'émettre pendant l'écoute (code 0x12) : retour au mode alterné, signalé\n");
  loop((int)RADIO_TX_TIME_MS + 100);
  first = fake_ble_sent_count;
  fake_ble_broadcast_fail_status = ERR_INVALID_HCI_CMD_PARAMS;
  fake_ble_broadcast_fail_next   = 1;
  usb_cmd("msg tous alterne\r");
  loop(400);
  CHECK((int)mesh_radio_stats()->tx_refused == refused + 1);
  CHECK(mesh_radio_mode() == RADIO_MODE_ALTERNATE && !mesh_radio_mode_forced());
  CHECK(mesh_radio_last_refusal() == 0x12U && mesh_radio_fallback_code() == 0x12U);
  CHECK(strstr(usb_out(), "!! Le module refuse d'émettre pendant l'écoute (code 0x12) : écoute arrêtée pendant "
                          "chaque émission") != NULL);
  CHECK(sent_text_during_scan(first, &during) && !during);  /* réessayée, émise écoute arrêtée */
  loop((int)RADIO_TX_TIME_MS + 300);
  CHECK(fake_ble_observing && (fake_ble_scan_window == RADIO_SCAN_WINDOW));   /* écoute continue reprise */
  usb_cmd("radio\r");
  CHECK(strstr(usb_out(), "écoute arrêtée pendant l'émission (le module a refusé d'émettre pendant l'écoute, "
                          "code 0x12)") != NULL);
  CHECK(strstr(s_usb, "Base de temps du module : période 50,0 ms, plus grand créneau libre 0,0 ms") != NULL);
  usb_cmd("stats\r");
  CHECK(strstr(usb_out(), "(dernier code 0x12)") != NULL);

  printf("- émission refusée à chaque essai : abandonnée, code du module affiché\n");
  fake_ble_broadcast_fail_next = (int)MESH_TX_ATTEMPTS;
  usb_cmd("msg tous perdu\r");
  loop(800);
  CHECK(strstr(usb_out(), "!! refusée par la radio TEXTE") != NULL);
  CHECK(strstr(s_usb, "(code 0x12 du module)") != NULL);
  fake_ble_broadcast_fail_status = ERR_COMMAND_DISALLOWED;
  fake_ble_broadcast_fail_next   = 0;

  printf("- mode alterné : la réponse à un ping attend la fin de l'émission du demandeur\n");
  {
    const uint8_t       id = 21U;
    const mesh_frame_t *p;
    mesh_frame_t        f = frame(5U, s_me, 90U, 3U, 0U, 5U, MESH_T_PING, &id, 1U);
    loop(400);                                       /* radio libre, écoute reprise */
    first = fake_ble_sent_count;
    fake_ble_mesh_frame(&f, -55);
    loop(3);
    CHECK(strstr(usb_out(), "Ping n°21 de 5 (direct, -55 dBm) -> réponse dans 370 ms") != NULL);
    loop((int)NODE_REPLY_MIN_ALT_MS - 20);
    CHECK(find_sent(MESH_T_PONG, s_me, first) == NULL);   /* le demandeur émet encore */
    loop(60);
    p = find_sent(MESH_T_PONG, s_me, first);
    CHECK(p != NULL && p->dst == 5U && p->payload[0] == 21U);
  }
  usb_cmd("radio auto\r");
  CHECK(mesh_radio_mode() == RADIO_MODE_ALTERNATE && !mesh_radio_mode_forced());
  CHECK(strstr(usb_out(), "Radio : écoute arrêtée pendant l'émission (automatique)") != NULL);
  CHECK(mesh_radio_fallback_code() == 0U);
}

static void test_module_events(void)
{
  int      inits = fake_ble_init_count;
  uint32_t adv   = mesh_radio_stats()->adv_reports;
  uint32_t mesh_frames = mesh_radio_stats()->mesh_frames;

  printf("- redémarrage inattendu du module : remis en service, écoute relancée\n");
  fake_ble_hal_initialized(5U);                      /* RESET_WATCHDOG */
  loop(5);
  CHECK(fake_ble_init_count == inits + 1);
  CHECK(fake_ble_observing);
  CHECK(mesh_radio_stats()->module_restarts == 1U);

  printf("- publicités étrangères et rapports tronqués : ignorés\n");
  {
    const uint8_t phone[] = { 2U, 0x01U, 0x06U, 5U, 0x09U, 'T', 'e', 'l', '.' };
    const uint8_t addr[6] = { 9, 8, 7, 6, 5, 4 };
    fake_ble_adv_report(addr, phone, sizeof(phone), -40);
  }
  fake_ble_bad_report();
  loop(3);
  CHECK(mesh_radio_stats()->adv_reports == adv + 1U);
  CHECK(mesh_radio_stats()->mesh_frames == mesh_frames);

  printf("- rafale de trames : file de réception pleine comptée, pas de plantage\n");
  for (uint8_t i = 0U; i < 20U; i++)
  {
    mesh_frame_t f = frame(30U, 99U, (uint8_t)(100U + i), 0U, 0U, 30U, MESH_T_TEXT, "r", 1U);
    fake_ble_mesh_frame(&f, -60);
  }
  loop(2);
  CHECK(mesh_radio_stats()->rx_overflow >= 4U);
}

static void test_id_conflict(void)
{
  uint8_t      data[MESH_PAYLOAD_MAX];
  mesh_frame_t f;
  uint8_t      old = mesh_id(node_mesh());

  printf("- numéro en double (HELLO d'une carte d'étiquette plus petite) : changement\n");
  mesh_put_u32(data, 1U);                            /* étiquette 1 : plus petite que la nôtre */
  data[4] = MESH_HELLO_RELAY;
  data[5] = 0U;
  f = frame(old, MESH_ADDR_ALL, 80U, 3U, 0U, old, MESH_T_HELLO, data, MESH_HELLO_LEN(0U));
  fake_ble_mesh_frame(&f, -60);
  loop(3);
  {
    const char *out = usb_out();
    CHECK(strstr(out, "en double : la carte d'étiquette 00000001 l'utilise aussi") != NULL);
    CHECK(strstr(out, "Numéro changé") != NULL);
  }
  CHECK(mesh_id(node_mesh()) != old);
  s_me = mesh_id(node_mesh());
}

static void test_eeprom(void)
{
  const char *out;

  printf("- sauve / redémarrage / oublie : réglages gardés dans l'EEPROM\n");
  usb_cmd("id 42\r");
  CHECK(strstr(usb_out(), "Numéro : 42 (fixé)") != NULL);
  usb_cmd("reseau 3\r");
  usb_cmd("groupe +2\r");
  usb_cmd("ttl 6\r");
  usb_cmd("auto 30\r");
  usb_cmd("sauve\r");
  out = usb_out();
  CHECK(strstr(out, "Réglages enregistrés dans l'EEPROM (numéro fixé, réseau 3)") != NULL);
  CHECK(fake_eeprom_writes == 1);
  CHECK(fake_eeprom_irq_violations == 0);
  CHECK(memcmp(&fake_eeprom_mem[CONFIG_EEPROM_ADDRESS], "MESH", 4) == 0);
  usb_cmd("info\r");
  CHECK(strstr(usb_out(), "EEPROM     réglages enregistrés par « sauve »") != NULL);

  node_init(RADIO_OK);                               /* « redémarrage » */
  CHECK(node_load_status() == STORE_OK);
  usb_cmd("info\r");
  CHECK(strstr(usb_out(), "EEPROM     réglages enregistrés, lus au démarrage") != NULL);
  CHECK(mesh_id(node_mesh()) == 42U && mesh_id_is_fixed(node_mesh()));
  CHECK(mesh_net(node_mesh()) == 3U);
  CHECK(mesh_group_has(node_mesh(), 2U) && !mesh_group_has(node_mesh(), 3U));
  CHECK(mesh_settings(node_mesh())->ttl == 6U);
  CHECK(node_setting_get(SETTING_AUTO) == 30);

  fake_eeprom_mem[CONFIG_EEPROM_ADDRESS + 10U] ^= 0x01U;   /* un bit abîmé */
  node_init(RADIO_OK);
  CHECK(node_load_status() == STORE_EMPTY);          /* CRC faux : valeurs par défaut */
  CHECK(!mesh_id_is_fixed(node_mesh()) && mesh_net(node_mesh()) == 0U);
  fake_eeprom_mem[CONFIG_EEPROM_ADDRESS + 10U] ^= 0x01U;

  usb_cmd("oublie\r");
  CHECK(strstr(usb_out(), "Réglages enregistrés effacés") != NULL);
  usb_cmd("info\r");
  CHECK(strstr(usb_out(), "EEPROM     aucun réglage enregistré") != NULL);
  node_init(RADIO_OK);
  CHECK(node_load_status() == STORE_EMPTY);

  fake_eeprom_present = false;
  usb_cmd("sauve\r");
  CHECK(strstr(usb_out(), "Enregistrement impossible : l'EEPROM ne répond pas") != NULL);
  usb_cmd("info\r");
  CHECK(strstr(usb_out(), "EEPROM     l'EEPROM ne répond pas") != NULL);
  fake_eeprom_present = true;
  CHECK(fake_eeprom_irq_violations == 0);
  s_me = mesh_id(node_mesh());
}

static void test_button_and_ota(void)
{
  int first = fake_ble_sent_count;

  printf("- bouton SW1 : vague ; #ota : passage au chargeur après la réponse\n");
  fake_button_set(true);
  fake_button_exti();
  loop(60);
  fake_button_set(false);
  loop(60);
  CHECK(strstr(usb_out(), "Vague lancée (bouton SW1)") != NULL);
  loop(400);
  CHECK(find_sent(MESH_T_WAVE, s_me, first) != NULL);

  usb_cmd("#ota\r");
  CHECK(strstr(usb_out(), "chargeur Bluetooth ObCP-OTA") != NULL);
  CHECK(fake_ota_enter_count == 0);
  loop(400);
  CHECK(fake_ota_enter_count == 1);
}

/* Redémarre la radio avec une version de pile donnée (module différent). */
static void restart_radio(uint16_t fw_version)
{
  loop(400);                                         /* émission en cours finie */
  fake_ble_fw_version = fw_version;
  CHECK(mesh_radio_init() == RADIO_OK);
  loop(5);                                           /* l'écoute reprend */
}

/* Trame envoyée par cette carte depuis first, et si l'écoute tournait. */
static bool sent_text_during_scan(int first, bool *during_scan)
{
  const mesh_frame_t *p = find_sent(MESH_T_TEXT, s_me, first);

  if (p == NULL)
  {
    return false;
  }
  *during_scan = fake_ble_sent_during_scan[p - fake_ble_sent];
  return true;
}

static void test_module_types(void)
{
  int  first;
  int  writes;
  int  refused;
  bool during_scan = false;

  printf("- SPBTLE-RF (pile 7.1e) : mode 4 accepté, mode alterné par défaut, simultané sur demande\n");
  restart_radio(0x0715U);
  CHECK(fake_ble_stack_mode == 4U && mesh_radio_stack() == RADIO_STACK_MODE4);
  CHECK(mesh_radio_mode() == RADIO_MODE_ALTERNATE);
  CHECK(fake_ble_late_config_writes == 0);
  usb_cmd("info\r");
  CHECK(strstr(usb_out(), "module     puce BlueNRG-MS 3.1, pile 7.1e, mode 4 accepté (« radio simultane » possible)")
        != NULL);
  first = fake_ble_sent_count;
  usb_cmd("msg tous spbtle\r");
  loop(800);
  CHECK(sent_text_during_scan(first, &during_scan) && !during_scan);
  usb_cmd("radio simultane\r");
  loop(5);
  first = fake_ble_sent_count;
  usb_cmd("msg tous simult\r");
  loop(400);
  CHECK(sent_text_during_scan(first, &during_scan) && during_scan);
  CHECK(mesh_radio_mode() == RADIO_MODE_SIMULTANEOUS);
  usb_cmd("radio auto\r");

  printf("- SPBTLE-RF ancien (pile 7.1a) : pas de mode 4, émission et écoute alternées d'emblée\n");
  writes = fake_ble_mode_writes;
  restart_radio(0x0711U);
  CHECK(fake_ble_mode_writes == writes);             /* mode 4 pas demandé */
  CHECK(fake_ble_stack_mode == 2U && mesh_radio_stack() == RADIO_STACK_TOO_OLD);
  CHECK(mesh_radio_mode() == RADIO_MODE_ALTERNATE && !mesh_radio_mode_forced());
  usb_cmd("info\r");
  CHECK(strstr(usb_out(), "pile 7.1a, antérieure à 7.1b (pas de mode 4)") != NULL);
  first   = fake_ble_sent_count;
  refused = (int)mesh_radio_stats()->tx_refused;
  usb_cmd("msg tous ancien\r");
  loop(800);
  CHECK(sent_text_during_scan(first, &during_scan) && !during_scan);
  CHECK((int)mesh_radio_stats()->tx_refused == refused);   /* aucun refus : alterné d'emblée */
  CHECK(fake_ble_observing);                         /* l'écoute a repris */
  usb_cmd("radio auto\r");
  CHECK(mesh_radio_mode() == RADIO_MODE_ALTERNATE);
  usb_cmd("radio simultane\r");
  CHECK(strstr(usb_out(), "Attention : la pile du module n'est pas en mode 4") != NULL);
  loop(400);
  first   = fake_ble_sent_count;
  refused = (int)mesh_radio_stats()->tx_refused;
  usb_cmd("msg tous essai\r");                      /* refus 0x0C pendant l'écoute, puis alterné */
  loop(800);
  CHECK((int)mesh_radio_stats()->tx_refused == refused + 1);
  CHECK(mesh_radio_last_refusal() == ERR_COMMAND_DISALLOWED);
  CHECK(mesh_radio_mode() == RADIO_MODE_ALTERNATE);
  CHECK(sent_text_during_scan(first, &during_scan) && !during_scan);
  CHECK(strstr(usb_out(), "!! Le module refuse d'émettre pendant l'écoute (code 0x0C)") != NULL);
  usb_cmd("radio auto\r");
  CHECK(strstr(usb_out(), "Attention") == NULL);

  printf("- mode 4 refusé par le module : second essai sans, émission et écoute alternées\n");
  fake_ble_mode4_refused = true;
  restart_radio(0x0723U);
  CHECK(mesh_radio_stack() == RADIO_STACK_MODE4_REFUSED && mesh_radio_mode() == RADIO_MODE_ALTERNATE);
  usb_cmd("info\r");
  CHECK(strstr(usb_out(), "pile 7.2c, mode 4 refusé\r\n") != NULL);
  fake_ble_mode4_refused = false;

  printf("- adresse refusée après le mode 4 : second démarrage complet sans le mode 4\n");
  fake_ble_pubaddr_fail_after_mode = true;
  restart_radio(0x0723U);
  CHECK(!fake_ble_pubaddr_fail_after_mode);          /* le refus a bien eu lieu */
  CHECK(mesh_radio_stack() == RADIO_STACK_MODE4_REFUSED);
  CHECK(memcmp(fake_ble_public_addr, fake_ble_random_addr, 6) == 0);

  printf("- BlueNRG-M0 à pile 7.3 (vue sur une carte) : mode 4 accepté\n");
  restart_radio(0x0730U);
  CHECK(mesh_radio_stack() == RADIO_STACK_MODE4 && mesh_radio_mode() == RADIO_MODE_ALTERNATE);
  usb_cmd("info\r");
  CHECK(strstr(usb_out(), "pile 7.3, mode 4 accepté") != NULL);

  printf("- BlueNRG-M0 (pile 7.2c) : mode 4, version dans la ligne JSON « info »\n");
  restart_radio(0x0723U);
  CHECK(mesh_radio_stack() == RADIO_STACK_MODE4 && mesh_radio_mode() == RADIO_MODE_ALTERNATE);
  CHECK(fake_ble_late_config_writes == 0);
  for (unsigned g = 1U; g <= MESH_GROUP_COUNT; g++)  /* ligne JSON la plus longue */
  {
    char c[16];
    snprintf(c, sizeof(c), "groupe +%u\r", g);
    usb_cmd(c);
  }
  usb_cmd("pc on\r");
  {
    const char *out = usb_out();
    CHECK(strstr(out, "\"fw\":\"7.2c\",\"stack_mode4\":1") != NULL);
    CHECK(strstr(out, "\"groups\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]}\r\n") != NULL);   /* pas tronquée */
  }
  usb_cmd("pc off\r");
  for (unsigned g = 1U; g <= MESH_GROUP_COUNT; g++)
  {
    char c[16];
    snprintf(c, sizeof(c), "groupe -%u\r", g);
    usb_cmd(c);
  }
}

static void test_no_module(void)
{
  printf("- module absent : signalé, les commandes d'envoi l'expliquent\n");
  fake_ble_present = false;
  CHECK(mesh_radio_init() == RADIO_ERR_NO_MODULE);
  node_init(RADIO_ERR_NO_MODULE);
  usb_cmd("ping 5\r");
  CHECK(strstr(usb_out(), "Radio hors service (le module Bluetooth ne répond pas)") != NULL);
  usb_cmd("info\r");
  CHECK(strstr(usb_out(), "HORS SERVICE") != NULL);
  fake_ble_present = true;
}

int main(void)
{
  test_boot();
  test_first_hello();
  test_usb_console();
  test_receive_and_relay();
  test_suppression();
  test_groups_and_led();
  test_ping_and_trace();
  test_measures();
  test_settings();
  test_remote_settings();
  test_text_and_json();
  test_radio_fallback();
  test_module_events();
  test_id_conflict();
  test_eeprom();
  test_button_and_ota();
  test_module_types();
  test_no_module();
  return check_summary("programme complet sur la carte simulée");
}
