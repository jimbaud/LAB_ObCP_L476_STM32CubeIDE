/**
 * @file audio.c
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
/** @def STEP_THRESHOLD
 * @brief Seuil du tempo, égal à 60 fois la fréquence d'échantillonnage.
 */
/** @def INC_440
 * @brief Incrément DDS entier produisant la note de diagnostic à 440 Hz.
 */
/** @var pitches
 * @brief Incréments DDS des huit notes, pour la basse puis le clavier une octave au-dessus.
 */

#include "obcp_audio.h"

#include <string.h>

/** @brief Constantes privées de ce module, avec leurs unités. */
enum {
    KICK, /**< Timbre de grosse caisse et indice de première piste. */
    SNARE, /**< Timbre de caisse claire et indice de deuxième piste. */
    HAT, /**< Timbre de charleston et indice de troisième piste. */
    BASS, /**< Timbre de basse et indice de quatrième piste. */
    KEYS, /**< Timbre tonal de la cinquième voix, réservée au clavier. */
};
#define STEP_THRESHOLD ((uint32_t)AUDIO_HZ * 60U)
#define INC_440 ((uint32_t)((UINT64_C(440) << 32) / AUDIO_HZ))

/* DDS sur 32 bits. Incrément = fréquence × 2^32 / 15625. */
static const uint32_t pitches[2][8] = {
    {35957549U, 40360983U, 45303672U, 47997558U, 53875438U, 60473140U, 67878818U, 71915097U},
    {71915097U, 80721966U, 90607345U, 95995117U, 107750875U, 120946279U, 135757608U, 143830167U},
};

/**
 * @brief Calculer une onde triangulaire à partir d'une phase DDS.
 *
 * Les neuf bits de poids fort désignent 512 positions dans la période. Deux segments
 * linéaires produisent le signal sans table ni fonction trigonométrique.
 * @param[in] phase Phase de l’oscillateur DDS sur 32 bits, une période correspondant à 2^32 incréments unitaires.
 * @return Amplitude entière de l'onde triangulaire.
 */
static int triangle(uint32_t phase)
{
    int position = (int)(phase >> 23);
    return position < 256 ? 2 * position - 255 : 767 - 2 * position;
}

/**
 * @brief Réarmer une voix pour une note.
 *
 * La piste choisit le timbre, puis la note choisit l'incrément DDS pour la basse ou le
 * clavier. La durée restante et la durée totale sont initialisées ensemble.
 * @param[in,out] a État du motif, du séquenceur et des voix. Pointeur valide.
 * @param[in] voice Indice de voix, entre zéro et AUDIO_VOICES moins un.
 * @param[in] note Hauteur de 1 à 8. Zéro signifie silence.
 */
static void trigger(Audio *a, unsigned voice, uint8_t note)
{
    /* Les durées sont comptées en échantillons, pas en millisecondes. À 15 625 Hz, 2 300 échantillons durent 147,2 ms. */
    static const uint16_t durations[AUDIO_VOICES] = {2300, 1350, 630, 2400, 5800};
    static const uint32_t base[AUDIO_VOICES] = {33000000U, 44000000U, 88000000U, 0U, 0U};
    if (note == 0U || note > 8U) {
        return;
    }
    unsigned timbre = voice < SEQ_TRACKS ? a->pattern.timbre[voice] : KEYS;
    uint32_t increment = base[timbre];
    if (timbre == BASS || timbre == KEYS) {
        increment = pitches[timbre == KEYS ? 1 : 0][note - 1U];
    }
    a->voices[voice] = (Voice){0U, increment, durations[timbre], durations[timbre], (uint8_t)timbre};
}

/**
 * @brief Déclencher les voix actives du pas courant.
 *
 * Chaque piste possède sa voix. La valeur zéro est un silence et les valeurs 1 à 8 sont
 * des hauteurs. Le moteur continue de faire décroître les voix déjà lancées.
 * @param[in,out] a État du motif, du séquenceur et des voix. Pointeur valide.
 */
static void play_step(Audio *a)
{
    for (unsigned track = 0; track < SEQ_TRACKS; ++track) {
        trigger(a, track, a->pattern.notes[track][a->step]);
    }
}

/*
 * Préparer un motif d'exemple et les oscillateurs.
 *
 * Réinitialise les états, choisit 112 BPM et un volume de 50 %, puis place quelques
 * événements sur les quatre pistes. La sortie matérielle reste sous le contrôle de
 * pwm_audio.c.
 */
void audio_init(Audio *a)
{
    memset(a, 0, sizeof(*a));
    a->pattern.bpm = 112;
    a->noise = UINT16_C(0xACE1);
    a->volume = 50;
    for (unsigned track = 0; track < SEQ_TRACKS; ++track) {
        a->pattern.timbre[track] = (uint8_t)track;
    }
    a->pattern.notes[KICK][0] = 1;
    a->pattern.notes[KICK][8] = 1;
    a->pattern.notes[SNARE][4] = 1;
    a->pattern.notes[SNARE][12] = 1;
    for (unsigned step = 2; step < SEQ_STEPS; step += 4) {
        a->pattern.notes[HAT][step] = 1;
    }
    a->pattern.notes[BASS][0] = 1;
    a->pattern.notes[BASS][6] = 3;
    a->pattern.notes[BASS][10] = 5;
    a->pattern.notes[BASS][14] = 4;
}

/*
 * Reprendre le séquenceur au premier pas.
 *
 * Remet la fraction de tempo et le curseur à zéro, active la lecture et déclenche
 * immédiatement les notes du premier pas.
 */
void audio_start(Audio *a)
{
    a->step = 0;
    a->beat_fraction = 0;
    a->playing = true;
    play_step(a);
}

/*
 * Arrêter le séquenceur, le test et les voix.
 *
 * Efface les enveloppes en cours. Le motif reste disponible pour une prochaine lecture. Le
 * transport PWM est arrêté par sa tâche dédiée.
 */
void audio_stop(Audio *a)
{
    a->playing = false;
    a->test_tone = false;
    memset(a->voices, 0, sizeof(a->voices));
}

/*
 * Déclencher la voix réservée au clavier.
 *
 * La voix manuelle est distincte des quatre pistes. La hauteur est interprétée dans la
 * même gamme de huit notes.
 */
void audio_key(Audio *a, uint8_t note)
{
    trigger(a, KEYS, note);
}

/*
 * Raccourcir la fin d'une note manuelle.
 *
 * La durée restante est bornée pour laisser une courte décroissance. Cette enveloppe évite
 * une coupure brutale de l'amplitude.
 */
void audio_key_release(Audio *a)
{
    Voice *voice = &a->voices[KEYS];
    if (voice->remaining > 500U) {
        voice->remaining = 500U;
        voice->length = 2600U;
    }
}

/**
 * @brief Calculer un échantillon d'une voix et avancer son état.
 *
 * Combine triangle ou bruit LFSR, glissement du kick, attaque puis décroissance. La phase
 * et la durée restante appartiennent à la voix et persistent entre deux blocs.
 * @param[in,out] a État du motif, du séquenceur et des voix. Pointeur valide.
 * @param[in,out] v Phase et enveloppe de la voix. Pointeur valide.
 * @return Échantillon signé. Zéro lorsque la voix est terminée.
 */
static int render_voice(Audio *a, Voice *v)
{
    if (v->remaining == 0) {
        return 0;
    }
    /* LFSR 16 bits pour les percussions. Les voix tonales utilisent la phase. */
    uint16_t noise_mask = (uint16_t)(0U - (a->noise & 1U));
    a->noise = (uint16_t)((a->noise >> 1) ^ (noise_mask & UINT16_C(0xB400)));
    int noise = (int)(a->noise & 511U) - 256;
    int sample = triangle(v->phase);
    if (v->type == SNARE || v->type == HAT) {
        sample = v->type == SNARE ? (3 * noise + sample) / 4 : noise;
    } else if (v->type == KICK && v->increment > 15000000U) {
        v->increment -= 11500U; /* glissement vers le grave */
    }
    /* Le débordement non signé sur 32 bits reboucle la phase. Il représente une période de l’oscillateur DDS. */
    v->phase += v->increment;
    unsigned elapsed = v->length - v->remaining;
    int attack = (int)(elapsed < 96U ? elapsed : 96U);
    int level = 256 * (int)v->remaining / (int)v->length;
    --v->remaining;
    return sample * level * attack / (256 * 96);
}

/*
 * Produire un bloc de rapports cycliques pour l'audio.
 *
 * Mélange les voix ou la note d'essai, applique le volume et borne l'amplitude autour de
 * 512. L'accumulateur de tempo avance exactement à la cadence des échantillons. Appel dans
 * la boucle principale.
 */
void audio_fill(Audio *a, uint16_t *dest, size_t samples)
{
    for (size_t i = 0; i < samples; ++i) {
        int mix = 0;
        if (a->test_tone) {
            a->test_phase += INC_440;
            mix = triangle(a->test_phase);
        } else {
            for (unsigned voice = 0; voice < AUDIO_VOICES; ++voice) {
                mix += render_voice(a, &a->voices[voice]);
            }
            mix /= 2;
        }
        mix = mix * (int)a->volume / 100;
        if (mix > 460) {
            mix = 460;
        } else if (mix < -460) {
            mix = -460;
        }
        /* Ajouter le centre transforme le signal signé en comparaison PWM. Le bornage laisse une marge aux rails 0 et 1 023. */
        dest[i] = (uint16_t)(AUDIO_CENTER + mix);

        /* 4 doubles croches par temps. L'accumulateur évite l'erreur
         * cumulative d'une durée entière arrondie à chaque pas. */
        if (a->playing) {
            a->beat_fraction += (uint32_t)a->pattern.bpm * 4U;
            if (a->beat_fraction >= STEP_THRESHOLD) {
                a->beat_fraction -= STEP_THRESHOLD;
                a->step = (uint8_t)((a->step + 1U) % SEQ_STEPS);
                play_step(a);
            }
        }
    }
}

/*
 * Encoder les quatre motifs dans un format stable.
 *
 * Écrit version, masque de présence, tempo little-endian, timbres et notes dans un tableau
 * d'octets. Les octets ne dépendent pas du remplissage des structures C.
 */
void book_encode(const PatternBook *book, uint8_t data[SEQ_BOOK_BYTES])
{
    data[0] = 1; /* version du format musical */
    data[1] = book->present;
    for (unsigned slot = 0; slot < SEQ_SLOTS; ++slot) {
        const Pattern *p = &book->pattern[slot];
        size_t offset = 2U + slot * SEQ_PATTERN_BYTES;
        data[offset] = (uint8_t)p->bpm;
        data[offset + 1U] = (uint8_t)(p->bpm >> 8);
        memcpy(&data[offset + 2U], p->timbre, sizeof(p->timbre));
        memcpy(&data[offset + 2U + SEQ_TRACKS], p->notes, sizeof(p->notes));
    }
}

/*
 * Valider puis décoder le livre musical.
 *
 * Travaille d'abord sur un candidat local. Le livre de destination est remplacé seulement
 * si version, masque, tempos, timbres et notes sont cohérents.
 */
bool book_decode(PatternBook *book, const uint8_t data[SEQ_BOOK_BYTES])
{
    /* Décoder dans un candidat local évite de remplacer un livre valide par un enregistrement partiellement incorrect. */
    PatternBook decoded = {0};
    if (data[0] != 1U || data[1] >= (1U << SEQ_SLOTS)) {
        return false;
    }
    decoded.present = data[1];
    for (unsigned slot = 0; slot < SEQ_SLOTS; ++slot) {
        Pattern *p = &decoded.pattern[slot];
        size_t offset = 2U + slot * SEQ_PATTERN_BYTES;
        if ((decoded.present & (1U << slot)) == 0) {
            continue;
        }
        p->bpm = (uint16_t)(data[offset] | (uint16_t)data[offset + 1U] << 8);
        if (p->bpm < SEQ_MIN_BPM || p->bpm > SEQ_MAX_BPM) {
            return false;
        }
        for (unsigned t = 0; t < SEQ_TRACKS; ++t) {
            uint8_t timbre = data[offset + 2U + t];
            if (timbre >= SEQ_TRACKS) {
                return false;
            }
            p->timbre[t] = timbre;
            for (unsigned step = 0; step < SEQ_STEPS; ++step) {
                uint8_t note = data[offset + 2U + SEQ_TRACKS + t * SEQ_STEPS + step];
                if (note > 8U) {
                    return false;
                }
                p->notes[t][step] = note;
            }
        }
    }
    *book = decoded;
    return true;
}
