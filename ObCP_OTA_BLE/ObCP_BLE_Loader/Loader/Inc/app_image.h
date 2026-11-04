/**
 ******************************************************************************
 * @file    app_image.h
 * @brief   Reconnaître une application valide dans la zone application.
 ******************************************************************************
 *
 *  LA TABLE DES VECTEURS
 *  ---------------------
 *  Tout programme pour Cortex-M commence par sa « table des vecteurs » :
 *   - mot 0 (adresse +0) : valeur initiale du pointeur de pile (une adresse
 *                          en RAM, souvent la fin de la RAM : 0x20018000) ;
 *   - mot 1 (adresse +4) : adresse de Reset_Handler, la toute première
 *                          fonction exécutée (elle prépare la RAM puis
 *                          appelle main()). Son bit 0 vaut 1 car le Cortex-M
 *                          n'exécute que des instructions « Thumb ».
 *
 *  Si ces deux mots sont plausibles, une application est installée. Le
 *  transfert OTA écrit justement ces 8 premiers octets EN DERNIER, après
 *  avoir vérifié le CRC : un transfert interrompu laisse donc la zone
 *  « vide » (0xFFFFFFFF) et le chargeur ne lancera jamais un programme
 *  incomplet.
 ******************************************************************************
 */

#ifndef APP_IMAGE_H
#define APP_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  Teste si les deux premiers mots d'une image ressemblent à une
 *         table des vecteurs prévue pour la zone application.
 * @param  initial_sp     Mot 0 : pointeur de pile initial.
 * @param  reset_handler  Mot 1 : adresse de Reset_Handler.
 * @param  image_size     Taille de l'image en octets (Reset_Handler doit
 *                        se trouver à l'intérieur).
 * @return true si les deux valeurs sont plausibles.
 */
bool app_image_vectors_ok(uint32_t initial_sp, uint32_t reset_handler,
                          uint32_t image_size);

/**
 * @brief  Indique si une application valide est installée en banque 2.
 * @return true si la table des vecteurs à 0x08080000 est plausible.
 * @note   Cette fonction n'utilise que des lectures en mémoire : elle peut
 *         être appelée au tout début de main(), avant HAL_Init().
 */
bool app_image_is_installed(void);

#endif /* APP_IMAGE_H */
