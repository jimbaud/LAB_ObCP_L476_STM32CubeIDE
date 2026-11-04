/**
 ******************************************************************************
 * @file    app_config.h
 * @brief   Paramètres de compilation et valeurs par défaut de la démo.
 *          Les réglages modifiables à chaud (SSID, broker MQTT, fuseau...)
 *          se font depuis le terminal (« set », « save ») : ce fichier ne
 *          fixe que les valeurs d'usine.
 ******************************************************************************
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define APP_NAME              "Démonstrateur Wi-Fi 6 ST67W611M1"
#define APP_BOARD             "ObCP (STM32L476RG) + X-NUCLEO-67W61M1"
#define APP_VERSION           "1.2.0"          /* 1.2 : démarrage automatique du serveur web, code commenté */

/* ---- Valeurs d'usine ---------------------------------------------------- */
#define DEF_COUNTRY           "EU"              /* CN, JP, US, EU ou 00 */
#define DEF_NTP_SERVER        "pool.ntp.org"
#define DEF_TZ_MIN            60                /* UTC+1 (Paris) */
#define DEF_DST_EU            1                 /* heure d'été européenne */
#define DEF_AP_PASS           "obcpdemo"        /* 8 caractères minimum (WPA2) */
#define DEF_AP_CHANNEL        6
#define DEF_AP_IP             { 10U, 19U, 96U, 1U }  /* sous-réseau par défaut du firmware */
#define DEF_WEB_PORT          80
#define DEF_MQTT_HOST         "broker.emqx.io"  /* broker public de test (pas de TLS) */
#define DEF_MQTT_PORT         1883
#define DEF_MQTT_PERIOD_S     10
#define DEF_TRACK_PERIOD_MS   1000

/* ---- Préfixes des noms générés à partir de l'identifiant unique (UID) --- */
#define AP_SSID_PREFIX        "ObCP-WiFi-"
#define HOSTNAME_PREFIX       "obcp-"
#define MQTT_TOPIC_PREFIX     "obcp/"

/* ---- Divers --------------------------------------------------------------- */
#define WIFI_RECONNECT_DELAY_MS   10000U        /* attente entre deux tentatives
                                                   (doublée à chaque échec, 60 s au plus) */
#define NTP_SYNC_TIMEOUT_MS       20000U        /* attente de la réponse du serveur NTP */
#define NTP_RESYNC_PERIOD_MS      (6UL * 3600UL * 1000UL)   /* nouvelle synchro toutes les 6 h */

#endif /* APP_CONFIG_H */
