/**
 ******************************************************************************
 * @file    qr_term.h
 * @brief   Affichage d'un QR code dans le terminal (demi-blocs UTF-8 et
 *          couleurs ANSI), via la bibliothèque QR-Code-generator de
 *          Project Nayuki (licence MIT, Core/Src/qrcodegen).
 ******************************************************************************
 */
#ifndef QR_TERM_H
#define QR_TERM_H

#include <stdbool.h>

/**
 * @brief  Affiche le QR code d'un texte dans le terminal (deux lignes du code
 *         par ligne de texte, grâce aux caractères « demi-bloc »).
 * @param  text  texte à coder, par exemple « WIFI:T:WPA;S:ObCP-WiFi-A1B2C3;P:obcpdemo;; »
 *               (connexion automatique d'un téléphone au point d'accès).
 * @retval false si le texte est trop long ou si les couleurs sont désactivées.
 */
bool qr_print(const char *text);

#endif /* QR_TERM_H */
