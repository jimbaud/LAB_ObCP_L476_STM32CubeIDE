/**
 ******************************************************************************
 * @file    app.h
 * @brief   Application ObCP : démarrage (app_init) et boucle principale
 *          (app_loop), appelés par main.c.
 *
 *  Le programme n'utilise pas de système d'exploitation : main() appelle
 *  app_init() une fois, puis app_loop() sans fin. Chaque tour de boucle
 *  regarde ce qui est arrivé (commande, bouton, seconde écoulée...), fait
 *  le travail correspondant sans jamais attendre, puis endort le processeur
 *  jusqu'à la prochaine interruption. Ce modèle s'appelle une « super
 *  boucle » (« super loop », ou « bare metal »).
 ******************************************************************************
 */
#ifndef APP_H
#define APP_H

/**
 * @brief  Démarre l'application : sorties PWM, accéléromètre, bouton,
 *         Bluetooth, puis signal de démarrage par la LED (voir app.c).
 *         À appeler une fois, après les MX_xxx_Init() de main.c.
 */
void app_init(void);

/**
 * @brief  Un tour de la boucle principale. À appeler sans fin dans le
 *         while (1) de main.c.
 */
void app_loop(void);

#endif /* APP_H */
