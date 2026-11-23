/** @file oa_config.h
 * @brief Réglages de physique, avec unités, indépendants du matériel.
 * @details Modifier ici les constantes de jeu puis relancer les tests. Les
 * durées comptent des pas de 1/60 s ; les vitesses sont en pixels par seconde.
 * SPDX-License-Identifier: MIT
 */
#ifndef OA_CONFIG_H
#define OA_CONFIG_H
/** Version du jeu, affichée par la console USB et le simulateur. */
#define OA_VERSION "1.0.0"
enum {
    OA_WALK_SPEED = 96,           /**< Vitesse horizontale en marche, px/s. */
    OA_RUN_SPEED = 160,           /**< Vitesse horizontale en course, px/s. */
    OA_BOOTS_SPEED_EXTRA = 32,    /**< Gain des bottes, px/s. */
    OA_MOVE_ACCELERATION = 1000,  /**< Accélération avec direction, px/s². */
    OA_BRAKE_ACCELERATION = 1200, /**< Freinage sans direction, px/s². */
    OA_GRAVITY = 1000,            /**< Gravité, px/s², vers le bas. */
    OA_TERMINAL_SPEED = 640,      /**< Vitesse de chute maximale, px/s. */
    OA_JUMP_SPEED = 380,          /**< Module de l'impulsion de saut, px/s. */
    OA_BOOTS_JUMP_SPEED = 420,    /**< Impulsion avec bottes, px/s. */
    OA_RELEASE_SPEED = 150,       /**< Montée maximale après relâchement, px/s. */
    OA_COYOTE_TICKS = 6,          /**< Grâce après le dernier contact au sol. */
    OA_JUMP_BUFFER_TICKS = 6,     /**< Durée d'une demande de saut en attente. */
    OA_MOVE_LIMIT = 64,           /**< Déplacement maximal accepté, px/pas. */
    OA_RESPAWN_PROTECTION = 90,   /**< Protection au retour en jeu, pas. */
    OA_POWERUP_TICKS = 600,       /**< Durée d'un bonus ramassé, dix secondes. */
    OA_MAX_BLOCK_CHANGES = 32     /**< Modifications de blocs sauvegardables. */
};
#endif
