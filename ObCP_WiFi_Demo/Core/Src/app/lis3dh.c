/**
 ******************************************************************************
 * @file    lis3dh.c
 * @brief   Accéléromètre LIS3DH ou LIS3DSH (voir lis3dh.h).
 *
 *  Dialogue SPI avec le capteur : le premier octet émis est l'adresse d'un
 *  registre (bit 7 à 1 pour une lecture), les suivants sont les données.
 *  Le capteur mesure l'accélération sur 3 axes ; immobile, il ne mesure
 *  que la pesanteur (1 g vers le bas), ce qui donne son inclinaison.
 ******************************************************************************
 */
#include <math.h>
#include "lis3dh.h"

/* Écriture d'un registre : adresse (bit 7 à 0) puis valeur */
static int write_reg(lis3dh_t *d, uint8_t reg, uint8_t value)
{
  uint8_t buf[2];

  buf[0] = reg;                           /* bit 7 à 0 : écriture */
  buf[1] = value;
  return d->xfer(buf, sizeof(buf), d->ctx);
}

/* Lecture de n registres consécutifs. Le LIS3DH n'incrémente l'adresse que si
 * le bit 6 (MS) est à 1 ; sur le LIS3DSH, ce bit fait partie de l'adresse et
 * l'incrément est réglé par CTRL_REG6 : il ne faut donc pas le mettre.      */
static int read_regs(lis3dh_t *d, uint8_t reg, uint8_t *out, size_t n)
{
  uint8_t buf[8];
  size_t  i;

  if (n > (sizeof(buf) - 1U))
  {
    return -1;
  }
  buf[0] = (uint8_t)(reg | LIS3DH_SPI_READ);
  if ((n > 1U) && (d->model == LIS3DX_LIS3DH))
  {
    buf[0] |= LIS3DH_SPI_INC;
  }
  for (i = 1U; i <= n; i++)
  {
    buf[i] = 0U;
  }
  if (d->xfer(buf, n + 1U, d->ctx) != 0)
  {
    return -1;
  }
  for (i = 0U; i < n; i++)
  {
    out[i] = buf[i + 1U];
  }
  return 0;
}

/* Écrit un registre puis le relit : vérifie que l'écriture a été prise en compte */
static int write_check(lis3dh_t *d, uint8_t reg, uint8_t value)
{
  uint8_t v = 0U;

  if ((write_reg(d, reg, value) != 0) || (read_regs(d, reg, &v, 1U) != 0))
  {
    return -1;
  }
  return (v == value) ? 0 : -1;
}

/* Voir lis3dh.h : lecture de WHO_AM_I (même adresse 0x0F sur les deux
 * modèles), puis réglage des registres de contrôle propres au modèle  */
int lis3dh_init(lis3dh_t *d, lis3dh_xfer_t xfer, void *ctx)
{
  uint8_t v = 0U;
  int     err;

  d->xfer  = xfer;
  d->ctx   = ctx;
  d->ok    = false;
  d->who   = 0U;
  d->model = LIS3DX_NONE;
  if (read_regs(d, LIS3DH_WHO_AM_I, &v, 1U) != 0)     /* lecture simple : même trame pour les deux */
  {
    return -1;
  }
  d->who = v;
  if (v == LIS3DH_WHO_AM_I_VALUE)
  {
    d->model = LIS3DX_LIS3DH;
    err = write_check(d, LIS3DH_CTRL_REG4, LIS3DH_CTRL4_BDU_HR_2G);
    if (err == 0)
    {
      err = write_check(d, LIS3DH_CTRL_REG1, LIS3DH_CTRL1_50HZ_XYZ);
    }
  }
  else if (v == LIS3DSH_WHO_AM_I_VALUE)
  {
    /* CTRL_REG3 (interruptions) reste à sa valeur de démarrage : INT1 et INT2 inactives */
    d->model = LIS3DX_LIS3DSH;
    err = write_check(d, LIS3DSH_CTRL_REG6, LIS3DSH_CTRL6_ADD_INC);
    if (err == 0)
    {
      err = write_check(d, LIS3DSH_CTRL_REG5, LIS3DSH_CTRL5_2G);
    }
    if (err == 0)
    {
      err = write_check(d, LIS3DSH_CTRL_REG4, LIS3DSH_CTRL4_50HZ_BDU_XYZ);
    }
  }
  else
  {
    err = -1;
  }
  if (err != 0)
  {
    d->model = LIS3DX_NONE;
    return -1;
  }
  d->ok = true;
  return 0;
}

/* Voir lis3dh.h */
const char *lis3dh_name(const lis3dh_t *d)
{
  if (!d->ok)
  {
    return NULL;
  }
  return (d->model == LIS3DX_LIS3DSH) ? "LIS3DSH" : "LIS3DH";
}

/* Voir lis3dh.h : 12 bits utiles justifiés à gauche -> division par 16,
 * 1 mg par pas à ±2 g en haute résolution                             */
int16_t lis3dh_raw_to_mg(uint8_t lo, uint8_t hi)
{
  int16_t raw = (int16_t)(uint16_t)(((uint16_t)hi << 8) | lo);
  /* 12 bits utiles justifiés à gauche, 1 mg par pas à ±2 g */
  return (int16_t)(raw / 16);
}

/* Voir lis3dh.h : 0,06 mg par pas = 3/50, arrondi au plus proche */
int16_t lis3dsh_raw_to_mg(uint8_t lo, uint8_t hi)
{
  int32_t raw = (int16_t)(uint16_t)(((uint16_t)hi << 8) | lo);
  /* 0,06 mg par pas à ±2 g, arrondi au mg le plus proche */
  int32_t num = raw * 3;
  return (int16_t)((num >= 0) ? ((num + 25) / 50) : ((num - 25) / 50));
}

/* Voir lis3dh.h : 6 registres consécutifs (X, Y, Z, octet bas puis haut) */
int lis3dh_read_mg(lis3dh_t *d, int16_t mg[3])
{
  uint8_t b[6];
  int     i;

  if (!d->ok || (read_regs(d, LIS3DH_OUT_X_L, b, sizeof(b)) != 0))
  {
    return -1;
  }
  for (i = 0; i < 3; i++)
  {
    mg[i] = (d->model == LIS3DX_LIS3DSH) ? lis3dsh_raw_to_mg(b[2 * i], b[(2 * i) + 1])
                                         : lis3dh_raw_to_mg(b[2 * i], b[(2 * i) + 1]);
  }
  return 0;
}

/* Voir lis3dh.h : tangage = atan2(-x, √(y² + z²)), roulis = atan2(y, z) */
void lis3dh_tilt(const int16_t mg[3], int16_t *pitch_x10, int16_t *roll_x10)
{
  float x = (float)mg[0];
  float y = (float)mg[1];
  float z = (float)mg[2];
  float pitch = atan2f(-x, sqrtf((y * y) + (z * z))) * 57.29578f;
  float roll  = atan2f(y, z) * 57.29578f;

  *pitch_x10 = (int16_t)lroundf(pitch * 10.0f);
  *roll_x10  = (int16_t)lroundf(roll * 10.0f);
}
