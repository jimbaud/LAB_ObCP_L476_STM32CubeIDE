/**
 ******************************************************************************
 * @file    loader_config.h
 * @brief   Réglages du chargeur BLE de l'ObCP : carte mémoire, nom BLE,
 *          temporisations. Tous les « nombres magiques » du projet sont ici.
 ******************************************************************************
 *
 *  À QUOI SERT CE FICHIER ?
 *  ------------------------
 *  Un programme lisible n'écrit jamais 0x08080000 au milieu du code : on
 *  donne un nom à chaque constante et on les rassemble dans un seul fichier.
 *  Pour modifier un réglage (par exemple le délai de retour automatique à
 *  l'application), il suffit de changer la valeur ici puis de recompiler.
 *
 *  CARTE MÉMOIRE DE LA FLASH (STM32L476RG : 1 Mo en deux banques de 512 Ko)
 *  ------------------------------------------------------------------------
 *
 *     0x0800 0000  +------------------------------+  Banque 1
 *                  |  CHARGEUR BLE (ce projet)    |  ~ 40 Ko utilisés
 *                  |  jamais modifié par l'OTA    |
 *     0x0808 0000  +------------------------------+  Banque 2
 *                  |  APPLICATION de l'étudiant   |  512 Ko au maximum
 *                  |  (table des vecteurs d'abord)|
 *     0x0810 0000  +------------------------------+
 *
 *  Les deux zones sont dans des banques différentes : le chargeur, qui
 *  s'exécute depuis la banque 1, peut effacer et écrire la banque 2 sans
 *  s'arrêter (« lecture pendant l'écriture », read-while-write).
 *
 *  ATTENTION : APP_ADDRESS doit être identique à l'origine FLASH du script
 *  d'édition de liens STM32L476RGTX_FLASH_OTA.ld de l'application, et
 *  OTA_REQUEST_* doivent être identiques à obcp_ota_request.h.
 ******************************************************************************
 */

#ifndef LOADER_CONFIG_H
#define LOADER_CONFIG_H

/* ------------------------------------------------------------------------- */
/* Version du chargeur (affichée par l'application PC)                       */
/* ------------------------------------------------------------------------- */
#define LOADER_VERSION_MAJOR        1U
#define LOADER_VERSION_MINOR        1U

/* ------------------------------------------------------------------------- */
/* Zone réservée à l'application (banque 2 de la flash)                      */
/* ------------------------------------------------------------------------- */
#define APP_ADDRESS                 0x08080000UL     /* début de la banque 2      */
#define APP_MAX_SIZE                (512UL * 1024UL) /* taille de la banque 2     */
#define LOADER_FLASH_PAGE_SIZE      2048UL           /* plus petite zone effaçable */

/* ------------------------------------------------------------------------- */
/* Mémoire vive : plages admises pour le pointeur de pile initial de         */
/* l'application (premier mot de sa table des vecteurs).                     */
/* SRAM1 = 96 Ko à 0x20000000, SRAM2 = 32 Ko à 0x10000000 (et visible aussi  */
/* juste après la SRAM1, à 0x20018000).                                      */
/* ------------------------------------------------------------------------- */
#define RAM_MAIN_START              0x20000000UL
#define RAM_MAIN_END                0x20020000UL     /* SRAM1 + alias de SRAM2 */
#define RAM_SRAM2_START             0x10000000UL
#define RAM_SRAM2_END               0x10008000UL

/* ------------------------------------------------------------------------- */
/* « Boîte aux lettres » : l'application peut demander le chargeur en        */
/* écrivant ce mot magique (et son complément) à la fin de la SRAM2, puis en */
/* redémarrant. La SRAM2 garde son contenu pendant un reset logiciel.        */
/* ------------------------------------------------------------------------- */
#define OTA_REQUEST_ADDRESS         0x10007FF0UL     /* 2 mots à la fin de la SRAM2 */
#define OTA_REQUEST_MAGIC           0x4F544121UL     /* "OTA!" en ASCII            */

/* ------------------------------------------------------------------------- */
/* Bluetooth                                                                 */
/* ------------------------------------------------------------------------- */
/* Nom annoncé : "ObCP-OTA-" suivi des 4 derniers chiffres hexadécimaux de   */
/* l'adresse BLE de la carte (exemple : ObCP-OTA-A1B2).                      */
#define LOADER_NAME_PREFIX          "ObCP-OTA-"

/* Intervalle des annonces (« advertising ») en unités de 0,625 ms :          */
/* 160 x 0,625 = 100 ms ; 320 x 0,625 = 200 ms. Des annonces rapprochées      */
/* permettent au PC de trouver la carte en moins d'une seconde.              */
#define LOADER_ADV_INTERVAL_MIN     160U
#define LOADER_ADV_INTERVAL_MAX     320U

/* Paramètres de connexion demandés au PC une fois connecté, en unités de    */
/* 1,25 ms (12 -> 15 ms, 24 -> 30 ms) et de 10 ms pour le délai de rupture   */
/* (400 -> 4 s). Des échanges plus fréquents accélèrent le transfert ; le PC */
/* reste libre de refuser, le transfert fonctionne alors un peu plus lentement. */
#define LOADER_CONN_INTERVAL_MIN    12U
#define LOADER_CONN_INTERVAL_MAX    24U
#define LOADER_CONN_LATENCY         0U
#define LOADER_CONN_TIMEOUT         400U

/* Puissance d'émission (mêmes valeurs que le programme modèle de l'ObCP).   */
#define LOADER_TX_HIGH_POWER        1U
#define LOADER_TX_PA_LEVEL          4U

/* ------------------------------------------------------------------------- */
/* Temporisations (en millisecondes)                                         */
/* ------------------------------------------------------------------------- */
/* Sans connexion pendant ce délai, et si une application valide est         */
/* présente, le chargeur redémarre sur l'application (0 = jamais).           */
#define LOADER_IDLE_TIMEOUT_MS      120000U

/* Avant de redémarrer, le chargeur laisse d'abord le PC se déconnecter      */
/* lui-même (au plus ce délai), puis coupe la liaison si le PC est encore là */
/* et attend la confirmation du module (au plus le second délai).            */
#define LOADER_PC_DISCONNECT_WAIT_MS 1000U
#define LOADER_DISCONNECT_WAIT_MS   500U

/* Durée minimale d'un appui sur le bouton USER (anti-rebond).               */
#define LOADER_BUTTON_DEBOUNCE_MS   50U

/* Durée du clignotement blanc demandé par la commande IDENTIFY.             */
#define LOADER_IDENTIFY_MS          3000U

/* Délai entre deux tentatives de lancement des annonces BLE.                */
#define LOADER_ADV_RETRY_MS         100U

/* Au démarrage : attente avant de lire le bouton USER. À la mise sous        */
/* tension, le condensateur C2 de l'anti-rebond (100 nF) se charge à travers */
/* R1 (4,7 kΩ) en environ 0,5 ms ; 5 ms laissent une large marge.            */
#define BOOT_BUTTON_SETTLE_MS       5U

/* Au démarrage : durée de l'impulsion de reset envoyée au module BLE avant  */
/* de lancer l'application (le module repart alors dans un état neutre).     */
/* 5 ms comme dans le pilote de ST (HCI_TL_SPI_Reset).                       */
#define BOOT_BLE_RESET_PULSE_MS     5U

#endif /* LOADER_CONFIG_H */
