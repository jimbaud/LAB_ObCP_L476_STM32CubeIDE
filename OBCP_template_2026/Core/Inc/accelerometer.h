/**
 ******************************************************************************
 * @file    accelerometer.h
 * @brief   Accéléromètre 3 axes de l'ObCP (U4), relié au bus SPI3.
 *
 *  Le composant monté est un LIS3DH de ST (nomenclature d'achat), placé sur
 *  l'empreinte dessinée pour un LIS3DSH (schéma). Les deux circuits ont le
 *  même boîtier mais pas les mêmes registres : le pilote lit d'abord le
 *  registre d'identité WHO_AM_I (0x33 pour le LIS3DH, 0x3F pour le LIS3DSH)
 *  puis configure le circuit trouvé. Réglages : ±2 g, environ 10 mesures
 *  par seconde, mise à jour « par bloc » des registres de sortie (BDU).
 *
 *  Câblage (schéma V2.3) : SCK = PC10, MISO = PC11 (SDO du capteur),
 *  MOSI = PC12 (SDI du capteur), CS = PC5 (actif à 0).
 *
 *  Unités : les accélérations sont rendues en g (1 g = 9,81 m/s²). Posée à
 *  plat, la carte mesure environ X = 0, Y = 0, Z = 1 (la pesanteur).
 *
 *  Documentation :
 *    LIS3DH  : https://www.st.com/resource/en/datasheet/lis3dh.pdf
 *    LIS3DSH : https://www.st.com/resource/en/datasheet/lis3dsh.pdf
 ******************************************************************************
 */
#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H

#include <stdbool.h>
#include <stdint.h>
#include "main.h"           /* SPI_HandleTypeDef (HAL) et étiquettes ACC_CS */

/** Circuit détecté au démarrage. */
typedef enum
{
  ACCEL_NONE = 0,     /**< aucun capteur ne répond (absent, mal soudé...) */
  ACCEL_LIS3DH,       /**< LIS3DH, WHO_AM_I = 0x33 */
  ACCEL_LIS3DSH       /**< LIS3DSH, WHO_AM_I = 0x3F */
} accel_chip_t;

/** Axe, pour accel_set_calibration(). */
typedef enum
{
  ACCEL_AXIS_X = 0,
  ACCEL_AXIS_Y,
  ACCEL_AXIS_Z
} accel_axis_t;

/** Accélération sur les trois axes, en g. */
typedef struct
{
  float x;
  float y;
  float z;
} accel_g_t;

/**
 * @brief  Détecte et configure le capteur.
 * @param  hspi  bus SPI du capteur (&hspi3, initialisé par MX_SPI3_Init)
 * @retval circuit trouvé, ACCEL_NONE si aucun ne répond correctement
 */
accel_chip_t accel_init(SPI_HandleTypeDef *hspi);

/**
 * @brief  Circuit détecté par accel_init().
 */
accel_chip_t accel_chip(void);

/**
 * @brief  Nom du circuit détecté : "LIS3DH", "LIS3DSH" ou "none".
 */
const char *accel_chip_name(void);

/**
 * @brief  Lit les valeurs brutes des trois axes (entiers signés 16 bits, tels
 *         que le capteur les fournit), lues en une seule fois pour que les
 *         trois axes viennent de la même mesure.
 * @param  raw  tableau de 3 cases : raw[0] = X, raw[1] = Y, raw[2] = Z
 * @retval true si la lecture a réussi
 */
bool accel_read_raw(int16_t raw[3]);

/**
 * @brief  Lit l'accélération sur les trois axes, en g, étalonnage compris.
 * @param  g  résultat
 * @retval true si la lecture a réussi, false si le capteur est absent ou
 *         ne répond pas
 */
bool accel_read(accel_g_t *g);

/**
 * @brief  Lit la température du capteur, en °C (entier).
 *
 *  Le capteur mesure les variations de sa température, pas sa valeur
 *  absolue : la formule « valeur lue + 25 °C », reprise de la version 2024,
 *  donne un ordre de grandeur (à quelques degrés près). Utile pour voir
 *  une variation (doigt posé sur le capteur), pas comme thermomètre.
 * @param  celsius  résultat
 * @retval true si la lecture a réussi
 */
bool accel_read_temperature(int *celsius);

/**
 * @brief  Étalonnage simple d'un axe, à partir des valeurs mesurées (en mg)
 *         quand cet axe pointe vers le haut (+1 g attendu, valeur max_mg :
 *         carte à plat pour Z) puis vers le bas (-1 g attendu, valeur
 *         min_mg : carte retournée pour Z). Ensuite, chaque mesure est
 *         corrigée : (mesure - décalage) x échelle, avec
 *         décalage = (max_mg + min_mg) / 2 et échelle = 2000 / (max_mg - min_mg) :
 *         max_mg devient +1000 mg et min_mg devient -1000 mg.
 *
 *  Exemple (valeurs relevées avec la commande « a ») :
 *  @code
 *    accel_set_calibration(ACCEL_AXIS_X, -1000.0f,  980.0f);
 *    accel_set_calibration(ACCEL_AXIS_Y, -1020.0f, 1040.0f);
 *    accel_set_calibration(ACCEL_AXIS_Z,  -920.0f, 1040.0f);
 *  @endcode
 * @param  axis    axe à étalonner
 * @param  min_mg  mesure brute à -1 g, en mg
 * @param  max_mg  mesure brute à +1 g, en mg (doit être > min_mg)
 */
void accel_set_calibration(accel_axis_t axis, float min_mg, float max_mg);

#endif /* ACCELEROMETER_H */
