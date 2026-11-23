/**
 * @file input.c
 * @brief Filtrage du contact, de SW1 et de l’inclinaison.
 * @ingroup obcp_input
 *
 * Les pilotes livrent des états physiques. Ce module construit les événements DOWN, MOVE
 * et UP, rejette les oscillations et exprime les commandes de jeu dans le repère de
 * l’écran. Les temps sont en millisecondes et les accélérations en g.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
#include "obcp_input.h"
#include "obcp_config.h"

/*
 * Passer des coordonnées natives capacitives au paysage.
 *
 * Vérifie les plages portrait du FT62xx, puis applique la rotation et éventuellement le
 * demi-tour choisi dans la configuration. Les sorties sont des pixels du TFT.
 */
bool touch_landscape(uint16_t raw_x, uint16_t raw_y, bool flipped, int16_t *x, int16_t *y)
{
    if (raw_x >= 240U || raw_y >= 320U) {
        return false;
    }
    /* Le FT62xx exprime un portrait 240 × 320. La rotation échange X et Y puis inverse la nouvelle abscisse. */
    *x = (int16_t)(319U - raw_y);
    *y = (int16_t)raw_x;
    if (flipped) {
        *x = (int16_t)(319 - *x);
        *y = (int16_t)(239 - *y);
    }
    return true;
}

/*
 * Produire un événement à partir du contact et de sa position.
 *
 * Compare l'état reçu au précédent. Le premier appui donne DOWN, un changement de position
 * donne MOVE et la disparition du contact donne UP. L'état mémorisé est mis à jour.
 */
TouchEvent touch_update(TouchState *s, bool down, int16_t x, int16_t y)
{
    TouchKind kind = TOUCH_NONE;
    if (down) {
        if (!s->down) {
            kind = TOUCH_DOWN;
        } else if (s->x != x || s->y != y) {
            kind = TOUCH_MOVE;
        }
        s->x = x;
        s->y = y;
    } else if (s->down) {
        kind = TOUCH_UP;
    }
    s->down = down;
    return (TouchEvent){kind, s->x, s->y};
}

/*
 * Classer le type de tactile.
 *
 * La reconnaissance TSC2007 ou STMPE610 indique qu'un profil affine est requis avant la
 * conversion en pixels.
 */
bool touch_resistive(TouchController controller)
{
    return controller == TOUCH_CTRL_TSC2007 || controller == TOUCH_CTRL_STMPE610;
}

/*
 * Fournir un nom de contrôleur à l'interface.
 *
 * Le texte appartient aux constantes du programme et suit la valeur reconnue par
 * touch_probe().
 */
const char *touch_name(TouchController controller)
{
    switch (controller) {
    case TOUCH_CTRL_FT62XX: return "FT62XX";
    case TOUCH_CTRL_STMPE610: return "STMPE610";
    case TOUCH_CTRL_TSC2007: return "TSC2007";
    default: return "ABSENT";
    }
}

/**
 * @brief Choisir la valeur médiane de trois coordonnées.
 *
 * Les comparaisons écartent une valeur aberrante isolée sans faire la moyenne des
 * extrêmes.
 * @param[in] a Valeur numérique à comparer.
 * @param[in] b Valeur numérique à comparer.
 * @param[in] c Troisième valeur à classer pour la médiane.
 * @return Valeur du milieu après classement.
 */
static uint16_t median(uint16_t a, uint16_t b, uint16_t c)
{
    uint16_t low = a < b ? a : b;
    uint16_t high = a > b ? a : b;
    return c < low ? low : (c > high ? high : c);
}

/*
 * Stabiliser un contact résistif et ses coordonnées.
 *
 * Deux mesures confirment les fronts. Une histoire de trois points permet ensuite une
 * médiane par axe. Le filtre conserve son état d'un appel au suivant.
 */
RawTouch touch_filter(ResistiveFilter *filter, RawTouch raw)
{
    /* Confirmer chaque front avec deux mesures. Un échantillon parasite
     * au lever du stylet ne termine pas le trait à lui seul. */
    if (raw.down != filter->current.down) {
        if (!filter->changing) {
            filter->changing = true;
            return filter->current;
        }
        filter->used = 0;
        filter->next = 0;
    }
    filter->changing = false;
    if (raw.down) {
        filter->history[filter->next] = raw.point;
        /* Le modulo fait de ces trois cases un historique circulaire sans déplacement de tableau. */
        filter->next = (uint8_t)((filter->next + 1U) % 3U);
        if (filter->used < 3U) {
            ++filter->used;
        }
        if (filter->used == 3U) {
            const RawPoint *p = filter->history;
            raw.point.x = median(p[0].x, p[1].x, p[2].x);
            raw.point.y = median(p[0].y, p[1].y, p[2].y);
        }
    }
    filter->current = raw;
    return raw;
}

/*
 * Reconnaître le maintien tactile demandé au reset.
 *
 * Le premier contact arme une mesure de durée. Un relâchement précoce consomme le geste.
 * Au seuil de 1,3 s, l'événement de calibrage est émis une seule fois.
 */
BootTouchEvent touch_boot_sample(TouchBoot *boot, bool down, uint32_t now)
{
    if (!boot->armed) {
        return BOOT_TOUCH_NONE;
    }
    if (!down) {
        /* Une dalle libre à la première mesure donne un démarrage normal.
         * Un appui de secours relâché trop tôt est consommé en entier. */
        boot->armed = false;
        return boot->held ? BOOT_TOUCH_USED : BOOT_TOUCH_NONE;
    }
    if (!boot->held) {
        boot->held = true;
        boot->since = now;
    }
    if (now - boot->since >= TOUCH_BOOT_HOLD_MS) {
        boot->armed = false;
        return BOOT_TOUCH_CALIBRATE;
    }
    return BOOT_TOUCH_USED;
}

/*
 * Initialiser l'anti-rebond de SW1.
 *
 * Un bouton déjà appuyé au démarrage est considéré consommé pour éviter de déclencher une
 * commande après la procédure de démarrage.
 */
void button_init(ButtonState *b, bool pressed, uint32_t now)
{
    *b = (ButtonState){pressed, pressed, pressed, now, now};
}

/*
 * Transformer le niveau de SW1 en appui court ou long.
 *
 * Valide un changement après 28 ms. Le court est émis au relâchement, le long après 900 ms
 * et une seule fois. Les différences de ticks non signés tolèrent leur débordement.
 */
ButtonEvent button_update(ButtonState *b, bool pressed, uint32_t now)
{
    if (pressed != b->raw) {
        b->raw = pressed;
        b->changed_at = now;
    }

    /* Valider le front seulement si le contact est resté stable assez longtemps. */
    if (b->stable != b->raw && now - b->changed_at >= BUTTON_DEBOUNCE_MS) {
        b->stable = b->raw;
        if (b->stable) {
            b->pressed_at = b->changed_at;
            b->consumed = false;
        } else if (!b->consumed) {
            b->consumed = true;
            return b->changed_at - b->pressed_at >= BUTTON_HOLD_MS ? BUTTON_LONG : BUTTON_SHORT;
        }
    }
    if (b->stable && b->raw && !b->consumed && now - b->pressed_at >= BUTTON_HOLD_MS) {
        b->consumed = true;
        return BUTTON_LONG;
    }
    return BUTTON_NONE;
}

/*
 * Préparer le repère et les réactivités initiales.
 *
 * Les signes et la permutation viennent de obcp_config.h. La réactivité initiale est six
 * sur dix pour chaque axe. Le neutre sera acquis par tilt_sample().
 */
void tilt_init(Tilt *t)
{
    *t = (Tilt){.axes = (OBCP_TILT_SWAP_XY ? TILT_SWAP_XY : 0U) |
                       (OBCP_TILT_SIGN_X < 0.0f ? TILT_INVERT_X : 0U) |
                       (OBCP_TILT_SIGN_Y < 0.0f ? TILT_INVERT_Y : 0U),
                .response_x = TILT_RESPONSE_DEFAULT, .response_y = TILT_RESPONSE_DEFAULT};
}

/*
 * Recommencer la mesure de la pose de repos.
 *
 * Efface sommes, compteurs et états du filtre, tout en conservant le repère et les deux
 * réglages de réactivité. Les 32 prochaines mesures valides établiront le neutre.
 */
void tilt_recenter(Tilt *t)
{
    *t = (Tilt){.axes = t->axes, .response_x = t->response_x, .response_y = t->response_y};
}

/**
 * @brief Borner une commande de jeu.
 *
 * La saturation évite qu'une forte inclinaison dépasse les amplitudes prévues par les
 * modèles physiques.
 * @param[in] x Commande normalisée à limiter à l’intervalle −1 à +1.
 * @return Valeur limitée à l'intervalle −1 à +1.
 */
static float clamp_command(float x)
{
    if (x > 1.0f) {
        return 1.0f;
    }
    if (x < -1.0f) {
        return -1.0f;
    }
    return x;
}

/**
 * @brief Appliquer la zone morte et mettre une accélération à l'échelle.
 *
 * La petite variation autour du neutre donne zéro. Au-delà, la commande est continue et
 * bornée. Le gain de réactivité sera appliqué ensuite.
 * @param[in] g Accélération en g avant normalisation.
 * @return Commande normalisée avant gain de réactivité.
 */
static float normalize(float g)
{
    const float dead_g = 0.045f;
    if (g > dead_g) {
        return clamp_command((g - dead_g) / 0.40f);
    }
    if (g < -dead_g) {
        return clamp_command((g + dead_g) / 0.40f);
    }
    return 0.0f;
}

/*
 * Projeter le filtre brut dans un repère écran choisi.
 *
 * Permute les composantes, applique leurs signes, la zone morte puis les gains X/Y. Un
 * capteur non prêt donne deux commandes nulles. L'aperçu peut ainsi essayer le repère
 * proposé avant sa validation.
 */
void tilt_project(const Tilt *t, uint8_t axes, float *x, float *y)
{
    if (!t->calibrated || !t->valid) {
        *x = *y = 0.0f;
        return;
    }
    /* Chaque option est un bit indépendant. Le ET binaire teste la permutation sans modifier les inversions. */
    bool swap = (axes & TILT_SWAP_XY) != 0U;
    float dx = swap ? t->filtered_y : t->filtered_x;
    float dy = swap ? t->filtered_x : t->filtered_y;
    /* Le gain agit après la zone morte pour garder un neutre stable.
     * À forte réactivité, une petite inclinaison donne une grande commande. */
    *x = clamp_command(normalize((axes & TILT_INVERT_X) != 0U ? -dx : dx) *
                       (0.6f + 0.2f * t->response_x));
    *y = clamp_command(normalize((axes & TILT_INVERT_Y) != 0U ? -dy : dy) *
                       (0.6f + 0.2f * t->response_y));
}

/*
 * Valider, recentrer et filtrer une mesure XYZ.
 *
 * Contrôle l'ordre de grandeur de la norme. Les 32 premières mesures valides définissent
 * le neutre. Le filtre IIR suit ensuite les écarts, avec le coefficient propre au bon axe
 * écran.
 */
void tilt_sample(Tilt *t, bool valid, float g_x, float g_y, float g_z)
{
    float norm_sq = g_x * g_x + g_y * g_y + g_z * g_z;
    t->valid = valid && norm_sq >= 0.35f && norm_sq <= 3.0f;
    if (!t->valid) {
        t->x = 0.0f;
        t->y = 0.0f;
        return;
    }
    if (!t->calibrated) {
        /* La référence et le filtre restent dans le repère du capteur. */
        t->sum_x += g_x;
        t->sum_y += g_y;
        ++t->neutral_count;
        if (t->neutral_count == 32U) {
            t->zero_x = t->sum_x / 32.0f;
            t->zero_y = t->sum_y / 32.0f;
            t->calibrated = true;
        }
        return;
    }

    /* La réactivité X/Y se règle dans le repère de l'écran. Retrouver
     * l'alpha de chaque axe brut avant d'appliquer le filtre IIR à 100 Hz. */
    bool swap = (t->axes & TILT_SWAP_XY) != 0U;
    float alpha_x = 0.10f + 0.08f * ((swap ? t->response_y : t->response_x) - 1U);
    float alpha_y = 0.10f + 0.08f * ((swap ? t->response_x : t->response_y) - 1U);
    t->filtered_x += alpha_x * (g_x - t->zero_x - t->filtered_x);
    t->filtered_y += alpha_y * (g_y - t->zero_y - t->filtered_y);
    tilt_project(t, t->axes, &t->x, &t->y);
}

/*
 * Commencer l'assistant d'orientation.
 *
 * Réinitialise le guide en phase de neutre et relance le recentrage. Le repère
 * actuellement actif est gardé jusqu'à la sauvegarde du candidat.
 */
void tilt_guide_start(TiltGuide *guide, Tilt *tilt)
{
    *guide = (TiltGuide){.phase = TILT_GUIDE_NEUTRAL};
    tilt_recenter(tilt);
}

/**
 * @brief Calculer la valeur absolue d'un flottant.
 *
 * Cette petite opération est utilisée pour comparer les dominantes du mouvement et
 * reconnaître le retour à plat.
 * @param[in] value Valeur à utiliser dans ce traitement.
 * @return Amplitude positive ou nulle de la valeur.
 */
static float amplitude(float value)
{
    return value >= 0.0f ? value : -value;
}

/**
 * @brief Classer une inclinaison dominante dans le repère brut.
 *
 * Une composante doit dépasser 0,13 g et l'autre rester assez faible. Les diagonales sont
 * donc rejetées pour ne pas confondre l'axe demandé.
 * @param[in] t Référence, filtre et commandes de l’accéléromètre, consulté sans
 *   modification. Pointeur valide.
 * @return ±1 pour X brut, ±2 pour Y brut, zéro sans direction nette.
 */
static int8_t direction(const Tilt *t)
{
    float x = amplitude(t->filtered_x), y = amplitude(t->filtered_y);
    /* Les valeurs ±1 désignent le X brut. ±2 désignent le Y brut.
     * Exiger une direction dominante pour écarter une inclinaison diagonale. */
    if (x >= 0.13f && y <= 0.55f * x) {
        return t->filtered_x > 0.0f ? 1 : -1;
    }
    if (y >= 0.13f && x <= 0.55f * y) {
        return t->filtered_y > 0.0f ? 2 : -2;
    }
    return 0;
}

/**
 * @brief Exiger seize observations successives d'une même pose.
 *
 * Un changement ou une pose indéfinie remet le compteur à zéro. La validation a lieu une
 * seule fois par série stable.
 * @param[in,out] guide Progression et candidat de l’assistant d’orientation. Pointeur valide.
 * @param[in] pose Code de pose candidate, zéro si indéfinie.
 * @return true quand le seuil de stabilité est atteint.
 */
static bool stable_pose(TiltGuide *guide, int8_t pose)
{
    if (guide->direction != pose || pose == 0) {
        guide->direction = pose;
        guide->stable = 0;
    }
    if (pose == 0 || ++guide->stable < 16U) {
        return false;
    }
    guide->stable = 0;
    return true;
}

/*
 * Faire avancer la reconnaissance droite puis bas.
 *
 * Après le neutre, le guide valide une inclinaison à droite, exige le retour à plat, puis
 * exige l'autre axe pour le bas. Les signes et la permutation sont déduits des poses
 * stabilisées.
 */
bool tilt_guide_sample(TiltGuide *guide, const Tilt *tilt)
{
    if (!tilt->valid || !tilt->calibrated) {
        guide->stable = 0;
        return false;
    }
    switch (guide->phase) {
    case TILT_GUIDE_NEUTRAL:
        guide->phase = TILT_GUIDE_RIGHT;
        return true;
    case TILT_GUIDE_RIGHT: {
        int8_t pose = direction(tilt);
        if (stable_pose(guide, pose)) {
            guide->right = pose;
            guide->phase = TILT_GUIDE_FLAT;
            return true;
        }
        break;
    }
    case TILT_GUIDE_FLAT:
        if (stable_pose(guide, amplitude(tilt->filtered_x) < 0.065f &&
                               amplitude(tilt->filtered_y) < 0.065f ? 3 : 0)) {
            guide->phase = TILT_GUIDE_DOWN;
            return true;
        }
        break;
    case TILT_GUIDE_DOWN: {
        int8_t pose = direction(tilt);
        if (pose == guide->right || pose == -guide->right) {
            pose = 0;
        }
        if (stable_pose(guide, pose)) {
            guide->axes = (guide->right == 2 || guide->right == -2 ? TILT_SWAP_XY : 0U) |
                          (guide->right < 0 ? TILT_INVERT_X : 0U) |
                          (pose < 0 ? TILT_INVERT_Y : 0U);
            guide->phase = TILT_GUIDE_PREVIEW;
            return true;
        }
        break;
    }
    default: break;
    }
    return false;
}
