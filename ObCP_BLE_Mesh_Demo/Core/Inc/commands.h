/**
 ******************************************************************************
 * @file    commands.h
 * @brief   Commandes tapées dans le terminal (port COM virtuel USB) : analyse
 *          de la ligne, action, réponse.
 *
 *  « dest » désigne un nœud (1 à 239), un groupe (g1 à g15) ou tous les
 *  nœuds (tous, ou *). Liste complète : commande « aide », ou guide de
 *  découverte (Doc/Guide_Decouverte_BLE_Mesh_ObCP.pdf), « Référence des
 *  commandes ».
 *
 *    Voir      info, voisins, noeuds, stats [raz], mesure
 *    Envoyer   msg dest texte, led dest couleur [s], vague, ident dest [s],
 *              ping dest, trace n, mesure dest, regle dest réglage valeur
 *    Régler    ttl, relais, suppr, rssimin, puissance, hello, auto, flash
 *              (suivis d'une valeur ; sans valeur : affichent la valeur)
 *              id n | id auto, reseau n, groupe [+g|-g], bloque [n],
 *              debloque n|tous, radio [auto|simultane|alterne]
 *    Divers    journal on|off, pc on|off, sauve, oublie, cls, #ota, aide
 *
 *  Deux étapes bien séparées, comme dans le programme modèle :
 *    1. commands_parse() lit la ligne et dit de quelle commande il s'agit,
 *       avec ses paramètres (sans rien exécuter : testée sur PC) ;
 *    2. commands_execute() agit et répond.
 ******************************************************************************
 */
#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "led_fx.h"
#include "settings.h"

/** Commandes reconnues. */
typedef enum
{
  CMD_EMPTY = 0,      /**< ligne vide */
  CMD_ERROR,          /**< ligne incorrecte : voir cmd_t.error */
  CMD_HELP,           /**< aide, ? */
  CMD_INFO,           /**< info */
  CMD_NEIGHBORS,      /**< voisins */
  CMD_NODES,          /**< noeuds */
  CMD_STATS,          /**< stats */
  CMD_STATS_CLEAR,    /**< stats raz */
  CMD_MEASURE_LOCAL,  /**< mesure (sans destination) */
  CMD_MEASURE_REQ,    /**< mesure dest */
  CMD_MSG,            /**< msg dest texte */
  CMD_LED,            /**< led dest couleur [s] */
  CMD_WAVE,           /**< vague */
  CMD_IDENT,          /**< ident dest [s] */
  CMD_PING,           /**< ping dest */
  CMD_TRACE,          /**< trace n */
  CMD_REMOTE_SET,     /**< regle dest réglage valeur */
  CMD_SET,            /**< ttl, relais... [valeur] */
  CMD_ID,             /**< id [n|auto] */
  CMD_NET,            /**< reseau [n] */
  CMD_GROUP,          /**< groupe [+g|-g] */
  CMD_BLOCK,          /**< bloque [n] */
  CMD_UNBLOCK,        /**< debloque n|tous */
  CMD_RADIO,          /**< radio [auto|simultane|alterne] */
  CMD_JOURNAL,        /**< journal [on|off] */
  CMD_PC,             /**< pc [on|off] */
  CMD_SAVE,           /**< sauve */
  CMD_FORGET,         /**< oublie */
  CMD_CLS,            /**< cls */
  CMD_OTA             /**< #ota */
} cmd_id_t;

/** Valeurs de « radio ». */
#define CMD_RADIO_AUTO          0
#define CMD_RADIO_SIMULTANEOUS  1
#define CMD_RADIO_ALTERNATE     2

/** Résultat de l'analyse d'une ligne. */
typedef struct
{
  cmd_id_t    id;
  uint8_t     dst;                    /**< destination (nœud, groupe, tous) */
  setting_t   key;                    /**< réglage (CMD_SET, CMD_REMOTE_SET) */
  bool        has_value;              /**< une valeur a été donnée */
  int32_t     value;                  /**< valeur : réglage, numéro, réseau,
                                           groupe, durée en s, on (1) / off (0)... */
  bool        join;                   /**< groupe : rejoindre (true) ou quitter */
  led_color_t color;                  /**< couleur (CMD_LED) */
  char        text[CMD_LINE_MAX + 1U];/**< texte (CMD_MSG) */
  const char *error;                  /**< message d'erreur (CMD_ERROR) */
  bool        bad_value;              /**< CMD_ERROR : valeur hors limites pour
                                           le réglage key (error = valeurs permises) */
} cmd_t;

/**
 * @brief  Analyse une ligne de commande, sans rien exécuter.
 * @param  line  ligne terminée par '\0'
 * @param  cmd   résultat
 */
void commands_parse(const char *line, cmd_t *cmd);

/**
 * @brief  Analyse et exécute une ligne, puis répond dans le terminal.
 */
void commands_execute(const char *line);

/**
 * @brief  Répond à une ligne rejetée car trop longue (voir line_builder.h).
 */
void commands_reject(void);

/** Texte d'aide de la commande « aide ». */
const char *commands_help_text(void);

#endif /* COMMANDS_H */
