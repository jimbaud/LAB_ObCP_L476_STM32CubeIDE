/**
 ******************************************************************************
 * @file    lis3dh.h
 * @brief   Accéléromètre 3 axes de l'ObCP (liaison SPI, mode 3) : LIS3DH ou
 *          LIS3DSH, reconnu automatiquement par son registre WHO_AM_I (la
 *          nomenclature de fabrication cite le LIS3DSH, le programme modèle
 *          de l'ObCP pilote un LIS3DH : les deux sont pris en charge).
 *          Indépendant du matériel : l'échange SPI est fourni par l'appelant
 *          (io.c sur la carte, simulation sur PC).
 *
 *          Réglage commun : 50 mesures/s, pleine échelle ±2 g, lecture des
 *          3 axes en une rafale (BDU = 1 : octets bas et haut d'une même
 *          mesure).
 *          LIS3DH  : mode haute résolution (12 bits), 1 mg par pas.
 *          LIS3DSH : 16 bits, 0,06 mg par pas.
 ******************************************************************************
 */
#ifndef LIS3DH_H
#define LIS3DH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIS3DH_WHO_AM_I        0x0FU       /* même adresse sur les deux capteurs */
#define LIS3DH_OUT_X_L         0x28U       /* idem : X, Y, Z sur 6 octets */
#define LIS3DH_SPI_READ        0x80U

/* LIS3DH */
#define LIS3DH_WHO_AM_I_VALUE  0x33U
#define LIS3DH_CTRL_REG1       0x20U
#define LIS3DH_CTRL_REG4       0x23U
#define LIS3DH_CTRL1_50HZ_XYZ  0x47U       /* ODR = 50 Hz, X, Y et Z actifs */
#define LIS3DH_CTRL4_BDU_HR_2G 0x88U       /* BDU, haute résolution, ±2 g */
#define LIS3DH_SPI_INC         0x40U       /* bit MS : adresse auto-incrémentée */

/* LIS3DSH (adresse sur 7 bits ; auto-incrément par ADD_INC de CTRL_REG6) */
#define LIS3DSH_WHO_AM_I_VALUE 0x3FU
#define LIS3DSH_CTRL_REG4      0x20U
#define LIS3DSH_CTRL_REG5      0x24U
#define LIS3DSH_CTRL_REG6      0x25U
#define LIS3DSH_CTRL4_50HZ_BDU_XYZ 0x5FU   /* ODR = 50 Hz, BDU, X, Y et Z actifs */
#define LIS3DSH_CTRL5_2G       0x00U       /* ±2 g, SPI 4 fils */
#define LIS3DSH_CTRL6_ADD_INC  0x10U       /* auto-incrément des adresses */

/** Modèle d'accéléromètre reconnu. */
typedef enum
{
  LIS3DX_NONE = 0,
  LIS3DX_LIS3DH,
  LIS3DX_LIS3DSH
} lis3dx_model_t;

/**
 * @brief  Échange SPI en duplex intégral fourni par l'appelant (CS actif
 *         pendant tout l'échange) : buf[0] = adresse du registre, la réponse
 *         est écrite à la place des octets émis.
 * @retval 0 si l'échange a réussi.
 */
typedef int (*lis3dh_xfer_t)(uint8_t *buf, size_t n, void *ctx);

/** État du capteur (rempli par lis3dh_init()). */
typedef struct
{
  lis3dh_xfer_t  xfer;
  void          *ctx;
  bool           ok;         /* capteur reconnu et configuré */
  uint8_t        who;        /* dernière valeur lue de WHO_AM_I */
  lis3dx_model_t model;
} lis3dh_t;

/**
 * @brief  Reconnaît le capteur (WHO_AM_I) et le configure (50 Hz, ±2 g).
 * @param  d     état du capteur, rempli.
 * @param  xfer  fonction d'échange SPI (voir lis3dh_xfer_t).
 * @param  ctx   pointeur libre transmis à xfer.
 * @retval 0 si le capteur répond (WHO_AM_I = 0x33 ou 0x3F) et accepte la configuration.
 */
int         lis3dh_init(lis3dh_t *d, lis3dh_xfer_t xfer, void *ctx);

/** @brief  Nom du capteur reconnu (« LIS3DH », « LIS3DSH ») ou NULL. */
const char *lis3dh_name(const lis3dh_t *d);

/**
 * @brief  Lit les accélérations x, y, z en mg (1000 mg = 1 g = pesanteur).
 * @retval 0 si la lecture a réussi.
 */
int         lis3dh_read_mg(lis3dh_t *d, int16_t mg[3]);

/** @brief  Conversion d'une mesure LIS3DH (16 bits justifiés à gauche, 12 utiles) en mg. */
int16_t     lis3dh_raw_to_mg(uint8_t lo, uint8_t hi);

/** @brief  Conversion d'une mesure LIS3DSH (0,06 mg par pas) en mg. */
int16_t     lis3dsh_raw_to_mg(uint8_t lo, uint8_t hi);

/**
 * @brief  Inclinaisons déduites de la pesanteur, carte immobile :
 *         tangage = rotation autour de Y, roulis = rotation autour de X.
 * @param  mg         accélérations x, y, z en mg.
 * @param  pitch_x10  reçoit le tangage en 1/10 de degré.
 * @param  roll_x10   reçoit le roulis en 1/10 de degré.
 */
void        lis3dh_tilt(const int16_t mg[3], int16_t *pitch_x10, int16_t *roll_x10);

#ifdef __cplusplus
}
#endif

#endif /* LIS3DH_H */
