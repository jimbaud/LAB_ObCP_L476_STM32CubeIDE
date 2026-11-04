/**
 ******************************************************************************
 * @file    web_page.h
 * @brief   Page HTML du serveur web de la démo 3 (HTML, CSS et JavaScript
 *          dans une seule chaîne de caractères, stockée en flash). Le
 *          JavaScript de la page interroge ensuite /api/status... (demo_web.c).
 ******************************************************************************
 */
#ifndef WEB_PAGE_H
#define WEB_PAGE_H

#include <stddef.h>

extern const char   WEB_PAGE_INDEX[];      /* texte de la page (environ 10 Ko) */
extern const size_t WEB_PAGE_INDEX_LEN;    /* sa longueur, sans le '\0' final */

#endif /* WEB_PAGE_H */
