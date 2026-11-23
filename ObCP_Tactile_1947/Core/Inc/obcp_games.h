/**
 * @file obcp_games.h
 * @brief Modèles, niveaux et commandes des trois jeux à inclinaison.
 * @ingroup obcp_apps
 *
 * Les trois modèles partagent GamePhase et un pas de 20 ms. Le labyrinthe utilise une
 * carte de cases, le casse-briques des masques de briques et la course un tableau
 * d'adversaires. Les positions sont flottantes, le rendu emploie des pixels entiers.
 * GAME_PAUSE fige la simulation. La course possède aussi un bref état de collision.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
#ifndef OBCP_GAMES_H
#define OBCP_GAMES_H

#include "obcp_gfx.h"

/**
 * @brief Dimensions, bornes et constantes du module.
 */
enum {
    GAME_STEP_MS = 20, /**< Pas de simulation de vingt millisecondes, soit 50 pas par seconde. */
    GAME_LEVELS = 10, /**< Dix niveaux par jeu, indices de zéro à neuf. */
    RACE_CARS = 6, /**< Six places d'adversaires au maximum dans la course. */
};
/**
 * @brief Phases de jeu partagées par les trois modèles.
 */
typedef enum {
    GAME_READY, /**< Positions préparées, attente du bouton Jouer. */
    GAME_RUN, /**< Simulation active à pas fixes. */
    GAME_PAUSE, /**< Simulation et chronomètre suspendus. */
    GAME_WON, /**< Objectif atteint, proposition du niveau suivant. */
    GAME_LOST, /**< Temps expiré, balle perdue ou collision. */
} GamePhase;

/**
 * @brief État physique et temporel du labyrinthe.
 *
 * La simulation avance par pas fixes de GAME_STEP_MS. Le rendu lit ce modèle sans
 * l’intégrer.
 */
typedef struct {
    float x; /**< Centre horizontal de la bille ou balle, en pixels flottants. */
    float y; /**< Centre vertical de la bille ou balle, en pixels flottants. */
    float vx; /**< Vitesse horizontale, en pixels par seconde. */
    float vy; /**< Vitesse verticale, en pixels par seconde. */
    uint32_t elapsed_ms; /**< Temps de jeu cumulé, en millisecondes. La pause ne l’incrémente pas. */
    uint8_t level; /**< Indice du niveau, de 0 à GAME_LEVELS moins un. */
    GamePhase phase; /**< Phase prête, en jeu, pause, victoire ou défaite. */
} Maze;
/**
 * @brief Préparer un niveau à son point de départ.
 *
 * Remet vitesses et temps à zéro. Un niveau hors plage revient à zéro et la phase initiale
 * demande l'appui Jouer.
 * @param[out] m État physique et temporel du labyrinthe, à initialiser par cet appel.
 *   Pointeur valide.
 * @param[in] level Indice de niveau à base zéro. Une valeur hors plage choisit le premier
 *   niveau.
 */
void maze_init(Maze *m, unsigned level);
/**
 * @brief Intégrer la physique et le temps sur vingt millisecondes.
 *
 * Calcule les vitesses avec inertie, avance séparément X et Y pour permettre le glissement
 * contre un mur, puis teste arrivée et expiration. Les emprises modifiées sont invalidées.
 * @param[in,out] m État physique et temporel du labyrinthe. Pointeur valide.
 * @param[in] tilt_x Commande écran X, adimensionnelle entre −1 et +1.
 * @param[in] tilt_y Commande écran Y, adimensionnelle entre −1 et +1.
 * @param[in,out] damage Liste de rectangles à recomposer. Pointeur valide.
 */
void maze_step(Maze *m, float tilt_x, float tilt_y, Damage *damage);
/**
 * @brief Dessiner les murs, les cases spéciales et la bille.
 *
 * Les rangées hors de la bande sont sautées. Le niveau sélectionne la carte, tandis que
 * les positions physiques ne sont jamais modifiées par ce rendu.
 * @param[in] m État physique et temporel du labyrinthe, consulté sans modification.
 *   Pointeur valide.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
void maze_draw(const Maze *m, Gfx *g);
/**
 * @brief Obtenir la cellule de départ commune aux cartes.
 *
 * Le départ est placé dans la case intérieure de coordonnées colonne un et ligne un.
 * @return Rectangle de la case de départ.
 */
Rect maze_start(void);
/**
 * @brief Obtenir la cellule d'arrivée commune aux cartes.
 *
 * L'arrivée occupe la dernière case intérieure en bas à droite. Le test de victoire et le
 * dessin lisent la même emprise.
 * @return Rectangle de la case d'arrivée.
 */
Rect maze_finish(void);
/**
 * @brief Vérifier si le disque de la bille touche une zone interdite.
 *
 * Limite les cases examinées à l'emprise du disque. Pour chaque mur, le point de rectangle
 * le plus proche du centre permet de tester la distance au carré.
 * @param[in] m État physique et temporel du labyrinthe, consulté sans modification.
 *   Pointeur valide.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 * @return true si la bille sortirait du plateau ou recouvrirait un mur.
 */
bool maze_solid(const Maze *m, float x, float y);
/**
 * @brief Convertir le temps restant en secondes affichables.
 *
 * Arrondit vers le haut pour que la dernière seconde soit visible. La valeur est nulle dès
 * que la limite du niveau est atteinte.
 * @param[in] m État physique et temporel du labyrinthe, consulté sans modification.
 *   Pointeur valide.
 * @return Nombre de secondes restantes, jamais négatif.
 */
uint32_t maze_seconds_left(const Maze *m);

/**
 * @brief Balle, raquette et briques du casse-briques.
 *
 * La simulation avance par pas fixes de GAME_STEP_MS. Le rendu lit ce modèle sans
 * l’intégrer.
 */
typedef struct {
    float x; /**< Centre horizontal de la bille ou balle, en pixels flottants. */
    float y; /**< Centre vertical de la bille ou balle, en pixels flottants. */
    float vx; /**< Vitesse horizontale, en pixels par seconde. */
    float vy; /**< Vitesse verticale, en pixels par seconde. */
    float paddle; /**< Centre horizontal de la raquette, en pixels flottants. */
    uint32_t bricks; /**< Masque des briques encore présentes. Un bit par brique, de 0 à 31. */
    uint32_t hard_bricks; /**< Masque des renforts encore présents, inclus dans bricks. */
    unsigned score; /**< Points du niveau courant, cinq par renfort et dix par destruction. */
    uint8_t level; /**< Indice du niveau, de 0 à GAME_LEVELS moins un. */
    GamePhase phase; /**< Phase prête, en jeu, pause, victoire ou défaite. */
} Breakout;
/**
 * @brief Préparer les positions et les briques d'un niveau.
 *
 * Un indice hors plage revient au premier niveau. L'état est prêt à jouer, avec les
 * masques, dimensions et vitesses choisis dans la table constante.
 * @param[out] b Balle, raquette et briques du casse-briques, à initialiser par cet
 *   appel. Pointeur valide.
 * @param[in] level Indice de niveau à base zéro. Une valeur hors plage choisit le premier
 *   niveau.
 */
void breakout_init(Breakout *b, unsigned level);
/**
 * @brief Avancer une simulation de vingt millisecondes.
 *
 * Seule GAME_RUN avance. La raquette rejoint 75 % de l'écart à sa cible, puis la balle
 * fait trois sous-pas. Les anciennes et nouvelles emprises sont invalidées. Une balle
 * sortie ou un masque vide termine le niveau.
 * @param[in,out] b Balle, raquette et briques du casse-briques. Pointeur valide.
 * @param[in] tilt_x Commande écran X, adimensionnelle entre −1 et +1.
 * @param[in,out] damage Liste de rectangles à recomposer. Pointeur valide.
 */
void breakout_step(Breakout *b, float tilt_x, Damage *damage);
/**
 * @brief Recomposer les briques, la raquette et la balle.
 *
 * Le modèle est lu sans modifier la simulation. Les briques renforcées reçoivent un cadre
 * clair. Le clipping des primitives limite le travail à la bande demandée.
 * @param[in] b Balle, raquette et briques du casse-briques, consulté sans modification.
 *   Pointeur valide.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
void breakout_draw(const Breakout *b, Gfx *g);
/**
 * @brief Calculer l'emprise d'une brique.
 *
 * L'indice va de 0 à 31. Le quotient et le reste de la division par huit donnent la rangée
 * et la colonne.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle de la brique, en pixels.
 */
Rect breakout_brick(unsigned i);

/**
 * @brief Types de véhicules admis par les niveaux de course.
 */
typedef enum {
    VEHICLE_CAR, /**< Voiture, emprise 29 × 32 pixels. */
    VEHICLE_BIKE, /**< Moto étroite, emprise 15 × 27 pixels. */
    VEHICLE_VAN, /**< Fourgonnette, emprise 35 × 44 pixels. */
    VEHICLE_TRUCK, /**< Camion, emprise 43 × 56 pixels. */
} Vehicle;
/**
 * @brief Adversaire occupant une voie de la course.
 *
 * Le type détermine largeur et hauteur. Une place inactive peut être réutilisée par le
 * générateur de trafic.
 */
typedef struct {
    float y; /**< Ordonnée du bord supérieur du véhicule, en pixels flottants. */
    uint8_t lane; /**< Voie de 0 à 2, utilisée pour retrouver le centre horizontal. */
    bool active; /**< true si le véhicule doit avancer et être dessiné. */
    Vehicle kind; /**< Voiture, moto, fourgonnette ou camion. */
} Rival;
/**
 * @brief État de la route, du joueur et de la progression.
 *
 * Les adversaires partagent la vitesse du niveau. Le compte à rebours de collision reste
 * animé après la défaite.
 */
typedef struct {
    Rival rival[RACE_CARS]; /**< Places des adversaires, actives ou libres. */
    float player_x; /**< Centre horizontal du joueur, en pixels flottants. */
    float scroll; /**< Décalage vertical des marquages modulo leur espacement de 38 pixels. */
    uint16_t spawn_ms; /**< Délai restant avant une tentative d'apparition, en millisecondes. */
    uint32_t seed; /**< État du générateur pseudo-aléatoire déterministe du trafic. */
    uint32_t score; /**< Nombre de véhicules entièrement dépassés. */
    uint8_t last_lane; /**< Voie de la précédente apparition pour éviter sa répétition immédiate. */
    uint8_t level; /**< Indice de difficulté, de 0 à 9. */
    uint16_t crash_ms; /**< Durée restante de l'éclat de collision, en millisecondes. */
    int16_t impact_x; /**< Centre horizontal de l'éclat BOUM, en pixels. */
    int16_t impact_y; /**< Centre vertical de l'éclat BOUM, en pixels. */
    GamePhase phase; /**< Phase de la partie commune aux trois jeux. */
} Race;
/**
 * @brief Préparer une course et son générateur déterministe.
 *
 * Place le joueur au centre, deux véhicules de départ sur les côtés et l'objectif du
 * niveau. Un indice hors plage choisit le niveau zéro.
 * @param[out] r Joueur, adversaires et progression de la course, à initialiser par cet
 *   appel. Pointeur valide.
 * @param[in] level Indice de niveau à base zéro. Une valeur hors plage choisit le premier
 *   niveau.
 */
void race_init(Race *r, unsigned level);
/**
 * @brief Lire l'objectif de dépassements du niveau.
 *
 * Le premier niveau vise 50 véhicules et le dernier 300. Les paramètres sont constants et
 * n'occupent pas de stockage persistant.
 * @param[in] r Joueur, adversaires et progression de la course, consulté sans
 *   modification. Pointeur valide.
 * @return Nombre de véhicules à dépasser pour gagner le niveau.
 */
unsigned race_goal(const Race *r);
/**
 * @brief Avancer direction, trafic et état de collision.
 *
 * En course, déplace le joueur, les marquages et les adversaires. Une collision précède la
 * validation du score gagnant. Après une collision, seul le compteur de BOUM continue
 * jusqu'au bilan.
 * @param[in,out] r Joueur, adversaires et progression de la course. Pointeur valide.
 * @param[in] tilt_x Commande écran X, adimensionnelle entre −1 et +1.
 * @param[in,out] damage Liste de rectangles à recomposer. Pointeur valide.
 */
void race_step(Race *r, float tilt_x, Damage *damage);
/**
 * @brief Recomposer route, véhicules et collision.
 *
 * Le fond et les marquages sont reconstruits avant les adversaires et le joueur. Le BOUM
 * est superposé si son compteur est encore positif.
 * @param[in] r Joueur, adversaires et progression de la course, consulté sans
 *   modification. Pointeur valide.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
void race_draw(const Race *r, Gfx *g);

#endif
