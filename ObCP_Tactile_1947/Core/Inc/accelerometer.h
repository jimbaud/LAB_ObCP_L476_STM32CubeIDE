/**
 * @file accelerometer.h
 * @brief Mesures XYZ cohérentes sur le SPI3 de la carte.
 * @ingroup obcp_drivers
 *
 * Le capteur est reconnu par WHO_AM_I, puis configuré à 100 Hz et ±2 g. Le pilote conserve
 * un pointeur vers la HAL et convertit les six octets XYZ en accélérations exprimées en g.
 * Appels synchrones depuis la boucle principale.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H

#include <stdbool.h>
#include <stdint.h>
#include "main.h"           /* SPI_HandleTypeDef (HAL) et étiquettes ACC_CS */

/**
 * @brief Identités de capteur reconnues par WHO_AM_I.
 */
typedef enum {
    ACCEL_NONE = 0, /**< Aucun capteur compatible configuré. */
    ACCEL_LIS3DH, /**< WHO_AM_I vaut 0x33, sensibilité haute résolution du LIS3DH. */
    ACCEL_LIS3DSH, /**< WHO_AM_I vaut 0x3F, sensibilité du LIS3DSH. */
} accel_chip_t;

/**
 * @brief Indices des trois axes bruts pour leur étalonnage.
 */
typedef enum {
    ACCEL_AXIS_X = 0, /**< Axe brut X, indice zéro. */
    ACCEL_AXIS_Y, /**< Axe brut Y, indice un. */
    ACCEL_AXIS_Z, /**< Axe brut Z, indice deux. */
} accel_axis_t;

/**
 * @brief Accélération XYZ dans le repère du capteur.
 *
 * Une carte immobile mesure la pesanteur. Le module de jeu enlève ensuite son neutre aux
 * deux axes utiles.
 */
typedef struct {
    float x; /**< Accélération selon l'axe X brut, en g. */
    float y; /**< Accélération selon l'axe Y brut, en g. */
    float z; /**< Accélération selon l'axe Z brut, en g. */
} accel_g_t;

/**
 * @brief Reconnaître et configurer le composant connecté.
 *
 * Mémorise le bus, lit WHO_AM_I puis choisit les registres et la sensibilité LIS3DH ou
 * LIS3DSH. Une erreur de configuration ramène le pilote à ACCEL_NONE.
 * @param[in,out] hspi Handle HAL du bus SPI déjà initialisé. Pointeur valide.
 * @return Type reconnu et configuré, ou ACCEL_NONE en cas d'échec.
 */
accel_chip_t accel_init(SPI_HandleTypeDef *hspi);

/**
 * @brief Consulter l'identité mémorisée du capteur.
 *
 * Cette interrogation lit seulement l'état logiciel. Elle ne déclenche pas de transaction
 * SPI.
 * @return Dernier type enregistré par accel_init().
 */
accel_chip_t accel_chip(void);

/**
 * @brief Obtenir un nom lisible du capteur.
 *
 * Le texte appartient au programme en flash. L'appelant peut l'afficher et ne doit pas
 * tenter de le modifier ou de le libérer.
 * @return Chaîne constante LIS3DH, LIS3DSH ou none.
 */
const char *accel_chip_name(void);

/**
 * @brief Lire les trois axes dans une même rafale.
 *
 * BDU protège la cohérence des registres pendant leur lecture. Chaque paire little-endian
 * est recomposée en entier signé sur 16 bits.
 * @param[out] raw Tableau valide de trois entiers signés pour X, Y et Z bruts.
 * @return true si les six octets sont lus, false si le capteur est absent ou si le SPI échoue.
 */
bool accel_read_raw(int16_t raw[3]);

/**
 * @brief Convertir la mesure XYZ en g.
 *
 * Applique successivement la sensibilité en mg par unité brute, le décalage et l'échelle
 * d'étalonnage, puis la division par 1000. Cette correction de capteur précède le neutre
 * des jeux.
 * @param[out] g Structure de résultat XYZ valide, à remplir en g.
 * @return true si une mesure valide a été acquise.
 */
bool accel_read(accel_g_t *g);

/**
 * @brief Lire une indication de température du capteur.
 *
 * La formule utilise l'octet signé et le décalage de 25 °C du modèle. Cette indication
 * sert à observer une variation du capteur. L'application tactile ne l'utilise pas pour
 * piloter les jeux.
 * @param[out] celsius Pointeur valide recevant l’indication entière en degrés Celsius.
 * @return true si le registre du capteur reconnu a été lu.
 */
bool accel_read_temperature(int *celsius);

/**
 * @brief Étalonner un axe à partir de deux mesures opposées.
 *
 * Les mesures de référence correspondent à −1 g et +1 g en mg. Le milieu donne le décalage
 * et leur différence donne l'échelle. Un axe invalide ou un intervalle inversé est ignoré.
 * @param[in] axis Axe brut X, Y ou Z à étalonner.
 * @param[in] min_mg Mesure d’étalonnage quand l’axe voit −1 g, exprimée en mg.
 * @param[in] max_mg Mesure d’étalonnage quand l’axe voit +1 g, en mg, supérieure à min_mg.
 */
void accel_set_calibration(accel_axis_t axis, float min_mg, float max_mg);

#endif /* ACCELEROMETER_H */
