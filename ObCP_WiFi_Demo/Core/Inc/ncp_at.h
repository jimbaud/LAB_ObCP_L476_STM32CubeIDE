/**
 ******************************************************************************
 * @file    ncp_at.h
 * @brief   Moteur de commandes AT du ST67W611M1 (firmware « mission T01 »).
 *
 *  Les commandes AT sont des lignes de texte envoyées au module (par exemple
 *  « AT+CWJAP="MaBox","motdepasse" » pour se connecter à un réseau). Le module
 *  répond par des lignes de texte, puis par « OK » ou « ERROR ».
 *
 *  Le flux AT reçu du module est traité comme un flot d'octets :
 *   - lignes terminées par CR/LF : réponses (OK, ERROR, +XXX:...) ou
 *     événements non sollicités (URC : +CW:, +CIP:, +IPD:, +MQTT:, ...) ;
 *   - trames « directes » contenant des données binaires :
 *       +CIPRECVDATA:<n>,<n octets>
 *       +MQTT:SUBRECV:<lien>,<lg topic>,<lg msg>,"<topic>",<msg>
 *       >   (invite d'envoi de données après AT+CIPSEND / AT+MQTTPUBRAW)
 *
 *  Les fonctions at_cmd*() sont bloquantes (avec délai de garde) ; pendant
 *  l'attente, les URC continuent d'être traités. Un gestionnaire d'URC ne
 *  doit JAMAIS envoyer lui-même de commande AT (il ne fait que mettre à jour
 *  des drapeaux / variables d'état).
 *
 *  Exemple :
 *    at_cmd("AT+CWJAP?", "+CWJAP:", on_line, &ctx, 2000)
 *  envoie la commande, appelle on_line() pour chaque ligne « +CWJAP:... »
 *  reçue (sans le préfixe) et rend AT_OK quand le module a répondu OK.
 ******************************************************************************
 */
#ifndef NCP_AT_H
#define NCP_AT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Résultat d'une commande AT (0 = succès, négatif = échec). */
typedef enum
{
  AT_OK      =  0,
  AT_ERROR   = -1,   /* le module a répondu ERROR                     */
  AT_TIMEOUT = -2,   /* pas de réponse dans le délai                   */
  AT_BUSY    = -3,   /* commande déjà en cours (appel réentrant)       */
  AT_IO      = -4,   /* erreur de transport SPI                        */
  AT_PARAM   = -5,   /* paramètre invalide (commande trop longue, ...) */
  AT_SENDFAIL= -6    /* données refusées / non envoyées                */
} at_status_t;

#define AT_CMD_MAX        512U    /* longueur max d'une commande            */
#define AT_LINE_MAX       256U    /* longueur max d'une ligne de réponse    */
#define AT_STREAM_SIZE   3072U    /* tampon de réception du flux AT         */

/**
 * @brief  Rappel recevant une ligne de réponse à la commande en cours.
 * @param  args  ligne reçue, préfixe déjà retiré (« 6,-47,... »).
 * @param  ctx   pointeur libre passé à at_cmd() (structure à remplir...).
 */
typedef void (*at_line_cb_t)(const char *args, void *ctx);

/**
 * @brief  Rappel recevant un événement non sollicité (ligne complète,
 *         par exemple « +CW:DISCONNECTED »).
 */
typedef void (*at_urc_cb_t)(const char *line);

/**
 * @brief  Rappel recevant un message MQTT (trame +MQTT:SUBRECV).
 * @param  topic  topic du message, sans guillemets.
 * @param  msg    contenu (pas forcément terminé par '\0').
 * @param  len    longueur du contenu.
 */
typedef void (*at_mqtt_cb_t)(const char *topic, const uint8_t *msg, uint16_t len);

/** Compteurs du flux AT (commande « stats »). */
typedef struct
{
  uint32_t lines;            /* lignes reçues                          */
  uint32_t urcs;             /* dont événements non sollicités         */
  uint32_t overflows;        /* débordements du tampon de réception    */
  uint32_t other_frames;     /* trames SPI d'un autre type (ignorées)  */
} at_stats_t;

/**
 * @brief  Initialise le moteur AT et la couche SPI (appelée par ncp_start()).
 * @param  urc_cb  fonction recevant les événements non sollicités.
 */
void        at_init(at_urc_cb_t urc_cb);

/** @brief  Désigne la fonction qui recevra les messages MQTT (demo_mqtt.c). */
void        at_set_mqtt_cb(at_mqtt_cb_t cb);

/** @brief  Active ou coupe le journal du trafic AT (commande « log on|off »). */
void        at_set_log(bool on);

/** @brief  État du journal du trafic AT. */
bool        at_get_log(void);

/** @brief  Vrai si une commande AT est en cours (attente de sa réponse). */
bool        at_busy(void);

/**
 * @brief  Lit les trames en attente dans le module (16 au plus) et traite
 *         leur contenu. À appeler régulièrement (boucle principale) ; elle
 *         est aussi appelée pendant l'attente des réponses.
 */
void        at_poll(void);

/**
 * @brief  Attend la ligne « ready » émise par le module après son démarrage.
 * @param  timeout_ms  délai maximal.
 * @retval AT_OK ou AT_TIMEOUT.
 */
at_status_t at_wait_ready(uint32_t timeout_ms);

/** @brief  Oublie un « ready » déjà reçu (avant de redémarrer le module). */
void        at_clear_ready(void);

/**
 * @brief  Envoie une commande (sans CR/LF) et attend OK / ERROR.
 * @param  cmd         commande, par exemple "AT+CWJAP?".
 * @param  prefix      préfixe des lignes de réponse à transmettre à cb
 *                     ("+CWJAP:"), "" pour recevoir toutes les lignes,
 *                     NULL si aucune.
 * @param  cb          rappel recevant ces lignes (peut être NULL).
 * @param  ctx         pointeur libre transmis à cb.
 * @param  timeout_ms  délai maximal ; 0 : la commande est envoyée sans
 *                     attendre de réponse.
 * @retval AT_OK, AT_ERROR, AT_TIMEOUT, AT_BUSY, AT_PARAM ou AT_IO.
 */
at_status_t at_cmd(const char *cmd, const char *prefix, at_line_cb_t cb, void *ctx,
                   uint32_t timeout_ms);

/**
 * @brief  Comme at_cmd(), la commande étant construite à la manière de
 *         printf() : at_cmdf(2000, NULL, NULL, NULL, "AT+CWCOUNTRY=0,\"%s\"", pays).
 */
at_status_t at_cmdf(uint32_t timeout_ms, const char *prefix, at_line_cb_t cb, void *ctx,
                    const char *fmt, ...) __attribute__((format(printf, 5, 6)));

/**
 * @brief  Commande suivie d'un bloc de données (AT+CIPSEND=..., AT+MQTTPUBRAW=...) :
 *         attend l'invite '>', envoie les données (en plusieurs trames si
 *         besoin) puis attend « Recv n bytes » ou « SEND OK ».
 * @param  cmd         commande annonçant les données.
 * @param  data        données à envoyer.
 * @param  len         nombre d'octets (1 ou plus).
 * @param  timeout_ms  délai maximal d'attente de la confirmation.
 * @retval AT_OK, AT_SENDFAIL (données refusées), AT_TIMEOUT, AT_ERROR...
 */
at_status_t at_cmd_data(const char *cmd, const uint8_t *data, uint16_t len, uint32_t timeout_ms);

/**
 * @brief  Désigne le tampon où ranger les données de la prochaine trame
 *         +CIPRECVDATA (lecture de données TCP reçues, voir net_recv()).
 * @param  buf  tampon de destination.
 * @param  max  taille du tampon.
 * @param  got  reçoit le nombre d'octets copiés.
 */
void        at_set_recv_sink(uint8_t *buf, uint16_t max, uint16_t *got);

/** @brief  Compteurs du flux AT (commande « stats »). */
const at_stats_t *at_stats(void);

/** @brief  Texte en français d'un code at_status_t. */
const char *at_status_str(at_status_t st);

/* ---- Outils d'analyse des réponses ---------------------------------------- */
/**
 * @brief  Découpe une liste "a","b",c en champs (séparateur ',', guillemets
 *         retirés). Modifie la chaîne en place.
 * @param  s     chaîne à découper (modifiée).
 * @param  argv  tableau recevant un pointeur vers chaque champ.
 * @param  max   taille du tableau argv.
 * @retval nombre de champs trouvés.
 */
int         at_split(char *s, char **argv, int max);

/** @brief  Vrai si la chaîne s commence par prefix. */
bool        at_starts(const char *s, const char *prefix);

/**
 * @brief  Lit une adresse IPv4 « a.b.c.d » (guillemet initial accepté).
 * @retval true si l'adresse est valide (ip rempli).
 */
bool        at_parse_ip(const char *s, uint8_t ip[4]);

/**
 * @brief  Lit une adresse MAC « aa:bb:cc:dd:ee:ff » (guillemet initial accepté).
 * @retval true si l'adresse est valide (mac rempli).
 */
bool        at_parse_mac(const char *s, uint8_t mac[6]);

/**
 * @brief  Journal du trafic AT, appelé pour chaque ligne émise ou reçue
 *         quand le journal est actif. Fonction « faible » (weak) : la
 *         version vide du pilote est remplacée par celle de app.c, qui
 *         affiche les lignes dans le terminal en masquant les mots de passe.
 * @param  dir   "->" (vers le module) ou "<-" (reçu du module).
 * @param  text  texte de la ligne (pas forcément terminé par '\0').
 * @param  len   longueur du texte.
 */
void        ncp_at_log_hook(const char *dir, const char *text, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* NCP_AT_H */
