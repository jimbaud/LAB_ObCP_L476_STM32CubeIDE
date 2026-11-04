/**
 ******************************************************************************
 * @file    demo.h
 * @brief   Application de démonstration du chargement par Bluetooth (OTA).
 ******************************************************************************
 *
 *  Ce que fait la démonstration :
 *   - la LED RVB clignote dans une couleur qui dépend de DEMO_VERSION
 *     (voir demo.c) : changez ce nombre, recompilez, transférez par
 *     Bluetooth, et la nouvelle couleur prouve que le transfert a marché ;
 *   - un appui court sur USER change la vitesse du clignotement ;
 *   - un appui long (2 s) sur USER redémarre la carte dans le chargeur BLE
 *     (la LED devient blanche pendant l'appui, puis bleue clignotante).
 *
 *  Comme pour le chargeur, main.c (généré par CubeMX) n'appelle que deux
 *  fonctions, placées dans les sections USER CODE :
 *      USER CODE BEGIN 2  ->  demo_init();
 *      USER CODE BEGIN 3  ->  demo_process();
 ******************************************************************************
 */

#ifndef DEMO_H
#define DEMO_H

/** @brief Prépare la démonstration (LED éteintes, chronomètres à zéro). */
void demo_init(void);

/** @brief Un tour de boucle : clignotement et bouton. Ne bloque jamais. */
void demo_process(void);

#endif /* DEMO_H */
