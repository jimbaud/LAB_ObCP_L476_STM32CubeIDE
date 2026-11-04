/**
 ******************************************************************************
 * @file    ncp_mqtt.c
 * @brief   Client MQTT du ST67W611M1 (voir ncp_mqtt.h).
 *
 *  Commandes reprises du pilote officiel (w61_at_mqtt.c, X-CUBE-ST67W61
 *  v1.3.0) : AT+MQTTUSERCFG, AT+MQTTCONNCFG, AT+MQTTCONN, AT+MQTTSUB,
 *  AT+MQTTPUBRAW, AT+MQTTCLEAN ; évènements +MQTT:CONNECTED,
 *  +MQTT:DISCONNECTED et +MQTT:SUBRECV (traité dans ncp_at.c).
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "ncp_mqtt.h"
#include "ncp_at.h"
#include "ncp_port.h"

#define MQTT_TIMEOUT_MS          6000U   /* commandes MQTT ordinaires    */
#define MQTT_CONNECT_TIMEOUT_MS 10000U   /* connexion au broker          */

mqtt_ctx_t g_mqtt;

/* Chaîne absente ou contenant un guillemet (interdit dans une commande AT) */
static bool bad_str(const char *s)
{
  return (s == NULL) || (strchr(s, '"') != NULL);
}

/* Voir ncp_mqtt.h : isole le premier mot après « +MQTT: » */
bool mqtt_urc(const char *line)
{
  char  tok[24];
  const char *p;
  size_t n = 0U;

  if (!at_starts(line, "+MQTT:"))
  {
    return false;
  }
  p = &line[6];
  while ((p[n] != '\0') && (p[n] != ',') && (n < (sizeof(tok) - 1U)))
  {
    tok[n] = p[n];
    n++;
  }
  tok[n] = '\0';

  if (strcmp(tok, "CONNECTED") == 0)
  {
    g_mqtt.connected = true;
    g_mqtt.evt_connected = true;
  }
  else if (strcmp(tok, "DISCONNECTED") == 0)
  {
    g_mqtt.connected = false;
    g_mqtt.evt_disconnected = true;
  }
  else
  {
    /* SUBRECV mal formé ou autre évènement : ignoré */
  }
  return true;
}

/* Voir ncp_mqtt.h : les messages arrivent par le moteur AT (+MQTT:SUBRECV) */
void mqtt_set_msg_cb(mqtt_msg_cb_t cb)
{
  at_set_mqtt_cb((at_mqtt_cb_t)cb);
}

/* Voir ncp_mqtt.h */
int mqtt_configure(uint8_t scheme, const char *client_id, const char *user, const char *pass,
                   uint16_t keepalive_s, const char *lwt_topic, const char *lwt_msg, bool lwt_retain)
{
  int st;

  if ((client_id == NULL) || (client_id[0] == '\0') || bad_str(client_id) || bad_str(user) ||
      bad_str(pass) || bad_str(lwt_topic) || bad_str(lwt_msg))
  {
    return AT_PARAM;
  }
  /* AT+MQTTUSERCFG=0,<schéma>,"<id>","<user>","<mdp>","<cert>","<clé>","<CA>" */
  st = at_cmdf(MQTT_TIMEOUT_MS, NULL, NULL, NULL, "AT+MQTTUSERCFG=0,%u,\"%s\",\"%s\",\"%s\",\"\",\"\",\"\"",
               (unsigned)scheme, client_id, user, pass);
  if (st != AT_OK)
  {
    return st;
  }
  /* AT+MQTTCONNCFG=0,<keepalive>,<pas de session propre>,"<LWT topic>","<LWT msg>",<qos>,<retain> */
  return at_cmdf(MQTT_TIMEOUT_MS, NULL, NULL, NULL, "AT+MQTTCONNCFG=0,%u,0,\"%s\",\"%s\",0,%u",
                 (unsigned)keepalive_s, lwt_topic, lwt_msg, lwt_retain ? 1U : 0U);
}

/* Voir ncp_mqtt.h : le OK de AT+MQTTCONN arrive avant la connexion effective,
 * signalée ensuite par l'événement +MQTT:CONNECTED.                       */
int mqtt_connect(const char *host, uint16_t port, uint32_t timeout_ms)
{
  uint32_t t0;
  int      st;

  if ((host == NULL) || (host[0] == '\0') || bad_str(host))
  {
    return AT_PARAM;
  }
  if (timeout_ms == 0U)
  {
    timeout_ms = MQTT_CONNECT_TIMEOUT_MS;
  }
  g_mqtt.evt_connected = false;
  g_mqtt.evt_disconnected = false;

  st = at_cmdf(MQTT_CONNECT_TIMEOUT_MS, NULL, NULL, NULL, "AT+MQTTCONN=0,\"%s\",%u,0", host, (unsigned)port);
  if (st != AT_OK)
  {
    return st;
  }
  t0 = ncp_port_tick();
  while (!g_mqtt.connected)
  {
    at_poll();
    if (g_mqtt.evt_disconnected || ((ncp_port_tick() - t0) >= timeout_ms))
    {
      return MQTT_ERR_NOTCONN;
    }
  }
  return AT_OK;
}

/* Voir ncp_mqtt.h */
int mqtt_disconnect(void)
{
  int st = at_cmd("AT+MQTTCLEAN=0", NULL, NULL, NULL, MQTT_TIMEOUT_MS);
  g_mqtt.connected = false;
  return st;
}

/* Voir ncp_mqtt.h */
int mqtt_subscribe(const char *topic, uint8_t qos)
{
  if ((topic == NULL) || (topic[0] == '\0') || bad_str(topic))
  {
    return AT_PARAM;
  }
  return at_cmdf(MQTT_TIMEOUT_MS, NULL, NULL, NULL, "AT+MQTTSUB=0,\"%s\",%u", topic, (unsigned)qos);
}

/* Voir ncp_mqtt.h : AT+MQTTPUBRAW annonce la longueur, puis les octets suivent
 * (même mécanisme que l'envoi TCP : invite '>', données, confirmation). */
int mqtt_publish(const char *topic, const uint8_t *msg, uint16_t len, uint8_t qos, bool retain)
{
  char cmd[160];

  if ((topic == NULL) || (topic[0] == '\0') || bad_str(topic) || (msg == NULL) || (len == 0U))
  {
    return AT_PARAM;
  }
  if (!g_mqtt.connected)
  {
    return MQTT_ERR_NOTCONN;
  }
  if (snprintf(cmd, sizeof(cmd), "AT+MQTTPUBRAW=0,\"%s\",%u,%u,%u", topic, (unsigned)len, (unsigned)qos,
               retain ? 1U : 0U) >= (int)sizeof(cmd))
  {
    return AT_PARAM;
  }
  return at_cmd_data(cmd, msg, len, MQTT_TIMEOUT_MS);
}
