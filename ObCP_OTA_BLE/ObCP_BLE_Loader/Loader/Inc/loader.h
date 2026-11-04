/**
 ******************************************************************************
 * @file    loader.h
 * @brief   Chef d'orchestre du chargeur : initialisation et boucle principale.
 ******************************************************************************
 *
 *  Seules ces deux fonctions sont appelées depuis main.c (fichier généré
 *  par CubeMX), dans les sections « USER CODE » que CubeMX conserve :
 *
 *      USER CODE BEGIN 2   ->  loader_init();
 *      USER CODE BEGIN 3   ->  loader_process();   (dans while (1))
 *
 *  Ainsi, régénérer le projet avec CubeMX ne touche pas au code du chargeur,
 *  qui vit entièrement dans le dossier Loader/.
 ******************************************************************************
 */

#ifndef LOADER_H
#define LOADER_H

/** @brief Démarre le Bluetooth et prépare l'affichage. */
void loader_init(void);

/**
 * @brief  Un tour de boucle : Bluetooth, LED, bouton, délai d'inactivité,
 *         redémarrage demandé par le PC. Ne bloque jamais longtemps.
 */
void loader_process(void);

#endif /* LOADER_H */
