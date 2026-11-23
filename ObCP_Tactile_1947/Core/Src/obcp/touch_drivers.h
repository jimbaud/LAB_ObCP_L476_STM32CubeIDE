/**
 * @file touch_drivers.h
 * @brief Reconnaissance des shields et façade tactile commune.
 * @ingroup obcp_drivers
 *
 * L’application lit toujours un RawTouch et un état TouchRead. La façade choisit un
 * protocole capacitif I²C, résistif SPI ou résistif I²C. Un état WAIT conserve la
 * possibilité de servir d’autres tâches entre deux étapes.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
#ifndef OBCP_TOUCH_DRIVERS_H
#define OBCP_TOUCH_DRIVERS_H

#include "obcp_touch.h"

/**
 * @brief Vérifier l'identifiant de famille du tactile capacitif.
 *
 * Le registre 0xA3 est lu. Les identifiants 0x06, 0x36 et 0x64 sont admis. Le résultat de
 * sortie commence à zéro pour éviter une identité résiduelle.
 * @param[out] chip Pointeur valide recevant l’identifiant lu du composant.
 * @return true si une signature compatible est reçue.
 */
bool ft6206_probe(uint8_t *chip);
/**
 * @brief Lire l'état et les coordonnées du premier doigt.
 *
 * Une rafale contient TD_STATUS et les deux coordonnées sur 12 bits. L'événement natif de
 * relâchement ne doit pas devenir un nouvel appui. Les coordonnées sont écrites seulement
 * si le doigt est présent.
 * @param[out] pressed Pointeur valide recevant true lorsqu’un doigt est présent.
 * @param[out] raw_x Coordonnée native de sortie, écrite seulement lorsque le contact est
 *   présent.
 * @param[out] raw_y Coordonnée native de sortie, écrite seulement lorsque le contact est
 *   présent.
 * @return true si la transaction réussit, y compris lorsque pressed vaut false.
 */
bool ft6206_sample(bool *pressed, uint16_t *raw_x, uint16_t *raw_y);
/**
 * @brief Reconnaître 0x0811 et configurer le tactile.
 *
 * Les modes SPI zéro puis un sont essayés à 1 MHz. La phase reconnue est conservée pour
 * les acquisitions suivantes. Les réglages du TFT sont restaurés à la sortie.
 * @param[out] chip Pointeur valide recevant l’identifiant lu du composant.
 * @return true si l'identifiant attendu et l'initialisation sont valides.
 */
bool stmpe610_probe(uint16_t *chip);
/**
 * @brief Acquérir une observation entre deux bandes TFT.
 *
 * BUSY donne WAIT pour rendre la main. Sinon le pilote emprunte le SPI, lit ses points,
 * puis remet les paramètres initiaux quelle que soit l'issue.
 * @param[out] sample Structure RawTouch valide recevant l’observation physique.
 * @return État tactile OK, WAIT, EMPTY ou ERROR.
 */
TouchRead stmpe610_sample(RawTouch *sample);
/**
 * @brief Tester le protocole de conversion du TSC2007.
 *
 * Remet l'état d'acquisition à zéro, demande TEMP0 avec mise en sommeil et attend une
 * milliseconde au seul démarrage. Le contrôle suppose un périphérique dédié à 0x48.
 * @return true si une réponse de conversion compatible est reçue.
 */
bool tsc2007_probe(void);
/**
 * @brief Avancer d'une étape dans l'observation résistive.
 *
 * Alterner commande, attente puis lecture permet de servir l'écran et l'audio. Z1 et Z2
 * décident du contact avec hystérésis. X et Y sont lus seulement quand le contact est
 * retenu.
 * @param[out] sample Structure RawTouch valide recevant l’observation physique.
 * @return TOUCH_READ_WAIT pendant une conversion, OK au résultat complet ou ERROR sur incident de bus.
 */
TouchRead tsc2007_sample(RawTouch *sample);

#endif
