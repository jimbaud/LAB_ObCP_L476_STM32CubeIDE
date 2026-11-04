/**
 ******************************************************************************
 * @file    ncp_net.c
 * @brief   Services réseau du ST67W611M1, architecture T01 (voir ncp_net.h).
 *
 *  Séquences reprises du pilote officiel (w61_at_net.c / w6x_net.c,
 *  X-CUBE-ST67W61 v1.3.0) : mode multi-connexions, réception PASSIVE
 *  (le module signale +IPD:<lien>,<n>,"ip",port puis l'hôte lit les données
 *  avec AT+CIPRECVDATA=<lien>,<n>).
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ncp_net.h"
#include "ncp_at.h"
#include "ncp_port.h"

#define NET_TIMEOUT_MS          6000U   /* commandes réseau ordinaires   */
#define NET_DNS_TIMEOUT_MS     20000U   /* résolution DNS                */
#define NET_RECV_TIMEOUT_MS     3000U   /* lecture de données reçues     */
#define NET_SEND_TIMEOUT_MS     6000U   /* envoi d'un bloc de données    */

net_ctx_t g_net;

/* Ping en cours : rappel, numéro du dernier écho, nombre de réponses */
static net_ping_cb_t     s_ping_cb;
static volatile uint16_t s_ping_seq;
static volatile uint16_t s_ping_replies;

/* -------------------------------------------------------------------------- */
/* Voir ncp_net.h. Exemples : « +CIP:2,CONNECTED », « +IPD:2,345,"10.19.96.2",51000 »,
 * « +PING:12 » (12 ms) ou « +PING:TIMEOUT ».                               */
bool net_urc(const char *line)
{
  char  buf[96];
  char *argv[6];
  int   argc;

  if (at_starts(line, "+CIP:"))
  {
    strncpy(buf, &line[5], sizeof(buf) - 1U);
    buf[sizeof(buf) - 1U] = '\0';
    argc = at_split(buf, argv, 6);
    if (argc >= 2)
    {
      int id = atoi(argv[0]);
      if ((id >= 0) && (id < (int)NET_LINKS))
      {
        if (strcmp(argv[1], "CONNECTED") == 0)
        {
          g_net.link[id].connected = true;
          g_net.link[id].avail = 0U;
          g_net.evt_link = true;
        }
        else if (strcmp(argv[1], "DISCONNECTED") == 0)
        {
          g_net.link[id].connected = false;
          g_net.evt_link = true;
        }
        else
        {
          /* autre état : ignoré */
        }
      }
    }
    return true;
  }

  if (at_starts(line, "+IPD:"))
  {
    strncpy(buf, &line[5], sizeof(buf) - 1U);
    buf[sizeof(buf) - 1U] = '\0';
    argc = at_split(buf, argv, 6);
    if (argc >= 2)
    {
      int id = atoi(argv[0]);
      if ((id >= 0) && (id < (int)NET_LINKS))
      {
        net_link_t *l = &g_net.link[id];
        l->avail += (uint32_t)strtoul(argv[1], NULL, 10);
        if (argc >= 4)
        {
          strncpy(l->rip, argv[2], sizeof(l->rip) - 1U);
          l->rip[sizeof(l->rip) - 1U] = '\0';
          l->rport = (uint16_t)atoi(argv[3]);
        }
      }
    }
    return true;
  }

  if (at_starts(line, "+PING:"))
  {
    const char *v = &line[6];
    int32_t     ms = -1;
    if (!at_starts(v, "TIMEOUT"))
    {
      ms = atoi(v);
      s_ping_replies++;
    }
    s_ping_seq++;
    if (s_ping_cb != NULL)
    {
      s_ping_cb(s_ping_seq, ms);
    }
    return true;
  }
  return false;
}

/* -------------------------------------------------------------------------- */
/* Voir ncp_net.h */
int net_init(const char *hostname, const uint8_t ap_ip[4])
{
  static const char *const init_cmds[] =
  {
    "AT+CIPV6=0",          /* IPv4 uniquement (démo)                */
    "AT+CIPMUX=1",         /* plusieurs connexions simultanées      */
    "AT+CIPRECVMODE=1",    /* réception passive                     */
    "AT+CIPDINFO=1",       /* +IPD avec adresse et port distants    */
    "AT+CWDHCP=0,3",       /* réinitialise DHCP (client STA + serveur AP) */
    "AT+CWDHCP=1,3"        /* active client DHCP (STA) et serveur DHCP (AP) */
  };
  unsigned i;
  int      st;

  memset((void *)&g_net, 0, sizeof(g_net));

  for (i = 0U; i < (sizeof(init_cmds) / sizeof(init_cmds[0])); i++)
  {
    st = at_cmd(init_cmds[i], NULL, NULL, NULL, 2000U);
    if (st != AT_OK)
    {
      return st;
    }
  }
  if ((hostname != NULL) && (hostname[0] != '\0'))
  {
    (void)at_cmdf(2000U, NULL, NULL, NULL, "AT+CWHOSTNAME=\"%s\"", hostname);
  }
  if (ap_ip != NULL)
  {
    st = at_cmdf(NET_TIMEOUT_MS, NULL, NULL, NULL, "AT+CIPAP=\"%u.%u.%u.1\",\"%u.%u.%u.1\",\"255.255.255.0\"",
                 ap_ip[0], ap_ip[1], ap_ip[2], ap_ip[0], ap_ip[1], ap_ip[2]);
    if (st != AT_OK)
    {
      return st;
    }
  }
  for (i = 0U; i < NET_LINKS; i++)
  {
    (void)at_cmdf(2000U, NULL, NULL, NULL, "AT+CIPRECVBUF=%u,4096", i);
  }
  return AT_OK;
}

/* Rappel de AT+CIPSTA? : lignes « ip:"..." », « gateway:"..." », « netmask:"..." » */
static void on_cipsta(const char *args, void *ctx)
{
  net_ipcfg_t *c = (net_ipcfg_t *)ctx;
  if (at_starts(args, "ip:"))
  {
    (void)at_parse_ip(&args[3], c->ip);
  }
  else if (at_starts(args, "gateway:"))
  {
    (void)at_parse_ip(&args[8], c->gw);
  }
  else if (at_starts(args, "netmask:"))
  {
    (void)at_parse_ip(&args[8], c->mask);
  }
  else
  {
    /* ip6ll / ip6gl : non utilisés */
  }
}

/* Voir ncp_net.h */
int net_sta_ipcfg(net_ipcfg_t *cfg)
{
  memset(cfg, 0, sizeof(*cfg));
  return at_cmd("AT+CIPSTA?", "+CIPSTA:", on_cipsta, cfg, NET_TIMEOUT_MS);
}

/* Rappel de AT+CIPAP? : seule la ligne « ip:"..." » est utile */
static void on_cipap(const char *args, void *ctx)
{
  if (at_starts(args, "ip:"))
  {
    (void)at_parse_ip(&args[3], (uint8_t *)ctx);
  }
}

/* Voir ncp_net.h */
int net_ap_ip(uint8_t ip[4])
{
  memset(ip, 0, 4);
  return at_cmd("AT+CIPAP?", "+CIPAP:", on_cipap, ip, NET_TIMEOUT_MS);
}

/* Rappel de AT+CIPSTAMAC? */
static void on_mac(const char *args, void *ctx)
{
  (void)at_parse_mac(args, (uint8_t *)ctx);
}

/* Voir ncp_net.h */
int net_sta_mac(uint8_t mac[6])
{
  memset(mac, 0, 6);
  return at_cmd("AT+CIPSTAMAC?", "+CIPSTAMAC:", on_mac, mac, NET_TIMEOUT_MS);
}

/* Rappel de AT+CIPDNS? : « <mode>,"dns1","dns2","dns3" » */
static void on_dns(const char *args, void *ctx)
{
  char (*dns)[16] = (char (*)[16])ctx;
  char  buf[96];
  char *argv[5];
  int   argc;
  int   i;

  strncpy(buf, args, sizeof(buf) - 1U);
  buf[sizeof(buf) - 1U] = '\0';
  argc = at_split(buf, argv, 5);
  for (i = 1; (i < argc) && (i <= 3); i++)
  {
    strncpy(dns[i - 1], argv[i], 15);
    dns[i - 1][15] = '\0';
  }
}

/* Voir ncp_net.h */
int net_dns_servers(char dns[3][16])
{
  memset(dns, 0, 3U * 16U);
  return at_cmd("AT+CIPDNS?", "+CIPDNS:", on_dns, dns, NET_TIMEOUT_MS);
}

/* Voir ncp_net.h : AT+CWDHCP=<0|1>,1 (le 1 final désigne la station) */
int net_set_dhcp(bool on)
{
  return at_cmdf(2000U, NULL, NULL, NULL, "AT+CWDHCP=%u,1", on ? 1U : 0U);
}

/* Voir ncp_net.h */
int net_set_static(const uint8_t ip[4], const uint8_t gw[4], const uint8_t mask[4])
{
  return at_cmdf(NET_TIMEOUT_MS, NULL, NULL, NULL,
                 "AT+CIPSTA=\"%u.%u.%u.%u\",\"%u.%u.%u.%u\",\"%u.%u.%u.%u\"",
                 ip[0], ip[1], ip[2], ip[3], gw[0], gw[1], gw[2], gw[3],
                 mask[0], mask[1], mask[2], mask[3]);
}

/* Voir ncp_net.h */
int net_set_dns(const uint8_t dns[4])
{
  if (dns == NULL)
  {
    return at_cmd("AT+CIPDNS=0", NULL, NULL, NULL, 2000U);
  }
  return at_cmdf(2000U, NULL, NULL, NULL, "AT+CIPDNS=1,\"%u.%u.%u.%u\"", dns[0], dns[1], dns[2], dns[3]);
}

/* Tampon de sortie d'un rappel (adresse et taille) */
typedef struct
{
  char  *out;
  size_t len;
} str_out_t;

/* Rappel de AT+CIPDOMAIN : « "a.b.c.d" » */
static void on_domain(const char *args, void *ctx)
{
  str_out_t *o = (str_out_t *)ctx;
  char       buf[64];
  char      *argv[2];

  strncpy(buf, args, sizeof(buf) - 1U);
  buf[sizeof(buf) - 1U] = '\0';
  if (at_split(buf, argv, 2) >= 1)
  {
    strncpy(o->out, argv[0], o->len - 1U);
    o->out[o->len - 1U] = '\0';
  }
}

/* Voir ncp_net.h */
int net_resolve(const char *host, char *ip, size_t len)
{
  str_out_t o = { ip, len };
  int       st;

  if ((host == NULL) || (strchr(host, '"') != NULL) || (len < 8U))
  {
    return AT_PARAM;
  }
  ip[0] = '\0';
  /* 2 = IPv4 uniquement */
  st = at_cmdf(NET_DNS_TIMEOUT_MS, "+CIPDOMAIN:", on_domain, &o, "AT+CIPDOMAIN=\"%s\",2", host);
  if ((st == AT_OK) && (ip[0] == '\0'))
  {
    st = AT_ERROR;
  }
  return st;
}

/* Voir ncp_net.h : les échos arrivent comme événements +PING: (net_urc()),
 * on attend qu'il y en ait count, dans la limite d'un budget de temps.  */
int net_ping(const char *host, uint16_t count, uint16_t interval_ms, net_ping_cb_t cb, uint16_t *replies)
{
  uint32_t t0;
  uint32_t budget;
  int      st;

  if ((host == NULL) || (strchr(host, '"') != NULL))
  {
    return AT_PARAM;
  }
  if (count == 0U)
  {
    count = 4U;
  }
  if ((interval_ms < 100U) || (interval_ms > 3500U))
  {
    interval_ms = 1000U;
  }
  budget = ((uint32_t)count * ((uint32_t)interval_ms + 3500U)) + 5000U;

  s_ping_cb = cb;
  s_ping_seq = 0U;
  s_ping_replies = 0U;

  /* AT+PING="<hôte>",<taille>,<nombre>,<intervalle ms> */
  st = at_cmdf(budget, NULL, NULL, NULL, "AT+PING=\"%s\",64,%u,%u", host, (unsigned)count, (unsigned)interval_ms);
  if (st == AT_OK)
  {
    t0 = ncp_port_tick();
    while ((s_ping_seq < count) && ((ncp_port_tick() - t0) < budget))
    {
      at_poll();
    }
  }
  s_ping_cb = NULL;
  if (replies != NULL)
  {
    *replies = s_ping_replies;
  }
  return st;
}

/* -------------------------------------------------------------------------- */
/* Voir ncp_net.h */
int net_sntp_config(bool enable, int tz_hours, const char *server)
{
  if ((server == NULL) || (server[0] == '\0') || (strchr(server, '"') != NULL))
  {
    return AT_PARAM;
  }
  return at_cmdf(2000U, NULL, NULL, NULL, "AT+CIPSNTPCFG=%u,%d,\"%s\"", enable ? 1U : 0U, tz_hours, server);
}

/* Rang de tok (3 premières lettres) dans un tableau de noms, -1 si absent */
static int index_of(const char *tok, const char *const *tab, int n)
{
  int i;
  for (i = 0; i < n; i++)
  {
    if (strncmp(tok, tab[i], 3) == 0)
    {
      return i;
    }
  }
  return -1;
}

/* Format asctime : "Thu Oct 23 08:45:12 2026" (jour éventuellement précédé d'un espace) */
bool net_parse_asctime(const char *s, net_time_t *t)
{
  static const char *const wd[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
  static const char *const mo[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  char  buf[48];
  char *tok[8];
  int   n = 0;
  char *p;
  int   w;
  int   m;
  int   hh;
  int   mm;
  int   ss;

  strncpy(buf, s, sizeof(buf) - 1U);
  buf[sizeof(buf) - 1U] = '\0';
  p = strtok(buf, " ");
  while ((p != NULL) && (n < 8))
  {
    tok[n++] = p;
    p = strtok(NULL, " ");
  }
  if (n < 5)
  {
    return false;
  }
  w = index_of(tok[0], wd, 7);
  m = index_of(tok[1], mo, 12);
  if ((w < 0) || (m < 0) || (sscanf(tok[3], "%d:%d:%d", &hh, &mm, &ss) != 3))
  {
    return false;
  }
  t->wday = (uint8_t)(w + 1);
  t->mon  = (uint8_t)(m + 1);
  t->day  = (uint8_t)atoi(tok[2]);
  t->hour = (uint8_t)hh;
  t->min  = (uint8_t)mm;
  t->sec  = (uint8_t)ss;
  t->year = (uint16_t)atoi(tok[4]);
  return (t->day >= 1U) && (t->day <= 31U) && (t->hour < 24U) && (t->min < 60U) && (t->sec < 61U);
}

/* Contexte du rappel de AT+CIPSNTPTIME? */
typedef struct
{
  net_time_t *t;
  bool        ok;
} time_ctx_t;

/* Rappel de AT+CIPSNTPTIME? : date au format asctime */
static void on_time(const char *args, void *ctx)
{
  time_ctx_t *c = (time_ctx_t *)ctx;
  c->ok = net_parse_asctime(args, c->t);
}

/* Voir ncp_net.h */
int net_sntp_time(net_time_t *t)
{
  time_ctx_t c = { t, false };
  int        st = at_cmd("AT+CIPSNTPTIME?", "+CIPSNTPTIME:", on_time, &c, NET_TIMEOUT_MS);
  if ((st == AT_OK) && !c.ok)
  {
    st = AT_ERROR;
  }
  return st;
}

/* -------------------------------------------------------------------------- */
/* Voir ncp_net.h */
int net_server_start(uint16_t port, uint8_t max_conn)
{
  int st;
  unsigned i;

  for (i = 0U; i < NET_LINKS; i++)
  {
    g_net.link[i].connected = false;
    g_net.link[i].avail = 0U;
  }
  (void)at_cmdf(2000U, NULL, NULL, NULL, "AT+CIPSERVERMAXCONN=%u", (unsigned)max_conn);
  st = at_cmdf(NET_TIMEOUT_MS, NULL, NULL, NULL, "AT+CIPSERVER=1,%u,\"TCP\",0,0", (unsigned)port);
  return st;
}

/* Voir ncp_net.h */
int net_server_stop(void)
{
  return at_cmd("AT+CIPSERVER=0,1", NULL, NULL, NULL, NET_TIMEOUT_MS);
}

/* Voir ncp_net.h : AT+CIPRECVDATA=<lien>,<max> ; le module répond par une
 * trame +CIPRECVDATA:<n>,<données> que ncp_at.c copie dans buf.        */
int net_recv(uint8_t link, uint8_t *buf, uint16_t max, uint16_t *got)
{
  uint16_t n = 0U;
  int      st;

  if ((link >= NET_LINKS) || (buf == NULL) || (max == 0U))
  {
    return AT_PARAM;
  }
  if (max > NET_RECV_MAX)
  {
    max = NET_RECV_MAX;
  }
  at_set_recv_sink(buf, max, &n);
  st = at_cmdf(NET_RECV_TIMEOUT_MS, NULL, NULL, NULL, "AT+CIPRECVDATA=%u,%u", (unsigned)link, (unsigned)max);
  at_set_recv_sink(NULL, 0U, NULL);

  if (n >= g_net.link[link].avail)
  {
    g_net.link[link].avail = 0U;
  }
  else
  {
    g_net.link[link].avail -= n;
  }
  if (got != NULL)
  {
    *got = n;
  }
  return st;
}

/* Voir ncp_net.h : AT+CIPSEND=<lien>,<n> puis les n octets, bloc par bloc */
int net_send(uint8_t link, const uint8_t *data, uint32_t len)
{
  uint32_t off = 0U;
  char     cmd[32];
  int      st = AT_OK;

  if ((link >= NET_LINKS) || (data == NULL))
  {
    return AT_PARAM;
  }
  while (off < len)
  {
    uint32_t n = len - off;
    if (n > NET_SEND_CHUNK)
    {
      n = NET_SEND_CHUNK;
    }
    (void)snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u,%lu", (unsigned)link, (unsigned long)n);
    st = at_cmd_data(cmd, &data[off], (uint16_t)n, NET_SEND_TIMEOUT_MS);
    if (st != AT_OK)
    {
      break;
    }
    off += n;
  }
  return st;
}

/* Voir ncp_net.h */
int net_close(uint8_t link)
{
  int st;
  if (link >= NET_LINKS)
  {
    return AT_PARAM;
  }
  st = at_cmdf(NET_TIMEOUT_MS, NULL, NULL, NULL, "AT+CIPCLOSE=%u", (unsigned)link);
  g_net.link[link].connected = false;
  g_net.link[link].avail = 0U;
  return st;
}
