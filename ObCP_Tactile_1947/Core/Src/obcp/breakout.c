/**
 * @file breakout.c
 * @brief Physique et dessin du casse-briques à dix niveaux.
 * @ingroup obcp_apps
 *
 * Un bit de bricks représente une brique. hard_bricks indique un renfort à retirer au
 * premier impact. La raquette suit une cible de position. La trajectoire de balle est
 * divisée en trois sous-pas pour borner le déplacement entre collisions.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
/** @var levels
 * @brief Paramètres des dix niveaux. Masques de briques, largeur et vitesses sont en flash.
 */

#include "obcp_games.h"

/** @brief Constantes privées de ce module, avec leurs unités. */
enum {
    PADDLE_Y = 213, /**< Ordonnée fixe de la raquette en pixels. */
    BALL_R = 4, /**< Rayon de la balle en pixels. */
    LEFT = 10, /**< Abscisse du bord gauche de l’aire de jeu, en pixels. */
    RIGHT = 310, /**< Abscisse du bord droit de l’aire de jeu, en pixels. */
    TOP = 44, /**< Ordonnée du bord supérieur de l’aire de jeu, en pixels. */
};
static const struct {
    uint32_t bricks; /**< Masque initial des briques présentes. */
    uint32_t hard; /**< Masque initial des renforts. */
    int paddle_width; /**< Largeur de raquette en pixels. */
    float vx; /**< Vitesse horizontale initiale de balle en px/s. */
    float vy; /**< Vitesse verticale initiale, négative vers le haut, en px/s. */
    float bounce; /**< Vitesse verticale après rebond sur la raquette, en px/s. */
} levels[GAME_LEVELS] = {
    {UINT32_C(0x0000FFFF), 0,                    72, 84,  -135, -155},
    {UINT32_C(0x000FFFFF), 0,                    68, 88,  -150, -170},
    {UINT32_C(0x00FFFFFF), 0,                    64, 92,  -170, -190},
    {UINT32_C(0x0FFFFFFF), 0,                    60, 96,  -190, -210},
    {UINT32_MAX,          0,                    56, 100, -210, -230},
    {UINT32_MAX,          UINT32_C(0x000000FF), 50, 104, -235, -255},
    {UINT32_MAX,          UINT32_C(0x0000FFFF), 44, 108, -260, -280},
    {UINT32_MAX,          UINT32_C(0x00FFFFFF), 38, 112, -290, -310},
    {UINT32_MAX,          UINT32_MAX,          32, 116, -325, -345},
    {UINT32_MAX,          UINT32_MAX,          26, 120, -360, -380},
};

/*
 * Calculer l'emprise d'une brique.
 *
 * L'indice va de 0 à 31. Le quotient et le reste de la division par huit donnent la rangée
 * et la colonne.
 */
Rect breakout_brick(unsigned i)
{
    return (Rect){(int16_t)(16 + (i % 8U) * 36U), (int16_t)(56 + (i / 8U) * 17U), 32, 13};
}

/**
 * @brief Calculer l'emprise de la raquette.
 *
 * La largeur dépend du niveau. La position conservée dans le modèle désigne le centre,
 * alors que le rectangle utilise son bord gauche.
 * @param[in] b Balle, raquette et briques du casse-briques, consulté sans modification.
 *   Pointeur valide.
 * @return Rectangle occupé par la raquette.
 */
static Rect paddle_bounds(const Breakout *b)
{
    int width = levels[b->level].paddle_width;
    return (Rect){(int16_t)((int)b->paddle - width / 2), PADDLE_Y, (int16_t)width, 7};
}

/**
 * @brief Calculer l'ancienne ou la nouvelle emprise de balle.
 *
 * Le rayon est ajouté de part et d'autre du centre pour que le rectangle englobe aussi les
 * pixels du bord.
 * @param[in] b Balle, raquette et briques du casse-briques, consulté sans modification.
 *   Pointeur valide.
 * @return Rectangle englobant la balle.
 */
static Rect ball_bounds(const Breakout *b)
{
    return (Rect){(int16_t)((int)b->x - BALL_R), (int16_t)((int)b->y - BALL_R),
                  2 * BALL_R + 1, 2 * BALL_R + 1};
}

/*
 * Préparer les positions et les briques d'un niveau.
 *
 * Un indice hors plage revient au premier niveau. L'état est prêt à jouer, avec les
 * masques, dimensions et vitesses choisis dans la table constante.
 */
void breakout_init(Breakout *b, unsigned level)
{
    unsigned i = level < GAME_LEVELS ? level : 0U;
    *b = (Breakout){.x = 160.0f, .y = 199.0f, .vx = levels[i].vx, .vy = levels[i].vy,
                    .paddle = 160.0f, .bricks = levels[i].bricks, .hard_bricks = levels[i].hard,
                    .phase = GAME_READY,
                    .level = (uint8_t)i};
}

/**
 * @brief Avancer la balle d'un sous-pas et traiter ses impacts.
 *
 * Teste murs, raquette puis briques. L'ancienne position aide à choisir la composante de
 * vitesse à inverser. Un impact sur renfort enlève d'abord ce renfort.
 * @param[in,out] b Balle, raquette et briques du casse-briques. Pointeur valide.
 * @param[in] dt Durée du sous-pas, en secondes.
 * @param[in,out] damage Liste de rectangles à recomposer. Pointeur valide.
 */
static void move_ball(Breakout *b, float dt, Damage *damage)
{
    float old_x = b->x;
    float old_y = b->y;
    b->x += b->vx * dt;
    b->y += b->vy * dt;
    if (b->x < LEFT + BALL_R) {
        b->x = LEFT + BALL_R;
        b->vx = -b->vx;
    } else if (b->x > RIGHT - BALL_R) {
        b->x = RIGHT - BALL_R;
        b->vx = -b->vx;
    }
    if (b->y < TOP + BALL_R) {
        b->y = TOP + BALL_R;
        b->vy = -b->vy;
    }
    int half = levels[b->level].paddle_width / 2;
    if (b->vy > 0.0f && old_y + BALL_R <= PADDLE_Y && b->y + BALL_R >= PADDLE_Y &&
        b->x + BALL_R >= b->paddle - half && b->x - BALL_R <= b->paddle + half) {
        b->y = PADDLE_Y - BALL_R;
        b->vx = 5.4f * (b->x - b->paddle);
        b->vy = levels[b->level].bounce;
    }
    for (unsigned i = 0; i < 32; ++i) {
        uint32_t bit = UINT32_C(1) << i;
        Rect brick = breakout_brick(i);
        if ((b->bricks & bit) == 0U || !rect_valid(rect_intersection(brick, ball_bounds(b)))) {
            continue;
        }
        if ((b->hard_bricks & bit) != 0U) {
            b->hard_bricks &= ~bit;
            b->score += 5U;
        } else {
            b->bricks &= ~bit;
            b->score += 10U;
        }
        if (old_y + BALL_R <= brick.y || old_y - BALL_R >= brick.y + brick.h) {
            b->y = old_y;
            b->vy = -b->vy;
        } else {
            b->x = old_x;
            b->vx = -b->vx;
        }
        damage_add(damage, brick);
        break;
    }
}

/*
 * Avancer une simulation de vingt millisecondes.
 *
 * Seule GAME_RUN avance. La raquette rejoint 75 % de l'écart à sa cible, puis la balle
 * fait trois sous-pas. Les anciennes et nouvelles emprises sont invalidées. Une balle
 * sortie ou un masque vide termine le niveau.
 */
void breakout_step(Breakout *b, float tilt_x, Damage *damage)
{
    if (b->phase != GAME_RUN) {
        return;
    }
    Rect old_paddle = paddle_bounds(b);
    Rect old_ball = ball_bounds(b);
    int half = levels[b->level].paddle_width / 2;
    /* Une inclinaison désigne maintenant une position. Le joueur peut viser
     * rapidement un bord sans attendre un déplacement à vitesse constante.
     * Le filtre d'entrée et son gain X sont communs aux trois jeux. */
    float target = (LEFT + RIGHT) / 2.0f + ((RIGHT - LEFT) / 2.0f - half) * tilt_x;
    b->paddle += 0.75f * (target - b->paddle);
    if (b->paddle < LEFT + half) {
        b->paddle = LEFT + half;
    } else if (b->paddle > RIGHT - half) {
        b->paddle = RIGHT - half;
    }

    /* Trois sous-pas bornent le déplacement entre deux tests de collision. */
    for (unsigned i = 0; i < 3; ++i) {
        move_ball(b, GAME_STEP_MS / 3000.0f, damage);
    }
    damage_add(damage, old_paddle);
    damage_add(damage, paddle_bounds(b));
    damage_add(damage, old_ball);
    damage_add(damage, ball_bounds(b));
    if (b->bricks == 0U || b->y - BALL_R >= LCD_HEIGHT) {
        b->phase = b->bricks == 0U ? GAME_WON : GAME_LOST;
        damage_full(damage);
    }
}

/*
 * Recomposer les briques, la raquette et la balle.
 *
 * Le modèle est lu sans modifier la simulation. Les briques renforcées reçoivent un cadre
 * clair. Le clipping des primitives limite le travail à la bande demandée.
 */
void breakout_draw(const Breakout *b, Gfx *g)
{
    static const uint16_t colors[4] = {
        RGB565(244,100,120), RGB565(248,177,66), RGB565(45,202,178), RGB565(80,143,246)
    };
    gfx_outline(g, (Rect){LEFT - 1, TOP - 1, RIGHT - LEFT + 2, LCD_HEIGHT - TOP + 1},
                RGB565(56,85,116));
    for (unsigned i = 0; i < 32; ++i) {
        if ((b->bricks & (UINT32_C(1) << i)) != 0) {
            gfx_fill(g, breakout_brick(i), colors[i / 8U]);
            if ((b->hard_bricks & (UINT32_C(1) << i)) != 0U) {
                Rect brick = breakout_brick(i);
                gfx_outline(g, (Rect){brick.x + 3, brick.y + 3, brick.w - 6, brick.h - 6},
                            RGB565(250,248,240));
            }
        }
    }
    gfx_fill(g, paddle_bounds(b), RGB565(226,241,252));
    gfx_disc(g, (int)b->x, (int)b->y, BALL_R, RGB565(252,209,82));
}
