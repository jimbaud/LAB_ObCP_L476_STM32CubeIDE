/**
 ******************************************************************************
 * @file    fault.h
 * @brief   Signalement des erreurs graves par la LED rouge, pour une carte
 *          sans débogueur.
 *
 *  Quand le programme ne peut plus continuer, il s'arrête dans fault_signal()
 *  qui coupe les sorties (moteur compris) et fait clignoter la LED rouge en
 *  boucle : N éclats brefs, une pause, N éclats... Le nombre d'éclats donne
 *  la cause :
 *
 *    1 éclat   Error_Handler() : une initialisation de CubeMX a échoué
 *              (horloge, minuterie, SPI, USB...) ;
 *    2 éclats  HardFault : faute grave qu'aucun des gestionnaires suivants
 *              n'a pu traiter (par exemple une faute pendant une faute) ;
 *    3 éclats  MemManage : exécution de code dans une zone interdite
 *              (registres des périphériques, par exemple) ;
 *    4 éclats  BusFault : lecture ou écriture à une adresse où il n'y a
 *              rien (pointeur jamais initialisé, tableau lu très loin...) ;
 *    5 éclats  UsageFault : instruction invalide, appel d'un pointeur de
 *              fonction NULL, division entière par zéro ;
 *    6 éclats  NMI : interruption non masquable inattendue.
 *  Les cas 3, 4 et 5 n'existent que grâce à fault_init() : sans elle, le
 *  Cortex-M4 les range tous dans HardFault, et la division par zéro donne 0
 *  sans rien dire.
 *
 *  Seul un reset (bouton RST, SW3) fait repartir le programme. Avec une
 *  sonde ST-LINK branchée sur J17 : mettre le programme en pause
 *  (« Suspend ») montre qu'il tourne dans fault_signal(), et la fenêtre
 *  « Fault Analyzer » de STM32CubeIDE détaille la faute.
 *  Pour aller plus loin : PM0214 (manuel de programmation du Cortex-M4),
 *  https://www.st.com/resource/en/programming_manual/pm0214-stm32-cortexm4-mcus-and-mpus-programming-manual-stmicroelectronics.pdf
 ******************************************************************************
 */
#ifndef FAULT_H
#define FAULT_H

/** Cause de l'arrêt : c'est aussi le nombre d'éclats de la LED rouge. */
typedef enum
{
  FAULT_ERROR_HANDLER = 1,
  FAULT_HARDFAULT     = 2,
  FAULT_MEMMANAGE     = 3,
  FAULT_BUSFAULT      = 4,
  FAULT_USAGEFAULT    = 5,
  FAULT_NMI           = 6
} fault_t;

/**
 * @brief  Active les gestionnaires de faute détaillés (MemManage, BusFault,
 *         UsageFault) et le piège de la division entière par zéro. À
 *         appeler une fois au démarrage (début de app_init()).
 */
void fault_init(void);

/**
 * @brief  Arrête le programme et signale la cause par la LED rouge. Ne rend
 *         jamais la main. Appelée par Error_Handler() (main.c) et par les
 *         gestionnaires d'exception (stm32l4xx_it.c), dans leurs sections
 *         USER CODE.
 * @param  cause  cause de l'arrêt (nombre d'éclats)
 */
void fault_signal(fault_t cause) __attribute__((noreturn));

#endif /* FAULT_H */
