/*
 * Module ST67W611M1 simulé, utilisé pour tester sur PC le pilote ST67W6X_Lite.
 * Implémente ncp_port.h : le pilote dialogue avec ce simulateur exactement
 * comme avec le vrai module (trames SPI, RDY, CS, rx_stall).
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim_module.h"
#include "ncp_port.h"
#include "sha256.h"

#define QMAX 512

typedef struct
{
  uint8_t  d[2100];
  uint16_t n;
} frame_t;

static frame_t  q[QMAX];
static int      qh, qt;
static bool     cs, en, rdy;
static uint32_t tick;
static int      phase;
static frame_t  cur;
static bool     cur_valid;
static uint16_t cur_off;
static int      stall_n;
static bool     stall_this;
static char     hstream[8192];
static int      hlen;
static bool     data_mode;
static int      data_need;
static uint8_t  last_data[8192];
static uint16_t last_data_len;
static char     last_cmd[600];
static char     cmd_log[8000][160];
static int      cmd_log_n;
static bool     ping_ok_last;
static uint8_t  sock_data[4096];
static uint16_t sock_len;
static int      rejected;
static uint16_t sim_magic = 0x55AA;
static bool     realistic;                     /* jeu de données « démonstration » */
static int      rssi_idx;
static char     data_cmd[160];                 /* commande ayant ouvert le mode données */
typedef struct { char cmd[160]; uint8_t d[1600]; uint16_t n; } data_rec_t;
static data_rec_t data_log[128];
static int      data_log_n;
static uint8_t  link_sent[5][65536];
static uint32_t link_sent_n[5];

/* Mise à jour OTA (AT+OTASTART / AT+OTASEND / AT+OTAFIN) */
#define OTA_CAP  (2u * 1024u * 1024u)
static uint8_t  ota_buf[OTA_CAP];
static size_t   ota_len;
static bool     ota_active;
static bool     ota_start_error, ota_fin_error, ota_trailing_ok;
static int      ota_n_start, ota_n_abort, ota_n_send, ota_n_install, ota_n_reject;
static uint16_t ota_sizes[64];
static char     gmr_version[24] = "2.0.106";

void sim_reset(void)
{
  qh = qt = 0;
  cs = en = rdy = false;
  tick = 0;
  phase = 0;
  cur_valid = false;
  stall_n = 0;
  hlen = 0;
  data_mode = false;
  last_data_len = 0;
  last_cmd[0] = 0;
  cmd_log_n = 0;
  ping_ok_last = false;
  sock_len = 0;
  rejected = 0;
  data_log_n = 0;
  memset(link_sent_n, 0, sizeof(link_sent_n));
  ota_len = 0;
  ota_active = false;
  ota_start_error = ota_fin_error = ota_trailing_ok = false;
  ota_n_start = ota_n_abort = ota_n_send = ota_n_install = ota_n_reject = 0;
  strcpy(gmr_version, "2.0.106");
}

void sim_set_version(const char *v) { snprintf(gmr_version, sizeof(gmr_version), "%s", v); }
const char *sim_version(void) { return gmr_version; }
void sim_ota_set_faults(bool start_error, bool fin_error, bool trailing_ok)
{
  ota_start_error = start_error;
  ota_fin_error = fin_error;
  ota_trailing_ok = trailing_ok;
}
const uint8_t *sim_ota_image(size_t *len) { *len = ota_len; return ota_buf; }
bool sim_ota_active(void) { return ota_active; }
uint16_t sim_ota_send_size(int i) { return ((i >= 0) && (i < 64)) ? ota_sizes[i] : 0; }
int sim_ota_count(const char *what)
{
  if (strcmp(what, "start") == 0) return ota_n_start;
  if (strcmp(what, "abort") == 0) return ota_n_abort;
  if (strcmp(what, "send") == 0) return ota_n_send;
  if (strcmp(what, "install") == 0) return ota_n_install;
  if (strcmp(what, "reject") == 0) return ota_n_reject;
  return -1;
}

/* Image .ota de test : en-tête de 512 o (format ST) + données pseudo-aléatoires */
size_t sim_make_ota_image(uint8_t *out, size_t cap, size_t payload_len, const char *version, bool xz, uint32_t seed)
{
  size_t   i;
  uint32_t x = seed ? seed : 1u;
  if (cap < 512 + payload_len)
  {
    return 0;
  }
  memset(out, 0xFF, 512);
  memcpy(out, "QCC74X_OTAVer1.0", 16);
  memcpy(&out[16], xz ? "XZ  " : "RAW ", 4);
  out[20] = (uint8_t)payload_len;
  out[21] = (uint8_t)(payload_len >> 8);
  out[22] = (uint8_t)(payload_len >> 16);
  out[23] = (uint8_t)(payload_len >> 24);
  for (i = 0; i < 8; i++)
  {
    out[24 + i] = (uint8_t)(i + 1);
  }
  memset(&out[32], 0, 32);
  memcpy(&out[32], "QCC_Module_v1.1", 15);
  memcpy(&out[48], version, strlen(version) < 15 ? strlen(version) : 15);
  for (i = 0; i < payload_len; i++)
  {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    out[512 + i] = (uint8_t)x;
  }
  sha256(&out[512], payload_len, &out[64]);
  return 512 + payload_len;
}

/* Contrôles faits par le module simulé lors de AT+OTAFIN */
static bool ota_image_ok(char *version, size_t vlen)
{
  uint8_t  h[32];
  uint32_t n;
  if ((ota_len < 512) || (memcmp(ota_buf, "QCC74X_OTAVer1.0", 16) != 0))
  {
    return false;
  }
  n = (uint32_t)ota_buf[20] | ((uint32_t)ota_buf[21] << 8) | ((uint32_t)ota_buf[22] << 16) | ((uint32_t)ota_buf[23] << 24);
  if (n != ota_len - 512)
  {
    return false;
  }
  sha256(&ota_buf[512], n, h);
  if (memcmp(h, &ota_buf[64], 32) != 0)
  {
    return false;
  }
  snprintf(version, vlen, "%.15s", (const char *)&ota_buf[48]);
  return true;
}

void sim_sent_clear(void)
{
  memset(link_sent_n, 0, sizeof(link_sent_n));
  data_log_n = 0;
}

const char *sim_link_sent(int link, uint32_t *len)
{
  static char out[65537];
  uint32_t n = link_sent_n[link];
  memcpy(out, link_sent[link], n);
  out[n] = 0;
  if (len != NULL)
  {
    *len = n;
  }
  return out;
}

/* Recherche parmi les 128 derniers blocs de données envoyés */
int sim_data_find(const char *cmd_part, const char *needle)
{
  int i, n = 0;
  int first = (data_log_n > 128) ? (data_log_n - 128) : 0;
  for (i = first; i < data_log_n; i++)
  {
    const data_rec_t *r = &data_log[i % 128];
    char tmp[1601];
    memcpy(tmp, r->d, r->n);
    tmp[r->n] = 0;
    if ((strstr(r->cmd, cmd_part) != NULL) && ((needle == NULL) || (strstr(tmp, needle) != NULL)))
    {
      n++;
    }
  }
  return n;
}

const char *sim_data_last(const char *cmd_part)
{
  static char tmp[1601];
  int i;
  int first = (data_log_n > 128) ? (data_log_n - 128) : 0;
  for (i = data_log_n - 1; i >= first; i--)
  {
    const data_rec_t *r = &data_log[i % 128];
    if (strstr(r->cmd, cmd_part) != NULL)
    {
      memcpy(tmp, r->d, r->n);
      tmp[r->n] = 0;
      return tmp;
    }
  }
  return "";
}

void sim_push_bin(const uint8_t *data, uint16_t len)
{
  assert(len <= sizeof(q[0].d));
  memcpy(q[qt].d, data, len);
  q[qt].n = len;
  qt = (qt + 1) % QMAX;
  assert(qt != qh);
  if (!cs)
  {
    rdy = true;
  }
}

void sim_push(const char *text)
{
  sim_push_bin((const uint8_t *)text, (uint16_t)strlen(text));
}

void sim_stall_next(int n) { stall_n = n; }
void sim_set_magic(uint16_t m) { sim_magic = m; }
void sim_set_realistic(bool on) { realistic = on; rssi_idx = 0; }
void sim_set_ping_ok_last(bool last) { ping_ok_last = last; }
void sim_set_socket_data(const uint8_t *d, uint16_t n) { memcpy(sock_data, d, n); sock_len = n; }
const uint8_t *sim_last_data(uint16_t *len) { *len = last_data_len; return last_data; }
const char *sim_last_cmd(void) { return last_cmd; }
uint32_t sim_tick(void) { return tick; }
int sim_frames_rejected(void) { return rejected; }

int sim_cmd_count(const char *prefix)
{
  int i, n = 0;
  for (i = 0; i < cmd_log_n; i++)
  {
    if (strncmp(cmd_log[i], prefix, strlen(prefix)) == 0)
    {
      n++;
    }
  }
  return n;
}

int sim_cmd_first(const char *prefix)
{
  int i;
  for (i = 0; i < cmd_log_n; i++)
  {
    if (strncmp(cmd_log[i], prefix, strlen(prefix)) == 0)
    {
      return i;
    }
  }
  return -1;
}

static bool starts(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

static void respond(const char *line)
{
  char buf[600];
  strncpy(last_cmd, line, sizeof(last_cmd) - 1);
  if (cmd_log_n < 8000)
  {
    strncpy(cmd_log[cmd_log_n++], line, 159);
  }

  if (strcmp(line, "AT") == 0)
  {
    sim_push("\r\nOK\r\n");
  }
  else if (strcmp(line, "AT+CWNETMODE?") == 0)
  {
    sim_push("+CWNETMODE:1\r\n\r\nOK\r\n");
  }
  else if (strcmp(line, "AT+GET_CLOCK") == 0)
  {
    sim_push("+GET_CLOCK:1\r\n\r\nOK\r\n");
  }
  else if (strcmp(line, "AT+GMR") == 0)
  {
    char r[400];
    /* Format relevé sur un vrai module (2.0.106) : AT 1.0.0, SDK = version du firmware */
    snprintf(r, sizeof(r), "AT version:1.0.0(Mar 20 2026 09:32:19)\r\n"
             "component_version_sdk_%s\r\n"
             "component_version_macsw_1.6.44\r\n"
             "lib_version_btblecontroller_1.0.3\r\n"
             "component_version_btble_1.4.0\r\n\r\nOK\r\n", gmr_version);
    sim_push(r);
  }
  else if (strcmp(line, "AT+OTASTART=1") == 0)
  {
    ota_n_start++;
    if (ota_start_error)
    {
      sim_push("\r\nERROR\r\n");
    }
    else
    {
      ota_active = true;
      ota_len = 0;
      sim_push("\r\nOK\r\n");
    }
  }
  else if (strcmp(line, "AT+OTASTART=0") == 0)
  {
    if (ota_active)
    {
      ota_n_abort++;
    }
    ota_active = false;
    sim_push("\r\nOK\r\n");
  }
  else if (starts(line, "AT+OTASEND="))
  {
    int n = 0;
    assert(sscanf(line, "AT+OTASEND=%d", &n) == 1);
    if (!ota_active || (n <= 0) || (n > 4096))
    {
      sim_push("\r\nERROR\r\n");
    }
    else
    {
      if (ota_n_send < 64)
      {
        ota_sizes[ota_n_send] = (uint16_t)n;
      }
      ota_n_send++;
      sim_push("\r\nOK\r\n\r\n>");
      data_mode = true;
      data_need = n;
      last_data_len = 0;
      strncpy(data_cmd, line, sizeof(data_cmd) - 1);
    }
  }
  else if (strcmp(line, "AT+OTAFIN") == 0)
  {
    char v[24];
    if (!ota_active || ota_fin_error || !ota_image_ok(v, sizeof(v)))
    {
      ota_n_reject++;
      sim_push("\r\nERROR\r\n");
    }
    else
    {
      ota_n_install++;
      ota_active = false;
      sim_push("\r\nOK\r\n");
      snprintf(gmr_version, sizeof(gmr_version), "%s", v);
      sim_push("\r\nready\r\n");         /* redémarrage sur le nouveau firmware */
    }
  }
  else if (realistic && starts(line, "AT+CWLAP="))
  {
    tick += strstr(line, ",,,0") ? 2380U : 160U;   /* durée réaliste d'un balayage */
    sim_push("\r\nOK\r\n");
    sim_push("+CWLAP:(7,\"Livebox-7A3C\",-48,\"d4:6b:a6:12:7a:3c\",6,4,15,0)\r\n"
             "+CWLAP:(3,\"Freebox-Labo\",-61,\"f4:ca:e5:44:10:22\",11,4,15,1)\r\n");
    sim_push("+CWLAP:(5,\"eduroam\",-67,\"00:3a:7d:21:9b:40\",1,4,7,0)\r\n"
             "+CWLAP:(3,\"\",-71,\"00:3a:7d:21:9b:41\",1,4,7,0)\r\n"
             "+CWLAP:(3,\"Labo-Invites\",-74,\"00:3a:7d:21:9b:42\",1,4,7,0)\r\n");
    sim_push("+CWLAP:(0,\"Imprimante-HP-3F2A\",-83,\"3c:52:82:aa:3f:2a\",6,0,7,0)\r\n"
             "+CWLAP:(4,\"SFR_8E10\",-79,\"e0:b9:e5:8e:10:01\",3,5,7,0)\r\n"
             "+CWLAP:(3,\"Pixel_7731\",-58,\"3a:21:9c:07:77:31\",11,4,15,0)\r\n");
    sim_push("+CW:SCAN_DONE\r\n");
  }
  else if (realistic && (strcmp(line, "AT+CWJAP?") == 0))
  {
    static const int seq[] = { -52, -53, -51, -55, -58, -63, -66, -61, -57, -54 };
    char r[160];
    snprintf(r, sizeof(r), "+CWJAP:\"Livebox-7A3C\",\"d4:6b:a6:12:7a:3c\",6,%d,0,1,3,0,0\r\n\r\nOK\r\n",
             seq[rssi_idx++ % 10]);
    sim_push(r);
  }
  else if (realistic && (strcmp(line, "AT+CWSTATE?") == 0))
  {
    sim_push("+CWSTATE:2,\"Livebox-7A3C\",7,4\r\n\r\nOK\r\n");
  }
  else if (starts(line, "AT+CWLAP="))
  {
    sim_push("\r\nOK\r\n");
    sim_push("+CWLAP:(3,\"Net1\",-45,\"aa:bb:cc:dd:ee:01\",6,4,15,0)\r\n");
    sim_push("+CWLAP:(0,\"Open, cafe\",-80,\"aa:bb:cc:dd:ee:02\",11,0,7,0)\r\n+CWLAP:(6,\"Lab\",-60,\"aa:bb:cc:dd:ee:03\",1,4,15,1)\r\n");
    sim_push("+CW:SCAN_DONE\r\n");
  }
  else if (starts(line, "AT+CWJAP=\""))
  {
    if (realistic)
    {
      tick += 2150U;                 /* association + DHCP */
    }
    sim_push("\r\nOK\r\n");
    sim_push("+CW:CONNECTING\r\n");
    if (strstr(line, "\"wrong\"") != NULL)
    {
      sim_push("+CW:ERROR,8\r\n+CW:DISCONNECTED\r\n");
    }
    else
    {
      sim_push("+CW:CONNECTED\r\n");
      sim_push("+CW:GOTIP\r\n");
    }
  }
  else if (strcmp(line, "AT+CWJAP?") == 0)
  {
    sim_push("+CWJAP:\"Net1\",\"aa:bb:cc:dd:ee:01\",6,-47,0,1,3,0,0\r\n\r\nOK\r\n");
  }
  else if (strcmp(line, "AT+CWSTATE?") == 0)
  {
    sim_push("+CWSTATE:2,\"Net1\",3,4\r\n\r\nOK\r\n");
  }
  else if (strcmp(line, "AT+CIPSTA?") == 0)
  {
    sim_push("+CIPSTA:ip:\"192.168.1.42\"\r\n+CIPSTA:gateway:\"192.168.1.1\"\r\n");
    sim_push("+CIPSTA:netmask:\"255.255.255.0\"\r\n+CIPSTA:ip6ll:\"fe80::1\"\r\n\r\nOK\r\n");
  }
  else if (strcmp(line, "AT+CIPAP?") == 0)
  {
    sim_push("+CIPAP:ip:\"10.19.96.1\"\r\n+CIPAP:netmask:\"255.255.255.0\"\r\n\r\nOK\r\n");
  }
  else if (strcmp(line, "AT+CIPSTAMAC?") == 0)
  {
    sim_push("+CIPSTAMAC:\"11:22:33:44:55:66\"\r\n\r\nOK\r\n");
  }
  else if (strcmp(line, "AT+CIPDNS?") == 0)
  {
    sim_push("+CIPDNS:1,\"192.168.1.1\",\"8.8.8.8\"\r\n\r\nOK\r\n");
  }
  else if (strcmp(line, "AT+CWLIF") == 0)
  {
    sim_push("+CWLIF:\"10.19.96.2\",\"de:ad:be:ef:00:01\"\r\n+CWLIF:\"10.19.96.3\",\"de:ad:be:ef:00:02\"\r\n\r\nOK\r\n");
  }
  else if (starts(line, "AT+CIPDOMAIN="))
  {
    if (realistic)
    {
      tick += 38U;
    }
    sim_push("+CIPDOMAIN:\"93.184.216.34\"\r\n\r\nOK\r\n");
  }
  else if (starts(line, "AT+PING="))
  {
    if (!ping_ok_last)
    {
      sim_push("\r\nOK\r\n");
    }
    sim_push("+PING:12\r\n");
    sim_push("+PING:TIMEOUT\r\n");
    sim_push("+PING:15\r\n+PING:11\r\n");
    if (ping_ok_last)
    {
      sim_push("\r\nOK\r\n");
    }
  }
  else if (strcmp(line, "AT+CIPSNTPTIME?") == 0)
  {
    sim_push("+CIPSNTPTIME:Fri Oct  3 08:45:12 2026\r\n\r\nOK\r\n");
  }
  else if (starts(line, "AT+CIPSEND="))
  {
    int id = 0, n = 0;
    assert(sscanf(line, "AT+CIPSEND=%d,%d", &id, &n) == 2);
    sim_push("\r\nOK\r\n\r\n>");
    data_mode = true;
    data_need = n;
    last_data_len = 0;
    strncpy(data_cmd, line, sizeof(data_cmd) - 1);
  }
  else if (starts(line, "AT+MQTTPUBRAW="))
  {
    const char *p = strrchr(line, '"');
    int n = 0, qos = 0, ret = 0;
    assert(p != NULL);
    assert(sscanf(p + 1, ",%d,%d,%d", &n, &qos, &ret) == 3);
    sim_push("\r\nOK\r\n");
    sim_push("\r\n>");
    data_mode = true;
    data_need = n;
    last_data_len = 0;
    strncpy(data_cmd, line, sizeof(data_cmd) - 1);
  }
  else if (starts(line, "AT+CIPRECVDATA="))
  {
    int id = 0, n = 0, k, h, hl;
    assert(sscanf(line, "AT+CIPRECVDATA=%d,%d", &id, &n) == 2);
    k = (n < sock_len) ? n : sock_len;
    hl = snprintf(buf, sizeof(buf), "+CIPRECVDATA:%d,", k);
    h = k / 2;
    memcpy(&buf[hl], sock_data, (size_t)h);
    sim_push_bin((const uint8_t *)buf, (uint16_t)(hl + h));
    memcpy(buf, &sock_data[h], (size_t)(k - h));
    memcpy(&buf[k - h], "\r\nOK\r\n", 6);
    sim_push_bin((const uint8_t *)buf, (uint16_t)(k - h + 6));
    memmove(sock_data, &sock_data[k], (size_t)(sock_len - k));
    sock_len = (uint16_t)(sock_len - k);
  }
  else if (starts(line, "AT+MQTTCONN="))
  {
    if (realistic)
    {
      tick += 640U;
    }
    sim_push("\r\nOK\r\n");
    sim_push("+MQTT:CONNECTED,0\r\n");
  }
  else if (starts(line, "AT+CWQAP"))
  {
    sim_push("\r\nOK\r\n");
    sim_push("+CW:DISCONNECTED\r\n");
  }
  else if (starts(line, "AT+BAD"))
  {
    sim_push("\r\nERR CODE:0x01090000\r\n\r\nERROR\r\n");
  }
  else if (starts(line, "AT+"))
  {
    sim_push("\r\nOK\r\n");
  }
  else
  {
    sim_push("\r\nERROR\r\n");
  }
}

static void host_bytes(const uint8_t *d, uint16_t n)
{
  uint16_t i;
  for (i = 0; i < n; i++)
  {
    uint8_t b = d[i];
    if (data_mode)
    {
      char tmp[48];
      last_data[last_data_len++] = b;
      if (--data_need == 0)
      {
        int link = -1;
        data_mode = false;
        {
          data_rec_t *r = &data_log[data_log_n % 128];
          data_log_n++;
          strncpy(r->cmd, data_cmd, sizeof(r->cmd) - 1);
          r->n = (last_data_len < sizeof(r->d)) ? last_data_len : (uint16_t)sizeof(r->d);
          memcpy(r->d, last_data, r->n);
        }
        if ((sscanf(data_cmd, "AT+CIPSEND=%d", &link) == 1) && (link >= 0) && (link < 5) &&
            ((link_sent_n[link] + last_data_len) < sizeof(link_sent[0])))
        {
          memcpy(&link_sent[link][link_sent_n[link]], last_data, last_data_len);
          link_sent_n[link] += last_data_len;
        }
        snprintf(tmp, sizeof(tmp), "\r\nRecv %u bytes\r\n", (unsigned)last_data_len);
        if (strncmp(data_cmd, "AT+OTASEND=", 11) == 0)
        {
          assert(ota_len + last_data_len <= OTA_CAP);
          memcpy(&ota_buf[ota_len], last_data, last_data_len);
          ota_len += last_data_len;
          sim_push(tmp);
          if (ota_trailing_ok)
          {
            sim_push("\r\nOK\r\n");        /* variante : OK après la confirmation */
          }
          continue;
        }
        sim_push(tmp);
        sim_push("\r\nSEND OK\r\n");
      }
      continue;
    }
    hstream[hlen++] = (char)b;
    if ((hlen >= 2) && (hstream[hlen - 2] == '\r') && (hstream[hlen - 1] == '\n'))
    {
      hstream[hlen - 2] = '\0';
      respond(hstream);
      hlen = 0;
    }
  }
}

/* ---- ncp_port.h ---------------------------------------------------------- */
void ncp_port_chip_enable(bool on)
{
  if (on && !en)
  {
    sim_push("\r\nready\r\n");
  }
  en = on;
}

void ncp_port_boot(bool on) { (void)on; }

void ncp_port_cs(bool active)
{
  if (active && !cs)
  {
    cs = true;
    phase = 1;
    stall_this = (stall_n > 0);
    if (stall_n > 0)
    {
      stall_n--;
    }
    rdy = true;
  }
  else if (!active && cs)
  {
    cs = false;
    if (cur_valid && (cur_off < cur.n))
    {
      rejected++;                  /* l'hôte n'a pas lu toute la trame */
    }
    cur_valid = false;
    phase = 0;
    rdy = (qh != qt);
  }
}

bool ncp_port_rdy(void) { return rdy; }

int ncp_port_spi_txrx(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
  assert(cs);
  assert((len % 4) == 0);
  if (phase == 1)
  {
    uint16_t magic = (uint16_t)(tx[0] | (tx[1] << 8));
    uint16_t hl = (uint16_t)(tx[2] | (tx[3] << 8));
    uint16_t sl;
    assert(magic == 0x55AA);
    assert(tx[5] == 0);
    assert(len >= 8 + hl);
    memset(rx, 0, len);
    if (qh != qt)
    {
      cur = q[qh];
      qh = (qh + 1) % QMAX;
      cur_valid = true;
      cur_off = 0;
    }
    sl = cur_valid ? cur.n : 0;
    rx[0] = (uint8_t)(sim_magic & 0xFF);
    rx[1] = (uint8_t)(sim_magic >> 8);
    rx[2] = (uint8_t)(sl & 0xFF);
    rx[3] = (uint8_t)(sl >> 8);
    rx[4] = stall_this ? 0x04 : 0x00;
    rx[5] = 0;
    if (cur_valid)
    {
      uint16_t can = (uint16_t)(len - 8);
      uint16_t k = (can < cur.n) ? can : cur.n;
      memcpy(&rx[8], cur.d, k);
      cur_off = k;
    }
    if (hl > 0)
    {
      if (!stall_this)
      {
        host_bytes(&tx[8], hl);
      }
    }
    phase = 2;
  }
  else
  {
    assert(phase == 2);
    memset(rx, 0, len);
    if (cur_valid)
    {
      uint16_t rem = (uint16_t)(cur.n - cur_off);
      uint16_t k = (len < rem) ? len : rem;
      memcpy(rx, &cur.d[cur_off], k);
      cur_off = (uint16_t)(cur_off + k);
    }
  }
  if (!cur_valid || (cur_off >= cur.n))
  {
    rdy = false;                   /* acquittement */
  }
  return 0;
}

#ifdef SIM_REALTIME
/* Temps réel (essai de bout en bout avec le script PC sur un pseudo-terminal) */
#include <time.h>
#include <unistd.h>
uint32_t ncp_port_tick(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u) + tick;
}
void ncp_port_delay(uint32_t ms) { usleep(ms * 1000u); }
uint32_t sim_now(void) { return ncp_port_tick(); }
#else
uint32_t ncp_port_tick(void) { return tick++; }
void ncp_port_delay(uint32_t ms) { tick += ms; }
uint32_t sim_now(void) { return tick; }      /* lecture sans faire avancer le temps */
#endif
