/**
 * @file obcp_calibration.h
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
#ifndef OBCP_CALIBRATION_H
#define OBCP_CALIBRATION_H

/** @var TOUCH_CAL_TARGETS
 * @brief Quatre cibles écran de référence. Les trois premières ajustent et la quatrième contrôle.
 */

#include "obcp_storage.h"
#include "obcp_touch.h"

/**
 * @brief Dimensions, bornes et constantes du module.
 */
enum {
    CAL_TARGET_COUNT = 4, /**< Quatre cibles utilisateur dont une cible de contrôle. */
    CAL_FIT_COUNT = 3, /**< Trois points définissant la transformation affine. */
    CAL_STABLE_SAMPLES = 6, /**< Six observations proches exigées par cible. */
    CAL_MAX_ERROR_PX = 14, /**< Erreur de contrôle admise sur chaque axe, 14 pixels. */
};

/* Une transformation affine et les trois observations qui l'ont définie. */
/**
 * @brief Transformation affine du brut résistif vers les pixels.
 *
 * Les trois observations suffisent à reconstruire les six coefficients. Seuls les points
 * sont encodés dans la flash.
 */
typedef struct {
    RawPoint points[CAL_FIT_COUNT]; /**< Trois observations brutes ayant défini le profil. */
    float x_from_x; /**< Coefficient de X brut dans le calcul de l'abscisse écran. */
    float x_from_y; /**< Coefficient de Y brut dans le calcul de l'abscisse écran. */
    float x_offset; /**< Décalage de l'abscisse écran, en pixels. */
    float y_from_x; /**< Coefficient de X brut dans le calcul de l'ordonnée écran. */
    float y_from_y; /**< Coefficient de Y brut dans le calcul de l'ordonnée écran. */
    float y_offset; /**< Décalage de l'ordonnée écran, en pixels. */
    bool valid; /**< true après le calcul réussi d'une base non dégénérée. */
} TouchCalibration;

/**
 * @brief Profils des deux familles résistives.
 *
 * Le TSC2007 occupe la première place et le STMPE610 la seconde.
 */
typedef struct {
    TouchCalibration profile[2]; /**< Profils reconstruits depuis leurs observations brutes. */
} TouchCalBook;

/**
 * @brief Acquisition progressive des quatre cibles.
 *
 * Un contact stable est moyenné puis validé au relâchement. Le quatrième contact contrôle
 * le candidat obtenu avec les trois premiers.
 */
typedef struct {
    TouchCalibration candidate; /**< Transformation construite et contrôlée pendant la session. */
    RawPoint observations[CAL_FIT_COUNT]; /**< Trois observations moyennées pour l'ajustement affine. */
    RawPoint anchor; /**< Première position du contact actuellement stabilisé. */
    uint32_t sum_x; /**< Somme des abscisses brutes du contact courant. */
    uint32_t sum_y; /**< Somme des ordonnées brutes du contact courant. */
    uint8_t target; /**< Indice de la cible attendue, de 0 à 3. */
    uint8_t samples; /**< Nombre de mesures stables du contact, de 0 à 6. */
    bool wait_release; /**< true quand le contact courant doit d'abord être relâché. */
    bool retry; /**< true après un rejet, pour demander de recommencer les cibles. */
} TouchCalSession;

/**
 * @brief Événements de l’assistant de calibrage résistif.
 */
typedef enum {
    CAL_UNCHANGED, /**< La session attend encore un événement utile. */
    CAL_READY, /**< Le doigt a été relâché, une nouvelle cible peut être acquise. */
    CAL_HELD, /**< Les six mesures de la cible sont stabilisées, attendre le relâchement. */
    CAL_ADVANCED, /**< La session a validé une cible et avance à la suivante. */
    CAL_RETRY, /**< Les observations sont incohérentes, reprendre les cibles. */
    CAL_COMPLETE, /**< La quatrième cible confirme la transformation candidate. */
} TouchCalEvent;

extern const RawPoint TOUCH_CAL_TARGETS[CAL_TARGET_COUNT];

/**
 * @brief Calculer une transformation affine de trois points bruts.
 *
 * Le déterminant de deux vecteurs permet d'inverser la base. Un déterminant trop petit
 * rejette des observations alignées ou trop proches. Le candidat remplace cal seulement si
 * le calcul est admis.
 * @param[in,out] cal Transformation affine et observations de référence. Pointeur valide.
 * @param[in] points Trois observations brutes, dans l’ordre des cibles de référence.
 * @return true si une transformation exploitable est calculée.
 */
bool touch_cal_fit(TouchCalibration *cal, const RawPoint points[CAL_FIT_COUNT]);
/**
 * @brief Transformer un point brut en pixels paysage.
 *
 * Vérifie le profil et les bornes des conversions 12 bits, applique les deux expressions
 * affines puis borne les résultats à l'écran.
 * @param[in] cal Transformation affine et observations de référence, consulté sans
 *   modification. Pointeur valide.
 * @param[in] raw Observation physique ou point natif à interpréter.
 * @param[out] x Abscisse calculée. Pointeur de sortie valide.
 * @param[out] y Ordonnée calculée. Pointeur de sortie valide.
 * @return true si les coordonnées ont été calculées.
 */
bool touch_cal_map(const TouchCalibration *cal, RawPoint raw, int16_t *x, int16_t *y);
/**
 * @brief Commencer une session en attendant le relâchement.
 *
 * Le contact qui a ouvert la procédure est consommé. La première cible attend donc un
 * nouveau geste de l'utilisateur.
 * @param[in,out] session Session d’acquisition des quatre cibles. Pointeur valide.
 */
void touch_cal_start(TouchCalSession *session);
/**
 * @brief Suspendre la stabilisation après une erreur de lecture.
 *
 * Les mesures partielles sont abandonnées et un relâchement sera demandé avant de
 * poursuivre la même cible.
 * @param[in,out] session Session d’acquisition des quatre cibles. Pointeur valide.
 */
void touch_cal_interrupt(TouchCalSession *session);
/**
 * @brief Faire avancer l'acquisition d'une cible résistive.
 *
 * Six mesures proches sont moyennées, puis validées au relâchement. Les trois premières
 * cibles construisent la transformation. La quatrième vérifie l'erreur et peut relancer
 * toute la session.
 * @param[in,out] session Session d’acquisition des quatre cibles. Pointeur valide.
 * @param[in] raw Observation physique ou point natif à interpréter.
 * @return Événement de session indiquant attente, maintien, avance, reprise ou fin.
 */
TouchCalEvent touch_cal_sample(TouchCalSession *session, RawTouch raw);

/**
 * @brief Consulter le profil valide d'un contrôleur.
 *
 * L'adresse renvoyée appartient au livre fourni. Elle reste utilisable tant que ce livre
 * existe et que son contenu n'est pas remplacé.
 * @param[in] book Livre des deux profils résistifs, consulté sans modification. Pointeur
 *   valide.
 * @param[in] controller Famille de contrôleur tactile.
 * @return Pointeur constant sur le profil, ou NULL s'il manque.
 */
const TouchCalibration *touch_cal_get(const TouchCalBook *book, TouchController controller);
/**
 * @brief Remplacer le profil associé au contrôleur.
 *
 * Copie les points et coefficients en RAM. L'écriture flash reste une action explicite de
 * touch_cal_save(). Un type non résistif est ignoré.
 * @param[in,out] book Livre des deux profils résistifs. Pointeur valide.
 * @param[in] controller Famille de contrôleur tactile.
 * @param[in] cal Transformation affine et observations de référence, consulté sans
 *   modification. Pointeur valide.
 */
void touch_cal_set(TouchCalBook *book, TouchController controller, const TouchCalibration *cal);
/**
 * @brief Charger et reconstruire les deux profils résistifs.
 *
 * Commence par un livre vide. Vérifie la version et l'orientation du TFT, décode les
 * points little-endian puis recalcule les coefficients.
 * @param[in,out] book Livre des deux profils résistifs. Pointeur valide.
 * @param[in,out] storage Port de stockage et ses callbacks synchrones. Pointeur valide.
 * @param[in] orientation Octet MADCTL auquel les observations persistantes sont associées.
 * @return Résultat du stockage ou STORE_INVALID pour une version ou orientation incompatible.
 */
StoreResult touch_cal_load(TouchCalBook *book, Storage *storage, uint8_t orientation);
/**
 * @brief Encoder puis enregistrer les deux profils.
 *
 * Le format contient version, orientation et observations brutes. Un profil absent est
 * encodé par des points nuls. Le service A/B fournit la validation et la résistance aux
 * coupures.
 * @param[in] book Livre des deux profils résistifs, consulté sans modification. Pointeur
 *   valide.
 * @param[in,out] storage Port de stockage et ses callbacks synchrones. Pointeur valide.
 * @param[in] orientation Octet MADCTL auquel les observations persistantes sont associées.
 * @return Résultat du service de sauvegarde.
 */
StoreResult touch_cal_save(const TouchCalBook *book, Storage *storage, uint8_t orientation);

#endif
