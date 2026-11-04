/**
 ******************************************************************************
 * @file    ncp.h
 * @brief   Pilote « ST67W6X_Lite » du coprocesseur Wi-Fi 6 / BLE ST67W611M1
 *          (carte X-NUCLEO-67W61M1), version bare-metal sans RTOS.
 *
 *  NCP = « Network Co-Processor » : le module ST67W611M1 contient sa propre
 *  pile TCP/IP. Le STM32 ne fait que lui envoyer des commandes texte (« AT »)
 *  par le bus SPI et lire ses réponses ; c'est le module qui gère le Wi-Fi,
 *  les adresses IP, les connexions TCP et MQTT.
 *
 *  Prérequis : module programmé avec le firmware « mission T01 » (pile
 *  TCP/IP dans le module) fourni par X-CUBE-ST67W61 (st67w611m_mission_t01_v2.x.y.bin).
 *
 *  Organisation du pilote (Core/Src/st67w6x/), de la couche la plus basse
 *  à la plus haute :
 *    ncp_port.h  : adaptation matérielle (GPIO, SPI, temps), écrite pour
 *                  chaque carte (ici Core/Src/ncp_port_obcp.c)
 *    ncp_spi.c   : trames SPI (en-tête 8 octets, magic 0x55AA)
 *    ncp_at.c    : commandes AT, réponses, évènements, données binaires
 *    ncp_wifi.c  : scan, station, point d'accès
 *    ncp_net.c   : IP, DNS, ping, SNTP, serveur TCP
 *    ncp_mqtt.c  : client MQTT du module
 *    ncp_fwu.c   : mise à jour du firmware du module (OTA par le SPI)
 *    ncp.c       : démarrage du module, boucle de traitement (ce fichier)
 *
 *  Utilisation type (voir app_module_start() dans app.c) :
 *    ncp_start(&info);   puis, dans la boucle principale :   ncp_poll();
 ******************************************************************************
 */
#ifndef NCP_H
#define NCP_H

#include "ncp_port.h"
#include "ncp_spi.h"
#include "ncp_at.h"
#include "ncp_wifi.h"
#include "ncp_net.h"
#include "ncp_mqtt.h"
#include "ncp_fwu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Codes d'erreur propres au démarrage (les autres sont dans ncp_at.h, ncp_wifi.h...) */
#define NCP_ERR_NOREADY   (-40)   /* pas de message « ready » au démarrage   */
#define NCP_ERR_ARCH      (-41)   /* firmware T02 (LwIP sur l'hôte) détecté   */

/** Versions et réglages lus dans le module par ncp_start(). */
typedef struct
{
  bool    ok;                     /* module démarré et reconnu              */
  int     net_mode;               /* 1 : pile réseau dans le module (T01)   */
  int     clock_source;           /* 1 : oscillateur RC interne             */
  char    at_version[64];         /* version du jeu de commandes AT         */
  char    sdk_version[48];        /* version du firmware (2.0.x)            */
  char    mac_version[48];        /* version de la couche radio Wi-Fi 6     */
} ncp_info_t;

/**
 * @brief  Démarre le module : impulsion sur CHIP_EN (reset), attente du
 *         message « ready », lecture des versions et vérification que le
 *         firmware est bien un « mission T01 ».
 * @param  info  structure remplie avec les versions lues (obligatoire).
 * @retval AT_OK (0) si le module est prêt, sinon NCP_ERR_NOREADY,
 *         NCP_ERR_ARCH ou une erreur AT (valeur négative).
 */
int        ncp_start(ncp_info_t *info);

/**
 * @brief  Traite les trames reçues du module (réponses, évènements Wi-Fi,
 *         données TCP, messages MQTT). Non bloquante : à appeler le plus
 *         souvent possible depuis la boucle principale (app_idle()).
 */
void       ncp_poll(void);

/**
 * @brief  Indique que le module a redémarré de lui-même (message « ready »
 *         reçu hors de ncp_start()), par exemple après une chute de tension.
 * @retval true une seule fois par redémarrage (l'indicateur est remis à zéro).
 */
bool       ncp_restarted(void);

/**
 * @brief  Texte en français d'un code d'erreur NCP_ERR_xxx ou NCP_FWU_ERR_xxx.
 * @param  err  code d'erreur (négatif).
 * @retval chaîne constante, jamais NULL.
 */
const char *ncp_err_str(int err);

/**
 * @brief  Accès bas niveau (commande « at » du terminal) : envoie une ligne AT
 *         brute et transmet chaque ligne reçue en réponse à cb.
 * @param  cmd         commande sans CR/LF, par exemple "AT+GMR".
 * @param  cb          appelée pour chaque ligne de réponse.
 * @param  ctx         pointeur libre transmis à cb.
 * @param  timeout_ms  délai maximal d'attente de OK / ERROR.
 * @retval AT_OK, AT_ERROR ou AT_TIMEOUT.
 */
int        ncp_raw(const char *cmd, at_line_cb_t cb, void *ctx, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* NCP_H */
