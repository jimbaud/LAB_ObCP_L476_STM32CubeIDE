/**
 ******************************************************************************
 * @file    qr_term.c
 * @brief   QR code dans le terminal (voir qr_term.h).
 *
 *  Deux rangées de modules par ligne de texte : ▀ (haut), ▄ (bas), █ (les
 *  deux) ou espace, en noir sur fond blanc (séquence ANSI 30;47) pour rester
 *  lisible par un téléphone quel que soit le thème du terminal.
 ******************************************************************************
 */
#include <stdint.h>
#include "qr_term.h"
#include "qrcodegen.h"
#include "console.h"

#define QR_VERSION_MAX   8          /* 49 x 49 modules : ~150 caractères */
#define QR_MARGIN        2

/* Voir qr_term.h : la bibliothèque qrcodegen calcule le code (tableau de
 * modules noirs ou blancs), puis deux rangées sont dessinées par ligne */
bool qr_print(const char *text)
{
  static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION_MAX)];
  static uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION_MAX)];
  int            size;
  int            x;
  int            y;

  if (!console_color())
  {
    return false;
  }
  if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, QR_VERSION_MAX,
                            qrcodegen_Mask_AUTO, true))
  {
    return false;
  }
  size = qrcodegen_getSize(qr);
  for (y = -QR_MARGIN; y < (size + QR_MARGIN); y += 2)
  {
    console_puts("    \033[30;47m");
    for (x = -QR_MARGIN; x < (size + QR_MARGIN); x++)
    {
      bool top = qrcodegen_getModule(qr, x, y);        /* hors limites : clair */
      bool bot = qrcodegen_getModule(qr, x, y + 1);
      console_puts(top ? (bot ? "\xE2\x96\x88" : "\xE2\x96\x80") : (bot ? "\xE2\x96\x84" : " "));
    }
    console_puts("\033[0m\n");
  }
  return true;
}
