/**
 * @file obcp_touch.h
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
#ifndef OBCP_TOUCH_H
#define OBCP_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Contrôleurs tactiles reconnus sur les shields compatibles.
 */
typedef enum {
    TOUCH_CTRL_NONE, /**< Aucun contrôleur compatible reconnu. */
    TOUCH_CTRL_FT62XX, /**< Capacitif FT62xx sur I²C à 0x38. */
    TOUCH_CTRL_STMPE610, /**< Résistif sur SPI1, identifié par 0x0811. */
    TOUCH_CTRL_TSC2007, /**< Résistif sur I²C à 0x48, reconnu par conversion. */
} TouchController;

/**
 * @brief Résultat de la reconnaissance tactile.
 *
 * Le type pilote la façade. L’identifiant complète l’affichage quand le composant possède
 * un registre d’identité.
 */
typedef struct {
    TouchController controller; /**< Famille de contrôleur ou TOUCH_CTRL_NONE. */
    uint16_t id; /**< Identifiant natif. Zéro pour TSC2007, reconnu par protocole. */
} TouchDevice;

/**
 * @brief Position native avant calibrage et rotation.
 *
 * Les familles résistives livrent des conversions 12 bits. Le capacitif livre les
 * coordonnées portrait propres à sa dalle.
 */
typedef struct {
    uint16_t x; /**< Coordonnée horizontale dans les unités natives du contrôleur. */
    uint16_t y; /**< Coordonnée verticale dans les unités natives du contrôleur. */
} RawPoint;
/**
 * @brief Observation physique d’un contrôleur tactile.
 *
 * Le type de contrôleur définit les unités. La transformation vers les pixels intervient
 * seulement après filtrage et choix du profil.
 */
typedef struct {
    RawPoint point; /**< Coordonnées natives du contact quand down est vrai. */
    uint16_t z1; /**< Pression Z1 du TSC2007 ou mesure Z du STMPE610. Zéro pour FT62xx. */
    uint16_t z2; /**< Pression Z2 du TSC2007, sinon zéro. */
    bool down; /**< true si le pilote retient un contact présent. */
} RawTouch;

/* WAIT cède la main au DMA ou à l'ADC. EMPTY reporte la lecture au
 * prochain échantillonnage. Les deux conservent le dernier contact. */
/**
 * @brief États de progression d’une acquisition physique.
 */
typedef enum {
    TOUCH_READ_OK, /**< Observation complète, avec ou sans contact. */
    TOUCH_READ_WAIT, /**< Le pilote attend une conversion ou la disponibilité de SPI1. */
    TOUCH_READ_EMPTY, /**< FIFO vide sous un contact, conserver le dernier état logique. */
    TOUCH_READ_ERROR, /**< Erreur de bus ou réponse incompatible. */
} TouchRead;

/* Retourne false si le bus SPI est encore occupé par une bande du TFT. */
/**
 * @brief Reconnaître le contrôleur tactile présent.
 *
 * Vérifie d'abord que SPI1 peut être utilisé, essaie les signatures FT62xx puis STMPE610,
 * enfin le protocole TSC2007. Une recherche terminée peut produire TOUCH_CTRL_NONE.
 * @param[in,out] device Type et identifiant du contrôleur tactile. Pointeur valide.
 * @return true si la recherche a pu être effectuée. false quand le SPI est occupé.
 */
bool touch_probe(TouchDevice *device);
/**
 * @brief Acquérir un RawTouch avec le pilote reconnu.
 *
 * Remet d'abord la structure de sortie à zéro. Le contrôleur détermine le pilote, et les
 * états asynchrones sont propagés à board.c.
 * @param[in] device Type et identifiant du contrôleur tactile, consulté sans modification.
 *   Pointeur valide.
 * @param[out] sample Structure RawTouch valide recevant l’observation physique.
 * @return TOUCH_READ_OK, WAIT, EMPTY ou ERROR.
 */
TouchRead touch_read(const TouchDevice *device, RawTouch *sample);
/**
 * @brief Classer le type de tactile.
 *
 * La reconnaissance TSC2007 ou STMPE610 indique qu'un profil affine est requis avant la
 * conversion en pixels.
 * @param[in] controller Famille de contrôleur tactile.
 * @return true pour les deux contrôleurs résistifs pris en charge.
 */
bool touch_resistive(TouchController controller);
/**
 * @brief Fournir un nom de contrôleur à l'interface.
 *
 * Le texte appartient aux constantes du programme et suit la valeur reconnue par
 * touch_probe().
 * @param[in] controller Famille de contrôleur tactile.
 * @return Chaîne constante descriptive du contrôleur.
 */
const char *touch_name(TouchController controller);

#endif
