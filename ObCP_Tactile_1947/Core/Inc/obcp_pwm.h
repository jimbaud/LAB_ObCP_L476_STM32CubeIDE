/**
 * @file obcp_pwm.h
 * @brief Transport des rapports cycliques par TIM1 et DMA circulaire.
 * @ingroup obcp_audio
 *
 * Deux demi-tampons de 256 comparaisons alternent sur DMA1 canal 6. Les interruptions
 * comptent les moitiés consommées. La synthèse reste dans la boucle principale et passe
 * par staging avant une copie protégée vers la moitié libre.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
#ifndef OBCP_PWM_H
#define OBCP_PWM_H

#include "obcp_audio.h"

/**
 * @brief Préparer l'état de repos de la sortie PWM.
 *
 * L'initialisation démarre avec PB1 en GPIO bas. Le timer et le DMA seront lancés
 * seulement quand l'application demandera de l'audio.
 */
void pwm_audio_init(void);
/* Activer ou arrêter le timer et remplir la demi-zone libérée par le DMA. */
/**
 * @brief Produire au bon moment la moitié libre de l'anneau.
 *
 * Compare epoch à served, synthétise dans staging puis relit le compteur DMA. La copie se
 * fait seulement si le DMA utilise toujours l'autre moitié avec une marge de huit
 * échantillons. Les échéances perdues sont comptées.
 * @param[in,out] audio État du motif, du séquenceur et des voix. Pointeur valide.
 * @param[in] needed true si l’application demande actuellement une sortie audio.
 */
void pwm_audio_task(Audio *audio, bool needed);
/**
 * @brief Consulter le nombre d'échéances ou erreurs audio.
 *
 * Ce compteur sert à qualifier le comportement lorsque l'écran et le tactile sollicitent
 * aussi le processeur.
 * @return Nombre cumulé d'événements audio manqués depuis le démarrage.
 */
uint32_t pwm_audio_missed(void);

#endif
