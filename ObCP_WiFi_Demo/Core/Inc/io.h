/**
 ******************************************************************************
 * @file    io.h
 * @brief   Entrées / sorties de la démo sur l'ObCP : LED RVB (TIM3), sortie
 *          PWM du bornier (TIM1), accéléromètre LIS3DH ou LIS3DSH (SPI3), boutons User1
 *          (ObCP) et SW1 (X-NUCLEO), capteur de température interne et VDDA.
 ******************************************************************************
 */
#ifndef IO_H
#define IO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Mesures lues par io_read(). Valeurs entières (« virgule fixe ») :
 *  temp_x10 = 245 signifie 24,5 °C ; pas de calcul en virgule flottante. */
typedef struct
{
  int16_t  temp_x10;     /* température de la puce en 1/10 °C        */
  uint16_t vdda_mv;      /* tension d'alimentation analogique         */
  bool     acc_ok;       /* accéléromètre présent                     */
  int16_t  acc_mg[3];    /* accélérations x, y, z en mg (1 g = 1000)  */
  int16_t  pitch_x10;    /* tangage en 1/10 de degré                  */
  int16_t  roll_x10;     /* roulis en 1/10 de degré                   */
  bool     user;         /* bouton User1 de l'ObCP appuyé             */
  bool     sw1;          /* bouton SW1 de la X-NUCLEO appuyé          */
} io_sensors_t;

/* Masques des boutons (résultat de io_buttons_poll(), argument de io_button_count()) */
#define IO_BTN_USER   0x01U      /* bouton User1 de l'ObCP      */
#define IO_BTN_SW1    0x02U      /* bouton SW1 de la X-NUCLEO   */

/** @brief  Initialise la LED (éteinte, blanche), la sortie PWM (0 %) et l'accéléromètre. */
void     io_init(void);

/** @brief  Lit tous les capteurs : température, VDDA, accéléromètre, boutons. */
void     io_read(io_sensors_t *s);

/* ---- LED RVB : couleur (0..255 par composante) et luminosité (0..100 %) ---- */
/** @brief  Luminosité de la LED RVB, 0 à 100 % (0 : éteinte). */
void     io_led_set(uint8_t pct);
/** @brief  Luminosité actuelle de la LED RVB (%). */
uint8_t  io_led_get(void);
/** @brief  Éteint la LED, ou la rallume au dernier niveau non nul (bouton User1). */
void     io_led_toggle(void);
/** @brief  Couleur de la LED RVB (rouge, vert, bleu de 0 à 255). */
void     io_led_color_set(const uint8_t rgb[3]);
/** @brief  Couleur actuelle de la LED RVB. */
void     io_led_color_get(uint8_t rgb[3]);

/* ---- Sortie PWM du bornier (charge entre +5 V et la borne PWM) ------------- */
/** @brief  Rapport cyclique de la sortie PWM, 0 à 100 % (1 kHz). */
void     io_pwm_set(uint8_t pct);
/** @brief  Rapport cyclique actuel de la sortie PWM (%). */
uint8_t  io_pwm_get(void);

/* ---- Accéléromètre ---------------------------------------------------------- */
/** @brief  Valeur de WHO_AM_I lue au démarrage (0x33 : LIS3DH, 0x3F : LIS3DSH). */
uint8_t     io_acc_whoami(void);
/** @brief  Nom du capteur reconnu (« LIS3DH », « LIS3DSH »), ou NULL si absent. */
const char *io_acc_name(void);

/* ---- Boutons -------------------------------------------------------------- */
/**
 * @brief  Anti-rebond des boutons, à appeler souvent (boucle principale).
 * @retval masque IO_BTN_xxx des boutons nouvellement appuyés depuis l'appel
 *         précédent (0 si aucun).
 */
uint8_t  io_buttons_poll(void);
/** @brief  Nombre d'appuis comptés depuis le démarrage sur un bouton (IO_BTN_xxx). */
uint32_t io_button_count(uint8_t btn);

#ifdef __cplusplus
}
#endif

#endif /* IO_H */
