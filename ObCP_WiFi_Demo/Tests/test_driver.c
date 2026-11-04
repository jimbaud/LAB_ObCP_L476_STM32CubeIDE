/*
 * Tests sur PC du pilote ST67W6X_Lite face au module simulé (sim_module.c).
 *   make -C Tests && ./Tests/build/test_driver
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ncp.h"
#include "sim_module.h"

static int g_fail, g_pass;

#define CHECK(c) do { if (c) { g_pass++; } else { g_fail++; printf("  ECHEC %s:%d : %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- journal AT (option -v) ------------------------------------------------ */
static int g_verbose;
void ncp_at_log_hook(const char *dir, const char *text, size_t len)
{
  if (g_verbose)
  {
    printf("    %s %.*s\n", dir, (int)len, text);
  }
}

static char     g_topic[128];
static uint8_t  g_msg[512];
static uint16_t g_msg_len;
static int      g_msg_count;

static void on_mqtt(const char *topic, const uint8_t *msg, uint16_t len)
{
  strncpy(g_topic, topic, sizeof(g_topic) - 1);
  memcpy(g_msg, msg, len);
  g_msg_len = len;
  g_msg_count++;
}

static int g_ping_ok, g_ping_tmo;
static void on_ping(uint16_t seq, int32_t ms)
{
  (void)seq;
  if (ms >= 0) g_ping_ok++; else g_ping_tmo++;
}

static void test_split(void)
{
  char  s1[] = "\"Open, cafe\",-80,\"aa:bb\",,7";
  char  s2[] = "";
  char  s3[] = "0,CONNECTED";
  char *v[8];
  int   n;

  printf("- at_split\n");
  n = at_split(s1, v, 8);
  CHECK(n == 5);
  CHECK(strcmp(v[0], "Open, cafe") == 0);
  CHECK(strcmp(v[1], "-80") == 0);
  CHECK(strcmp(v[2], "aa:bb") == 0);
  CHECK(strcmp(v[3], "") == 0);
  CHECK(strcmp(v[4], "7") == 0);
  n = at_split(s2, v, 8);
  CHECK(n == 1 && v[0][0] == '\0');
  n = at_split(s3, v, 8);
  CHECK(n == 2 && strcmp(v[1], "CONNECTED") == 0);
  {
    uint8_t ip[4], mac[6];
    CHECK(at_parse_ip("\"10.19.96.1\"", ip) && ip[0] == 10 && ip[3] == 1);
    CHECK(!at_parse_ip("10.19.300.1", ip));
    CHECK(at_parse_mac("de:ad:BE:ef:00:01", mac) && mac[2] == 0xBE && mac[5] == 1);
  }
}

static void test_start(void)
{
  ncp_info_t info;
  int        st;

  printf("- démarrage du module\n");
  sim_reset();
  st = ncp_start(&info);
  CHECK(st == AT_OK);
  CHECK(info.ok);
  CHECK(info.net_mode == 1);
  CHECK(info.clock_source == 1);
  CHECK(strncmp(info.at_version, "1.0.0(", 6) == 0);  /* AT 1.0.0, firmware = SDK */
  CHECK(strcmp(info.sdk_version, "2.0.106") == 0);
  CHECK(sim_cmd_count("AT+PWR=0") == 1);
  CHECK(sim_frames_rejected() == 0);
}

static void test_wifi(void)
{
  int         st;
  int16_t     rssi = 0;
  uint8_t     sec = 0, proto = 0, n = 0;
  wifi_link_t link;
  int         i;
  bool        found = false;

  printf("- Wi-Fi : init, scan, connexion\n");
  st = wifi_init("EU");
  CHECK(st == AT_OK);
  CHECK(sim_cmd_count("AT+CWCOUNTRY=0,\"EU\"") == 1);
  CHECK(sim_cmd_count("AT+CWMODE=1,0") == 1);

  st = wifi_scan(false, 0, 10000);
  CHECK(st == AT_OK);
  CHECK(g_wifi.scan_count == 3);
  CHECK(sim_cmd_count("AT+CWLAP=0,,,0") == 1);
  for (i = 0; i < g_wifi.scan_count; i++)
  {
    if (strcmp(g_wifi.scan[i].ssid, "Open, cafe") == 0)
    {
      found = true;
      CHECK(g_wifi.scan[i].rssi == -80);
      CHECK(g_wifi.scan[i].channel == 11);
      CHECK(g_wifi.scan[i].security == WIFI_SEC_OPEN);
      CHECK(g_wifi.scan[i].proto == 7);
      CHECK(g_wifi.scan[i].bssid[5] == 2);
    }
  }
  CHECK(found);
  CHECK(g_wifi.scan[2].security == WIFI_SEC_WPA3 && g_wifi.scan[2].wps == 1);

  st = wifi_connect("Net1", "wrong", true, 0);
  CHECK(st == WIFI_ERR_REASON);
  CHECK(g_wifi.last_reason == 8);

  st = wifi_connect("Net1", "secret123", true, 0);
  CHECK(st == AT_OK);
  CHECK(g_wifi.sta == WIFI_STA_GOT_IP);
  CHECK(wifi_is_connected());

  st = wifi_rssi(&rssi);
  CHECK(st == AT_OK && rssi == -47);
  st = wifi_link_info(&link);
  CHECK(st == AT_OK && link.channel == 6 && strcmp(link.ssid, "Net1") == 0);
  st = wifi_conn_details(&sec, &proto);
  CHECK(st == AT_OK && sec == 3 && proto == 4);

  CHECK(wifi_connect("Bad\"ssid", "x", true, 0) == WIFI_ERR_CHARS);

  st = wifi_ap_start("L476-Demo", "stm32demo", 6, 4);
  CHECK(st == AT_OK && g_wifi.ap_running);
  CHECK(sim_cmd_count("AT+CWMODE=3,1") == 1);
  CHECK(sim_cmd_count("AT+CWSAP=\"L476-Demo\",\"stm32demo\",6,3,4,0") == 1);
  CHECK(wifi_ap_start("x", "court", 6, 4) == AT_PARAM);
  st = wifi_ap_clients(&n);
  CHECK(st == AT_OK && n == 2);

  sim_push("+CW:STA_CONNECTED,\"de:ad:be:ef:00:09\"\r\n+CW:DIST_STA_IP,\"de:ad:be:ef:00:09\",\"10.19.96.4\"\r\n");
  ncp_poll();
  CHECK(g_wifi.evt_ap_join && g_wifi.evt_ap_ip);
  CHECK(strcmp(g_wifi.ap_evt_ip, "10.19.96.4") == 0 && g_wifi.ap_evt_mac[5] == 9);
}

static void test_net(void)
{
  int         st;
  net_ipcfg_t c;
  char        ip[46];
  char        dns[3][16];
  uint8_t     mac[6];
  uint8_t     apip[4];
  net_time_t  t;
  uint16_t    replies = 0;

  printf("- réseau : IP, DNS, ping, SNTP\n");
  st = net_init("L476-WiFi", (const uint8_t[]){ 10, 19, 96, 1 });
  CHECK(st == AT_OK);
  CHECK(sim_cmd_count("AT+CIPRECVMODE=1") == 1);
  CHECK(sim_cmd_count("AT+CIPAP=\"10.19.96.1\",\"10.19.96.1\",\"255.255.255.0\"") == 1);

  st = net_sta_ipcfg(&c);
  CHECK(st == AT_OK && c.ip[3] == 42 && c.gw[3] == 1 && c.mask[0] == 255);
  st = net_ap_ip(apip);
  CHECK(st == AT_OK && apip[0] == 10 && apip[3] == 1);
  st = net_sta_mac(mac);
  CHECK(st == AT_OK && mac[0] == 0x11 && mac[5] == 0x66);
  st = net_dns_servers(dns);
  CHECK(st == AT_OK && strcmp(dns[0], "192.168.1.1") == 0 && strcmp(dns[1], "8.8.8.8") == 0);
  st = net_resolve("example.com", ip, sizeof(ip));
  CHECK(st == AT_OK && strcmp(ip, "93.184.216.34") == 0);

  g_ping_ok = g_ping_tmo = 0;
  st = net_ping("example.com", 4, 1000, on_ping, &replies);
  CHECK(st == AT_OK && replies == 3 && g_ping_ok == 3 && g_ping_tmo == 1);
  sim_set_ping_ok_last(true);
  g_ping_ok = g_ping_tmo = 0;
  st = net_ping("example.com", 4, 1000, on_ping, &replies);
  CHECK(st == AT_OK && replies == 3 && g_ping_ok == 3);
  sim_set_ping_ok_last(false);

  st = net_sntp_time(&t);
  CHECK(st == AT_OK && t.year == 2026 && t.mon == 10 && t.day == 3 && t.hour == 8 && t.wday == 5);
}

static void test_sockets(void)
{
  static uint8_t big[3000];
  uint8_t        rx[600];
  uint16_t       got = 0, dl;
  const uint8_t *d;
  const char     req[] = "GET /api/status HTTP/1.1\r\nHost: 10.19.96.1\r\n\r\n";
  int            st, i;

  printf("- sockets : +CIP / +IPD / CIPRECVDATA / CIPSEND\n");
  st = net_server_start(80, 4);
  CHECK(st == AT_OK);
  CHECK(sim_cmd_count("AT+CIPSERVER=1,80,\"TCP\",0,0") == 1);

  sim_push("+CIP:2,CONNECTED\r\n");
  sim_push("+IPD:2,46,\"10.19.96.2\",51000\r\n");
  ncp_poll();
  CHECK(g_net.link[2].connected);
  CHECK(g_net.link[2].avail == 46);
  CHECK(strcmp(g_net.link[2].rip, "10.19.96.2") == 0 && g_net.link[2].rport == 51000);

  sim_set_socket_data((const uint8_t *)req, (uint16_t)strlen(req));
  st = net_recv(2, rx, sizeof(rx), &got);
  CHECK(st == AT_OK);
  CHECK(got == strlen(req));
  CHECK(memcmp(rx, req, strlen(req)) == 0);
  CHECK(g_net.link[2].avail == 0);

  for (i = 0; i < (int)sizeof(big); i++)
  {
    big[i] = (uint8_t)(i * 7);
  }
  st = net_send(2, big, sizeof(big));
  CHECK(st == AT_OK);
  CHECK(sim_cmd_count("AT+CIPSEND=2,") == 3);
  d = sim_last_data(&dl);
  CHECK(dl == 3000 - 2048);
  CHECK(memcmp(d, &big[2048], dl) == 0);

  st = net_close(2);
  CHECK(st == AT_OK && !g_net.link[2].connected);
  sim_push("+CIP:2,DISCONNECTED\r\n");
  ncp_poll();
  CHECK(!g_net.link[2].connected);
}

static void test_mqtt(void)
{
  const char  part1[] = "+MQTT:SUBRECV:0,12,";
  const char  part2[] = "16,\"st67/abc/c";
  const char  part3[] = "md\",led on,\r\n pwm 50\r\n";
  const char  pub[] = "{\"t\":2431}";
  uint16_t    dl;
  int         st;

  printf("- MQTT : connexion, réception fragmentée, publication\n");
  mqtt_set_msg_cb(on_mqtt);
  st = mqtt_configure(0, "l476-abc", "", "", 60, "st67/abc/status", "offline", true);
  CHECK(st == AT_OK);
  CHECK(sim_cmd_count("AT+MQTTUSERCFG=0,0,\"l476-abc\",\"\",\"\",\"\",\"\",\"\"") == 1);
  st = mqtt_connect("broker.emqx.io", 1883, 0);
  CHECK(st == AT_OK && g_mqtt.connected);

  g_msg_count = 0;
  sim_push(part1);
  sim_push(part2);
  sim_push(part3);
  ncp_poll();
  CHECK(g_msg_count == 1);
  CHECK(strcmp(g_topic, "st67/abc/cmd") == 0);
  CHECK(g_msg_len == 16);
  CHECK(memcmp(g_msg, "led on,\r\n pwm 50", 16) == 0);

  st = mqtt_publish("st67/abc/telemetry", (const uint8_t *)pub, (uint16_t)strlen(pub), 0, false);
  CHECK(st == AT_OK);
  CHECK(memcmp(sim_last_data(&dl), pub, strlen(pub)) == 0 && dl == strlen(pub));

  sim_push("+MQTT:DISCONNECTED,0\r\n");
  ncp_poll();
  CHECK(!g_mqtt.connected && g_mqtt.evt_disconnected);
  CHECK(mqtt_publish("t", (const uint8_t *)"x", 1, 0, false) == MQTT_ERR_NOTCONN);
}

static void test_robustness(void)
{
  int st;
  int before;

  printf("- robustesse : rx_stall, erreurs, longues réponses\n");
  before = sim_cmd_count("AT+CWHOSTNAME");
  sim_stall_next(2);
  st = at_cmd("AT+CWHOSTNAME=\"x\"", NULL, NULL, NULL, 2000);
  CHECK(st == AT_OK);
  CHECK(sim_cmd_count("AT+CWHOSTNAME") == before + 1);   /* reçue une seule fois */
  CHECK(ncp_spi_stats()->stalls >= 1);

  st = at_cmd("AT+BAD", NULL, NULL, NULL, 2000);
  CHECK(st == AT_ERROR);

  {
    ncp_info_t info;
    memset(&info, 0, sizeof(info));
    st = ncp_raw("AT+GMR", NULL, NULL, 2000);
    CHECK(st == AT_OK);
  }
  CHECK(ncp_spi_stats()->hdr_errors == 0);
  CHECK(sim_frames_rejected() == 0);
  CHECK(at_stats()->overflows == 0);
}

/* En-tête réel de st67w611m_mission_t01_v2.0.106.xz.ota (X-CUBE-ST67W61 v1.3.0) */
static const uint8_t k_real_hdr[96] =
{
    0x51, 0x43, 0x43, 0x37, 0x34, 0x58, 0x5F, 0x4F, 0x54, 0x41, 0x56, 0x65, 0x72, 0x31, 0x2E, 0x30,
    0x58, 0x5A, 0x20, 0x20, 0xA8, 0x1F, 0x0D, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x51, 0x43, 0x43, 0x5F, 0x4D, 0x6F, 0x64, 0x75, 0x6C, 0x65, 0x5F, 0x76, 0x31, 0x2E, 0x31, 0x00,
    0x32, 0x2E, 0x30, 0x2E, 0x31, 0x30, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xF2, 0xA0, 0xFB, 0x94, 0x25, 0x77, 0x50, 0x14, 0xF0, 0x5F, 0x05, 0x44, 0x81, 0xF9, 0x26, 0x84,
    0xCA, 0x86, 0x29, 0xB0, 0xBE, 0xB5, 0x12, 0xE6, 0x34, 0xDF, 0x4E, 0xBF, 0x08, 0xFF, 0x19, 0x4A,
};

static uint8_t g_img[300000];

/* Transmet l'image par morceaux de tailles irrégulières (comme les blocs YMODEM) */
static int fwu_send_image(const uint8_t *img, size_t n)
{
  static const uint16_t steps[] = { 1024, 128, 700, 1024, 1, 333, 1024 };
  size_t off = 0;
  int    i = 0;
  int    st = AT_OK;
  while ((off < n) && (st == AT_OK))
  {
    size_t k = steps[i++ % 7];
    if (k > (n - off))
    {
      k = n - off;
    }
    st = ncp_fwu_write(&img[off], (uint32_t)k);
    off += k;
  }
  return st;
}

static void test_fwu(void)
{
  ncp_fwu_header_t h;
  uint8_t          hdr[512];
  const uint8_t   *got;
  size_t           got_len;
  size_t           n;
  int              st;
  int              i;
  ncp_info_t       info;

  printf("- mise à jour OTA du module (AT+OTASTART / AT+OTASEND / AT+OTAFIN)\n");
  /* analyse de l'en-tête réel */
  memset(hdr, 0xFF, sizeof(hdr));
  memcpy(hdr, k_real_hdr, sizeof(k_real_hdr));
  CHECK(ncp_fwu_parse_header(hdr, sizeof(hdr), &h));
  CHECK(strcmp(h.type, "XZ") == 0);
  CHECK(h.length == 860072UL);
  CHECK(strcmp(h.version, "2.0.106") == 0);
  CHECK(strcmp(h.module, "QCC_Module_v1.1") == 0);
  CHECK(h.sha256[0] == 0xF2 && h.sha256[31] == 0x4A);
  hdr[0] = 'X';
  CHECK(!ncp_fwu_parse_header(hdr, sizeof(hdr), &h));              /* signature absente */
  hdr[0] = 'Q';
  memcpy(&hdr[16], "ZIP ", 4);
  CHECK(!ncp_fwu_parse_header(hdr, sizeof(hdr), &h));              /* type inconnu */
  memcpy(&hdr[16], "RAW ", 4);
  hdr[20] = hdr[21] = hdr[22] = hdr[23] = 0;
  CHECK(!ncp_fwu_parse_header(hdr, sizeof(hdr), &h));              /* longueur nulle */
  CHECK(!ncp_fwu_parse_header(hdr, 40, &h));                       /* trop court */

  /* séquence complète sur le module simulé */
  sim_set_version("2.0.97");
  n = sim_make_ota_image(g_img, sizeof(g_img), 100000, "2.0.106", true, 1234);
  CHECK(n == 100512);
  CHECK(ncp_fwu_write(g_img, 10) == NCP_FWU_ERR_STATE);              /* session non ouverte */
  st = ncp_fwu_begin();
  CHECK(st == AT_OK);
  CHECK(sim_ota_active());
  CHECK(fwu_send_image(g_img, n) == AT_OK);
  CHECK(ncp_fwu_end() == AT_OK);
  CHECK(ncp_fwu_sent() == n);
  got = sim_ota_image(&got_len);
  CHECK((got_len == n) && (memcmp(got, g_img, n) == 0));
  CHECK(sim_ota_send_size(0) == 512);                              /* en-tête seul d'abord */
  for (i = 1; i < 49; i++)
  {
    CHECK(sim_ota_send_size(i) == 2048);                           /* puis blocs de 2048 o */
  }
  CHECK(sim_ota_send_size(49) == (uint16_t)(n - 512 - 48 * 2048)); /* reliquat */
  st = ncp_fwu_finish(5000);
  CHECK(st == AT_OK);
  CHECK(sim_ota_count("install") == 1);
  CHECK(strcmp(sim_version(), "2.0.106") == 0);
  CHECK(ncp_start(&info) == AT_OK);
  CHECK(strcmp(info.sdk_version, "2.0.106") == 0);

  /* image corrompue : refusée à AT+OTAFIN, le module garde son firmware */
  n = sim_make_ota_image(g_img, sizeof(g_img), 30000, "2.0.200", false, 99);
  g_img[20000] ^= 0x01;
  CHECK(ncp_fwu_begin() == AT_OK);
  CHECK(fwu_send_image(g_img, n) == AT_OK);
  CHECK(ncp_fwu_end() == AT_OK);
  CHECK(ncp_fwu_finish(5000) == NCP_FWU_ERR_REJECTED);
  CHECK(sim_ota_count("reject") == 1);
  CHECK(strcmp(sim_version(), "2.0.106") == 0);
  CHECK(ncp_fwu_abort() == AT_OK);

  /* abandon en cours de transfert */
  n = sim_make_ota_image(g_img, sizeof(g_img), 20000, "2.0.200", false, 7);
  CHECK(ncp_fwu_begin() == AT_OK);
  CHECK(ncp_fwu_write(g_img, 6000) == AT_OK);
  CHECK(ncp_fwu_abort() == AT_OK);
  CHECK(!sim_ota_active());
  CHECK(sim_ota_count("abort") >= 1);
  CHECK(ncp_fwu_end() == NCP_FWU_ERR_STATE);

  /* image plus courte que l'en-tête */
  CHECK(ncp_fwu_begin() == AT_OK);
  CHECK(ncp_fwu_write(g_img, 100) == AT_OK);
  CHECK(ncp_fwu_end() == NCP_FWU_ERR_STATE);
  (void)ncp_fwu_abort();

  /* variante : « OK » après chaque « Recv n bytes » (confirmations tardives) */
  sim_ota_set_faults(false, false, true);
  n = sim_make_ota_image(g_img, sizeof(g_img), 50000, "2.0.300", true, 5);
  CHECK(ncp_fwu_begin() == AT_OK);
  CHECK(fwu_send_image(g_img, n) == AT_OK);
  CHECK(ncp_fwu_end() == AT_OK);
  got = sim_ota_image(&got_len);
  CHECK((got_len == n) && (memcmp(got, g_img, n) == 0));
  CHECK(ncp_fwu_finish(5000) == AT_OK);
  CHECK(strcmp(sim_version(), "2.0.300") == 0);

  /* module refusant d'ouvrir la session */
  sim_ota_set_faults(true, false, false);
  CHECK(ncp_fwu_begin() == AT_ERROR);
  CHECK(!ncp_fwu_active());
  sim_ota_set_faults(false, false, false);
  CHECK(ncp_start(&info) == AT_OK);
  CHECK(sim_frames_rejected() == 0);
  CHECK(ncp_spi_stats()->hdr_errors == 0);
}

int main(int argc, char **argv)
{
  g_verbose = (argc > 1) && (strcmp(argv[1], "-v") == 0);
  at_set_log(g_verbose != 0);
  printf("Tests du pilote ST67W6X_Lite (module simulé)\n");
  test_split();
  test_start();
  test_wifi();
  test_net();
  test_sockets();
  test_mqtt();
  test_robustness();
  test_fwu();
  printf("Résultat : %d vérifications réussies, %d échecs\n", g_pass, g_fail);
  return (g_fail == 0) ? 0 : 1;
}
