/*
 * Simulation complète sur PC : l'application (Core/Src/app) tourne avec le pilote
 * ST67W6X_Lite face au module simulé (sim_module.c). Les commandes sont
 * « tapées » dans la console simulée et les réponses vérifiées ; le journal
 * complet est écrit dans build/sim_transcript.txt.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "stm32l4xx_hal.h"
#include "app.h"
#include "config.h"
#include "demos.h"
#include "io.h"
#include "ncp.h"
#include "sim_module.h"
#include "host.h"
#include "ymodem_sender.h"

static int g_fail, g_pass;

#define CHECK(c) do { if (c) { g_pass++; } else { g_fail++; printf("  ECHEC %s:%d : %s\n", __FILE__, __LINE__, #c); } } while (0)
#define EXPECT(s) do { if (strstr(host_output(), (s)) != NULL) { g_pass++; } else { g_fail++; \
  printf("  ECHEC %s:%d : sortie sans « %s »\n", __FILE__, __LINE__, (s)); } } while (0)
#define REJECT(s) do { if (strstr(host_output(), (s)) == NULL) { g_pass++; } else { g_fail++; \
  printf("  ECHEC %s:%d : sortie avec « %s »\n", __FILE__, __LINE__, (s)); } } while (0)

static void loop_ms(uint32_t ms)
{
  uint32_t t0 = HAL_GetTick();
  while ((HAL_GetTick() - t0) < ms)
  {
    app_loop();
  }
}

static void cmd(const char *line)
{
  char buf[256];
  host_clear();
  (void)snprintf(buf, sizeof(buf), "%s\r", line);
  host_type(buf, 0U);
  while (host_input_pending())
  {
    app_loop();
  }
  app_loop();
}

/* commande longue arrêtée par une touche au bout de stop_ms */
/* Commande puis réponse au clavier une seconde plus tard (confirmation) */
static void cmd_keys(const char *line, const char *keys)
{
  char buf[256];
  host_clear();
  (void)snprintf(buf, sizeof(buf), "%s\r", line);
  host_type(buf, 0U);
  host_type(keys, HAL_GetTick() + 1000U);
  while (host_input_pending())
  {
    app_loop();
  }
  app_loop();
}

static void cmd_stop_after(const char *line, uint32_t stop_ms)
{
  char buf[256];
  host_clear();
  (void)snprintf(buf, sizeof(buf), "%s\r", line);
  host_type(buf, 0U);
  host_type("x", HAL_GetTick() + stop_ms);
  while (host_input_pending())
  {
    app_loop();
  }
  app_loop();
}

static void save_file(const char *path, const char *data, size_t n)
{
  FILE *f = fopen(path, "wb");
  if (f != NULL)
  {
    fwrite(data, 1, n, f);
    fclose(f);
  }
}

/* Requête HTTP simulée sur le lien link ; retourne la réponse complète */
static const char *http(int link, const char *req)
{
  char     ipd[96];
  uint32_t n;

  sim_sent_clear();
  sim_set_socket_data((const uint8_t *)req, (uint16_t)strlen(req));
  (void)snprintf(ipd, sizeof(ipd), "+CIP:%d,CONNECTED\r\n+IPD:%d,%u,\"10.19.96.2\",51000\r\n", link, link,
                 (unsigned)strlen(req));
  sim_push(ipd);
  loop_ms(4000);
  return sim_link_sent(link, &n);
}

static const char *body_of(const char *resp)
{
  const char *p = strstr(resp, "\r\n\r\n");
  return (p != NULL) ? (p + 4) : "";
}

static long content_length(const char *resp)
{
  const char *p = strstr(resp, "Content-Length: ");
  return (p != NULL) ? strtol(p + 16, NULL, 10) : -1;
}

/* Requête POST /api/wifi avec une longueur de corps exacte */
static const char *post_req(const char *body)
{
  static char req[512];
  (void)snprintf(req, sizeof(req), "POST /api/wifi HTTP/1.1\r\nHost: 10.19.96.1\r\n"
                 "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %u\r\n\r\n%s",
                 (unsigned)strlen(body), body);
  return req;
}

static void mqtt_rx(const char *topic, const char *msg)
{
  char buf[512];
  (void)snprintf(buf, sizeof(buf), "+MQTT:SUBRECV:0,%u,%u,\"%s\",%s\r\n", (unsigned)strlen(topic),
                 (unsigned)strlen(msg), topic, msg);
  host_clear();
  sim_push(buf);
  loop_ms(800);
}

/* ---- Émetteur YMODEM simulé branché sur la console (commande « module ota ») ---- */
static struct
{
  ys_t   ys;
  bool   on;
  bool   armed;          /* repère « En attente du fichier » vu dans la sortie */
  size_t pos;            /* octets de sortie déjà transmis à l'émetteur        */
  size_t start;          /* début du protocole dans la sortie                  */
} S;

static uint8_t g_ota[200000];

static void ys_emit_to_console(const uint8_t *d, size_t n, void *ctx)
{
  (void)ctx;
  host_feed(d, n, 0U);
}

static void ymodem_hook(void)
{
  const char *out = host_output();
  size_t      len = host_output_len();

  if (!S.on)
  {
    return;
  }
  if (!S.armed)
  {
    const char *m = strstr(out, "En attente du fichier (YMODEM)...");
    const char *nl;
    if (m == NULL)
    {
      return;
    }
    nl = strchr(m, '\n');
    if (nl == NULL)
    {
      return;
    }
    S.armed = true;
    S.pos = (size_t)(nl + 1 - out);
    S.start = S.pos;
  }
  while (S.pos < len)
  {
    ys_on_byte(&S.ys, (uint8_t)out[S.pos++]);
  }
}

/* Lance « module ota » avec l'émetteur simulé configuré par l'appelant */
static void ota_run(void)
{
  S.armed = false;
  S.pos = 0;
  S.on = true;
  host_set_rx_hook(ymodem_hook);
  cmd("module ota");
  host_set_rx_hook(NULL);
  S.on = false;
}

static void ota_prepare(const char *name, size_t payload, const char *version, size_t *n)
{
  *n = sim_make_ota_image(g_ota, sizeof(g_ota), payload, version, true, 4242);
  ys_init(&S.ys, name, g_ota, *n, ys_emit_to_console, NULL);
}

/* Pendant le transfert, seuls des octets de protocole doivent circuler */
static bool ota_protocol_clean(void)
{
  const char *out = host_output();
  size_t      i;
  if (!S.armed)
  {
    return false;
  }
  for (i = S.start; (out[i] != '\0') && (out[i] != '\n'); i++)
  {
    char c = out[i];
    if ((c != 'C') && (c != 0x06) && (c != 0x15) && (c != 0x18))
    {
      return false;
    }
  }
  return true;
}

static void test_ota(void)
{
  size_t         n;
  size_t         got_len;
  const uint8_t *got;
  int            st0;
  int            ab0;

  printf("- mise à jour du module par le terminal (module ota, YMODEM)\n");
  sim_set_version("2.0.97");
  cmd("module reset");
  CHECK(g_app.ncp_ok);
  loop_ms(6000);
  CHECK(netapp_sta_ready());
  io_led_set(35);

  /* 1) mise à jour normale */
  ota_prepare("st67w611m_mission_t01_v2.0.106.xz.ota", 60000, "2.0.106", &n);
  ota_run();
  EXPECT("Firmware actuel   : 2.0.97");
  EXPECT("Fichier reçu : st67w611m_mission_t01_v2.0.106.xz.ota (60512 octets");
  EXPECT("Image 2.0.106, compressée (XZ)");
  EXPECT("Installation par le module");
  EXPECT("Firmware du module mis à jour : 2.0.97 -> 2.0.106");
  CHECK(S.ys.finished_ok);
  CHECK(ota_protocol_clean());
  CHECK(sim_ota_count("install") == 1);
  got = sim_ota_image(&got_len);
  CHECK((got_len == n) && (memcmp(got, g_ota, n) == 0));
  CHECK(sim_ota_send_size(0) == 512);
  CHECK(g_app.ncp_ok);
  CHECK(io_led_get() == 35);                       /* LED rendue après la progression */
  loop_ms(8000);
  CHECK(netapp_sta_ready());                       /* liaison Wi-Fi rétablie */
  loop_ms(18000);
  CHECK(mqttapp_connected());                      /* MQTT rétabli */

  /* 2) image T02 refusée avant toute écriture dans le module */
  st0 = sim_ota_count("start");
  ota_prepare("st67w611m_mission_t02_v2.0.106.xz.ota", 4000, "2.0.106", &n);
  ota_run();
  EXPECT("image T02 ou de test (mfg)");
  CHECK(S.ys.cancelled_by_receiver);
  CHECK(sim_ota_count("start") == st0);

  /* 3) nom sans .ota */
  ota_prepare("firmware.bin", 4000, "2.0.106", &n);
  ota_run();
  EXPECT("fichier .ota attendu");

  /* 4) en-tête invalide : session ouverte puis abandonnée */
  ab0 = sim_ota_count("abort");
  ota_prepare("st67w611m_mission_t01_v2.0.107.xz.ota", 4000, "2.0.107", &n);
  S.ys.corrupt_header = 'X';
  ota_run();
  EXPECT("en-tête OTA invalide");
  EXPECT("conserve son firmware actuel");
  CHECK(sim_ota_count("abort") == ab0 + 1);
  CHECK(strcmp(sim_version(), "2.0.106") == 0);

  /* 5) image corrompue : refusée par le module lors de AT+OTAFIN */
  ota_prepare("st67w611m_mission_t01_v2.0.108.bin.ota", 30000, "2.0.108", &n);
  g_ota[15000] ^= 0x40;                            /* SHA-256 faux */
  ota_run();
  EXPECT("Fichier reçu");
  EXPECT("image refusée par le module");
  EXPECT("Le module conserve son firmware actuel");
  CHECK(sim_ota_count("reject") == 1);
  CHECK(strcmp(sim_version(), "2.0.106") == 0);
  CHECK(g_app.ncp_ok);

  /* 6) annulation par l'émetteur en cours de transfert */
  ab0 = sim_ota_count("abort");
  ota_prepare("st67w611m_mission_t01_v2.0.109.xz.ota", 30000, "2.0.109", &n);
  S.ys.cancel_at_block = 6;
  ota_run();
  EXPECT("transfert annulé par l'émetteur");
  EXPECT("Mise à jour abandonnée");
  CHECK(sim_ota_count("abort") == ab0 + 1);

  /* 7) erreur de CRC et ACK perdu : transfert quand même correct */
  ota_prepare("st67w611m_mission_t01_v2.0.110.xz.ota", 20000, "2.0.110", &n);
  S.ys.corrupt_block = 4;
  S.ys.drop_ack_block = 9;
  ota_run();
  EXPECT("Firmware du module mis à jour : 2.0.106 -> 2.0.110");
  got = sim_ota_image(&got_len);
  CHECK((got_len == n) && (memcmp(got, g_ota, n) == 0));

  /* 8) version plus ancienne : avertissement */
  ota_prepare("st67w611m_mission_t01_v2.0.89.xz.ota", 8000, "2.0.89", &n);
  ota_run();
  EXPECT("Version plus ancienne");
  EXPECT("2.0.110 -> 2.0.89");

  /* 9) annulation au clavier avant le début */
  S.on = false;
  host_clear();
  host_type("module ota\r", 0U);
  host_type("q", HAL_GetTick() + 6000U);
  while (host_input_pending())
  {
    app_loop();
  }
  app_loop();
  EXPECT("annulé au clavier");

  /* 10) aucun émetteur : délai dépassé (120 s) */
  cmd("module ota");
  EXPECT("aucun transfert reçu");
  REJECT("abandonnée");                            /* aucune session ouverte */

  sim_set_version("2.0.106");
  cmd("module reset");
  CHECK(g_app.ncp_ok);
  loop_ms(8000);
}

int main(void)
{
  const char *r;
  int         n0;
  int         n1;

  printf("Simulation de l'application complète (module simulé)\n");
  sim_reset();
  host_transcript("build/sim_transcript.txt");

  /* ---- démarrage ---- */
  printf("- démarrage\n");
  g_host_fault_pending = true;                    /* faute avant le redémarrage */
  app_init();
  loop_ms(200);
  EXPECT("ObCP (STM32L476RG) + X-NUCLEO-67W61M1");
  EXPECT("accéléromètre LIS3DH OK");
  EXPECT("Redémarrage après une faute BusFault : PC=0x08001234");
  EXPECT("Démarrage du module Wi-Fi ST67W611M1... ");
  EXPECT("OK");
  EXPECT("Firmware     : 2.0.106");
  EXPECT("Aucun réseau Wi-Fi configuré");
  CHECK(g_app.ncp_ok);
  CHECK(sim_cmd_count("AT+CIPAP=\"10.19.96.1\"") == 1);
  CHECK(sim_cmd_count("AT+CWHOSTNAME=\"obcp-A1B2C3\"") == 1);

  /* ---- aide ---- */
  printf("- aide et commandes générales\n");
  cmd("help");
  EXPECT("Démo 4 - Télémétrie MQTT");
  EXPECT("scan [passif]");
  cmd("?");
  EXPECT("Démo 1 - Scanner Wi-Fi");
  cmd("help scan");
  EXPECT("Exemples");
  cmd("help demo");
  EXPECT("Guide rapide");
  EXPECT("obcp/A1B2C3/telemetry");
  cmd("foo bar");
  EXPECT("Commande inconnue");
  cmd("");
  cmd("sensors");
  EXPECT("24.5 °C");
  EXPECT("3.30 V");
  EXPECT("Accélération     : X +0.01 g  Y -0.02 g  Z +0.99 g");
  EXPECT("Inclinaison      : tangage -0.7°, roulis -1.4°");
  EXPECT("Sortie PWM       : 0 %");
  /* LED RVB */
  cmd("led 30");
  EXPECT("LED RVB : 30 %, couleur blanc (#FFFFFF)");
  CHECK(io_led_get() == 30);
  cmd("led 130");
  EXPECT("Luminosité de 0 à 100 %");
  CHECK(io_led_get() == 30);
  cmd("led off");
  CHECK(io_led_get() == 0);
  cmd("led rouge");                              /* couleur sur LED éteinte : rallumée */
  EXPECT("LED RVB : 30 %, couleur rouge (#FF0000)");
  cmd("led bleu 20");
  EXPECT("LED RVB : 20 %, couleur bleu (#0000FF)");
  cmd("led couleur #ff8000");
  EXPECT("couleur #FF8000");
  cmd("led 10,20,30");
  EXPECT("couleur #0A141E");
  cmd("led chartreuse");
  EXPECT("Couleur inconnue");
  cmd("led blanc 0");
  CHECK(io_led_get() == 0);
  /* sortie PWM */
  cmd("pwm 40");
  EXPECT("Sortie PWM : 40 % (1 kHz)");
  CHECK(io_pwm_get() == 40);
  cmd("pwm 101");
  EXPECT("Usage : pwm");
  cmd("pwm off");
  CHECK(io_pwm_get() == 0);
  /* passage dans le chargeur DFU : confirmation demandée */
  cmd_keys("dfu", "n");
  EXPECT("chargeur USB DFU");
  EXPECT("Annulé");
  CHECK(g_host_dfu == 0);
  cmd_keys("dfu", "o");
  EXPECT("Passage en DFU");
  CHECK(g_host_dfu == 1);
  /* terminal refermé puis rouvert */
  host_reconnect();
  cmd("");
  EXPECT("terminal reconnecté");
  cmd("config wifi");
  EXPECT("wifi.ssid");
  REJECT("mqtt.host");
  cmd("set nope 1");
  EXPECT("Clé inconnue");
  cmd("set time.tz 9999");
  EXPECT("Valeurs admises : -720 à 840");
  cmd("set ap.pass court");
  EXPECT("hors limites");
  cmd("stats");
  EXPECT("SPI :");
  cmd("time");
  EXPECT("non synchronisée");

  /* ---- démo 1 ---- */
  printf("- démo 1 : scan, canaux\n");
  cmd("scan");
  EXPECT("Net1");
  EXPECT("Open, cafe");
  EXPECT("3 réseau(x)");
  EXPECT("dont");
  EXPECT("2 en Wi-Fi 6");
  EXPECT("1 ouvert(s)");
  CHECK(sim_cmd_count("AT+CWLAP=0,,,0") == 1);
  cmd("scan passif canal 6 tri canal");
  CHECK(sim_cmd_count("AT+CWLAP=1,,,6") == 1);
  cmd("scan canal 99");
  EXPECT("Canal invalide");
  cmd("channels");
  EXPECT("Canal conseillé pour le point d'accès");
  cmd("canaux appliquer");
  EXPECT("ap.channel = ");
  cmd("track");
  EXPECT("préciser le réseau");

  /* ---- démo 2 ---- */
  printf("- démo 2 : connexion, IP, DNS, ping, NTP\n");
  cmd("connect Net1 wrong");
  EXPECT("Mot de passe incorrect");
  cmd("connect Net1 secret123");
  EXPECT("Connecté en");
  EXPECT("192.168.1.42");
  EXPECT("-47 dBm");
  EXPECT("WPA2, norme Wi-Fi 6 (802.11ax)");
  EXPECT("« save » pour mémoriser");
  CHECK(netapp_sta_ready());
  host_clear();
  loop_ms(3000);
  EXPECT("RTC réglée par NTP");
  CHECK(sim_cmd_count("AT+CIPSNTPCFG=1,0,\"pool.ntp.org\"") >= 1);
  cmd("time");
  EXPECT("2026-10-03 08:45");
  EXPECT("2026-10-03 10:45");
  EXPECT("UTC+2, heure d'été");
  cmd("status");
  EXPECT("connectée");
  EXPECT("Point d'accès : arrêté");
  cmd("dns www.st.com");
  EXPECT("93.184.216.34");
  cmd("ping 8.8.8.8");
  EXPECT("Bilan : 4 envoyée(s), 3 reçue(s), 25 % de perte");
  EXPECT("11/12/15 ms");
  cmd("ping www.st.com 4 500");
  EXPECT("PING www.st.com (93.184.216.34)");
  CHECK(sim_cmd_count("AT+PING=\"93.184.216.34\",64,4,500") == 1);
  cmd("ping");
  EXPECT("Usage");
  cmd("ntp");
  EXPECT("Heure locale");
  EXPECT("Correction de la RTC");

  n0 = sim_cmd_count("AT+CWJAP=");
  cmd("ip 192.168.1.50 255.255.255.0 192.168.1.1");
  EXPECT("Reconnexion");
  EXPECT("(fixe)");
  CHECK(sim_cmd_count("AT+CWDHCP=0,1") >= 1);
  CHECK(sim_cmd_count("AT+CIPSTA=\"192.168.1.50\",\"192.168.1.1\",\"255.255.255.0\"") == 1);
  CHECK(sim_cmd_count("AT+CIPDNS=1,\"192.168.1.1\"") == 1);
  CHECK(sim_cmd_count("AT+CWJAP=") == n0 + 1);
  cmd("ip");
  EXPECT("IP fixe 192.168.1.50");
  cmd("ip dhcp");
  EXPECT("DHCP");
  CHECK(sim_cmd_count("AT+CWDHCP=1,1") >= 1);
  cmd("ip 1.2.3");
  EXPECT("Usage");
  cmd("ip 1.2.3 255.255.255.0 1.2.3.1");
  EXPECT("Adresse invalide");
  cmd("ip dhcp");

  /* ---- démo 1 bis : suivi du signal ---- */
  printf("- démo 1 : suivi RSSI\n");
  cmd_stop_after("track", 4500);
  EXPECT("Suivi de « ");
  EXPECT("-47 dBm");
  EXPECT("Bilan : ");
  cmd_stop_after("track Lab csv", 3500);
  EXPECT("temps_ms;rssi_dbm;canal");
  EXPECT(";-60;1");
  cmd("track Inconnu");
  EXPECT("introuvable");

  /* ---- démo 3 ---- */
  printf("- démo 3 : point d'accès et serveur web\n");
  cmd("web start");
  EXPECT("Point d'accès et serveur web actifs");
  EXPECT("ObCP-WiFi-A1B2C3");
  EXPECT("http://10.19.96.1/");
  EXPECT("http://192.168.1.42/");
  EXPECT("\033[30;47m");
  CHECK(sim_cmd_count("AT+CWMODE=3,1") == 1);
  CHECK(sim_cmd_count("AT+CWSAP=\"ObCP-WiFi-A1B2C3\",\"obcpdemo\"") == 1);
  CHECK(sim_cmd_count("AT+CIPSERVER=1,80") == 1);

  r = http(0, "GET / HTTP/1.1\r\nHost: 10.19.96.1\r\nUser-Agent: test\r\n\r\n");
  CHECK(strncmp(r, "HTTP/1.1 200 OK\r\n", 17) == 0);
  CHECK(strstr(r, "text/html; charset=utf-8") != NULL);
  CHECK(strstr(r, "</html>") != NULL);
  CHECK(content_length(r) == (long)strlen(body_of(r)));
  save_file("build/sim_index.html", body_of(r), strlen(body_of(r)));
  CHECK(sim_cmd_count("AT+CIPCLOSE=0") >= 1);

  r = http(1, "GET /api/status HTTP/1.1\r\nHost: 10.19.96.1\r\n\r\n");
  CHECK(strncmp(r, "HTTP/1.1 200 OK", 15) == 0);
  CHECK(strstr(r, "\"id\":\"A1B2C3\"") != NULL);
  CHECK(strstr(r, "\"rssi\":-47") != NULL);
  CHECK(strstr(r, "\"ip\":\"192.168.1.42\"") != NULL);
  CHECK(strstr(r, "\"temp\":24.5") != NULL);
  CHECK(content_length(r) == (long)strlen(body_of(r)));
  save_file("build/sim_status.json", body_of(r), strlen(body_of(r)));

  CHECK(strstr(r, "\"ax\":0.012,\"ay\":-0.025,\"az\":0.998") != NULL);
  CHECK(strstr(r, "\"pwm\":0") != NULL);
  r = http(2, "GET /api/led?v=40 HTTP/1.1\r\n\r\n");
  CHECK(strstr(r, "{\"led\":40,\"rgb\":\"#FFFFFF\"}") != NULL);
  CHECK(io_led_get() == 40);
  r = http(2, "GET /api/led?c=00FF00 HTTP/1.1\r\n\r\n");
  CHECK(strstr(r, "{\"led\":40,\"rgb\":\"#00FF00\"}") != NULL);
  r = http(2, "GET /api/led?v=0&c=FFFFFF HTTP/1.1\r\n\r\n");
  CHECK(strstr(r, "{\"led\":0,\"rgb\":\"#FFFFFF\"}") != NULL);
  r = http(2, "GET /api/pwm?v=25 HTTP/1.1\r\n\r\n");
  CHECK(strstr(r, "{\"pwm\":25}") != NULL);
  CHECK(io_pwm_get() == 25);
  r = http(2, "GET /api/pwm?v=0 HTTP/1.1\r\n\r\n");
  r = http(2, "GET /api/led?v=40 HTTP/1.1\r\n\r\n");

  r = http(0, "GET /api/scan HTTP/1.1\r\n\r\n");
  CHECK(strstr(r, "\"ssid\":\"Net1\"") != NULL);
  CHECK(strstr(r, "\"std\":\"Wi-Fi 6\"") != NULL);
  save_file("build/sim_scan.json", body_of(r), strlen(body_of(r)));

  r = http(3, "GET /nope HTTP/1.1\r\n\r\n");
  CHECK(strncmp(r, "HTTP/1.1 404", 12) == 0);
  r = http(4, "FOO\r\nbar\r\n\r\n");
  CHECK(strncmp(r, "HTTP/1.1 400", 12) == 0);
  r = http(1, "HEAD / HTTP/1.1\r\n\r\n");
  CHECK((strncmp(r, "HTTP/1.1 200", 12) == 0) && (strlen(body_of(r)) == 0));
  r = http(2, "GET /favicon.ico HTTP/1.1\r\n\r\n");
  CHECK(strncmp(r, "HTTP/1.1 204", 12) == 0);

  n0 = g_host_flash_writes;
  n1 = sim_cmd_count("AT+CWJAP=\"Lab\"");
  host_clear();
  r = http(0, post_req("ssid=Lab&pass=labpass%2B123&save=1"));
  CHECK(strstr(r, "\"ok\":true") != NULL);
  CHECK(strcmp(g_cfg.sta_ssid, "Lab") == 0);
  CHECK(strcmp(g_cfg.sta_pass, "labpass+123") == 0);
  CHECK(g_host_flash_writes == n0 + 1);
  save_file("build/sim_wifi.json", body_of(r), strlen(body_of(r)));
  loop_ms(6000);
  CHECK(sim_cmd_count("AT+CWJAP=\"Lab\"") == n1 + 1);
  EXPECT("connexion demandée au réseau « Lab »");
  EXPECT("Wi-Fi : connecté à « Lab »");
  r = http(1, post_req("ssid=a%22b&pass=12345678"));
  CHECK(strstr(r, "\"ok\":false") != NULL);
  r = http(2, "GET /index.html HTTP/1.1\r\n\r\nGET / HTTP/1.1\r\n\r\n");     /* requête suivie d'octets en trop */
  CHECK(strncmp(r, "HTTP/1.1 200", 12) == 0);

  /* connexion ouverte sans requête : fermée au bout de 8 s */
  n0 = sim_cmd_count("AT+CIPCLOSE=3");
  sim_push("+CIP:3,CONNECTED\r\n");
  loop_ms(10000);
  CHECK(sim_cmd_count("AT+CIPCLOSE=3") == n0 + 1);

  host_clear();
  sim_push("+CW:STA_CONNECTED,\"de:ad:be:ef:00:01\"\r\n+CW:DIST_STA_IP,\"de:ad:be:ef:00:01\",\"10.19.96.2\"\r\n");
  loop_ms(500);
  EXPECT("appareil DE:AD:BE:EF:00:01 connecté");
  EXPECT("a reçu l'adresse 10.19.96.2");
  cmd("web status");
  EXPECT("Requêtes");
  EXPECT("2 appareil(s)");
  cmd("web start");
  EXPECT("déjà actif");
  cmd("web stop");
  EXPECT("arrêtés");
  CHECK(sim_cmd_count("AT+CIPSERVER=0,1") == 1);
  cmd("web");
  EXPECT("arrêté");

  /* ---- démo 4 ---- */
  printf("- démo 4 : MQTT\n");
  sim_sent_clear();
  cmd("mqtt start");
  EXPECT("Connecté au broker MQTT");
  EXPECT("obcp/A1B2C3/telemetry");
  EXPECT("mosquitto_sub -h broker.emqx.io -p 1883 -t \"obcp/A1B2C3/#\" -v");
  EXPECT("Broker public");
  CHECK(sim_cmd_count("AT+MQTTUSERCFG=0,0,\"obcp-A1B2C3\"") == 1);
  CHECK(sim_cmd_count("AT+MQTTCONNCFG=0,60,0,\"obcp/A1B2C3/status\",\"offline\",0,1") == 1);
  CHECK(sim_cmd_count("AT+MQTTCONN=0,\"broker.emqx.io\",1883,0") == 1);
  CHECK(sim_cmd_count("AT+MQTTSUB=0,\"obcp/A1B2C3/cmd\",1") == 1);
  CHECK(sim_data_find("\"obcp/A1B2C3/status\",6,1,1", "online") == 1);
  loop_ms(500);
  CHECK(sim_data_find("\"obcp/A1B2C3/telemetry\"", "\"id\":\"A1B2C3\"") == 1);
  save_file("build/sim_telemetry.json", sim_data_last("telemetry"), strlen(sim_data_last("telemetry")));

  mqtt_rx("obcp/A1B2C3/cmd", "led=75");
  CHECK(io_led_get() == 75);
  EXPECT("LED à 75 %");
  CHECK(sim_data_find("obcp/A1B2C3/ack", "\"cmd\":\"led\",\"ok\":true,\"led\":75") == 1);
  mqtt_rx("obcp/A1B2C3/cmd", "{\"period\":5}");
  CHECK(g_cfg.mqtt_period == 5);
  mqtt_rx("obcp/A1B2C3/cmd", "msg=Bonjour le labo");
  EXPECT("Bonjour le labo");
  mqtt_rx("obcp/A1B2C3/cmd", "reboot");
  EXPECT("commande inconnue");
  mqtt_rx("obcp/A1B2C3/cmd", "led=500");
  CHECK(io_led_get() == 75);
  CHECK(sim_data_find("obcp/A1B2C3/ack", "\"cmd\":\"led\",\"ok\":false") == 1);
  mqtt_rx("obcp/A1B2C3/cmd", "color=vert");
  CHECK(sim_data_find("obcp/A1B2C3/ack", "\"cmd\":\"color\",\"ok\":true") == 1);
  {
    uint8_t rgb[3];
    io_led_color_get(rgb);
    CHECK((rgb[0] == 0U) && (rgb[1] == 255U) && (rgb[2] == 0U));
  }
  mqtt_rx("obcp/A1B2C3/cmd", "{\"color\":\"#0000ff\"}");
  CHECK(sim_data_find("obcp/A1B2C3/ack", "\"cmd\":\"color\",\"ok\":true") == 2);
  mqtt_rx("obcp/A1B2C3/cmd", "color=fuchsia");
  CHECK(sim_data_find("obcp/A1B2C3/ack", "\"cmd\":\"color\",\"ok\":false") == 1);
  mqtt_rx("obcp/A1B2C3/cmd", "pwm=60");
  CHECK(sim_data_find("obcp/A1B2C3/ack", "\"cmd\":\"pwm\",\"ok\":true,\"pwm\":60") == 1);
  CHECK(io_pwm_get() == 60);
  mqtt_rx("obcp/A1B2C3/cmd", "pwm=0");
  mqtt_rx("obcp/A1B2C3/cmd", "ping");
  CHECK(sim_data_find("obcp/A1B2C3/ack", "\"cmd\":\"ping\",\"ok\":true,\"uptime\":") == 1);

  n0 = sim_data_find("obcp/A1B2C3/telemetry", NULL);
  loop_ms(12000);
  n1 = sim_data_find("obcp/A1B2C3/telemetry", NULL);
  CHECK((n1 - n0) >= 2);

  host_clear();
  host_press(IO_BTN_USER);
  loop_ms(300);
  EXPECT("Bouton User1 : LED éteinte");
  CHECK(io_led_get() == 0);
  CHECK(sim_data_find("obcp/A1B2C3/event", "\"name\":\"User1\",\"count\":1") == 1);
  host_press(IO_BTN_USER);
  loop_ms(300);
  CHECK(io_led_get() == 75);

  cmd("mqtt pub test hello world");
  EXPECT("Publié sur obcp/A1B2C3/test : hello world");
  CHECK(sim_data_find("\"obcp/A1B2C3/test\"", "hello world") == 1);
  cmd("mqtt sub /autre/topic");
  CHECK(sim_cmd_count("AT+MQTTSUB=0,\"autre/topic\",0") == 1);
  mqtt_rx("autre/topic", "salut");
  EXPECT("[MQTT] autre/topic : salut");
  cmd("mqtt status");
  EXPECT("connecté");
  EXPECT("publié(s)");

  printf("- reprises sur incident\n");
  n0 = sim_cmd_count("AT+MQTTCONN=");
  host_clear();
  sim_push("+MQTT:DISCONNECTED,0\r\n");
  loop_ms(1000);
  EXPECT("connexion au broker perdue");
  loop_ms(16000);
  CHECK(sim_cmd_count("AT+MQTTCONN=") == n0 + 1);
  EXPECT("MQTT : connecté à broker.emqx.io");

  n0 = sim_cmd_count("AT+CWJAP=");
  host_clear();
  sim_push("+CW:DISCONNECTED\r\n");
  loop_ms(6000);
  EXPECT("liaison perdue");
  EXPECT("reconnexion automatique");
  CHECK(sim_cmd_count("AT+CWJAP=") == n0 + 1);
  EXPECT("Wi-Fi : connecté à « Lab »");
  loop_ms(3000);
  EXPECT("MQTT : connecté");

  n0 = sim_cmd_count("AT+CWJAP=");
  host_clear();
  sim_push("\r\nready\r\n");
  loop_ms(8000);
  EXPECT("a redémarré de lui-même");
  CHECK(g_app.ncp_ok);
  CHECK(sim_cmd_count("AT+CWJAP=") == n0 + 1);
  EXPECT("Wi-Fi : connecté");
  CHECK(g_app.module_restarts == 1);

  /* ---- mise à jour du module ---- */
  test_ota();

  /* ---- outils ---- */
  printf("- outils avancés et configuration\n");
  cmd("log on");
  cmd("at AT+GMR");
  EXPECT("-> AT+GMR");
  EXPECT("component_version_sdk_2.0.106");
  EXPECT("OK");
  cmd("connect Lab labpass+123");
  REJECT("labpass+123\"");                       /* mot de passe masqué dans le journal */
  EXPECT("AT+CWJAP=\"Lab\",\"***********\"");
  cmd("log off");
  cmd("AT+CWJAP?");
  EXPECT("+CWJAP:\"Net1\"");
  cmd("AT+BAD");
  EXPECT("ERROR");
  cmd("module");
  EXPECT("mission T01");
  cmd("help module");
  EXPECT("module boot");
  cmd("module boot");                            /* passerelle vers QConn_Flash (simulée) */
  EXPECT("Première programmation du module");
  EXPECT("QConn_Flash_Cmd.exe --port COMx");
  EXPECT("Passerelle terminée (terminal rouvert) au bout de 95 s.");
  EXPECT("1489536 octets envoyés au module, 4096 reçus, dernier débit 2000000 bit/s.");
  CHECK(g_host_bridge_runs == 1);
  EXPECT("Firmware     : 2.0.106");               /* module réinitialisé après la passerelle */
  CHECK(g_app.ncp_ok);
  sim_set_magic(0x5501);                         /* firmware trop ancien */
  cmd("module reset");
  EXPECT("magic 0x5501");
  CHECK(!g_app.ncp_ok);
  cmd("module ota");
  EXPECT("première programmation");
  cmd("scan");
  EXPECT("module Wi-Fi non disponible");
  sim_set_magic(0x55AA);
  cmd("module reset");
  EXPECT("OK");
  CHECK(g_app.ncp_ok);
  loop_ms(6000);
  CHECK(netapp_sta_ready());                     /* liaison rétablie après le redémarrage */
  cmd("info");
  EXPECT("Module Wi-Fi   : ");
  EXPECT("Station        : connectée");
  loop_ms(3000);
  CHECK(mqttapp_connected());                    /* MQTT rétabli après la nouvelle connexion */
  cmd("mqtt stop");
  EXPECT("offline");
  CHECK(sim_data_find("\"obcp/A1B2C3/status\",7,1,1", "offline") >= 1);
  cmd("disconnect");
  EXPECT("Station déconnectée");
  CHECK(!netapp_sta_ready());
  loop_ms(15000);
  CHECK(!wifi_is_connected());                   /* pas de reconnexion après disconnect */
  cmd("disconnect");
  EXPECT("déjà déconnectée");
  cmd("mqtt start");
  EXPECT("station non connectée");
  cmd("set wifi.ssid Ma Box");
  EXPECT("wifi.ssid = ");
  CHECK(strcmp(g_cfg.sta_ssid, "Ma Box") == 0);
  cmd("set term.color 0");
  cmd("config");
  EXPECT("********");
  EXPECT("Modifications non enregistrées");
  cmd("save");
  EXPECT("Configuration enregistrée");
  cmd("factory");
  EXPECT("Valeurs d'usine");
  CHECK(g_cfg.sta_ssid[0] == '\0');
  CHECK(config_load() == 0 && strcmp(g_cfg.sta_ssid, "Ma Box") == 0);
  cmd("reboot");
  CHECK(g_host_resets == 1);

  printf("Résultat : %d vérifications réussies, %d échec(s) - journal : build/sim_transcript.txt\n", g_pass, g_fail);
  return (g_fail == 0) ? 0 : 1;
}
