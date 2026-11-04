/**
 ******************************************************************************
 * @file    ncp_mqtt.h
 * @brief   Client MQTT intégré au ST67W611M1 (architecture T01).
 *
 *  MQTT en bref : un serveur central, le « broker », relaie des messages.
 *  Un client « publie » un message sur un « topic » (un nom hiérarchique,
 *  par exemple obcp/A1B2C3/telemetry) ; tous les clients « abonnés » à ce
 *  topic le reçoivent. La carte publie sa télémétrie et s'abonne au topic
 *  .../cmd pour recevoir des ordres (démo 4, demo_mqtt.c).
 *  « LWT » (Last Will and Testament) : message que le broker publie à la
 *  place de la carte si elle disparaît sans prévenir (« offline »).
 ******************************************************************************
 */
#ifndef NCP_MQTT_H
#define NCP_MQTT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_ERR_NOTCONN   (-30)   /* pas connecté au broker */

/** État MQTT partagé, mis à jour par les événements +MQTT: (défini dans ncp_mqtt.c). */
typedef struct
{
  volatile bool     connected;
  volatile bool     evt_connected;
  volatile bool     evt_disconnected;
  volatile uint32_t rx_count;
} mqtt_ctx_t;

extern mqtt_ctx_t g_mqtt;

/**
 * @brief  Rappel de réception d'un message sur un topic souscrit. Appelé
 *         depuis l'analyse du flux AT : copier les données, ne pas envoyer
 *         de commande AT dans ce rappel (voir mqttapp_on_message()).
 */
typedef void (*mqtt_msg_cb_t)(const char *topic, const uint8_t *msg, uint16_t len);

/**
 * @brief  Traite les événements +MQTT:CONNECTED / DISCONNECTED (appelée par
 *         urc_router()).
 * @retval true si la ligne commence par « +MQTT: ».
 */
bool mqtt_urc(const char *line);

/** @brief  Désigne la fonction qui recevra les messages des topics souscrits. */
void mqtt_set_msg_cb(mqtt_msg_cb_t cb);

/**
 * @brief  Configure le client MQTT avant la connexion.
 * @param  scheme       0 : TCP sans authentification, 1 : TCP + identifiant / mot de passe.
 * @param  client_id    identifiant unique du client auprès du broker.
 * @param  user, pass   identifiant et mot de passe ("" si scheme = 0).
 * @param  keepalive_s  période de « signe de vie » envoyée au broker (s).
 * @param  lwt_topic    topic du message de dernière volonté.
 * @param  lwt_msg      message publié par le broker si la carte disparaît.
 * @param  lwt_retain   true : le broker conserve ce message pour les nouveaux abonnés.
 */
int  mqtt_configure(uint8_t scheme, const char *client_id, const char *user, const char *pass,
                    uint16_t keepalive_s, const char *lwt_topic, const char *lwt_msg, bool lwt_retain);

/**
 * @brief  Connexion au broker (bloquante) : attend l'événement +MQTT:CONNECTED.
 * @param  host        adresse du broker (broker.emqx.io).
 * @param  port        port (1883 : MQTT sans chiffrement).
 * @param  timeout_ms  délai maximal (0 : 10 s).
 * @retval AT_OK ou MQTT_ERR_NOTCONN.
 */
int  mqtt_connect(const char *host, uint16_t port, uint32_t timeout_ms);

/** @brief  Ferme la connexion au broker. */
int  mqtt_disconnect(void);

/**
 * @brief  Abonnement à un topic (les jokers + et # sont admis).
 * @param  qos  qualité de service : 0 (au plus une fois) ou 1 (au moins une fois).
 */
int  mqtt_subscribe(const char *topic, uint8_t qos);

/**
 * @brief  Publie un message (données binaires admises).
 * @param  topic   topic complet.
 * @param  msg     contenu du message.
 * @param  len     longueur (1 ou plus).
 * @param  qos     qualité de service (0 ou 1).
 * @param  retain  true : le broker garde ce message pour les futurs abonnés.
 * @retval AT_OK, MQTT_ERR_NOTCONN, AT_SENDFAIL...
 */
int  mqtt_publish(const char *topic, const uint8_t *msg, uint16_t len, uint8_t qos, bool retain);

#ifdef __cplusplus
}
#endif

#endif /* NCP_MQTT_H */
