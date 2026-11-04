/**
 ******************************************************************************
 * @file    app.h
 * @brief   Application de démonstration : initialisation, boucle principale,
 *          tâches de fond et fonctions communes aux quatre démos.
 *
 *  Point d'entrée de l'application, appelé par main.c (code généré) :
 *    app_init()  une fois, après l'initialisation du matériel ;
 *    app_loop()  sans fin, dans la boucle while (1) de main().
 *
 *  Le programme n'utilise pas de système d'exploitation temps réel (RTOS) :
 *  tout se déroule dans cette boucle, qui ne doit jamais rester bloquée
 *  longtemps. Chaque tour lit le terminal, exécute la commande éventuelle,
 *  puis appelle app_idle() : chaque démo y fait avancer ses « tâches de
 *  fond » (reconnexion Wi-Fi, requêtes web, télémétrie MQTT...). Les
 *  commandes longues (scan, ping, track...) appellent elles aussi app_idle()
 *  pendant leurs attentes.
 ******************************************************************************
 */
#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ncp.h"
#include "timeutil.h"

#ifdef __cplusplus
extern "C" {
#endif

/** État général de l'application (variable globale g_app, définie dans app.c). */
typedef struct
{
  bool       ncp_ok;          /* module démarré et initialisé           */
  int        ncp_err;         /* dernière erreur de démarrage           */
  ncp_info_t ncp;             /* versions du firmware du module         */
  uint8_t    sta_mac[6];
  uint8_t    ap_mac[6];
  uint32_t   module_restarts;
  bool       cfg_dirty;       /* configuration modifiée, non enregistrée */
  int        cfg_load_status; /* résultat de config_load() au démarrage   */
} app_t;

extern app_t g_app;

/* Codes d'erreur propres à l'application (le pilote utilise -1 à -43) */
#define APP_ERR_NOMODULE   (-50)   /* module Wi-Fi absent ou non initialisé */
#define APP_ERR_NOSTA      (-51)   /* station non connectée                 */
#define APP_ERR_USAGE      (-52)   /* arguments invalides (aide affichée)   */
#define APP_ERR_FLASH      (-53)   /* écriture en flash impossible          */
#define APP_ERR_NOTFOUND   (-54)   /* réseau / élément introuvable          */
#define APP_ERR_CANCELED   (-55)   /* opération annulée par l'utilisateur   */
#define APP_ERR_TRANSFER   (-56)   /* transfert de fichier (YMODEM) échoué  */
#define APP_ERR_BADFILE    (-57)   /* fichier refusé (nom, en-tête, taille) */

/**
 * @brief  Initialisation de l'application (appelée par main.c, USER CODE 2) :
 *         console, configuration en flash, entrées / sorties, bannière,
 *         démarrage du module Wi-Fi, connexion et services automatiques.
 */
void        app_init(void);

/**
 * @brief  Un tour de la boucle principale (appelée sans fin par main.c) :
 *         lecture du terminal, exécution d'une commande, tâches de fond.
 */
void        app_loop(void);

/**
 * @brief  Tâches de fond : événements du module, reconnexion Wi-Fi, NTP,
 *         serveur web, MQTT, boutons. Appelée par la boucle principale et
 *         par les commandes longues pendant leurs attentes. Protégée contre
 *         les appels imbriqués.
 */
void        app_idle(void);

/**
 * @brief  (Re)démarre le module Wi-Fi et initialise les couches Wi-Fi et réseau.
 * @param  verbose  true : affiche la progression et les versions du firmware.
 * @retval AT_OK (0) si le module répond, sinon un code d'erreur négatif.
 */
int         app_module_start(bool verbose);

/** @brief  Affiche les versions du firmware et l'adresse MAC du module. */
void        app_print_module_info(void);

/**
 * @brief  Message en français pour tout code d'erreur du pilote ou de
 *         l'application (AT_xxx, WIFI_ERR_xxx, APP_ERR_xxx...).
 */
const char *app_err_str(int err);

/**
 * @brief  Affiche « ✗ <what> : <raison> » en rouge dans le terminal.
 * @retval err (pour écrire : return app_report(st, "connexion");).
 */
int         app_report(int err, const char *what);

/**
 * @brief  État de la carte en JSON (page web /api/status, télémétrie MQTT) :
 *         capteurs, LED, PWM, boutons, liaison Wi-Fi, heure.
 * @param  out        tampon de destination.
 * @param  len        taille du tampon.
 * @param  with_rssi  true : interroge le module pour le niveau du signal.
 * @retval longueur du texte JSON, 0 si le tampon est trop petit.
 */
size_t      app_status_json(char *out, size_t len, bool with_rssi);

/**
 * @brief  Services lancés à la mise sous tension, en clair (bannière,
 *         commandes « info » et « auto ») : « connexion à « Box », serveur web, MQTT ».
 */
void        app_autostart_summary(char *out, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
