/**
 * @file race.c
 * @brief Trafic, conduite, collisions et éclat de bande dessinée.
 * @ingroup obcp_apps
 *
 * Le jeu possède trois voies et jusqu’à six adversaires. Leur type choisit les dimensions,
 * tandis que leur vitesse verticale commune dépend du niveau. L’animation de collision est
 * un état temporisé et n’utilise aucune attente bloquante.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
/** @var road
 * @brief Emprise du ruban routier de 216 pixels de large.
 */
/** @var levels
 * @brief Objectifs, intervalles d'apparition et vitesse des dix niveaux de course.
 */
/** @var vehicles
 * @brief Dimensions correspondant à chaque valeur de l'énumération Vehicle.
 */

#include "obcp_games.h"

/** @brief Constantes privées de ce module, avec leurs unités. */
enum {
    ROAD_X = 52, /**< Abscisse du bord gauche de la route en pixels. */
    ROAD_W = 216, /**< Largeur de la route en pixels. */
    ROAD_Y = 34, /**< Ordonnée du début visible de la route en pixels. */
    CAR_W = 29, /**< Largeur du véhicule du joueur en pixels. */
    CAR_H = 32, /**< Hauteur du véhicule du joueur en pixels. */
    HERO_Y = 195, /**< Ordonnée fixe du véhicule du joueur en pixels. */
};
static const Rect road = {ROAD_X, ROAD_Y, ROAD_W, LCD_HEIGHT - ROAD_Y};
static const struct {
    uint16_t spawn_period; /**< Temps entre deux apparitions en millisecondes. */
    uint16_t goal; /**< Objectif de dépassements pour gagner. */
    float traffic_step; /**< Déplacement vertical en pixels par pas de vingt millisecondes. */
} levels[GAME_LEVELS] = {
    {1100, 50, 2.4f}, {1000, 60, 2.65f}, {900, 75, 2.9f}, {800, 90, 3.15f},
    {720, 110, 3.4f}, {640, 135, 3.7f}, {560, 165, 4.0f}, {480, 200, 4.4f},
    {420, 240, 4.8f}, {360, 300, 5.2f},
};
static const struct {
    int16_t width; /**< Largeur de collision et de dessin en pixels. */
    int16_t height; /**< Hauteur de collision et de dessin en pixels. */
} vehicles[] = {
    {CAR_W, CAR_H}, {15, 27}, {35, 44}, {43, 56}
};

/**
 * @brief Convertir le centre horizontal du joueur en rectangle.
 *
 * La coordonnée verticale désigne le haut. Les dimensions du joueur sont fixes afin de
 * partager la même emprise entre collision et dessin.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 * @return Rectangle de la voiture du joueur.
 */
static Rect car_box(float x, float y)
{
    return (Rect){(int16_t)((int)x - CAR_W / 2), (int16_t)y, CAR_W, CAR_H};
}

/**
 * @brief Calculer l'emprise d'un adversaire selon son véhicule.
 *
 * Le numéro de voie donne le centre horizontal. Le type donne largeur et hauteur, ce qui
 * distingue notamment moto étroite et camion long.
 * @param[in] r Position, voie et type de l’adversaire, consulté sans modification.
 *   Pointeur valide.
 * @return Rectangle de collision et de rendu de l'adversaire.
 */
static Rect rival_box(const Rival *r)
{
    int center = ROAD_X + ROAD_W / 6 + r->lane * ROAD_W / 3;
    return (Rect){(int16_t)(center - vehicles[r->kind].width / 2), (int16_t)r->y,
                  vehicles[r->kind].width, vehicles[r->kind].height};
}

/**
 * @brief Calculer la partie visible d'un marquage routier.
 *
 * Le décalage de défilement est ajouté à la ligne de base, puis l'emprise est limitée à la
 * route.
 * @param[in] lane Indice du séparateur de voie à dessiner.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 * @param[in] phase Décalage vertical des pointillés de route, en pixels.
 * @return Rectangle visible du marquage.
 */
static Rect dash(unsigned lane, int y, int phase)
{
    return rect_intersection(road, (Rect){(int16_t)(ROAD_X + ROAD_W * (lane + 1U) / 3U - 2U),
                                          (int16_t)(y + phase), 4, 15});
}

/**
 * @brief Invalider les marquages à un décalage donné.
 *
 * Cette routine est appelée avant et après l'avancement. Les anciennes marques sont donc
 * effacées et les nouvelles recomposées.
 * @param[in,out] d Liste de rectangles à recomposer. Pointeur valide.
 * @param[in] phase Décalage vertical des pointillés de route, en pixels.
 */
static void dash_damage(Damage *d, int phase)
{
    for (unsigned lane = 0; lane < 2; ++lane) {
        for (int y = ROAD_Y - 38; y < LCD_HEIGHT; y += 38) {
            damage_add(d, dash(lane, y, phase));
        }
    }
}

/*
 * Préparer une course et son générateur déterministe.
 *
 * Place le joueur au centre, deux véhicules de départ sur les côtés et l'objectif du
 * niveau. Un indice hors plage choisit le niveau zéro.
 */
void race_init(Race *r, unsigned level)
{
    *r = (Race){.player_x = 160.0f, .spawn_ms = 880, .seed = UINT32_C(0x1947),
                .last_lane = 2, .phase = GAME_READY,
                .level = (uint8_t)(level < GAME_LEVELS ? level : 0U)};
    r->rival[0] = (Rival){.y = 63, .lane = 0, .active = true};
    r->rival[1] = (Rival){.y = 114, .lane = 2, .active = true};
}

/*
 * Lire l'objectif de dépassements du niveau.
 *
 * Le premier niveau vise 50 véhicules et le dernier 300. Les paramètres sont constants et
 * n'occupent pas de stockage persistant.
 */
unsigned race_goal(const Race *r)
{
    return levels[r->level].goal;
}

/**
 * @brief Créer un adversaire dans une place libre.
 *
 * Le générateur choisit une voie différente de la précédente et un type autorisé par le
 * niveau. Un seul véhicule est créé par échéance pour éviter une rangée complète.
 * @param[in,out] r Joueur, adversaires et progression de la course. Pointeur valide.
 */
static void spawn(Race *r)
{
    for (unsigned i = 0; i < RACE_CARS; ++i) {
        if (!r->rival[i].active) {
            r->seed = r->seed * UINT32_C(1664525) + UINT32_C(1013904223);
            /* Un seul rival par apparition. Tous avancent à la même vitesse.
             * Même au niveau 10, 360 ms * 260 px/s = 93,6 px séparent les
             * véhicules, plus que camion + joueur (56 + 32 px). Une rangée
             * complète ne ferme donc pas les trois voies au même instant. */
            uint8_t lane = (uint8_t)((r->last_lane + 1U + (r->seed >> 31)) % 3U);
            unsigned types = r->level >= 6U ? 4U : (r->level >= 4U ? 3U : (r->level >= 2U ? 2U : 1U));
            Vehicle kind = (Vehicle)((r->seed >> 8) % types);
            r->rival[i] = (Rival){.y = (float)(ROAD_Y - vehicles[kind].height),
                                  .lane = lane, .active = true, .kind = kind};
            r->last_lane = lane;
            break;
        }
    }
}

/*
 * Avancer direction, trafic et état de collision.
 *
 * En course, déplace le joueur, les marquages et les adversaires. Une collision précède la
 * validation du score gagnant. Après une collision, seul le compteur de BOUM continue
 * jusqu'au bilan.
 */
void race_step(Race *r, float tilt_x, Damage *damage)
{
    if (r->phase == GAME_LOST && r->crash_ms > 0U) {
        unsigned previous = r->crash_ms / 100U;
        r->crash_ms = r->crash_ms > GAME_STEP_MS ? (uint16_t)(r->crash_ms - GAME_STEP_MS) : 0;
        if (previous != r->crash_ms / 100U || r->crash_ms == 0U) {
            damage_full(damage);
        }
        return;
    }
    if (r->phase != GAME_RUN) {
        return;
    }
    Rect previous = car_box(r->player_x, HERO_Y);
    r->player_x += 300.0f * tilt_x * (GAME_STEP_MS / 1000.0f);
    if (r->player_x < ROAD_X + CAR_W / 2 + 5) {
        r->player_x = ROAD_X + CAR_W / 2 + 5;
    } else if (r->player_x > ROAD_X + ROAD_W - CAR_W / 2 - 5) {
        r->player_x = ROAD_X + ROAD_W - CAR_W / 2 - 5;
    }
    Rect current = car_box(r->player_x, HERO_Y);
    if (current.x != previous.x) {
        damage_add(damage, previous);
        damage_add(damage, current);
    }
    dash_damage(damage, (int)r->scroll);
    float speed = levels[r->level].traffic_step;
    r->scroll += speed;
    if (r->scroll >= 38.0f) {
        r->scroll -= 38.0f;
    }
    dash_damage(damage, (int)r->scroll);

    for (unsigned i = 0; i < RACE_CARS; ++i) {
        Rival *v = &r->rival[i];
        if (!v->active) {
            continue;
        }
        damage_add(damage, rect_intersection(road, rival_box(v)));
        v->y += speed;
        Rect bounds = rival_box(v);
        if (v->y >= LCD_HEIGHT) {
            v->active = false;
            ++r->score;
        } else {
            damage_add(damage, rect_intersection(road, bounds));
            Rect impact = rect_intersection(bounds, current);
            if (rect_valid(impact)) {
                r->phase = GAME_LOST;
                r->crash_ms = 900;
                r->impact_x = impact.x + impact.w / 2;
                r->impact_y = 183;
                damage_full(damage);
                return;
            }
        }
    }
    if (r->score >= race_goal(r)) {
        r->phase = GAME_WON;
        damage_full(damage);
        return;
    }
    if (r->spawn_ms <= GAME_STEP_MS) {
        spawn(r);
        r->spawn_ms = levels[r->level].spawn_period;
    } else {
        r->spawn_ms -= GAME_STEP_MS;
    }
}

/**
 * @brief Dessiner les éléments visibles d'un véhicule.
 *
 * Les primitives sont intersectées avec la route en conservant le pas du tampon Gfx. Moto,
 * fourgonnette et camion ont leurs détails et dimensions propres.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 * @param[in] body Emprise graphique et de collision du véhicule.
 * @param[in] kind Type de véhicule déterminant ses détails graphiques.
 * @param[in] color Couleur encodée au format RGB565.
 */
static void draw_vehicle(Gfx *g, Rect body, Vehicle kind, uint16_t color)
{
    /* La bordure supérieure découpe les véhicules entrant par le haut. */
    Gfx clipped = *g;
    Rect visible = rect_intersection(g->clip, road);
    if (!rect_valid(visible) || !rect_valid(rect_intersection(body, visible))) {
        return;
    }
    /* Garder le pas du tampon Gfx. Couper chaque primitive au rectangle route. */
    gfx_fill(&clipped, rect_intersection(road, body), color);
    Rect windshield = {(int16_t)(body.x + 4), (int16_t)(body.y + 7),
                        (int16_t)(body.w - 8), 8};
    gfx_fill(&clipped, rect_intersection(road, windshield), RGB565(29,47,73));
    Rect plate = {(int16_t)(body.x + 5), (int16_t)(body.y + body.h - 7),
                  (int16_t)(body.w - 10), 3};
    gfx_fill(&clipped, rect_intersection(road, plate), RGB565(236,238,213));
    if (kind == VEHICLE_BIKE) {
        /* Roues dans l'emprise de collision, guidon puis casque du pilote. */
        gfx_fill(&clipped, rect_intersection(road, (Rect){body.x + 5, body.y, 5, body.h}), RGB565(14,23,35));
        gfx_fill(&clipped, rect_intersection(road, (Rect){body.x, body.y + 8, body.w, 3}), color);
        gfx_fill(&clipped, rect_intersection(road, (Rect){body.x + 4, body.y + 12, 7, 7}),
                 RGB565(252,209,82));
    } else if (kind == VEHICLE_TRUCK || kind == VEHICLE_VAN) {
        Rect load = {body.x + 3, body.y + 19, body.w - 6, body.h - 28};
        gfx_fill(&clipped, rect_intersection(road, load), RGB565(180,196,210));
    }
}

/**
 * @brief Dessiner l'éclat jaune et le texte BOUM.
 *
 * Les triangles et le disque créent un éclat centré sur l'impact. La teinte alterne avec
 * les tranches du compteur d'animation pour donner une brève pulsation.
 * @param[in] r Joueur, adversaires et progression de la course, consulté sans
 *   modification. Pointeur valide.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
static void draw_crash(const Race *r, Gfx *g)
{
    int x = r->impact_x, y = r->impact_y;
    uint16_t yellow = (r->crash_ms / 100U & 1U) ? RGB565(255,221,46) : RGB565(255,183,32);
    /* Éclat dessiné en primitives, donc identique dans les bandes du TFT et
     * dans les captures PC. Les pointes entourent le mot de bande dessinée. */
    gfx_disc(g, x, y, 29, yellow);
    gfx_triangle(g, x - 46, y - 11, x - 18, y - 24, x - 24, y + 13, yellow);
    gfx_triangle(g, x + 46, y - 13, x + 18, y - 25, x + 23, y + 14, yellow);
    gfx_triangle(g, x - 12, y - 39, x + 17, y - 21, x - 26, y - 7, yellow);
    gfx_triangle(g, x + 12, y + 39, x - 18, y + 22, x + 27, y + 7, yellow);
    gfx_triangle(g, x + 26, y - 39, x + 36, y - 39, x + 21, y - 13, RGB565(255,247,100));
    gfx_triangle(g, x + 21, y - 13, x + 34, y - 19, x + 17, y + 2, RGB565(255,247,100));
    gfx_text(g, x - 29, y - 7, "BOUM!", 2, RGB565(26,34,49));
}

/*
 * Recomposer route, véhicules et collision.
 *
 * Le fond et les marquages sont reconstruits avant les adversaires et le joueur. Le BOUM
 * est superposé si son compteur est encore positif.
 */
void race_draw(const Race *r, Gfx *g)
{
    gfx_fill(g, (Rect){0, ROAD_Y, LCD_WIDTH, LCD_HEIGHT - ROAD_Y}, RGB565(24,65,51));
    gfx_fill(g, road, RGB565(42,53,66));
    gfx_fill(g, (Rect){ROAD_X, ROAD_Y, 3, road.h}, RGB565(223,232,224));
    gfx_fill(g, (Rect){ROAD_X + ROAD_W - 3, ROAD_Y, 3, road.h}, RGB565(223,232,224));
    for (unsigned lane = 0; lane < 2; ++lane) {
        for (int y = ROAD_Y - 38; y < LCD_HEIGHT; y += 38) {
            gfx_fill(g, dash(lane, y, (int)r->scroll), RGB565(182,190,197));
        }
    }
    for (unsigned i = 0; i < RACE_CARS; ++i) {
        if (r->rival[i].active) {
            static const uint16_t colors[] = {
                RGB565(239,102,113), RGB565(251,198,65), RGB565(131,172,240), RGB565(203,151,231)
            };
            draw_vehicle(g, rival_box(&r->rival[i]), r->rival[i].kind, colors[r->rival[i].kind]);
        }
    }
    draw_vehicle(g, car_box(r->player_x, HERO_Y), VEHICLE_CAR, RGB565(74,221,183));
    if (r->crash_ms > 0U) {
        draw_crash(r, g);
    }
}
