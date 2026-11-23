/**
 ******************************************************************************
 * @file    app.h
 * @brief   Application : démarrage (app_init) et boucle principale
 *          (app_loop), appelés par main.c.
 *
 *  Le programme n'utilise pas de système d'exploitation : main() appelle
 *  app_init() une fois, puis app_loop() sans fin. Chaque tour de boucle
 *  regarde ce qui est arrivé (trame radio, commande, bouton, seconde
 *  écoulée...), fait le travail correspondant sans jamais attendre, puis
 *  endort le processeur jusqu'à la prochaine interruption. Ce modèle
 *  s'appelle une « super boucle » (« super loop », ou « bare metal »).
 ******************************************************************************
 */
#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  Démarre l'application : sorties PWM, accéléromètre, bouton,
 *         module Bluetooth, réseau maillé, puis signal de démarrage par la
 *         LED (voir app.c). À appeler une fois, après les MX_xxx_Init().
 */
void app_init(void);

/**
 * @brief  Un tour de la boucle principale. À appeler sans fin dans le
 *         while (1) de main.c.
 */
void app_loop(void);

/**
 * @brief  Demande le redémarrage dans le chargeur Bluetooth ObCP-OTA
 *         (commande « #ota »). Le redémarrage a lieu 300 ms plus tard, le
 *         temps que la réponse parte vers le PC.
 */
void app_request_ota(void);

/**
 * @brief  Version du programme : true pour la version OTA (compilée pour
 *         0x08080000, derrière le chargeur Bluetooth ObCP-OTA), false pour
 *         la version USB (compilée pour 0x08000000).
 * @note   Les deux versions ne diffèrent que par le script d'édition de
 *         liens, qui place la table des vecteurs (g_pfnVectors, fichier de
 *         démarrage) au début de la zone du programme : son adresse suffit
 *         à reconnaître la version.
 */
bool app_is_ota(void);

/**
 * @brief  Indique si une adresse est dans la zone de la version OTA
 *         (APP_OTA_FLASH_START à APP_OTA_FLASH_END, app_config.h).
 *         Utilisée par app_is_ota() ; séparée pour les tests sur PC.
 */
bool app_address_is_ota(uintptr_t address);

#endif /* APP_H */
