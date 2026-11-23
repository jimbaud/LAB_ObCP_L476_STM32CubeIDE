/**
 * @file obcp_audio.h
 * @brief Synthèse numérique et séquenceur musical sans accès matériel.
 * @ingroup obcp_audio
 *
 * Les échantillons sont des valeurs de comparaison CCR3 sur 10 bits, centrées autour de
 * 512. Le transport DMA consomme ces valeurs à 15 625 échantillons par seconde. Le format
 * des motifs encode chaque champ explicitement.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
#ifndef OBCP_AUDIO_H
#define OBCP_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* TIM1 à 64 MHz, ARR=1023, RCR=3. Un nouveau rapport toutes les 4 périodes. */
/**
 * @brief Dimensions, bornes et constantes du module.
 */
enum {
    AUDIO_HZ = 15625, /**< Cadence de 15 625 échantillons audio par seconde. */
    AUDIO_CENTER = 512, /**< Comparaison 512, centre du signal sur dix bits. */
    AUDIO_MAX_DUTY = 1023, /**< Comparaison maximale du timer, ARR vaut 1023. */
    AUDIO_VOICES = 5, /**< Cinq voix, quatre pistes et une voix de clavier. */
    SEQ_TRACKS = 4, /**< Quatre pistes musicales. */
    SEQ_STEPS = 16, /**< Seize doubles croches dans une boucle. */
    SEQ_SLOTS = 4, /**< Quatre emplacements musicaux persistants. */
    SEQ_MIN_BPM = 60, /**< Tempo minimum admis, 60 battements par minute. */
    SEQ_MAX_BPM = 180, /**< Tempo maximum admis, 180 battements par minute. */
    SEQ_PATTERN_BYTES = 2 + SEQ_TRACKS + SEQ_TRACKS * SEQ_STEPS, /**< Longueur encodée d'un motif, 70 octets. */
    SEQ_BOOK_BYTES = 2 + SEQ_SLOTS * SEQ_PATTERN_BYTES, /**< Longueur du livre versionné, 282 octets. */
};

/**
 * @brief Motif musical de quatre pistes et seize pas.
 *
 * La valeur zéro d’une note signifie silence. Les autres valeurs choisissent une des huit
 * hauteurs.
 */
typedef struct {
    uint16_t bpm; /**< Tempo en battements par minute, entre 60 et 180. */
    uint8_t timbre[SEQ_TRACKS]; /**< Type de son de chaque piste. 0 kick, 1 snare, 2 hat, 3 bass. */
    uint8_t notes[SEQ_TRACKS][SEQ_STEPS]; /**< Grille piste puis pas. Chaque case est zéro ou une hauteur de 1 à 8. */
} Pattern;
/**
 * @brief Quatre emplacements de motifs musicaux.
 *
 * Le masque de présence distingue un motif enregistré d’un emplacement jamais initialisé.
 */
typedef struct {
    Pattern pattern[SEQ_SLOTS]; /**< Tableau des quatre motifs, indexés de 0 à 3. */
    uint8_t present; /**< Bits de présence. Le bit i indique que le motif i est exploitable. */
} PatternBook;
/**
 * @brief État d’une voix de synthèse DDS et de son enveloppe.
 *
 * Quand remaining est non nul, length est positif. Les durées sont exprimées en
 * échantillons audio.
 */
typedef struct {
    uint32_t phase; /**< Accumulateur de phase non signé sur 32 bits. Son débordement réalise une période. */
    uint32_t increment; /**< Pas ajouté à la phase pour chaque échantillon. */
    uint16_t remaining; /**< Nombre d'échantillons restant à produire pour la voix. */
    uint16_t length; /**< Durée de référence de l'enveloppe, en échantillons. */
    uint8_t type; /**< Timbre interne, percussions, basse ou clavier. */
} Voice;
/**
 * @brief Séquenceur et synthèse partagés par la groovebox et le diagnostic.
 *
 * Les interruptions ne modifient pas cette structure. audio_fill() fait avancer les phases
 * et le tempo dans la boucle principale.
 */
typedef struct {
    Pattern pattern; /**< Motif actuellement édité et joué. */
    Voice voices[AUDIO_VOICES]; /**< Quatre voix de piste et une voix manuelle de clavier. */
    uint32_t beat_fraction; /**< Reste de l'accumulateur de tempo, à comparer à 60 fois AUDIO_HZ. */
    uint32_t test_phase; /**< Phase indépendante de la note de diagnostic à 440 Hz. */
    uint16_t noise; /**< Registre LFSR 16 bits utilisé pour le bruit des percussions. */
    uint8_t step; /**< Pas courant du séquenceur, de 0 à 15. */
    uint8_t volume; /**< Pourcentage d'amplitude de sortie, de 0 à 100. */
    bool playing; /**< true lorsque le séquenceur fait avancer les pas. */
    bool test_tone; /**< true lorsque la note de diagnostic remplace le mélange des voix. */
} Audio;

/**
 * @brief Préparer un motif d'exemple et les oscillateurs.
 *
 * Réinitialise les états, choisit 112 BPM et un volume de 50 %, puis place quelques
 * événements sur les quatre pistes. La sortie matérielle reste sous le contrôle de
 * pwm_audio.c.
 * @param[out] a État du motif, du séquenceur et des voix, à initialiser par cet appel.
 *   Pointeur valide.
 */
void audio_init(Audio *a);
/**
 * @brief Reprendre le séquenceur au premier pas.
 *
 * Remet la fraction de tempo et le curseur à zéro, active la lecture et déclenche
 * immédiatement les notes du premier pas.
 * @param[in,out] a État du motif, du séquenceur et des voix. Pointeur valide.
 */
void audio_start(Audio *a);
/**
 * @brief Arrêter le séquenceur, le test et les voix.
 *
 * Efface les enveloppes en cours. Le motif reste disponible pour une prochaine lecture. Le
 * transport PWM est arrêté par sa tâche dédiée.
 * @param[in,out] a État du motif, du séquenceur et des voix. Pointeur valide.
 */
void audio_stop(Audio *a);
/**
 * @brief Déclencher la voix réservée au clavier.
 *
 * La voix manuelle est distincte des quatre pistes. La hauteur est interprétée dans la
 * même gamme de huit notes.
 * @param[in,out] a État du motif, du séquenceur et des voix. Pointeur valide.
 * @param[in] note Hauteur de 1 à 8. Zéro signifie silence.
 */
void audio_key(Audio *a, uint8_t note);
/**
 * @brief Raccourcir la fin d'une note manuelle.
 *
 * La durée restante est bornée pour laisser une courte décroissance. Cette enveloppe évite
 * une coupure brutale de l'amplitude.
 * @param[in,out] a État du motif, du séquenceur et des voix. Pointeur valide.
 */
void audio_key_release(Audio *a);
/* Produit des comparaisons CCR3. Appel en boucle principale, jamais en IRQ. */
/**
 * @brief Produire un bloc de rapports cycliques pour l'audio.
 *
 * Mélange les voix ou la note d'essai, applique le volume et borne l'amplitude autour de
 * 512. L'accumulateur de tempo avance exactement à la cadence des échantillons. Appel dans
 * la boucle principale.
 * @param[in,out] a État du motif, du séquenceur et des voix. Pointeur valide.
 * @param[out] dest Tampon valide d’au moins samples comparaisons uint16_t.
 * @param[in] samples Nombre de comparaisons audio à produire dans dest.
 */
void audio_fill(Audio *a, uint16_t *dest, size_t samples);
/**
 * @brief Encoder les quatre motifs dans un format stable.
 *
 * Écrit version, masque de présence, tempo little-endian, timbres et notes dans un tableau
 * d'octets. Les octets ne dépendent pas du remplissage des structures C.
 * @param[in] book Livre des quatre motifs et masque de présence, consulté sans
 *   modification. Pointeur valide.
 * @param[out] data Tableau de SEQ_BOOK_BYTES octets à remplir.
 */
void book_encode(const PatternBook *book, uint8_t data[SEQ_BOOK_BYTES]);
/**
 * @brief Valider puis décoder le livre musical.
 *
 * Travaille d'abord sur un candidat local. Le livre de destination est remplacé seulement
 * si version, masque, tempos, timbres et notes sont cohérents.
 * @param[in,out] book Livre des quatre motifs et masque de présence. Pointeur valide.
 * @param[in] data Tableau de SEQ_BOOK_BYTES octets à décoder.
 * @return true si tous les motifs présents sont valides.
 */
bool book_decode(PatternBook *book, const uint8_t data[SEQ_BOOK_BYTES]);

#endif
