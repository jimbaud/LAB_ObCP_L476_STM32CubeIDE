/**
 * @file calibration.c
 * @brief Transformation affine et assistant de calibrage résistif.
 * @ingroup obcp_input
 *
 * Trois contacts bruts définissent une base. Une quatrième cible indépendante vérifie la
 * transformation avant sauvegarde. Les deux profils, TSC2007 et STMPE610, sont stockés
 * sous forme de points pour reconstruire les coefficients au chargement.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
/** @var TOUCH_CAL_TARGETS
 * @brief Quatre cibles écran de référence. Les trois premières ajustent et la quatrième contrôle.
 */

#include "obcp_calibration.h"
#include "obcp_gfx.h"

#include <stdlib.h>
#include <string.h>

/** @brief Constantes privées de ce module, avec leurs unités. */
enum {
    CAL_RAW_MAX = 4095, /**< Plus grande valeur d’une conversion sur 12 bits. */
    CAL_MAX_DRIFT = 52, /**< Écart brut maximal autour de l’ancre pendant une acquisition. */
    CAL_MIN_DET = 160000, /**< Déterminant minimal en valeur absolue pour accepter la base brute. */
};
/** @brief Constantes privées de ce module, avec leurs unités. */
enum {
    CAL_VERSION = 1, /**< Version du format des profils résistifs. */
    CAL_WIRE_BYTES = 2 + 2 * CAL_FIT_COUNT * 4, /**< 26 octets pour version, orientation et deux jeux de trois points. */
};

/* Les trois premières cibles définissent deux vecteurs dans le repère
 * brut. La quatrième fournit une vérification indépendante. */
const RawPoint TOUCH_CAL_TARGETS[CAL_TARGET_COUNT] = {
    {26, 60}, {LCD_WIDTH - 27, 60},
    {26, LCD_HEIGHT - 27}, {LCD_WIDTH - 27, LCD_HEIGHT - 27}
};

/*
 * Calculer une transformation affine de trois points bruts.
 *
 * Le déterminant de deux vecteurs permet d'inverser la base. Un déterminant trop petit
 * rejette des observations alignées ou trop proches. Le candidat remplace cal seulement si
 * le calcul est admis.
 */
bool touch_cal_fit(TouchCalibration *cal, const RawPoint p[CAL_FIT_COUNT])
{
    for (unsigned i = 0; i < CAL_FIT_COUNT; ++i) {
        if (p[i].x > CAL_RAW_MAX || p[i].y > CAL_RAW_MAX) {
            return false;
        }
    }
    int32_t dx1 = (int32_t)p[1].x - p[0].x, dy1 = (int32_t)p[1].y - p[0].y;
    int32_t dx2 = (int32_t)p[2].x - p[0].x, dy2 = (int32_t)p[2].y - p[0].y;
    /* Le déterminant mesure l’aire orientée des deux vecteurs. Près de zéro, l’inversion amplifierait les erreurs. */
    int32_t det = dx1 * dy2 - dx2 * dy1;
    if (det > -CAL_MIN_DET && det < CAL_MIN_DET) {
        return false;
    }

    /* Inverser la base (P1 - P0, P2 - P0) et passer de cette base
     * aux pixels. Un déterminant négatif représente un axe inversé. */
    const RawPoint *target = TOUCH_CAL_TARGETS;
    float sx = (float)(target[1].x - target[0].x) / (float)det;
    float sy = (float)(target[2].y - target[0].y) / (float)det;
    TouchCalibration fit = {
        .x_from_x = sx * (float)dy2, .x_from_y = -sx * (float)dx2,
        .y_from_x = -sy * (float)dy1, .y_from_y = sy * (float)dx1,
        .valid = true
    };
    fit.x_offset = (float)target[0].x - fit.x_from_x * p[0].x - fit.x_from_y * p[0].y;
    fit.y_offset = (float)target[0].y - fit.y_from_x * p[0].x - fit.y_from_y * p[0].y;
    memcpy(fit.points, p, sizeof(fit.points));
    /* Publier le résultat seulement après validation. Une tentative refusée conserve le précédent profil. */
    *cal = fit;
    return true;
}

/**
 * @brief Arrondir et borner une coordonnée calculée.
 *
 * Les valeurs sous zéro et au-delà du dernier pixel sont saturées. L'ajout de 0,5 réalise
 * l'arrondi avant la conversion en entier.
 * @param[in] value Valeur à utiliser dans ce traitement.
 * @param[in] maximum Dernière coordonnée admise dans le résultat.
 * @return Coordonnée comprise entre zéro et maximum.
 */
static int16_t pixel(float value, int maximum)
{
    if (value <= 0.0f) {
        return 0;
    }
    if (value >= (float)maximum) {
        return (int16_t)maximum;
    }
    return (int16_t)(value + 0.5f);
}

/*
 * Transformer un point brut en pixels paysage.
 *
 * Vérifie le profil et les bornes des conversions 12 bits, applique les deux expressions
 * affines puis borne les résultats à l'écran.
 */
bool touch_cal_map(const TouchCalibration *cal, RawPoint raw, int16_t *x, int16_t *y)
{
    if (cal == NULL || !cal->valid || raw.x > CAL_RAW_MAX || raw.y > CAL_RAW_MAX) {
        return false;
    }
    *x = pixel(cal->x_from_x * raw.x + cal->x_from_y * raw.y + cal->x_offset, LCD_WIDTH - 1);
    *y = pixel(cal->y_from_x * raw.x + cal->y_from_y * raw.y + cal->y_offset, LCD_HEIGHT - 1);
    return true;
}

/*
 * Commencer une session en attendant le relâchement.
 *
 * Le contact qui a ouvert la procédure est consommé. La première cible attend donc un
 * nouveau geste de l'utilisateur.
 */
void touch_cal_start(TouchCalSession *session)
{
    *session = (TouchCalSession){.wait_release = true};
}

/*
 * Suspendre la stabilisation après une erreur de lecture.
 *
 * Les mesures partielles sont abandonnées et un relâchement sera demandé avant de
 * poursuivre la même cible.
 */
void touch_cal_interrupt(TouchCalSession *session)
{
    session->samples = 0;
    session->wait_release = true;
}

/*
 * Faire avancer l'acquisition d'une cible résistive.
 *
 * Six mesures proches sont moyennées, puis validées au relâchement. Les trois premières
 * cibles construisent la transformation. La quatrième vérifie l'erreur et peut relancer
 * toute la session.
 */
TouchCalEvent touch_cal_sample(TouchCalSession *s, RawTouch raw)
{
    if (s->wait_release) {
        s->wait_release = raw.down;
        return raw.down ? CAL_UNCHANGED : CAL_READY;
    }
    if (raw.down) {
        if (s->samples >= CAL_STABLE_SAMPLES) {
            return CAL_UNCHANGED;
        }
        if (s->samples == 0U || abs((int)raw.point.x - s->anchor.x) > CAL_MAX_DRIFT ||
            abs((int)raw.point.y - s->anchor.y) > CAL_MAX_DRIFT) {
            /* Un déplacement important recommence la moyenne sur la même cible, sans valider un point instable. */
            s->anchor = raw.point;
            s->sum_x = s->sum_y = 0;
            s->samples = 0;
        }
        s->sum_x += raw.point.x;
        s->sum_y += raw.point.y;
        return ++s->samples == CAL_STABLE_SAMPLES ? CAL_HELD : CAL_UNCHANGED;
    }

    /* Passer à la cible suivante au relâchement. Un contact bref ou
     * instable laisse la cible en attente. */
    bool held = s->samples == CAL_STABLE_SAMPLES;
    s->samples = 0;
    if (!held) {
        return CAL_UNCHANGED;
    }
    RawPoint p = {(uint16_t)(s->sum_x / CAL_STABLE_SAMPLES),
                  (uint16_t)(s->sum_y / CAL_STABLE_SAMPLES)};
    if (s->target < CAL_FIT_COUNT) {
        s->observations[s->target++] = p;
        s->retry = false;
        if (s->target < CAL_FIT_COUNT || touch_cal_fit(&s->candidate, s->observations)) {
            return CAL_ADVANCED;
        }
    } else {
        /* Cette quatrième cible n’a participé à aucun coefficient. Son erreur constitue un contrôle indépendant. */
        const RawPoint expected = TOUCH_CAL_TARGETS[CAL_FIT_COUNT];
        int16_t x, y;
        if (touch_cal_map(&s->candidate, p, &x, &y) &&
            abs((int)x - expected.x) <= CAL_MAX_ERROR_PX &&
            abs((int)y - expected.y) <= CAL_MAX_ERROR_PX) {
            return CAL_COMPLETE;
        }
    }
    *s = (TouchCalSession){.retry = true};
    return CAL_RETRY;
}

/**
 * @brief Associer un contrôleur résistif à sa place dans le livre.
 *
 * Le TSC2007 et le STMPE610 ont chacun un profil. Le capacitif utilise déjà des
 * coordonnées natives et n'occupe pas de profil résistif.
 * @param[in] controller Famille de contrôleur tactile.
 * @return Zéro pour TSC2007, un pour STMPE610, moins un sinon.
 */
static int profile_index(TouchController controller)
{
    if (controller == TOUCH_CTRL_TSC2007) {
        return 0;
    }
    if (controller == TOUCH_CTRL_STMPE610) {
        return 1;
    }
    return -1;
}

/*
 * Consulter le profil valide d'un contrôleur.
 *
 * L'adresse renvoyée appartient au livre fourni. Elle reste utilisable tant que ce livre
 * existe et que son contenu n'est pas remplacé.
 */
const TouchCalibration *touch_cal_get(const TouchCalBook *book, TouchController controller)
{
    int i = profile_index(controller);
    return i >= 0 && book->profile[i].valid ? &book->profile[i] : NULL;
}

/*
 * Remplacer le profil associé au contrôleur.
 *
 * Copie les points et coefficients en RAM. L'écriture flash reste une action explicite de
 * touch_cal_save(). Un type non résistif est ignoré.
 */
void touch_cal_set(TouchCalBook *book, TouchController controller, const TouchCalibration *cal)
{
    int i = profile_index(controller);
    if (i >= 0) {
        book->profile[i] = *cal;
    }
}

/*
 * Charger et reconstruire les deux profils résistifs.
 *
 * Commence par un livre vide. Vérifie la version et l'orientation du TFT, décode les
 * points little-endian puis recalcule les coefficients.
 */
StoreResult touch_cal_load(TouchCalBook *book, Storage *storage, uint8_t orientation)
{
    *book = (TouchCalBook){0};
    uint8_t data[CAL_WIRE_BYTES];
    StoreResult result = storage_read(storage, SAVE_TOUCH_CAL, data, sizeof(data));
    if (result != STORE_OK) {
        return result;
    }
    if (data[0] != CAL_VERSION || data[1] != orientation) {
        return STORE_INVALID;
    }
    const uint8_t *read = data + 2;
    for (unsigned i = 0; i < 2U; ++i) {
        RawPoint p[CAL_FIT_COUNT];
        for (unsigned j = 0; j < CAL_FIT_COUNT; ++j) {
            p[j].x = (uint16_t)(read[0] | (uint16_t)read[1] << 8);
            p[j].y = (uint16_t)(read[2] | (uint16_t)read[3] << 8);
            read += 4;
        }
        (void)touch_cal_fit(&book->profile[i], p);
    }
    return STORE_OK;
}

/*
 * Encoder puis enregistrer les deux profils.
 *
 * Le format contient version, orientation et observations brutes. Un profil absent est
 * encodé par des points nuls. Le service A/B fournit la validation et la résistance aux
 * coupures.
 */
StoreResult touch_cal_save(const TouchCalBook *book, Storage *storage, uint8_t orientation)
{
    /* Encoder les observations en little-endian, indépendamment du
     * remplissage des structures C et du format des flottants. */
    uint8_t data[CAL_WIRE_BYTES] = {CAL_VERSION, orientation};
    uint8_t *write = data + 2;
    for (unsigned i = 0; i < 2U; ++i) {
        const TouchCalibration *cal = &book->profile[i];
        for (unsigned j = 0; j < CAL_FIT_COUNT; ++j) {
            if (cal->valid) {
                RawPoint p = cal->points[j];
                write[0] = (uint8_t)p.x;
                write[1] = (uint8_t)(p.x >> 8);
                write[2] = (uint8_t)p.y;
                write[3] = (uint8_t)(p.y >> 8);
            }
            write += 4;
        }
    }
    return storage_save(storage, SAVE_TOUCH_CAL, data, sizeof(data));
}
