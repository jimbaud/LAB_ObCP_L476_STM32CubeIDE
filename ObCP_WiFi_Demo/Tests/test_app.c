/*
 * Tests sur PC des modules de l'application indépendants du matériel :
 * calendrier / heure d'été, utilitaires (CRC, analyse, formatage, JSON,
 * QR Wi-Fi), analyse HTTP, configuration (accès par clé, stockage), réception
 * YMODEM, couleurs de la LED RVB et accéléromètre LIS3DH (SPI simulé).
 *   make -C Tests && ./Tests/build/test_app
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "timeutil.h"
#include "util.h"
#include "http_parse.h"
#include "cfg_codec.h"
#include "ymodem.h"
#include "ymodem_sender.h"
#include "color.h"
#include "bridge_ctl.h"
#include "lis3dh.h"

static int g_fail, g_pass;

#define CHECK(c) do { if (c) { g_pass++; } else { g_fail++; printf("  ECHEC %s:%d : %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_STR(a, b) do { if (strcmp((a), (b)) == 0) { g_pass++; } else { g_fail++; \
  printf("  ECHEC %s:%d : « %s » au lieu de « %s »\n", __FILE__, __LINE__, (a), (b)); } } while (0)

/* -------------------------------------------------------------------------- */
static int64_t ep(int y, int mo, int d, int h, int mi, int s)
{
  dt_t t = { (uint16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)h, (uint8_t)mi, (uint8_t)s, 0 };
  return dt_to_epoch(&t);
}

static void test_time(void)
{
  dt_t    d;
  int64_t t;
  char    buf[32];
  int     errors = 0;

  printf("- timeutil\n");
  CHECK(ep(1970, 1, 1, 0, 0, 0) == 0);
  CHECK(ep(2000, 3, 1, 0, 0, 0) == 951868800LL);
  CHECK(ep(2026, 10, 23, 12, 34, 56) == 1792758896LL);
  CHECK(ep(2099, 12, 31, 23, 59, 59) == 4102444799LL);

  /* aller-retour sur 130 ans, pas irrégulier */
  for (t = 0; t < 4102444800LL; t += 86400LL * 7 + 3671)
  {
    epoch_to_dt(t, &d);
    if (dt_to_epoch(&d) != t)
    {
      errors++;
    }
  }
  CHECK(errors == 0);
  epoch_to_dt(1792758896LL, &d);
  CHECK(d.year == 2026 && d.mon == 10 && d.day == 23 && d.hour == 12 && d.min == 34 && d.sec == 56);
  CHECK(d.wday == 5);                           /* vendredi */
  CHECK(dt_weekday(2024, 2, 29) == 4);          /* jeudi    */
  CHECK(dt_weekday(1970, 1, 1) == 4);
  epoch_to_dt(951782400LL, &d);                 /* 29/02/2000 (année bissextile séculaire) */
  CHECK(d.mon == 2 && d.day == 29);

  /* heure d'été européenne : bascules à 01:00 UTC */
  CHECK(!eu_dst_active(1774746000LL - 1));      /* 2026-03-29 00:59:59 UTC */
  CHECK(eu_dst_active(1774746000LL));
  CHECK(eu_dst_active(1792890000LL - 1));       /* 2026-10-25 00:59:59 UTC */
  CHECK(!eu_dst_active(1792890000LL));
  CHECK(eu_dst_active(1711846800LL) && !eu_dst_active(1711846800LL - 1));   /* 2024-03-31 */
  CHECK(!eu_dst_active(1729990800LL) && eu_dst_active(1729990800LL - 1));   /* 2024-10-27 */
  CHECK(eu_dst_active(1824944400LL - 1) && !eu_dst_active(1824944400LL));   /* 2027-10-31 */
  CHECK(local_offset_s(ep(2026, 7, 1, 12, 0, 0), 60, true) == 7200);
  CHECK(local_offset_s(ep(2026, 1, 1, 12, 0, 0), 60, true) == 3600);
  CHECK(local_offset_s(ep(2026, 7, 1, 12, 0, 0), 60, false) == 3600);
  CHECK(local_offset_s(ep(2026, 7, 1, 12, 0, 0), -300, false) == -18000);

  d.year = 2026; d.mon = 3; d.day = 5; d.hour = 7; d.min = 8; d.sec = 9;
  dt_format(&d, buf, sizeof(buf));
  CHECK_STR(buf, "2026-03-05 07:08:09");
  CHECK_STR(dt_wday_fr(1), "lundi");
  CHECK_STR(dt_wday_fr(7), "dimanche");
  CHECK_STR(dt_wday_fr(9), "?");
}

/* -------------------------------------------------------------------------- */
static void test_util(void)
{
  uint8_t ip[4];
  long    l;
  bool    b;
  char    buf[256];
  char    line[128];
  char   *argv[6];
  int     n;

  printf("- util\n");
  CHECK(util_crc32("123456789", 9) == 0xCBF43926UL);
  CHECK(util_crc32("", 0) == 0);

  CHECK(util_parse_ip("192.168.1.50", ip) && ip[0] == 192 && ip[3] == 50);
  CHECK(util_parse_ip("0.0.0.0", ip) && ip[0] == 0);
  CHECK(!util_parse_ip("1.2.3", ip));
  CHECK(!util_parse_ip("256.1.1.1", ip));
  CHECK(!util_parse_ip("1.2.3.4.5", ip));
  CHECK(!util_parse_ip("1.2.3.4 ", ip));
  CHECK(!util_parse_ip("a.b.c.d", ip));
  CHECK(!util_parse_ip("1..2.3", ip));
  CHECK(!util_parse_ip("1234.1.1.1", ip));

  CHECK(util_parse_long("-720", &l) && l == -720);
  CHECK(!util_parse_long("12a", &l));
  CHECK(!util_parse_long("", &l));
  CHECK(util_parse_bool("oui", &b) && b);
  CHECK(util_parse_bool("OFF", &b) && !b);
  CHECK(util_parse_bool("1", &b) && b);
  CHECK(!util_parse_bool("peut-être", &b));
  CHECK(util_strieq("Wifi.SSID", "wifi.ssid"));
  CHECK(!util_strieq("wifi", "wifi.ssid"));

  util_fmt_fixed(buf, sizeof(buf), 245, 1);   CHECK_STR(buf, "24.5");
  util_fmt_fixed(buf, sizeof(buf), -5, 1);    CHECK_STR(buf, "-0.5");
  util_fmt_fixed(buf, sizeof(buf), 329, 2);   CHECK_STR(buf, "3.29");
  util_fmt_fixed(buf, sizeof(buf), 0, 1);     CHECK_STR(buf, "0.0");
  util_fmt_fixed(buf, sizeof(buf), -1234, 2); CHECK_STR(buf, "-12.34");
  util_fmt_fixed(buf, sizeof(buf), 7, 0);     CHECK_STR(buf, "7");
  util_fmt_fixed(buf, sizeof(buf), 305, 2);   CHECK_STR(buf, "3.05");
  util_fmt_uptime(buf, sizeof(buf), 3725);    CHECK_STR(buf, "01:02:05");
  util_fmt_uptime(buf, sizeof(buf), 90061);   CHECK_STR(buf, "1 j 01:01:01");
  util_fmt_ip(buf, sizeof(buf), (const uint8_t *)"\x0a\x13\x60\x01"); CHECK_STR(buf, "10.19.96.1");
  util_fmt_mac(buf, sizeof(buf), (const uint8_t *)"\xde\xad\xbe\xef\x00\x01"); CHECK_STR(buf, "DE:AD:BE:EF:00:01");

  util_json_escape(buf, sizeof(buf), "a\"b\\c\nd"); CHECK_STR(buf, "a\\\"b\\\\c\\u000ad");
  util_html_escape(buf, sizeof(buf), "<a href='x'>&</a>"); CHECK_STR(buf, "&lt;a href=&#39;x&#39;&gt;&amp;&lt;/a&gt;");
  CHECK(util_json_escape(buf, 4, "abcdef") == 3 && strcmp(buf, "abc") == 0);   /* troncature sûre */

  strcpy(line, "  connect \"Ma Box\"   motdepasse  ");
  n = util_tokenize(line, argv, 6);
  CHECK(n == 3);
  CHECK_STR(argv[0], "connect");
  CHECK_STR(argv[1], "Ma Box");
  CHECK_STR(argv[2], "motdepasse");
  strcpy(line, "set wifi.pass \"\"");
  n = util_tokenize(line, argv, 6);
  CHECK(n == 3 && argv[2][0] == '\0');
  strcpy(line, "a b c d e f g");
  CHECK(util_tokenize(line, argv, 6) == -1);
  strcpy(line, "   ");
  CHECK(util_tokenize(line, argv, 6) == 0);
  strcpy(line, "x \"non termine");
  n = util_tokenize(line, argv, 6);
  CHECK(n == 2 && strcmp(argv[1], "non termine") == 0);

  CHECK(util_rssi_quality(-40) == 100);
  CHECK(util_rssi_quality(-75) == 50);
  CHECK(util_rssi_quality(-105) == 0);
  CHECK_STR(util_rssi_label(-55), "très bon");
  util_bar(buf, sizeof(buf), 50, 0, 100, 10);
  CHECK(strlen(buf) == 30 && util_utf8_len(buf) == 10);
  CHECK(strncmp(buf, "\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x91", 18) == 0);
  util_bar(buf, 10, 50, 0, 100, 10);          /* tampon trop petit : tronqué proprement */
  CHECK(strlen(buf) == 9);
  CHECK(util_utf8_len("Température") == 11);
  CHECK(util_channel_mhz(1) == 2412 && util_channel_mhz(13) == 2472 && util_channel_mhz(14) == 2484);
  CHECK(util_channel_mhz(36) == 0);
  CHECK(util_isqrt(0) == 0 && util_isqrt(1) == 1 && util_isqrt(15) == 3 && util_isqrt(16) == 4);
  CHECK(util_isqrt(1000000000000ULL) == 1000000UL);

  CHECK(util_wifi_qr(buf, sizeof(buf), "ST67-Demo-A1B2C3", "st67demo") > 0);
  CHECK_STR(buf, "WIFI:T:WPA;S:ST67-Demo-A1B2C3;P:st67demo;;");
  CHECK(util_wifi_qr(buf, sizeof(buf), "a;b,c", "p:\"\\") > 0);
  CHECK_STR(buf, "WIFI:T:WPA;S:a\\;b\\,c;P:p\\:\\\"\\\\;;");
  CHECK(util_wifi_qr(buf, sizeof(buf), "Libre", "") > 0);
  CHECK_STR(buf, "WIFI:T:nopass;S:Libre;;");
  CHECK(util_wifi_qr(buf, 20, "ST67-Demo-A1B2C3", "st67demo") == 0 && buf[0] == '\0');
}

static void test_json(void)
{
  char   buf[128];
  jbuf_t j;
  int    i;

  printf("- json\n");
  jb_init(&j, buf, sizeof(buf));
  jb_open(&j, '{');
  jb_kv_int(&j, "a", -1);
  jb_kv_str(&j, "b", "x\"y");
  jb_key(&j, "c");
  jb_open(&j, '[');
  for (i = 0; i < 2; i++)
  {
    jb_item(&j);
    jb_open(&j, '{');
    jb_kv_bool(&j, "d", i == 0);
    jb_close(&j, '}');
  }
  jb_close(&j, ']');
  jb_kv_raw(&j, "e", "null");
  jb_close(&j, '}');
  CHECK(!j.overflow);
  CHECK_STR(buf, "{\"a\":-1,\"b\":\"x\\\"y\",\"c\":[{\"d\":true},{\"d\":false}],\"e\":null}");
  CHECK(j.pos == strlen(buf));

  jb_init(&j, buf, 10);
  jb_open(&j, '{');
  jb_kv_str(&j, "long", "abcdefghij");
  CHECK(j.overflow);
  CHECK(strlen(buf) < 10);
}

/* -------------------------------------------------------------------------- */
static void test_http(void)
{
  http_req_t  r;
  char        v[64];
  const char *get = "GET /api/led?v=50&x HTTP/1.1\r\nHost: 10.19.96.1\r\nAccept: */*\r\n\r\n";
  const char *post = "POST /api/wifi HTTP/1.1\r\nHost: a\r\ncontent-length: 33\r\n"
                     "Content-Type: application/x-www-form-urlencoded\r\n\r\nssid=Ma+Box&pass=p%40ss%3B&save=1";
  size_t      plen = strlen(post);

  printf("- http_parse\n");
  CHECK(http_parse_request(get, strlen(get), 1536, &r) == HTTP_PARSE_OK);
  CHECK(r.method == HTTP_GET);
  CHECK_STR(r.path, "/api/led");
  CHECK_STR(r.query, "v=50&x");
  CHECK(r.total_len == strlen(get));
  CHECK(http_get_param(r.query, "v", v, sizeof(v)) && strcmp(v, "50") == 0);
  CHECK(http_get_param(r.query, "x", v, sizeof(v)) && v[0] == '\0');
  CHECK(!http_get_param(r.query, "y", v, sizeof(v)));
  CHECK(!http_get_param(r.query, "", v, sizeof(v)));

  CHECK(http_parse_request(get, 20, 1536, &r) == HTTP_PARSE_MORE);
  CHECK(http_parse_request(get, 20, 20, &r) == HTTP_PARSE_TOO_BIG);
  CHECK(http_parse_request("HELLO WORLD\r\n\r\n", 15, 1536, &r) == HTTP_PARSE_BAD);
  CHECK(http_parse_request("GARBAGE\r\nMORE", 13, 1536, &r) == HTTP_PARSE_BAD);
  CHECK(http_parse_request("GET nopath HTTP/1.1\r\n\r\n", 23, 1536, &r) == HTTP_PARSE_BAD);
  CHECK(http_parse_request("GET / HTTP/1.0\r\n\r\n", 18, 1536, &r) == HTTP_PARSE_OK && strcmp(r.path, "/") == 0);
  CHECK(http_parse_request("DELETE /x HTTP/1.1\r\n\r\n", 22, 1536, &r) == HTTP_PARSE_OK && r.method == HTTP_OTHER);
  CHECK(http_parse_request("HEAD /a%20b HTTP/1.1\r\n\r\n", 24, 1536, &r) == HTTP_PARSE_OK);
  CHECK(r.method == HTTP_HEAD && strcmp(r.path, "/a b") == 0);

  CHECK(http_parse_request(post, plen - 5, 1536, &r) == HTTP_PARSE_MORE);
  CHECK(http_parse_request(post, plen, 1536, &r) == HTTP_PARSE_OK);
  CHECK(r.method == HTTP_POST && r.content_length == 33);
  CHECK(http_get_param(r.query, "ssid", v, sizeof(v)) && strcmp(v, "Ma Box") == 0);
  CHECK(http_get_param(r.query, "pass", v, sizeof(v)) && strcmp(v, "p@ss;") == 0);
  CHECK(http_get_param(r.query, "save", v, sizeof(v)) && strcmp(v, "1") == 0);
  CHECK(http_parse_request(post, plen, 100, &r) == HTTP_PARSE_TOO_BIG);

  CHECK(http_url_decode(v, sizeof(v), "%C3%A9t%C3%A9", 13) == 5 && strcmp(v, "été") == 0);
  CHECK(http_url_decode(v, sizeof(v), "100%", 4) == 4 && strcmp(v, "100%") == 0);
  CHECK(http_url_decode(v, sizeof(v), "%zz%4", 5) == 5 && strcmp(v, "%zz%4") == 0);
  CHECK(http_url_decode(v, 4, "abcdef", 6) == 3);
  CHECK_STR(http_status_text(404), "Not Found");
}

/* -------------------------------------------------------------------------- */
static void test_cfg(void)
{
  cfg_t             c;
  cfg_t             c2;
  uint8_t           blob[CFG_BLOB_MAX];
  char              v[80];
  size_t            n;
  size_t            count;
  const cfg_item_t *it;

  printf("- cfg_codec\n");
  cfg_defaults(&c);
  CHECK(c.sta_auto == 1 && c.dhcp == 1 && strcmp(c.country, "EU") == 0);
  CHECK(c.ap_ip[0] == 10 && c.ap_ip[1] == 19 && c.ap_ip[2] == 96 && c.ap_ip[3] == 1);
  CHECK(strlen(c.ap_pass) >= 8 && c.web_port == 80 && c.mqtt_port == 1883);
  CHECK(cfg_items(&count) != NULL && count >= 25);

  CHECK(cfg_set(&c, "wifi.ssid", "Ma Box 5G") == CFG_OK && strcmp(c.sta_ssid, "Ma Box 5G") == 0);
  CHECK(cfg_set(&c, "WIFI.SSID", "Box") == CFG_OK && strcmp(c.sta_ssid, "Box") == 0);
  CHECK(cfg_set(&c, "wifi.ssid", "a\"b") == CFG_ERR_CHARS);
  CHECK(cfg_set(&c, "wifi.ssid", "123456789012345678901234567890123") == CFG_ERR_RANGE);
  CHECK(cfg_set(&c, "wifi.ssid", "12345678901234567890123456789012") == CFG_OK);
  CHECK(cfg_set(&c, "wifi.country", "fr") == CFG_ERR_VALUE);
  CHECK(cfg_set(&c, "wifi.country", "us") == CFG_OK && strcmp(c.country, "US") == 0);
  CHECK(cfg_set(&c, "wifi.country", "EUR") == CFG_ERR_RANGE);
  CHECK(cfg_set(&c, "ap.pass", "court") == CFG_ERR_RANGE);
  CHECK(cfg_set(&c, "ap.pass", "") == CFG_OK);
  CHECK(cfg_set(&c, "ap.pass", "assez long") == CFG_OK);
  CHECK(cfg_set(&c, "time.tz", "-800") == CFG_ERR_RANGE);
  CHECK(cfg_set(&c, "time.tz", "-300") == CFG_OK && c.tz_min == -300);
  CHECK(cfg_set(&c, "time.tz", "x") == CFG_ERR_VALUE);
  CHECK(cfg_set(&c, "ap.channel", "14") == CFG_ERR_RANGE);
  CHECK(cfg_set(&c, "ap.channel", "11") == CFG_OK && c.ap_channel == 11);
  CHECK(cfg_set(&c, "web.port", "8080") == CFG_OK && c.web_port == 8080);
  CHECK(cfg_set(&c, "ip.addr", "300.1.1.1") == CFG_ERR_VALUE);
  CHECK(cfg_set(&c, "ip.addr", "192.168.0.20") == CFG_OK && c.ip[3] == 20);
  CHECK(cfg_set(&c, "ap.ip", "192.168.4.77") == CFG_OK && c.ap_ip[2] == 4 && c.ap_ip[3] == 1);
  CHECK(cfg_set(&c, "mqtt.topic", "maison/#") == CFG_ERR_CHARS);
  CHECK(cfg_set(&c, "mqtt.topic", "maison/labo") == CFG_OK);
  CHECK(cfg_set(&c, "mqtt.host", "") == CFG_ERR_RANGE);
  CHECK(cfg_set(&c, "term.color", "non") == CFG_OK && c.color == 0);
  CHECK(cfg_set(&c, "term.color", "peut-être") == CFG_ERR_VALUE);
  CHECK(cfg_set(&c, "inconnu", "1") == CFG_ERR_KEY);
  CHECK(cfg_set(&c, "wifi.pass", "secret\x01") == CFG_ERR_CHARS);
  CHECK(cfg_set(&c, "wifi.pass", "un mot de passe") == CFG_OK);

  it = cfg_find("wifi.pass");
  CHECK(it != NULL);
  cfg_get(&c, it, v, sizeof(v), true);
  CHECK_STR(v, "********");
  cfg_get(&c, it, v, sizeof(v), false);
  CHECK_STR(v, "un mot de passe");
  cfg_get(&c, cfg_find("time.tz"), v, sizeof(v), true);
  CHECK_STR(v, "-300");
  cfg_get(&c, cfg_find("ap.ip"), v, sizeof(v), true);
  CHECK_STR(v, "192.168.4.1");
  cfg_get(&c, cfg_find("term.color"), v, sizeof(v), true);
  CHECK_STR(v, "0");
  CHECK(strlen(cfg_err_str(CFG_ERR_CHARS)) > 0);

  /* stockage */
  n = cfg_pack(&c, blob, sizeof(blob));
  CHECK(n > 0 && (n % 8) == 0);
  memset(&c2, 0, sizeof(c2));
  CHECK(cfg_unpack(&c2, blob, n) == CFG_UNPACK_OK);
  CHECK(memcmp(&c, &c2, sizeof(c)) == 0);
  blob[20] ^= 0x40;
  CHECK(cfg_unpack(&c2, blob, n) == CFG_UNPACK_CRC);
  blob[20] ^= 0x40;
  blob[4] = 99;                                  /* version */
  CHECK(cfg_unpack(&c2, blob, n) == CFG_UNPACK_VERSION);
  memset(blob, 0xFF, sizeof(blob));              /* flash vierge */
  CHECK(cfg_unpack(&c2, blob, sizeof(blob)) == CFG_UNPACK_EMPTY);
  CHECK(cfg_pack(&c, blob, 16) == 0);

  /* format version 2 (programme 1.2) : web.auto ; configurations plus anciennes relues */
  CHECK(cfg_set(&c, "web.auto", "1") == CFG_OK && c.web_auto == 1);
  CHECK(cfg_set(&c, "mqtt.auto", "oui") == CFG_OK && c.mqtt_auto == 1);
  n = cfg_pack(&c, blob, sizeof(blob));
  CHECK((blob[4] == CFG_VERSION) && (CFG_VERSION == 2));
  CHECK(cfg_unpack(&c2, blob, n) == CFG_UNPACK_OK && c2.web_auto == 1);
  {
    uint16_t ver = 1U;                           /* enregistrée par la version 1.0 ou 1.1 */
    uint16_t size = (uint16_t)offsetof(cfg_t, web_auto);
    uint32_t crc;

    memcpy(&blob[4], &ver, 2);
    CHECK(cfg_unpack(&c2, blob, n) == CFG_UNPACK_OK);
    CHECK(c2.web_auto == 0 && c2.mqtt_auto == 1 && strcmp(c2.sta_pass, "un mot de passe") == 0);
    memcpy(&blob[6], &size, 2);                  /* format 1 plus court : fin complétée */
    crc = util_crc32(&blob[16], size);
    memcpy(&blob[8], &crc, 4);
    CHECK(cfg_unpack(&c2, blob, n) == CFG_UNPACK_OK);
    CHECK(c2.web_auto == 0 && c2.color == 0 && c2.web_port == 8080);
    ver = 2U;                                    /* version courante : taille exacte exigée */
    memcpy(&blob[4], &ver, 2);
    CHECK(cfg_unpack(&c2, blob, n) == CFG_UNPACK_VERSION);
    ver = 0U;
    memcpy(&blob[4], &ver, 2);
    CHECK(cfg_unpack(&c2, blob, n) == CFG_UNPACK_VERSION);
    ver = 1U;
    size = (uint16_t)(sizeof(cfg_t) + 8U);      /* plus long que la structure : refusé */
    memcpy(&blob[4], &ver, 2);
    memcpy(&blob[6], &size, 2);
    CHECK(cfg_unpack(&c2, blob, sizeof(blob)) == CFG_UNPACK_VERSION);
  }
  cfg_get(&c, cfg_find("web.auto"), v, sizeof(v), true);
  CHECK_STR(v, "1");

  /* remise en ordre d'une configuration abîmée */
  cfg_defaults(&c);
  c.ap_channel = 99;
  memset(c.sta_ssid, 'A', sizeof(c.sta_ssid));
  c.mqtt_host[0] = '\0';
  c.color = 7;
  cfg_sanitize(&c);
  CHECK(c.ap_channel == 6);
  CHECK(strlen(c.sta_ssid) == 32);
  CHECK(strcmp(c.mqtt_host, "broker.emqx.io") == 0);
  CHECK(c.color == 1);
}

/* ---- YMODEM : récepteur face à l'émetteur simulé ------------------------------ */
static struct
{
  uint8_t  q[16384];
  size_t   h, t;
  uint32_t now;
  ys_t     ys;
  bool     sender_on;
  int      sender_stop_after;     /* l'émetteur se tait après n blocs (0 : jamais) */
  uint8_t  rx[200000];
  size_t   rx_len;
  char     name[64];
  uint32_t size;
  bool     refuse_file;
  size_t   refuse_at;
  int      n_c, n_ack, n_nak, n_can;
} Y;

static void y_emit(const uint8_t *d, size_t n, void *ctx)
{
  size_t i;
  (void)ctx;
  for (i = 0; i < n; i++)
  {
    Y.q[Y.h] = d[i];
    Y.h = (Y.h + 1) % sizeof(Y.q);
  }
}

static int y_getc(uint32_t timeout_ms, void *io)
{
  int c;
  (void)io;
  if (Y.h == Y.t)
  {
    Y.now += timeout_ms;           /* rien n'arrive pendant timeout_ms */
    return -1;
  }
  c = Y.q[Y.t];
  Y.t = (Y.t + 1) % sizeof(Y.q);
  return c;
}

static void y_putc(uint8_t c, void *io)
{
  (void)io;
  if (c == 'C') Y.n_c++;
  if (c == 0x06) Y.n_ack++;
  if (c == 0x15) Y.n_nak++;
  if (c == 0x18) Y.n_can++;
  if (Y.sender_on)
  {
    if ((Y.sender_stop_after > 0) && (Y.ys.blocks_sent >= Y.sender_stop_after))
    {
      return;                      /* émetteur devenu muet */
    }
    ys_on_byte(&Y.ys, c);
  }
}

static uint32_t y_tick(void *io)
{
  (void)io;
  return Y.now++;
}

static int y_on_file(const char *name, uint32_t size, void *ctx)
{
  (void)ctx;
  snprintf(Y.name, sizeof(Y.name), "%s", name);
  Y.size = size;
  return Y.refuse_file ? -1 : 0;
}

static int y_on_data(const uint8_t *d, uint32_t n, void *ctx)
{
  (void)ctx;
  if ((Y.refuse_at > 0) && ((Y.rx_len + n) > Y.refuse_at))
  {
    return -1;
  }
  memcpy(&Y.rx[Y.rx_len], d, n);
  Y.rx_len += n;
  return 0;
}

static uint8_t g_file[60000];

static void y_reset(size_t size, const char *name)
{
  size_t i;
  memset(&Y, 0, sizeof(Y));
  for (i = 0; i < size; i++)
  {
    g_file[i] = (uint8_t)((i * 7u) ^ (i >> 5));
  }
  ys_init(&Y.ys, name, g_file, size, y_emit, NULL);
  Y.sender_on = true;
}

static int y_run(ymodem_result_t *res)
{
  ymodem_cfg_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.getc = y_getc;
  cfg.putc = y_putc;
  cfg.tick = y_tick;
  cfg.on_file = y_on_file;
  cfg.on_data = y_on_data;
  cfg.start_timeout_ms = 60000;
  return ymodem_receive(&cfg, res);
}

static void test_ymodem(void)
{
  ymodem_result_t r;
  int             st;

  printf("- YMODEM (réception, erreurs injectées)\n");
  CHECK(ymodem_crc16((const uint8_t *)"123456789", 9) == 0x31C3);   /* CRC-16/XMODEM */
  CHECK(ys_crc16((const uint8_t *)"123456789", 9) == 0x31C3);

  /* transfert normal, blocs de 1 Ko, taille non multiple */
  y_reset(5000, "st67w611m_mission_t01_v2.0.106.xz.ota");
  st = y_run(&r);
  CHECK(st == YM_OK);
  CHECK(strcmp(r.name, "st67w611m_mission_t01_v2.0.106.xz.ota") == 0);
  CHECK(r.size == 5000 && r.received == 5000 && r.blocks == 5);
  CHECK((Y.rx_len == 5000) && (memcmp(Y.rx, g_file, 5000) == 0));
  CHECK(Y.ys.finished_ok);
  CHECK(r.retries == 0);

  /* blocs de 128 octets */
  y_reset(1000, "a.ota");
  Y.ys.use_1k = false;
  st = y_run(&r);
  CHECK(st == YM_OK && r.blocks == 8 && Y.rx_len == 1000 && memcmp(Y.rx, g_file, 1000) == 0);

  /* taille non annoncée : le bourrage du dernier bloc est transmis */
  y_reset(5000, "b.ota");
  Y.ys.send_size = false;
  st = y_run(&r);
  CHECK(st == YM_OK && r.size == 0 && Y.rx_len == 5120 && memcmp(Y.rx, g_file, 5000) == 0);
  CHECK(Y.rx[5000] == 0x1A);

  /* CRC faux sur le bloc 3 : redemandé (NAK) puis accepté */
  y_reset(9000, "c.ota");
  Y.ys.corrupt_block = 3;
  st = y_run(&r);
  CHECK(st == YM_OK && Y.rx_len == 9000 && memcmp(Y.rx, g_file, 9000) == 0);
  CHECK(r.retries >= 1 && Y.ys.naks == 1);

  /* ACK perdu : bloc 2 reçu en double, non dupliqué dans le fichier */
  y_reset(9000, "d.ota");
  Y.ys.drop_ack_block = 2;
  st = y_run(&r);
  CHECK(st == YM_OK && Y.rx_len == 9000 && memcmp(Y.rx, g_file, 9000) == 0);
  CHECK(r.retries == 1);

  /* annulation par l'émetteur */
  y_reset(9000, "e.ota");
  Y.ys.cancel_at_block = 3;
  st = y_run(&r);
  CHECK(st == YM_ERR_REMOTE_CANCEL && Y.rx_len == 2048);

  /* fichier refusé dès le bloc 0, puis en cours de route */
  y_reset(3000, "f.bin");
  Y.refuse_file = true;
  st = y_run(&r);
  CHECK(st == YM_ERR_SINK && Y.ys.cancelled_by_receiver && Y.rx_len == 0);
  y_reset(9000, "g.ota");
  Y.refuse_at = 4000;
  st = y_run(&r);
  CHECK(st == YM_ERR_SINK && Y.ys.cancelled_by_receiver && Y.n_can >= 2);

  /* émetteur XMODEM (pas de bloc 0) */
  y_reset(3000, "h.ota");
  Y.ys.xmodem = true;
  st = y_run(&r);
  CHECK(st == YM_ERR_NOTYMODEM);

  /* aucun émetteur : délai dépassé, 'C' répété toutes les 2 s */
  y_reset(100, "i.ota");
  Y.sender_on = false;
  st = y_run(&r);
  CHECK(st == YM_ERR_NOSTART);
  CHECK(Y.n_c >= 25 && Y.n_c <= 32);

  /* annulation au clavier avant le début (fin de ligne ignorée) */
  y_reset(100, "j.ota");
  Y.sender_on = false;
  y_emit((const uint8_t *)"\n\rq", 3, NULL);
  st = y_run(&r);
  CHECK(st == YM_ERR_USER_CANCEL);

  /* émetteur muet en plein transfert */
  y_reset(9000, "k.ota");
  Y.sender_stop_after = 3;
  st = y_run(&r);
  CHECK(st == YM_ERR_TIMEOUT && Y.n_can >= 2);

  /* taille annoncée supérieure aux données envoyées */
  y_reset(4000, "l.ota");
  Y.ys.announce = 5000;
  st = y_run(&r);
  CHECK(st == YM_ERR_SIZE && r.size == 5000 && r.received == 4096);   /* bourrage compris */
  CHECK(strcmp(ymodem_status_str(YM_ERR_NOSTART), "aucun transfert reçu (délai dépassé)") == 0);
}

/* -------------------------------------------------------------------------- */
static void test_color(void)
{
  uint8_t rgb[3];
  char    hex[8];

  printf("- couleurs de la LED RVB\n");
  CHECK(color_parse("rouge", rgb) && (rgb[0] == 255U) && (rgb[1] == 0U) && (rgb[2] == 0U));
  CHECK(color_parse("Blue", rgb) && (rgb[2] == 255U) && (rgb[0] == 0U));
  CHECK(color_parse("  VERT", rgb) && (rgb[1] == 255U));
  CHECK(color_parse("#ff8000", rgb) && (rgb[0] == 255U) && (rgb[1] == 128U) && (rgb[2] == 0U));
  CHECK(color_parse("0A141E", rgb) && (rgb[0] == 10U) && (rgb[1] == 20U) && (rgb[2] == 30U));
  CHECK(color_parse("10,20,30", rgb) && (rgb[0] == 10U) && (rgb[1] == 20U) && (rgb[2] == 30U));
  CHECK(color_parse("255; 0 ;7", rgb) && (rgb[2] == 7U));
  CHECK(!color_parse("256,0,0", rgb));
  CHECK(!color_parse("10,20", rgb));
  CHECK(!color_parse("#12345", rgb));
  CHECK(!color_parse("#1234567", rgb));
  CHECK(!color_parse("#GG0000", rgb));
  CHECK(!color_parse("chartreuse", rgb));
  CHECK(!color_parse("", rgb));
  rgb[0] = 255U; rgb[1] = 0U; rgb[2] = 255U;
  CHECK_STR(color_name(rgb), "magenta");
  rgb[1] = 1U;
  CHECK(color_name(rgb) == NULL);
  color_hex(rgb, hex);
  CHECK_STR(hex, "#FF01FF");
  /* dégradé de qualité : rouge -> jaune -> vert */
  color_quality(0U, rgb);
  CHECK((rgb[0] == 255U) && (rgb[1] == 0U) && (rgb[2] == 0U));
  color_quality(50U, rgb);
  CHECK((rgb[0] == 255U) && (rgb[1] == 255U));
  color_quality(100U, rgb);
  CHECK((rgb[0] == 0U) && (rgb[1] == 255U));
  color_quality(200U, rgb);
  CHECK((rgb[0] == 0U) && (rgb[1] == 255U));
  /* rapport cyclique avec correction gamma 2, période 1000 */
  CHECK(color_duty(255U, 100U, 1000U) == 1000U);
  CHECK(color_duty(255U, 50U, 1000U) == 500U);
  CHECK(color_duty(0U, 100U, 1000U) == 0U);
  CHECK(color_duty(128U, 100U, 1000U) == 251U);      /* (128/255)^2 = 0,252 */
  CHECK(color_duty(255U, 0U, 1000U) == 0U);
  CHECK(color_duty(255U, 150U, 1000U) == 1000U);     /* luminosité bornée à 100 % */
}

/* Accéléromètres simulés : registres et protocole SPI.
 *   LIS3DH  : bit 7 = lecture, bit 6 = auto-incrément, adresse sur 6 bits
 *   LIS3DSH : bit 7 = lecture, adresse sur 7 bits, auto-incrément si ADD_INC (CTRL_REG6) */
static uint8_t g_acc_regs[128];
static int     g_acc_xfers;
static bool    g_acc_dsh;                  /* protocole du LIS3DSH */
static bool    g_acc_ro;                   /* écritures ignorées (capteur défaillant) */
static uint8_t g_acc_last_cmd;

static int fake_acc_xfer(uint8_t *buf, size_t n, void *ctx)
{
  bool    rd   = (buf[0] & 0x80U) != 0U;
  uint8_t mask = g_acc_dsh ? 0x7FU : 0x3FU;
  uint8_t addr = buf[0] & mask;
  bool    inc  = g_acc_dsh ? ((g_acc_regs[LIS3DSH_CTRL_REG6] & 0x10U) != 0U) : ((buf[0] & 0x40U) != 0U);
  size_t  i;

  (void)ctx;
  g_acc_xfers++;
  g_acc_last_cmd = buf[0];
  for (i = 1U; i < n; i++)
  {
    uint8_t a = (uint8_t)((addr + (inc ? (i - 1U) : 0U)) & mask);
    if (rd)
    {
      buf[i] = g_acc_regs[a];
    }
    else if (!g_acc_ro)
    {
      g_acc_regs[a] = buf[i];
    }
  }
  return 0;
}

static void put16(uint8_t reg, int16_t v)
{
  g_acc_regs[reg] = (uint8_t)((uint16_t)v & 0xFFU);
  g_acc_regs[reg + 1U] = (uint8_t)((uint16_t)v >> 8);
}

static void test_lis3dh(void)
{
  lis3dh_t d;
  int16_t  mg[3];
  int16_t  pitch;
  int16_t  roll;

  printf("- accéléromètre LIS3DH ou LIS3DSH (SPI simulé)\n");
  memset(g_acc_regs, 0, sizeof(g_acc_regs));
  g_acc_dsh = false;
  g_acc_regs[LIS3DH_WHO_AM_I] = 0x00U;               /* aucun capteur (MISO à 0) */
  CHECK(lis3dh_init(&d, fake_acc_xfer, NULL) != 0);
  CHECK(!d.ok && (d.who == 0x00U) && (d.model == LIS3DX_NONE));
  CHECK(lis3dh_name(&d) == NULL);
  CHECK(lis3dh_read_mg(&d, mg) != 0);
  g_acc_regs[LIS3DH_WHO_AM_I] = 0xFFU;               /* MISO en l'air */
  CHECK(lis3dh_init(&d, fake_acc_xfer, NULL) != 0);
  g_acc_regs[LIS3DH_WHO_AM_I] = LIS3DH_WHO_AM_I_VALUE;
  g_acc_ro = true;                                    /* configuration refusée */
  CHECK(lis3dh_init(&d, fake_acc_xfer, NULL) != 0);
  CHECK(!d.ok && (d.model == LIS3DX_NONE));
  g_acc_ro = false;
  CHECK(lis3dh_init(&d, fake_acc_xfer, NULL) == 0);
  CHECK(d.ok && (d.who == 0x33U) && (d.model == LIS3DX_LIS3DH));
  CHECK((lis3dh_name(&d) != NULL) && (strcmp(lis3dh_name(&d), "LIS3DH") == 0));
  CHECK(g_acc_regs[LIS3DH_CTRL_REG1] == 0x47U);      /* 50 Hz, X Y Z */
  CHECK(g_acc_regs[LIS3DH_CTRL_REG4] == 0x88U);      /* BDU, haute résolution, ±2 g */
  /* conversion : 12 bits justifiés à gauche, 1 mg par pas */
  CHECK(lis3dh_raw_to_mg(0x00U, 0x40U) == 1024);
  CHECK(lis3dh_raw_to_mg(0xF0U, 0x3EU) == 1007);
  CHECK(lis3dh_raw_to_mg(0x00U, 0xC0U) == -1024);
  CHECK(lis3dh_raw_to_mg(0x10U, 0x00U) == 1);
  put16(LIS3DH_OUT_X_L, (int16_t)(12 * 16));
  put16((uint8_t)(LIS3DH_OUT_X_L + 2U), (int16_t)(-25 * 16));
  put16((uint8_t)(LIS3DH_OUT_X_L + 4U), (int16_t)(998 * 16));
  g_acc_xfers = 0;
  CHECK(lis3dh_read_mg(&d, mg) == 0);
  CHECK(g_acc_xfers == 1);                            /* une seule rafale de 6 octets */
  CHECK(g_acc_last_cmd == 0xE8U);                     /* lecture + auto-incrément (MS) */
  CHECK((mg[0] == 12) && (mg[1] == -25) && (mg[2] == 998));

  /* LIS3DSH : autre protocole d'adressage et autre échelle (0,06 mg par pas) */
  memset(g_acc_regs, 0, sizeof(g_acc_regs));
  g_acc_dsh = true;
  g_acc_regs[LIS3DH_WHO_AM_I] = LIS3DSH_WHO_AM_I_VALUE;
  g_acc_regs[LIS3DSH_CTRL_REG6] = 0x10U;             /* valeur de démarrage */
  CHECK(lis3dh_init(&d, fake_acc_xfer, NULL) == 0);
  CHECK(d.ok && (d.who == 0x3FU) && (d.model == LIS3DX_LIS3DSH));
  CHECK((lis3dh_name(&d) != NULL) && (strcmp(lis3dh_name(&d), "LIS3DSH") == 0));
  CHECK(g_acc_regs[LIS3DSH_CTRL_REG4] == 0x5FU);     /* 50 Hz, BDU, X Y Z */
  CHECK(g_acc_regs[LIS3DSH_CTRL_REG5] == 0x00U);     /* ±2 g */
  CHECK(g_acc_regs[LIS3DSH_CTRL_REG6] == 0x10U);     /* auto-incrément */
  CHECK(g_acc_regs[0x23] == 0x00U);                   /* CTRL_REG3 : interruptions inactives */
  CHECK(lis3dsh_raw_to_mg(0x1BU, 0x41U) == 1000);    /* 16667 x 0,06 = 1000,02 mg */
  CHECK(lis3dsh_raw_to_mg(0xE5U, 0xBEU) == -1000);
  CHECK(lis3dsh_raw_to_mg(0x00U, 0x40U) == 983);     /* 16384 x 0,06 = 983,04 */
  CHECK(lis3dsh_raw_to_mg(0x09U, 0x00U) == 1);       /* 0,54 -> 1 */
  CHECK(lis3dsh_raw_to_mg(0x08U, 0x00U) == 0);       /* 0,48 -> 0 */
  CHECK(lis3dsh_raw_to_mg(0xFFU, 0x7FU) == 1966);    /* pleine échelle */
  CHECK(lis3dsh_raw_to_mg(0x00U, 0x80U) == -1966);
  put16(LIS3DH_OUT_X_L, 200);                         /* 12 mg */
  put16((uint8_t)(LIS3DH_OUT_X_L + 2U), -417);        /* -25,02 mg */
  put16((uint8_t)(LIS3DH_OUT_X_L + 4U), 16633);       /* 997,98 mg */
  g_acc_xfers = 0;
  CHECK(lis3dh_read_mg(&d, mg) == 0);
  CHECK(g_acc_xfers == 1);
  CHECK(g_acc_last_cmd == 0xA8U);                     /* bit 6 à 0 : il ferait partie de l'adresse */
  CHECK((mg[0] == 12) && (mg[1] == -25) && (mg[2] == 998));
  g_acc_dsh = false;
  /* inclinaisons */
  lis3dh_tilt(mg, &pitch, &roll);
  CHECK((pitch == -7) && (roll == -14));
  mg[0] = -1000; mg[1] = 0; mg[2] = 0;                /* axe X vers le haut : tangage +90° */
  lis3dh_tilt(mg, &pitch, &roll);
  CHECK(pitch == 900);
  mg[0] = 0; mg[1] = 707; mg[2] = 707;                /* roulis de 45° */
  lis3dh_tilt(mg, &pitch, &roll);
  CHECK((pitch == 0) && (roll == 450));
  mg[0] = 0; mg[1] = 0; mg[2] = -1000;                /* carte retournée */
  lis3dh_tilt(mg, &pitch, &roll);
  CHECK((roll == 1800) || (roll == -1800));
}


/* ---- Fin de la passerelle « module boot » ------------------------------------------ */
static bridge_ctl_t g_bc;
static uint32_t     g_bt;                 /* temps simulé (ms) */

/* Avance le temps de ms millisecondes par pas de 1 ms ; in / out : octets par ms.
 * Renvoie la première fin rencontrée (BRIDGE_END_NONE si aucune). */
static bridge_end_t bc_run(uint32_t ms, bool dtr, uint32_t in, uint32_t out, bool button)
{
  uint32_t i;
  for (i = 0U; i < ms; i++)
  {
    bridge_end_t e;
    g_bt++;
    e = bridge_ctl_step(&g_bc, g_bt, dtr, in, out, button);
    if (e != BRIDGE_END_NONE)
    {
      return e;
    }
  }
  return BRIDGE_END_NONE;
}

static void test_bridge(void)
{
  uint32_t t_end;

  printf("- passerelle module boot : fin de session\n");
  /* 1. Cas nominal : terminal fermé, QConn_Flash garde DTR à 1, effacement silencieux,
   *    puis referme le port ; l'utilisateur rouvre le terminal 5 s plus tard. */
  g_bt = 1000U;
  bridge_ctl_init(&g_bc, true, g_bt);
  CHECK(g_bc.phase == BRIDGE_WAIT_CLOSE);
  CHECK(bc_run(3000U, true, 0U, 0U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(10000U, false, 0U, 0U, false) == BRIDGE_END_NONE);     /* terminal fermé */
  CHECK(g_bc.phase == BRIDGE_WAIT_OPEN);
  CHECK(bc_run(1U, true, 0U, 0U, false) == BRIDGE_END_NONE);          /* QConn_Flash ouvre le port */
  CHECK(g_bc.phase == BRIDGE_ACTIVE);
  CHECK(bc_run(5000U, true, 50U, 2U, false) == BRIDGE_END_NONE);      /* poignée de main, données */
  CHECK(bc_run(55000U, true, 0U, 0U, false) == BRIDGE_END_NONE);      /* effacement : 55 s de silence */
  CHECK(bc_run(8000U, true, 200U, 1U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(5000U, false, 0U, 0U, false) == BRIDGE_END_NONE);      /* port refermé */
  t_end = g_bt;
  CHECK(bc_run(10U, true, 0U, 0U, false) == BRIDGE_END_REOPENED);     /* terminal rouvert */
  CHECK(g_bt == t_end + 1U);
  CHECK(g_bc.to_module == (5000U * 50U) + (8000U * 200U));
  CHECK(g_bc.from_module == (5000U * 2U) + 8000U);

  /* 2. QConn_Flash laisse DTR à 0 pendant toute la programmation (option do_reset) */
  bridge_ctl_init(&g_bc, false, g_bt);
  CHECK(bc_run(2000U, true, 0U, 0U, false) == BRIDGE_END_NONE);       /* ouverture */
  CHECK(bc_run(50U, false, 0U, 0U, false) == BRIDGE_END_NONE);        /* impulsion de reset */
  CHECK(bc_run(50U, true, 0U, 0U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(4000U, false, 50U, 2U, false) == BRIDGE_END_NONE);     /* DTR à 0 et échanges */
  CHECK(bc_run(60000U, false, 0U, 0U, false) == BRIDGE_END_NONE);     /* effacement silencieux */
  CHECK(bc_run(8000U, false, 200U, 1U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(30000U, false, 0U, 0U, false) == BRIDGE_END_NONE);     /* fin, port refermé */
  CHECK(bc_run(1U, true, 0U, 0U, false) == BRIDGE_END_REOPENED);

  /* 3. Terminal rouvert moins de 2 s après les derniers octets : fin 2 s après eux */
  bridge_ctl_init(&g_bc, false, g_bt);
  CHECK(bc_run(1000U, true, 30U, 3U, false) == BRIDGE_END_NONE);
  t_end = g_bt;
  CHECK(bc_run(300U, false, 0U, 0U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(5000U, true, 0U, 0U, false) == BRIDGE_END_REOPENED);
  CHECK(g_bt == t_end + BRIDGE_REOPEN_QUIET_MS);

  /* 4. Impulsion de DTR pendant la programmation (nouvelle tentative de reset) suivie
   *    d'échanges : pas de fin */
  bridge_ctl_init(&g_bc, false, g_bt);
  CHECK(bc_run(1000U, true, 30U, 3U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(500U, false, 0U, 0U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(100U, true, 0U, 0U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(3000U, true, 30U, 3U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(BRIDGE_IDLE_ACTIVE_MS - 1U, true, 0U, 0U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(1U, true, 0U, 0U, false) == BRIDGE_END_IDLE);           /* 2 min de silence */

  /* 5. Saisie dans le terminal rouvert : pas de fin « terminal rouvert » */
  bridge_ctl_init(&g_bc, false, g_bt);
  CHECK(bc_run(1000U, true, 30U, 3U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(4000U, false, 0U, 0U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(1U, true, 1U, 0U, false) == BRIDGE_END_NONE);          /* frappe au clavier */
  CHECK(bc_run(BRIDGE_IDLE_ACTIVE_MS - 2U, true, 0U, 0U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(10U, true, 0U, 0U, false) == BRIDGE_END_IDLE);

  /* 6. Bouton : un appui bref est ignoré, 50 ms arrêtent la session */
  bridge_ctl_init(&g_bc, true, g_bt);
  CHECK(bc_run(30U, true, 0U, 0U, true) == BRIDGE_END_NONE);
  CHECK(bc_run(10U, true, 0U, 0U, false) == BRIDGE_END_NONE);
  t_end = g_bt;
  CHECK(bc_run(100U, true, 0U, 0U, true) == BRIDGE_END_BUTTON);
  CHECK(g_bt == t_end + BRIDGE_BUTTON_MS + 1U);

  /* 7. « mode COMx » (ouverture sans échange) puis rien : fin après 15 min seulement ;
   *    module muet (pas en mode téléchargement) : idem */
  bridge_ctl_init(&g_bc, false, g_bt);
  CHECK(bc_run(200U, true, 0U, 0U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(5000U, false, 0U, 0U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(1U, true, 0U, 0U, false) == BRIDGE_END_NONE);          /* front montant sans échange */
  CHECK(bc_run(2000U, true, 20U, 0U, false) == BRIDGE_END_NONE);      /* poignée de main sans réponse */
  CHECK(bc_run(10000U, false, 0U, 0U, false) == BRIDGE_END_NONE);
  CHECK(bc_run(1U, true, 0U, 0U, false) == BRIDGE_END_NONE);          /* pas de fin : le module n'a rien dit */
  CHECK(bc_run(BRIDGE_IDLE_WAIT_MS, true, 0U, 0U, false) == BRIDGE_END_IDLE);
}

int main(void)
{
  printf("Tests de l'application (PC)\n");
  test_time();
  test_util();
  test_json();
  test_http();
  test_cfg();
  test_ymodem();
  test_color();
  test_lis3dh();
  test_bridge();
  printf("%d vérifications réussies, %d échec(s)\n", g_pass, g_fail);
  return (g_fail == 0) ? 0 : 1;
}
