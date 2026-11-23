/**
 * @file obcp_logo.h
 * @brief Ressource graphique SUPMICROTECH compressée par lignes.
 * @ingroup obcp_graphics
 *
 * Le fichier est produit par Tools/import_logo.py depuis le JPEG CMJN fourni. La palette
 * RGB565 et les couples longueur-indice sont en flash. Une table d’offsets permet de lire
 * directement les seules lignes de la bande active.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
#ifndef OBCP_LOGO_H
#define OBCP_LOGO_H

#include "obcp_gfx.h"

/* Logo fourni par l'utilisateur, compressé par lignes dans la flash. */
/**
 * @brief Décompresser les lignes visibles du logo dans une bande.
 *
 * Pour chaque ligne utile, les couples longueur-indice décrivent des segments de couleur
 * constante. Le rendu ne crée aucun tampon du logo et garde le pas de la bande Gfx.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 */
void logo_draw(Gfx *g, int x, int y);

#endif
