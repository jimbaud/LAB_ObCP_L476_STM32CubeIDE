/**
 ******************************************************************************
 * @file    commands.h
 * @brief   Interpréteur des commandes reçues par le port COM virtuel (USB)
 *          ou par Bluetooth : analyse de la ligne, action, réponse.
 *
 *  Les commandes et leurs réponses sont exactement celles de la version 2024
 *  (OBCP_template_2024), au caractère près, pour que les outils existants
 *  (scripts Python, VI LabVIEW, application Android) fonctionnent sans
 *  changement. Les réponses se terminent par « \n\r » (saut de ligne puis
 *  retour chariot), comme avant.
 *
 *  | Commande          | Action                                | Réponse                 |
 *  |-------------------|---------------------------------------|-------------------------|
 *  | r<n> ou R<n>      | LED rouge, n de 0 à 65535             | Red = n                 |
 *  | v<n> ou V<n>      | LED verte                             | Green = n               |
 *  | b<n> ou B<n>      | LED bleue                             | Blue = n                |
 *  | m<n> ou M<n>      | sortie PWM du bornier (moteur)        | PWM = n                 |
 *  | a ou A            | lit l'accéléromètre (en g)            | Acceleration : X=.. Y=.. Z=.. |
 *  | t ou T            | lit la température du capteur (°C)    | T = n                   |
 *  | ?                 | aide (seulement vers l'interface qui a posé la question) | texte d'aide |
 *  | cls               | efface l'écran du terminal (USB seulement) | séquence ANSI      |
 *  | start timer       | envoi des mesures par BLE chaque seconde | (aucune)             |
 *  | stop timer        | arrête cet envoi                      | (aucune)                |
 *  | #rst              | éteint LED et PWM, arrête la minuterie | (aucune)               |
 *  | autre             | -                                     | False command           |
 *
 *  Comme avant, une réponse est envoyée à l'interface qui a reçu la commande
 *  et recopiée sur l'autre si elle est active (USB énumérée, téléphone
 *  connecté) : on voit dans le terminal du PC les commandes envoyées depuis
 *  le téléphone, et inversement.
 *
 *  Nouveautés de la version 2 (cas d'erreur seulement) : une valeur absente,
 *  non décimale ou hors de 0..65535 n'est plus appliquée et donne
 *  « False value (0-65535) » ; un capteur absent donne « Accelerometer
 *  error » ; une ligne vide est ignorée.
 ******************************************************************************
 */
#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

/** Interface par laquelle une commande est arrivée. */
typedef enum
{
  CMD_SRC_USB = 0,   /**< port COM virtuel (câble USB) */
  CMD_SRC_BLE        /**< Bluetooth (téléphone) */
} cmd_source_t;

/** Commandes reconnues par commands_parse(). */
typedef enum
{
  CMD_EMPTY = 0,      /**< ligne vide ou faite d'espaces : rien à faire */
  CMD_RED,            /**< r<n> */
  CMD_GREEN,          /**< v<n> */
  CMD_BLUE,           /**< b<n> */
  CMD_MOTOR,          /**< m<n> */
  CMD_ACCEL,          /**< a */
  CMD_TEMPERATURE,    /**< t */
  CMD_HELP,           /**< ? */
  CMD_CLEAR_SCREEN,   /**< cls */
  CMD_TIMER_START,    /**< start timer */
  CMD_TIMER_STOP,     /**< stop timer */
  CMD_RESET,          /**< #rst */
  CMD_BAD_VALUE,      /**< r, v, b ou m avec une valeur absente ou invalide */
  CMD_UNKNOWN         /**< commande inconnue */
} cmd_id_t;

/** Résultat de l'analyse d'une ligne. */
typedef struct
{
  cmd_id_t id;        /**< commande reconnue */
  uint16_t value;     /**< valeur de r, v, b, m (0 à 65535) */
} cmd_t;

/**
 * @brief  Analyse une ligne de commande, sans rien exécuter (fonction
 *         « pure », testée sur PC dans Tests/test_unit.c).
 * @param  line  ligne terminée par '\0', sans caractère de fin de ligne
 * @retval commande reconnue et sa valeur
 */
cmd_t commands_parse(const char *line);

/**
 * @brief  Analyse et exécute une ligne, puis envoie la réponse.
 * @param  line  ligne terminée par '\0'
 * @param  src   interface d'où vient la ligne (pour savoir où répondre)
 */
void commands_execute(const char *line, cmd_source_t src);

/**
 * @brief  Répond « False command » à une ligne rejetée avant analyse (ligne
 *         trop longue, voir line_builder.h).
 * @param  src   interface d'où vient la ligne
 */
void commands_reject(cmd_source_t src);

/**
 * @brief  Texte d'aide affiché par la commande « ? ».
 */
const char *commands_help_text(void);

#endif /* COMMANDS_H */
