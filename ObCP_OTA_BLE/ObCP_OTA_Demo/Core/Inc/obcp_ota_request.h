/**
 ******************************************************************************
 * @file    obcp_ota_request.h
 * @brief   Deux petites fonctions pour une application chargée par Bluetooth
 *          (OTA) sur l'ObCP. Fichier à copier tel quel dans Core/Inc de
 *          n'importe quel projet STM32CubeIDE.
 ******************************************************************************
 *
 *  RAPPEL : où vit une application « OTA » ?
 *  -----------------------------------------
 *  Le chargeur BLE occupe le début de la flash (banque 1, 0x08000000).
 *  L'application doit donc être placée en banque 2, à 0x08080000. Pour cela,
 *  une seule ligne change dans le script d'édition de liens
 *  STM32L476RGTX_FLASH.ld du projet :
 *
 *      FLASH (rx) : ORIGIN = 0x8080000, LENGTH = 512K
 *
 *  Ce fichier n'est pas obligatoire : une application sans lui fonctionne,
 *  et l'on revient au chargeur en maintenant USER pendant un appui sur RESET.
 *  Il apporte deux fonctions pratiques :
 *
 *   obcp_ota_init_vectors()  à appeler au tout début de main(), dans
 *                            USER CODE BEGIN 1. Indique au processeur où se
 *                            trouve la table des interruptions de
 *                            l'application. Le chargeur le fait déjà avant de
 *                            lancer l'application ; cette ligne rend
 *                            l'application autonome (débogage avec une sonde).
 *
 *   obcp_ota_enter_loader()  redémarre la carte dans le chargeur BLE, prête à
 *                            recevoir un nouveau programme. À appeler par
 *                            exemple sur un appui long du bouton USER, ou à
 *                            la réception d'une commande « #ota ».
 *
 *  Tout est écrit en fonctions « static inline » : il n'y a pas de fichier .c
 *  à ajouter au projet, l'inclusion de ce fichier suffit.
 ******************************************************************************
 */

#ifndef OBCP_OTA_REQUEST_H
#define OBCP_OTA_REQUEST_H

#include "stm32l4xx.h"   /* registres du STM32L476, SCB, NVIC_SystemReset() */

/* « Boîte aux lettres » partagée avec le chargeur : 2 mots de 32 bits à la
 * fin de la SRAM2 (cette mémoire garde son contenu pendant un reset
 * logiciel). Valeurs identiques à loader_config.h du chargeur.             */
#define OBCP_OTA_REQUEST_ADDRESS    0x10007FF0UL
#define OBCP_OTA_REQUEST_MAGIC      0x4F544121UL      /* "OTA!" en ASCII */

/* Table des vecteurs de l'application, définie dans le fichier de
 * démarrage startup_stm32l476rgtx.s (étiquette g_pfnVectors). L'éditeur de
 * liens la place au début de la zone FLASH, donc à 0x08080000.            */
extern const uint32_t g_pfnVectors[];

/**
 * @brief  Fait pointer le registre VTOR sur la table des vecteurs de
 *         l'application.
 * @note   VTOR (« Vector Table Offset Register ») indique au Cortex-M4 où
 *         chercher l'adresse de la fonction à exécuter à chaque interruption.
 *         Après un reset il vaut 0, c'est-à-dire la table du CHARGEUR !
 */
static inline void obcp_ota_init_vectors(void)
{
    SCB->VTOR = (uint32_t)g_pfnVectors;
    __DSB();   /* attend que l'écriture soit effective */
}

/**
 * @brief  Redémarre la carte dans le chargeur BLE. Ne revient jamais.
 * @note   Principe : on dépose un mot « magique » (et son complément) dans
 *         la boîte aux lettres, puis on provoque un reset logiciel. Au
 *         redémarrage, le chargeur trouve le mot, l'efface et reste actif
 *         au lieu de relancer l'application.
 */
static inline void obcp_ota_enter_loader(void)
{
    volatile uint32_t *mailbox = (volatile uint32_t *)OBCP_OTA_REQUEST_ADDRESS;

    __disable_irq();                         /* plus d'interruption à partir d'ici */
    mailbox[0] = OBCP_OTA_REQUEST_MAGIC;
    mailbox[1] = (uint32_t)~OBCP_OTA_REQUEST_MAGIC;
    __DSB();                                 /* écritures terminées avant le reset */
    NVIC_SystemReset();                      /* reset logiciel (CMSIS)             */
}

#endif /* OBCP_OTA_REQUEST_H */
