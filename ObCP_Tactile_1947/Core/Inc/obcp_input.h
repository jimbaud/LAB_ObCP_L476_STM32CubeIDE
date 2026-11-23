/**
 * @file obcp_input.h
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
#ifndef OBCP_INPUT_H
#define OBCP_INPUT_H

#include "obcp_touch.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Événements construits à partir des contacts successifs.
 */
typedef enum {
    TOUCH_NONE, /**< Aucun changement par rapport au contact précédent. */
    TOUCH_DOWN, /**< Premier contact validé. */
    TOUCH_MOVE, /**< Contact maintenu dont la position a changé. */
    TOUCH_UP, /**< Disparition validée du contact précédent. */
} TouchKind;
/**
 * @brief Événement de contact en coordonnées écran.
 *
 * Les coordonnées UP sont celles du dernier contact. TOUCH_NONE indique une absence de
 * changement.
 */
typedef struct {
    TouchKind kind; /**< Nature de la transition du doigt. */
    int16_t x; /**< Abscisse du contact, en pixels paysage. */
    int16_t y; /**< Ordonnée du contact, en pixels paysage. */
} TouchEvent;
/**
 * @brief Dernier contact accepté pour détecter une transition.
 *
 * Cette mémoire permet à touch_update() de comparer le nouvel échantillon au précédent.
 */
typedef struct {
    bool down; /**< true si le contact précédent était présent. */
    int16_t x; /**< Dernière abscisse écran mémorisée. */
    int16_t y; /**< Dernière ordonnée écran mémorisée. */
} TouchState;

/* Les axes natifs du FT6206 vont de 0 à 239 et de 0 à 319. Le paramètre
 * flipped représente un demi-tour relatif du tactile en mode paysage. */
/**
 * @brief Passer des coordonnées natives capacitives au paysage.
 *
 * Vérifie les plages portrait du FT62xx, puis applique la rotation et éventuellement le
 * demi-tour choisi dans la configuration. Les sorties sont des pixels du TFT.
 * @param[in] raw_x Abscisse native capacitive, de zéro à 239.
 * @param[in] raw_y Ordonnée native capacitive, de zéro à 319.
 * @param[in] flipped true pour ajouter un demi-tour au tactile capacitif.
 * @param[out] x Abscisse calculée. Pointeur de sortie valide.
 * @param[out] y Ordonnée calculée. Pointeur de sortie valide.
 * @return true si les coordonnées brutes appartiennent à la dalle attendue.
 */
bool touch_landscape(uint16_t raw_x, uint16_t raw_y, bool flipped, int16_t *x, int16_t *y);
/**
 * @brief Produire un événement à partir du contact et de sa position.
 *
 * Compare l'état reçu au précédent. Le premier appui donne DOWN, un changement de position
 * donne MOVE et la disparition du contact donne UP. L'état mémorisé est mis à jour.
 * @param[in,out] s Dernier état de contact utilisé pour détecter les transitions. Pointeur
 *   valide.
 * @param[in] down Présence physique ou filtrée d’un contact.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 * @return Événement de transition ou TOUCH_NONE si rien n'a changé.
 */
TouchEvent touch_update(TouchState *s, bool down, int16_t x, int16_t y);

/**
 * @brief Confirmation des fronts et médiane résistive.
 *
 * L’historique contient au plus trois points. Il est conservé entre les acquisitions
 * physiques.
 */
typedef struct {
    RawTouch current; /**< Dernier état de contact accepté par le filtre. */
    RawPoint history[3]; /**< Trois observations brutes pour la médiane par axe. */
    uint8_t used; /**< Nombre de positions utiles de l'historique, de 0 à 3. */
    uint8_t next; /**< Prochaine case circulaire à remplacer. */
    bool changing; /**< true lorsqu'un premier échantillon a annoncé un changement de contact. */
} ResistiveFilter;

/**
 * @brief Stabiliser un contact résistif et ses coordonnées.
 *
 * Deux mesures confirment les fronts. Une histoire de trois points permet ensuite une
 * médiane par axe. Le filtre conserve son état d'un appel au suivant.
 * @param[in,out] filter Historique et état de validation du contact résistif. Pointeur valide.
 * @param[in] raw Observation physique ou point natif à interpréter.
 * @return Dernière observation acceptée par le filtre.
 */
RawTouch touch_filter(ResistiveFilter *filter, RawTouch raw);

/**
 * @brief Dimensions, bornes et constantes du module.
 */
enum {
    TOUCH_BOOT_HOLD_MS = 1300, /**< Seuil du raccourci de démarrage, 1 300 millisecondes. */
};
/**
 * @brief Détection d’un doigt maintenu dès le reset.
 *
 * Une fois désarmée, cette mémoire ne déclenche plus de raccourci pendant la session.
 */
typedef struct {
    uint32_t since; /**< Tick du premier contact, en millisecondes non signées. */
    bool armed; /**< true tant que la séquence de démarrage doit examiner ce geste. */
    bool held; /**< true si un premier contact a commencé la mesure de durée. */
} TouchBoot;
/**
 * @brief Résultat du raccourci tactile de démarrage.
 */
typedef enum {
    BOOT_TOUCH_NONE, /**< Pas de commande de démarrage à traiter. */
    BOOT_TOUCH_USED, /**< Contact de démarrage consommé sans événement écran. */
    BOOT_TOUCH_CALIBRATE, /**< Maintien assez long pour ouvrir le calibrage brut. */
} BootTouchEvent;
/**
 * @brief Reconnaître le maintien tactile demandé au reset.
 *
 * Le premier contact arme une mesure de durée. Un relâchement précoce consomme le geste.
 * Au seuil de 1,3 s, l'événement de calibrage est émis une seule fois.
 * @param[in,out] boot Mémoire du contact maintenu au démarrage. Pointeur valide.
 * @param[in] down Présence physique ou filtrée d’un contact.
 * @param[in] now Tick courant en millisecondes non signées.
 * @return Événement absent, geste consommé ou ouverture du calibrage de secours.
 */
BootTouchEvent touch_boot_sample(TouchBoot *boot, bool down, uint32_t now);

/**
 * @brief Dimensions, bornes et constantes du module.
 */
enum {
    BUTTON_DEBOUNCE_MS = 28, /**< Stabilité minimale de SW1, 28 millisecondes. */
    BUTTON_HOLD_MS = 900, /**< Seuil d'appui long de SW1, 900 millisecondes. */
};
/**
 * @brief Événements déjà débouncés du bouton utilisateur.
 */
typedef enum {
    BUTTON_NONE, /**< Aucun nouvel événement de SW1. */
    BUTTON_SHORT, /**< Appui validé au relâchement avant le seuil long. */
    BUTTON_LONG, /**< Appui ayant franchi le seuil long, émis une seule fois. */
} ButtonEvent;
/**
 * @brief Mémoire de l’anti-rebond de SW1.
 *
 * Le niveau logique pressed est déjà converti depuis PC13 actif à zéro par board.c.
 */
typedef struct {
    bool raw; /**< Dernier niveau lu avant validation de sa stabilité. */
    bool stable; /**< Niveau retenu après le délai de débounce. */
    bool consumed; /**< true si l'appui long ou le démarrage a déjà consommé le geste. */
    uint32_t changed_at; /**< Tick du dernier changement de niveau brut, en millisecondes. */
    uint32_t pressed_at; /**< Tick de début de l'appui, utilisé pour sa durée. */
} ButtonState;

/* Horodatages en ms. Un appui déjà présent au démarrage est réservé à l'amorçage. */
/**
 * @brief Initialiser l'anti-rebond de SW1.
 *
 * Un bouton déjà appuyé au démarrage est considéré consommé pour éviter de déclencher une
 * commande après la procédure de démarrage.
 * @param[in,out] b Mémoire de stabilité et de durée du bouton. Pointeur valide.
 * @param[in] pressed État logique appuyé, après conversion du GPIO actif bas.
 * @param[in] now Tick courant en millisecondes non signées.
 */
void button_init(ButtonState *b, bool pressed, uint32_t now);
/**
 * @brief Transformer le niveau de SW1 en appui court ou long.
 *
 * Valide un changement après 28 ms. Le court est émis au relâchement, le long après 900 ms
 * et une seule fois. Les différences de ticks non signés tolèrent leur débordement.
 * @param[in,out] b Mémoire de stabilité et de durée du bouton. Pointeur valide.
 * @param[in] pressed État logique appuyé, après conversion du GPIO actif bas.
 * @param[in] now Tick courant en millisecondes non signées.
 * @return BUTTON_NONE, BUTTON_SHORT ou BUTTON_LONG.
 */
ButtonEvent button_update(ButtonState *b, bool pressed, uint32_t now);

/**
 * @brief Neutre, filtre et commande d’inclinaison.
 *
 * Le neutre et les mesures filtrées sont dans le repère brut du capteur. x et y sont dans
 * le repère écran après permutation, signes, zone morte et gain.
 */
typedef struct {
    float zero_x; /**< Référence de repos X brut, en g. */
    float zero_y; /**< Référence de repos Y brut, en g. */
    float sum_x; /**< Somme des mesures X pendant l'acquisition du neutre, en g. */
    float sum_y; /**< Somme des mesures Y pendant l'acquisition du neutre, en g. */
    float filtered_x; /**< Écart X brut au neutre après filtre IIR, en g. */
    float filtered_y; /**< Écart Y brut au neutre après filtre IIR, en g. */
    float x; /**< Commande horizontale écran adimensionnelle, bornée entre −1 et +1. */
    float y; /**< Commande verticale écran adimensionnelle, bornée entre −1 et +1. */
    unsigned neutral_count; /**< Mesures valides accumulées pour la moyenne de 32 points. */
    uint8_t axes; /**< Masque TILT_SWAP_XY, TILT_INVERT_X et TILT_INVERT_Y. */
    uint8_t response_x; /**< Réactivité de l'axe écran X, de 1 à 10. */
    uint8_t response_y; /**< Réactivité de l'axe écran Y, de 1 à 10. */
    bool calibrated; /**< true après les 32 mesures de neutre valides. */
    bool valid; /**< true si la dernière lecture et l'ordre de grandeur de la norme sont admis. */
} Tilt;

/**
 * @brief Dimensions, bornes et constantes du module.
 */
enum {
    TILT_SWAP_XY = 1, /**< Bit zéro, permuter les axes bruts pour le repère écran. */
    TILT_INVERT_X = 2, /**< Bit un, inverser le signe de l'axe écran X. */
    TILT_INVERT_Y = 4, /**< Bit deux, inverser le signe de l'axe écran Y. */
    TILT_AXES_MASK = TILT_SWAP_XY | TILT_INVERT_X | TILT_INVERT_Y, /**< Masque des trois seuls bits de repère admis. */
};
/**
 * @brief Dimensions, bornes et constantes du module.
 */
enum {
    TILT_RESPONSE_MIN = 1, /**< Réactivité minimale, un sur dix. */
    TILT_RESPONSE_MAX = 10, /**< Réactivité maximale, dix sur dix. */
    TILT_RESPONSE_DEFAULT = 6, /**< Réactivité initiale de six sur dix pour chaque axe. */
};
/**
 * @brief Préparer le repère et les réactivités initiales.
 *
 * Les signes et la permutation viennent de obcp_config.h. La réactivité initiale est six
 * sur dix pour chaque axe. Le neutre sera acquis par tilt_sample().
 * @param[out] t Référence, filtre et commandes de l’accéléromètre, à initialiser par cet
 *   appel. Pointeur valide.
 */
void tilt_init(Tilt *t);
/**
 * @brief Recommencer la mesure de la pose de repos.
 *
 * Efface sommes, compteurs et états du filtre, tout en conservant le repère et les deux
 * réglages de réactivité. Les 32 prochaines mesures valides établiront le neutre.
 * @param[in,out] t Référence, filtre et commandes de l’accéléromètre. Pointeur valide.
 */
void tilt_recenter(Tilt *t);
/**
 * @brief Valider, recentrer et filtrer une mesure XYZ.
 *
 * Contrôle l'ordre de grandeur de la norme. Les 32 premières mesures valides définissent
 * le neutre. Le filtre IIR suit ensuite les écarts, avec le coefficient propre au bon axe
 * écran.
 * @param[in,out] t Référence, filtre et commandes de l’accéléromètre. Pointeur valide.
 * @param[in] valid true si la transaction du capteur a réussi.
 * @param[in] g_x Mesure X brute exprimée en g.
 * @param[in] g_y Mesure Y brute exprimée en g.
 * @param[in] g_z Mesure Z brute exprimée en g.
 */
void tilt_sample(Tilt *t, bool valid, float g_x, float g_y, float g_z);
/* Appliquer un repère écran aux mesures filtrées, pour le jeu ou l'aperçu. */
/**
 * @brief Projeter le filtre brut dans un repère écran choisi.
 *
 * Permute les composantes, applique leurs signes, la zone morte puis les gains X/Y. Un
 * capteur non prêt donne deux commandes nulles. L'aperçu peut ainsi essayer le repère
 * proposé avant sa validation.
 * @param[in] t Référence, filtre et commandes de l’accéléromètre, consulté sans
 *   modification. Pointeur valide.
 * @param[in] axes Masque de permutation et de signes TILT_AXES_MASK.
 * @param[out] x Commande horizontale normalisée entre −1 et +1. Pointeur de sortie valide.
 * @param[out] y Commande verticale normalisée entre −1 et +1. Pointeur de sortie valide.
 */
void tilt_project(const Tilt *t, uint8_t axes, float *x, float *y);

/**
 * @brief Étapes de reconnaissance du repère de l’écran.
 */
typedef enum {
    TILT_GUIDE_INTRO, /**< Attente de Mesurer avec la carte posée à plat. */
    TILT_GUIDE_NEUTRAL, /**< Acquisition du neutre avant les inclinaisons. */
    TILT_GUIDE_RIGHT, /**< Reconnaissance du bord droit abaissé. */
    TILT_GUIDE_FLAT, /**< Retour à plat entre les deux gestes. */
    TILT_GUIDE_DOWN, /**< Reconnaissance du bord inférieur abaissé sur l'autre axe. */
    TILT_GUIDE_PREVIEW, /**< Aperçu des bits proposés avant validation. */
} TiltGuidePhase;
/**
 * @brief Candidat de repère de l’assistant d’axes.
 *
 * Les directions signées ±1 et ±2 désignent les axes bruts X et Y. Le candidat est
 * appliqué seulement à la validation utilisateur.
 */
typedef struct {
    TiltGuidePhase phase; /**< Instruction actuellement présentée à l'utilisateur. */
    int8_t direction; /**< Pose candidate en cours de stabilisation. */
    int8_t right; /**< Axe brut et signe validés quand le bord droit a été abaissé. */
    uint8_t stable; /**< Nombre de poses successives identiques, seuil de seize mesures. */
    uint8_t axes; /**< Bits de repère proposés à la fin du guide. */
} TiltGuide;

/**
 * @brief Commencer l'assistant d'orientation.
 *
 * Réinitialise le guide en phase de neutre et relance le recentrage. Le repère
 * actuellement actif est gardé jusqu'à la sauvegarde du candidat.
 * @param[in,out] guide Progression et candidat de l’assistant d’orientation. Pointeur valide.
 * @param[in,out] tilt Référence, filtre et commandes de l’accéléromètre. Pointeur valide.
 */
void tilt_guide_start(TiltGuide *guide, Tilt *tilt);
/* true demande un rafraîchissement parce que l'instruction a changé. */
/**
 * @brief Faire avancer la reconnaissance droite puis bas.
 *
 * Après le neutre, le guide valide une inclinaison à droite, exige le retour à plat, puis
 * exige l'autre axe pour le bas. Les signes et la permutation sont déduits des poses
 * stabilisées.
 * @param[in,out] guide Progression et candidat de l’assistant d’orientation. Pointeur valide.
 * @param[in] tilt Référence, filtre et commandes de l’accéléromètre, consulté sans
 *   modification. Pointeur valide.
 * @return true si l'instruction a changé et nécessite un rafraîchissement complet.
 */
bool tilt_guide_sample(TiltGuide *guide, const Tilt *tilt);

#endif
