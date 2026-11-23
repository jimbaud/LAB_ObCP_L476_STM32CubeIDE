/**
 * @file maze.c
 * @brief Cartes fixes, inertie et collisions du labyrinthe.
 * @ingroup obcp_apps
 *
 * Les cartes utilisent des cases de 16 pixels. Le modèle conserve positions et vitesses en
 * flottants, tandis que le dessin utilise des pixels entiers. Le temps progresse seulement
 * dans GAME_RUN et la pause fige donc le compteur.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
/** @var maps
 * @brief Dix cartes fixes. Dièse signifie mur, S départ, G arrivée et point passage.
 */
/** @var limits_ms
 * @brief Temps maximum de chaque niveau, de 75 000 à 23 000 millisecondes.
 */

#include "obcp_games.h"

/** @brief Constantes privées de ce module, avec leurs unités. */
enum {
    COLS = 17, /**< Nombre de colonnes de la carte de labyrinthe. */
    ROWS = 11, /**< Nombre de rangées de la carte de labyrinthe. */
    TILE = 16, /**< Côté d’une case carrée en pixels. */
    LEFT = 24, /**< Abscisse du bord gauche de l’aire de jeu, en pixels. */
    TOP = 46, /**< Ordonnée du bord supérieur de l’aire de jeu, en pixels. */
    BALL_RADIUS = 5, /**< Rayon de la bille en pixels. */
};
/* Dix cartes fixes. Un # est un mur. Les plus courts trajets font
 * 22, 26, 30, 34, 38, 46, 54, 62, 70 et 74 déplacements de 16 px. */
static const char maps[GAME_LEVELS][ROWS][COLS + 1] = {
    {
        "#################",
        "#S#...#.........#",
        "#.#.###.###.###.#",
        "#.#...#.#...#...#",
        "#.#.#.#.#.###.###",
        "#.#.#...#...#...#",
        "#.#######.#.#####",
        "#.......#.#.....#",
        "#######.#######.#",
        "#..............G#",
        "#################",
    },
    {
        "#################",
        "#S#...#.........#",
        "#.#.###.#####.###",
        "#.#...#.....#...#",
        "#.#.#.#####.###.#",
        "#.#.#.......#.#.#",
        "#.#.#####.###.#.#",
        "#.#.#...#.....#.#",
        "#.###.#.#######.#",
        "#.....#........G#",
        "#################",
    },
    {
        "#################",
        "#S......#...#...#",
        "#######.#.#.#.#.#",
        "#.....#...#...#.#",
        "#.###.#########.#",
        "#...#.........#.#",
        "###.###.#######.#",
        "#.#...#.#.....#.#",
        "#.###.#.#.###.#.#",
        "#.....#.....#..G#",
        "#################",
    },
    {
        "#################",
        "#S....#.........#",
        "#####.###.#.###.#",
        "#...#...#.#...#.#",
        "###.###.#####.###",
        "#...#...#...#...#",
        "#.#.#.###.#.###.#",
        "#.#.#.#...#.....#",
        "#.###.#.#######.#",
        "#.......#......G#",
        "#################",
    },
    {
        "#################",
        "#S..#.....#.....#",
        "###.#.#####.###.#",
        "#.#.#.........#.#",
        "#.#.#.#######.#.#",
        "#.#.#.#.....#.#.#",
        "#.#.###.###.###.#",
        "#.#...#...#.#...#",
        "#.###.###.#.#.#.#",
        "#.........#...#G#",
        "#################",
    },
    {
        "#################",
        "#S#.#...........#",
        "#.#.#.#####.###.#",
        "#.#...#...#...#.#",
        "#.#####.#.###.#.#",
        "#...#...#.....#.#",
        "###.#.#########.#",
        "#.#.#.#.......#.#",
        "#.#.#.#.#######.#",
        "#.....#........G#",
        "#################",
    },
    {
        "#################",
        "#S..#...#.......#",
        "###.#.#.#######.#",
        "#...#.#...#.....#",
        "#.###.###.#.###.#",
        "#.....#...#.#.#.#",
        "#######.###.#.#.#",
        "#.....#.#...#...#",
        "#.#####.###.#.###",
        "#...........#..G#",
        "#################",
    },
    {
        "#################",
        "#S..#.......#...#",
        "###.#.#####.###.#",
        "#...#.....#.#...#",
        "#.#######.#.#.###",
        "#...#.....#.#...#",
        "###.#.#####.#.#.#",
        "#...#.#.....#.#.#",
        "#.###.#.#######.#",
        "#.....#........G#",
        "#################",
    },
    {
        "#################",
        "#S....#...#.....#",
        "#####.#.#.#####.#",
        "#.....#.#.......#",
        "#.#####.#######.#",
        "#.#.....#.....#.#",
        "#.#.#####.###.#.#",
        "#.#...#...#.#...#",
        "#.###.#.###.#####",
        "#.....#........G#",
        "#################",
    },
    {
        "#################",
        "#S..........#...#",
        "###########.#.#.#",
        "#...#.....#...#.#",
        "#.#.#.###.#####.#",
        "#.#...#.#.....#.#",
        "#.#####.#####.#.#",
        "#.#.....#...#...#",
        "#.#.###.#.#.#####",
        "#...#.....#....G#",
        "#################",
    },
};
static const uint32_t limits_ms[GAME_LEVELS] = {
    75000, 68000, 62000, 56000, 50000, 44000, 38000, 32000, 27000, 23000
};
/**
 * @brief Convertir une cellule de carte en rectangle écran.
 *
 * Ajoute les marges de la scène au produit de l'indice par la taille de tuile.
 * @param[in] col Indice de colonne dans la carte.
 * @param[in] row Indice de ligne dans la carte.
 * @return Rectangle de la cellule, en pixels.
 */
static Rect cell(int col, int row)
{
    return (Rect){(int16_t)(LEFT + col * TILE), (int16_t)(TOP + row * TILE), TILE, TILE};
}

/*
 * Obtenir la cellule de départ commune aux cartes.
 *
 * Le départ est placé dans la case intérieure de coordonnées colonne un et ligne un.
 */
Rect maze_start(void) { return cell(1, 1); }
/*
 * Obtenir la cellule d'arrivée commune aux cartes.
 *
 * L'arrivée occupe la dernière case intérieure en bas à droite. Le test de victoire et le
 * dessin lisent la même emprise.
 */
Rect maze_finish(void) { return cell(COLS - 2, ROWS - 2); }

/**
 * @brief Limiter une valeur flottante à un intervalle.
 *
 * Le même helper borne une vitesse ou trouve le point le plus proche sur une tuile murale.
 * @param[in] v Valeur flottante à limiter.
 * @param[in] low Borne inférieure de saturation.
 * @param[in] high Borne supérieure de saturation.
 * @return Valeur saturée entre low et high.
 */
static float clamp(float v, float low, float high)
{
    return v < low ? low : (v > high ? high : v);
}

/*
 * Vérifier si le disque de la bille touche une zone interdite.
 *
 * Limite les cases examinées à l'emprise du disque. Pour chaque mur, le point de rectangle
 * le plus proche du centre permet de tester la distance au carré.
 */
bool maze_solid(const Maze *m, float x, float y)
{
    if (x < LEFT + BALL_RADIUS || y < TOP + BALL_RADIUS ||
        x >= LEFT + COLS * TILE - BALL_RADIUS || y >= TOP + ROWS * TILE - BALL_RADIUS) {
        return true;
    }
    int c0 = ((int)x - BALL_RADIUS - LEFT) / TILE;
    int c1 = ((int)x + BALL_RADIUS - LEFT) / TILE;
    int r0 = ((int)y - BALL_RADIUS - TOP) / TILE;
    int r1 = ((int)y + BALL_RADIUS - TOP) / TILE;
    for (int row = r0; row <= r1; ++row) {
        for (int col = c0; col <= c1; ++col) {
            if (maps[m->level][row][col] == '#') {
                Rect tile = cell(col, row);
                float dx = x - clamp(x, tile.x, tile.x + tile.w);
                float dy = y - clamp(y, tile.y, tile.y + tile.h);
                if (dx * dx + dy * dy < BALL_RADIUS * BALL_RADIUS) {
                    return true;
                }
            }
        }
    }
    return false;
}

/**
 * @brief Calculer l'emprise de la bille pour le rendu.
 *
 * Le rayon et un pixel de bord sont inclus afin d'effacer complètement l'ancienne image.
 * @param[in] m État physique et temporel du labyrinthe, consulté sans modification.
 *   Pointeur valide.
 * @return Rectangle englobant la bille.
 */
static Rect ball_bounds(const Maze *m)
{
    return (Rect){(int16_t)((int)m->x - BALL_RADIUS), (int16_t)((int)m->y - BALL_RADIUS),
                  2 * BALL_RADIUS + 1, 2 * BALL_RADIUS + 1};
}

/*
 * Préparer un niveau à son point de départ.
 *
 * Remet vitesses et temps à zéro. Un niveau hors plage revient à zéro et la phase initiale
 * demande l'appui Jouer.
 */
void maze_init(Maze *m, unsigned level)
{
    Rect start = maze_start();
    *m = (Maze){.x = start.x + TILE / 2, .y = start.y + TILE / 2, .phase = GAME_READY,
                .level = (uint8_t)(level < GAME_LEVELS ? level : 0U)};
}

/*
 * Convertir le temps restant en secondes affichables.
 *
 * Arrondit vers le haut pour que la dernière seconde soit visible. La valeur est nulle dès
 * que la limite du niveau est atteinte.
 */
uint32_t maze_seconds_left(const Maze *m)
{
    uint32_t limit = limits_ms[m->level];
    return m->elapsed_ms >= limit ? 0 : (limit - m->elapsed_ms + 999U) / 1000U;
}

/*
 * Intégrer la physique et le temps sur vingt millisecondes.
 *
 * Calcule les vitesses avec inertie, avance séparément X et Y pour permettre le glissement
 * contre un mur, puis teste arrivée et expiration. Les emprises modifiées sont invalidées.
 */
void maze_step(Maze *m, float tilt_x, float tilt_y, Damage *damage)
{
    if (m->phase != GAME_RUN) {
        return;
    }
    Rect old = ball_bounds(m);
    /* v est en px/s. À 100 px/s et 20 ms, on parcourt au plus 2 px
     * par axe. Le test séparé laisse la bille glisser le long d'un mur. */
    const float dt = GAME_STEP_MS / 1000.0f;
    m->vx = clamp(0.96f * m->vx + 550.0f * tilt_x * dt, -100.0f, 100.0f);
    m->vy = clamp(0.96f * m->vy + 550.0f * tilt_y * dt, -100.0f, 100.0f);
    float x = m->x + dt * m->vx;
    if (maze_solid(m, x, m->y)) {
        m->vx = 0.0f;
    } else {
        m->x = x;
    }
    float y = m->y + dt * m->vy;
    if (maze_solid(m, m->x, y)) {
        m->vy = 0.0f;
    } else {
        m->y = y;
    }
    Rect current = ball_bounds(m);
    if (old.x != current.x || old.y != current.y) {
        damage_add(damage, old);
        damage_add(damage, current);
    }
    m->elapsed_ms += GAME_STEP_MS;
    if (rect_contains(maze_finish(), (int)m->x, (int)m->y)) {
        m->phase = GAME_WON;
    } else if (m->elapsed_ms >= limits_ms[m->level]) {
        m->phase = GAME_LOST;
    }
    if (m->phase != GAME_RUN) {
        damage_full(damage);
    }
}

/*
 * Dessiner les murs, les cases spéciales et la bille.
 *
 * Les rangées hors de la bande sont sautées. Le niveau sélectionne la carte, tandis que
 * les positions physiques ne sont jamais modifiées par ce rendu.
 */
void maze_draw(const Maze *m, Gfx *g)
{
    for (int row = 0; row < ROWS; ++row) {
        if (TOP + (row + 1) * TILE <= g->clip.y || TOP + row * TILE >= g->clip.y + g->clip.h) {
            continue;
        }
        for (int col = 0; col < COLS; ++col) {
            Rect tile = cell(col, row);
            char type = maps[m->level][row][col];
            uint16_t color = type == '#' ? RGB565(40,68,103) : RGB565(16,27,43);
            gfx_fill(g, tile, color);
            if (type == '#') {
                gfx_outline(g, tile, RGB565(51,88,123));
            } else if (type == 'G' || type == 'S') {
                gfx_disc(g, tile.x + 8, tile.y + 8, 6,
                         type == 'G' ? RGB565(62,233,170) : RGB565(57,112,170));
            }
        }
    }
    gfx_disc(g, (int)m->x, (int)m->y, BALL_RADIUS, RGB565(255,201,73));
    gfx_disc(g, (int)m->x - 1, (int)m->y - 2, 2, RGB565(255,241,186));
}
