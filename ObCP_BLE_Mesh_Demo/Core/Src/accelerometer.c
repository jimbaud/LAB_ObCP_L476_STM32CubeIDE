/**
 ******************************************************************************
 * @file    accelerometer.c
 * @brief   Pilote de l'accéléromètre LIS3DH (ou LIS3DSH) sur SPI3 (voir
 *          accelerometer.h).
 *
 *  Dialogue SPI avec le capteur : la carte (maître) met CS à 0, envoie un
 *  premier octet qui dit quoi faire, puis échange les données ; CS repasse
 *  à 1 à la fin. En SPI, chaque octet envoyé fait revenir un octet : pour
 *  lire, on envoie des octets sans importance (0) pendant que le capteur
 *  renvoie ses registres.
 *
 *  Premier octet (LIS3DH, datasheet § 6.2.1) :
 *      bit 7   RW : 1 = lecture, 0 = écriture
 *      bit 6   MS : 1 = l'adresse avance d'un cran après chaque octet
 *                   (pour lire plusieurs registres qui se suivent)
 *      bits 5..0  adresse du premier registre
 *  Le LIS3DSH n'a pas de bit MS (son adresse fait 7 bits) : l'avance
 *  automatique y est réglée une fois pour toutes (bit ADD_INC de CTRL_REG6).
 *
 *  La version 2024 utilisait les sensibilités du LIS3DSH (0,06 mg par pas)
 *  avec un LIS3DH (0,0625 mg par pas en mode haute résolution) : ses
 *  accélérations étaient 4 % trop faibles. Elle lisait aussi les trois axes
 *  en trois échanges séparés (axes de mesures différentes) et copiait la
 *  structure de gestion du SPI (deux copies pour un seul périphérique).
 ******************************************************************************
 */
#include "accelerometer.h"

#include <stddef.h>

/* -------------------------------------------------------------------------- */
/* Registres (adresses communes aux deux circuits sauf mention)              */
/* -------------------------------------------------------------------------- */
#define REG_WHO_AM_I          0x0FU   /* identité du circuit */
#define REG_OUT_X_L           0x28U   /* 6 registres : X_L, X_H, Y_L, Y_H, Z_L, Z_H */
#define REG_OUT_TEMP          0x0CU   /* LIS3DH : OUT_ADC3_L/H ; LIS3DSH : OUT_T */

#define WHO_AM_I_LIS3DH       0x33U
#define WHO_AM_I_LIS3DSH      0x3FU

/* LIS3DH */
#define LIS3DH_TEMP_CFG_REG   0x1FU   /* ADC_EN (bit 7), TEMP_EN (bit 6) */
#define LIS3DH_CTRL_REG1      0x20U   /* ODR (bits 7..4), LPen, Zen, Yen, Xen */
#define LIS3DH_CTRL_REG4      0x23U   /* BDU (7), BLE, FS1-FS0 (5..4), HR (3)... */

#define LIS3DH_CTRL1_VALUE    0x27U   /* 0010 0111 : 10 mesures/s, X, Y, Z actifs */
#define LIS3DH_CTRL4_VALUE    0x88U   /* 1000 1000 : BDU = 1, ±2 g, haute résolution */
#define LIS3DH_TEMP_CFG_VALUE 0xC0U   /* convertisseur et capteur de température actifs */

/* LIS3DSH */
#define LIS3DSH_CTRL_REG4     0x20U   /* ODR (7..4), BDU (3), Zen, Yen, Xen */
#define LIS3DSH_CTRL_REG5     0x24U   /* filtre (7..6), échelle FSCALE (5..3) */
#define LIS3DSH_CTRL_REG6     0x25U   /* ADD_INC (bit 4) : avance automatique */

#define LIS3DSH_CTRL4_VALUE   0x3FU   /* 0011 1111 : 12,5 mesures/s, BDU, X, Y, Z */
#define LIS3DSH_CTRL5_VALUE   0x00U   /* ±2 g, filtre 800 Hz */
#define LIS3DSH_CTRL6_VALUE   0x10U   /* ADD_INC = 1 */

/* Premier octet d'un échange SPI */
#define SPI_READ              0x80U   /* bit 7 : lecture */
#define SPI_LIS3DH_MULTI      0x40U   /* bit 6 du LIS3DH : adresse qui avance */

/* Sensibilités à ±2 g, en mg par unité de la valeur brute 16 bits :
 *   LIS3DH en haute résolution : 1 mg par pas sur 12 bits, valeur alignée à
 *   gauche dans 16 bits (multipliée par 16), donc 1/16 = 0,0625 mg par unité
 *   (datasheet LIS3DH, tableau 4 : « So », FS = 00, mode HR) ;
 *   LIS3DSH : 0,06 mg par unité (datasheet LIS3DSH, tableau 3).
 * Autres échelles (non utilisées ici) : LIS3DH ±4 g = 0,125, ±8 g = 0,25,
 * ±16 g = 0,75 ; LIS3DSH ±4 g = 0,12, ±8 g = 0,24, ±16 g = 0,73.          */
#define LIS3DH_MG_PER_LSB     0.0625f
#define LIS3DSH_MG_PER_LSB    0.06f

#define SPI_TIMEOUT_MS        10U     /* un échange dure quelques µs à 4 Mbit/s */
#define SPI_MAX_DATA          6U      /* plus long échange : les 3 axes */

/* -------------------------------------------------------------------------- */
/* État du pilote                                                             */
/* -------------------------------------------------------------------------- */
static SPI_HandleTypeDef *s_hspi;               /* bus SPI (pointeur, pas une copie) */
static accel_chip_t       s_chip = ACCEL_NONE;  /* circuit détecté */
static float              s_mg_per_lsb;         /* sensibilité du circuit détecté */

/* Étalonnage de chaque axe : décalage (mg) et facteur d'échelle. */
static float s_offset_mg[3] = { 0.0f, 0.0f, 0.0f };
static float s_scale[3]     = { 1.0f, 1.0f, 1.0f };

/* -------------------------------------------------------------------------- */
/* Échanges SPI                                                               */
/* -------------------------------------------------------------------------- */

/* Un échange complet : CS à 0, len octets envoyés (tx) et reçus (rx) en
 * même temps, CS à 1. Rend true si la HAL n'a signalé aucune erreur.      */
static bool spi_transfer(uint8_t *tx, uint8_t *rx, uint16_t len)
{
  HAL_StatusTypeDef status;

  HAL_GPIO_WritePin(ACC_CS_GPIO_Port, ACC_CS_Pin, GPIO_PIN_RESET);   /* sélection */
  status = HAL_SPI_TransmitReceive(s_hspi, tx, rx, len, SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(ACC_CS_GPIO_Port, ACC_CS_Pin, GPIO_PIN_SET);     /* fin */
  return status == HAL_OK;
}

/* Écrit un registre du capteur. */
static bool write_reg(uint8_t reg, uint8_t value)
{
  uint8_t tx[2] = { reg, value };      /* bit 7 = 0 : écriture */
  uint8_t rx[2];

  return spi_transfer(tx, rx, 2U);
}

/* Lit len registres consécutifs à partir de reg (len <= SPI_MAX_DATA). */
static bool read_regs(uint8_t reg, uint8_t *data, uint16_t len)
{
  uint8_t tx[SPI_MAX_DATA + 1U] = { 0 };
  uint8_t rx[SPI_MAX_DATA + 1U];

  if ((len == 0U) || (len > SPI_MAX_DATA))
  {
    return false;
  }
  tx[0] = (uint8_t)(SPI_READ | reg);
  if ((s_chip == ACCEL_LIS3DH) && (len > 1U))
  {
    tx[0] |= SPI_LIS3DH_MULTI;         /* LIS3DH : demander l'avance de l'adresse */
  }
  if (!spi_transfer(tx, rx, (uint16_t)(len + 1U)))
  {
    return false;
  }
  for (uint16_t i = 0U; i < len; i++)
  {
    data[i] = rx[i + 1U];               /* rx[0] arrive pendant l'envoi de l'adresse */
  }
  return true;
}

/* Écrit un registre puis le relit pour vérifier que le capteur l'a bien pris. */
static bool write_and_check(uint8_t reg, uint8_t value)
{
  uint8_t check = 0U;

  return write_reg(reg, value) && read_regs(reg, &check, 1U) && (check == value);
}

/* -------------------------------------------------------------------------- */
/* Fonctions publiques                                                        */
/* -------------------------------------------------------------------------- */

/* Voir accelerometer.h */
accel_chip_t accel_init(SPI_HandleTypeDef *hspi)
{
  uint8_t who = 0U;
  bool    ok;

  s_hspi = hspi;
  s_chip = ACCEL_NONE;

  /* 1) Qui est là ? Lecture d'un seul registre : même premier octet (0x8F)
   *    pour les deux circuits.                                            */
  if (!read_regs(REG_WHO_AM_I, &who, 1U))
  {
    return ACCEL_NONE;
  }

  /* 2) Configuration du circuit reconnu, avec relecture de contrôle. */
  if (who == WHO_AM_I_LIS3DH)
  {
    s_chip = ACCEL_LIS3DH;
    s_mg_per_lsb = LIS3DH_MG_PER_LSB;
    ok = write_and_check(LIS3DH_CTRL_REG1, LIS3DH_CTRL1_VALUE)
         && write_and_check(LIS3DH_CTRL_REG4, LIS3DH_CTRL4_VALUE)
         && write_and_check(LIS3DH_TEMP_CFG_REG, LIS3DH_TEMP_CFG_VALUE);
  }
  else if (who == WHO_AM_I_LIS3DSH)
  {
    s_chip = ACCEL_LIS3DSH;
    s_mg_per_lsb = LIS3DSH_MG_PER_LSB;
    ok = write_and_check(LIS3DSH_CTRL_REG6, LIS3DSH_CTRL6_VALUE)
         && write_and_check(LIS3DSH_CTRL_REG4, LIS3DSH_CTRL4_VALUE)
         && write_and_check(LIS3DSH_CTRL_REG5, LIS3DSH_CTRL5_VALUE);
  }
  else
  {
    ok = false;                         /* 0x00 ou 0xFF : rien ne répond */
  }

  if (!ok)
  {
    s_chip = ACCEL_NONE;
  }
  return s_chip;
}

/* Voir accelerometer.h */
accel_chip_t accel_chip(void)
{
  return s_chip;
}

/* Voir accelerometer.h */
const char *accel_chip_name(void)
{
  switch (s_chip)
  {
    case ACCEL_LIS3DH:  return "LIS3DH";
    case ACCEL_LIS3DSH: return "LIS3DSH";
    case ACCEL_NONE:
    default:            return "none";
  }
}

/* Voir accelerometer.h */
bool accel_read_raw(int16_t raw[3])
{
  uint8_t data[6];

  if ((s_chip == ACCEL_NONE) || !read_regs(REG_OUT_X_L, data, 6U))
  {
    return false;
  }
  /* Chaque axe : octet de poids faible (L) puis de poids fort (H), valeur
   * signée en complément à 2 : on recolle les deux octets sur 16 bits.     */
  for (int axis = 0; axis < 3; axis++)
  {
    raw[axis] = (int16_t)(((uint16_t)data[2 * axis + 1] << 8) | data[2 * axis]);
  }
  return true;
}

/* Voir accelerometer.h */
bool accel_read(accel_g_t *g)
{
  int16_t raw[3];
  float   value_g[3];

  if (!accel_read_raw(raw))
  {
    return false;
  }
  for (int axis = 0; axis < 3; axis++)
  {
    float mg = (float)raw[axis] * s_mg_per_lsb;              /* brut -> mg */
    mg = (mg - s_offset_mg[axis]) * s_scale[axis];            /* étalonnage */
    value_g[axis] = mg / 1000.0f;                             /* mg -> g */
  }
  g->x = value_g[0];
  g->y = value_g[1];
  g->z = value_g[2];
  return true;
}

/* Voir accelerometer.h */
bool accel_read_temperature(int *celsius)
{
  uint8_t data[2];

  if (s_chip == ACCEL_LIS3DH)
  {
    /* OUT_ADC3_L puis OUT_ADC3_H : l'octet de poids fort porte la variation
     * de température en °C (entier signé).                                 */
    if (!read_regs(REG_OUT_TEMP, data, 2U))
    {
      return false;
    }
    *celsius = (int)(int8_t)data[1] + 25;
    return true;
  }
  if (s_chip == ACCEL_LIS3DSH)
  {
    /* OUT_T : entier signé, 1 °C par unité, 0 vers 25 °C. */
    if (!read_regs(REG_OUT_TEMP, data, 1U))
    {
      return false;
    }
    *celsius = (int)(int8_t)data[0] + 25;
    return true;
  }
  return false;
}

/* Voir accelerometer.h */
void accel_set_calibration(accel_axis_t axis, float min_mg, float max_mg)
{
  if (((unsigned)axis > (unsigned)ACCEL_AXIS_Z) || (max_mg <= min_mg))
  {
    return;                             /* valeurs incohérentes : ignorées */
  }
  s_offset_mg[axis] = (max_mg + min_mg) / 2.0f;
  s_scale[axis]     = 2000.0f / (max_mg - min_mg);
}
