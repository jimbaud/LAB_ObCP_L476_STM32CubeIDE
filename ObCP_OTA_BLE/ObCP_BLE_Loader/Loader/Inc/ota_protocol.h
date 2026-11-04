/**
 ******************************************************************************
 * @file    ota_protocol.h
 * @brief   Description du protocole de transfert OTA entre le PC et l'ObCP.
 ******************************************************************************
 *
 *  Ce fichier est le « contrat » entre les deux programmes : le chargeur
 *  (en C, sur l'ObCP) et l'application PC obcp_ota.py (en Python). Toute
 *  valeur modifiée ici doit l'être aussi dans ota_protocole.py (le script de
 *  test Tests/test_outil_pc.py vérifie que les deux fichiers sont d'accord).
 *
 *  LE SERVICE BLE « ObCP OTA »
 *  ---------------------------
 *  En Bluetooth Low Energy, un appareil expose des « services » qui
 *  regroupent des « caractéristiques » : de petites cases de données que le
 *  PC peut lire, écrire ou recevoir en notification. Chaque élément est
 *  identifié par un UUID de 128 bits. Notre service contient trois cases :
 *
 *   Caractéristique  Sens        Propriétés                  Rôle
 *   ---------------  ----------  --------------------------  -------------------------
 *   CTRL             PC <-> ObCP écriture + notification      commandes et réponses
 *   DATA             PC  -> ObCP écriture sans réponse        morceaux du programme
 *   INFO             PC <-  ObCP lecture                      version, adresses, état
 *
 *  DÉROULEMENT D'UN TRANSFERT
 *  --------------------------
 *   PC                                          ObCP (chargeur)
 *   lit INFO                         ---->      (version, taille max, bloc)
 *   CTRL : START(taille, crc)        ---->      efface les pages nécessaires
 *                                    <----      notification READY
 *   DATA : [seq][19 octets] x N      ---->      écrit en flash, compte les octets
 *          ... 1024 octets ...       <----      notification ACK(octets reçus)
 *          (le PC attend l'ACK avant d'envoyer le bloc suivant)
 *   CTRL : END                       ---->      calcule le CRC, vérifie l'image,
 *                                               écrit les 8 premiers octets
 *                                    <----      notification DONE(crc)
 *   CTRL : RUN                       ---->      attend que le PC se déconnecte,
 *                                               puis redémarre sur la nouvelle
 *                                               application
 *
 *  La commande IDENTIFY (sans réponse) fait clignoter la LED en blanc :
 *  utile en salle de TP pour vérifier qu'on s'adresse à la bonne carte.
 *
 *  Tous les nombres sur plusieurs octets sont en « petit-boutiste »
 *  (little-endian) : l'octet de poids faible est envoyé en premier, comme le
 *  range le processeur Cortex-M4 en mémoire.
 ******************************************************************************
 */

#ifndef OTA_PROTOCOL_H
#define OTA_PROTOCOL_H

/* Version du protocole : à augmenter si le format des messages change.      */
#define OTA_PROTOCOL_VERSION        1U

/* ------------------------------------------------------------------------- */
/* UUID du service et des caractéristiques (128 bits).                       */
/* Écriture usuelle (celle affichée par nRF Connect ou par obcp_ota.py) :    */
/*   service : 9E090001-33D3-4337-976A-6F024CA6B676                          */
/*   CTRL    : 9E090002-33D3-4337-976A-6F024CA6B676                          */
/*   DATA    : 9E090003-33D3-4337-976A-6F024CA6B676                          */
/*   INFO    : 9E090004-33D3-4337-976A-6F024CA6B676                          */
/* La pile BlueNRG attend les 16 octets dans l'ordre inverse (petit-boutiste). */
/* ------------------------------------------------------------------------- */
#define OTA_UUID_BYTES(n)           { 0x76, 0xB6, 0xA6, 0x4C, 0x02, 0x6F, 0x6A, 0x97, \
                                      0x37, 0x43, 0xD3, 0x33, (n), 0x00, 0x09, 0x9E }
#define OTA_UUID_SERVICE            OTA_UUID_BYTES(0x01)
#define OTA_UUID_CTRL               OTA_UUID_BYTES(0x02)
#define OTA_UUID_DATA               OTA_UUID_BYTES(0x03)
#define OTA_UUID_INFO               OTA_UUID_BYTES(0x04)

/* ------------------------------------------------------------------------- */
/* Commandes : le PC les écrit dans CTRL. Le premier octet est le code.      */
/* ------------------------------------------------------------------------- */
#define OTA_CMD_START               0x01U   /* + taille (4 octets) + CRC-32 (4 octets) */
#define OTA_CMD_END                 0x02U   /* toutes les données sont envoyées        */
#define OTA_CMD_ABORT               0x03U   /* abandon du transfert en cours           */
#define OTA_CMD_RUN                 0x04U   /* redémarrer sur l'application            */
#define OTA_CMD_IDENTIFY            0x05U   /* LED blanche clignotante pendant 3 s     */

#define OTA_START_LEN               9U      /* 1 + 4 + 4 octets */

/* ------------------------------------------------------------------------- */
/* Réponses : le chargeur les envoie en notification sur CTRL.               */
/* Format fixe de 5 octets : [code][valeur sur 4 octets].                    */
/* ------------------------------------------------------------------------- */
#define OTA_RSP_READY               0x10U   /* valeur = taille acceptée              */
#define OTA_RSP_ACK                 0x11U   /* valeur = nombre d'octets reçus        */
#define OTA_RSP_DONE                0x12U   /* valeur = CRC-32 calculé par l'ObCP    */

/* Codes d'erreur (valeur = information complémentaire, voir chaque ligne)    */
#define OTA_ERR_STATE               0x20U   /* commande inattendue ; valeur = état   */
#define OTA_ERR_SIZE                0x21U   /* taille refusée ; valeur = taille      */
#define OTA_ERR_FLASH               0x22U   /* échec effacement/écriture ; valeur = code HAL */
#define OTA_ERR_SEQUENCE            0x23U   /* paquet perdu ; valeur = numéro attendu */
#define OTA_ERR_OVERFLOW            0x24U   /* trop de données ; valeur = total reçu  */
#define OTA_ERR_CRC                 0x25U   /* CRC faux ; valeur = CRC calculé       */
#define OTA_ERR_INCOMPLETE          0x26U   /* END trop tôt ; valeur = octets reçus  */
#define OTA_ERR_COMMAND             0x27U   /* message mal formé ; valeur = longueur */
#define OTA_ERR_IMAGE               0x28U   /* table des vecteurs invalide ; valeur = adresse de Reset_Handler */

#define OTA_RSP_LEN                 5U

/* ------------------------------------------------------------------------- */
/* Paquets de données (caractéristique DATA)                                  */
/* Le module BlueNRG-MS limite chaque écriture BLE à 20 octets (MTU de 23).  */
/* Le premier octet est un numéro de séquence (0, 1, ... 255, 0, 1 ...) qui  */
/* permet au chargeur de détecter immédiatement un paquet perdu.             */
/* ------------------------------------------------------------------------- */
#define OTA_DATA_MAX_LEN            20U     /* longueur maximale d'une écriture */
#define OTA_DATA_PAYLOAD            19U     /* octets de programme par paquet   */

/* Le chargeur envoie un accusé de réception (ACK) chaque fois que le nombre */
/* d'octets reçus est un multiple de cette taille, et à la fin du transfert. */
#define OTA_BLOCK_SIZE              1024U

/* ------------------------------------------------------------------------- */
/* Contenu de la caractéristique INFO (18 octets)                            */
/*   [0]      version du protocole                                           */
/*   [1] [2]  version du chargeur (majeure, mineure)                         */
/*   [3]      1 si une application valide est installée, 0 sinon            */
/*   [4..7]   adresse de la zone application (0x08080000)                    */
/*   [8..11]  taille maximale d'une application (524288)                     */
/*   [12..13] taille d'un bloc (1024)                                        */
/*   [14]     octets de programme par paquet DATA (19)                       */
/*   [15]     raison du démarrage dans le chargeur (voir boot.h)             */
/* Diagnostic, depuis la version 1.1 du chargeur (un chargeur 1.0 n'envoie   */
/* que les 16 premiers octets) :                                             */
/*   [16]     cause de la dernière déconnexion Bluetooth : code d'erreur de  */
/*            la norme Bluetooth (0x08 = plus aucun échange radio, 0x13 =    */
/*            déconnexion demandée par le PC...) ; 0 = aucune depuis le      */
/*            démarrage du chargeur                                          */
/*   [17]     cause du dernier redémarrage imprévu du module BLE (code de    */
/*            l'événement « HAL initialized », 8 = plantage...) ; 0 = aucun  */
/* Après une coupure, le PC se reconnecte et relit INFO : il sait alors si   */
/* le programme a été validé (octet 3) et pourquoi la liaison est tombée.    */
/* ------------------------------------------------------------------------- */
#define OTA_INFO_LEN                18U
#define OTA_INFO_DISCONNECT_REASON  16U     /* position de l'octet dans INFO */
#define OTA_INFO_MODULE_RESTART     17U     /* position de l'octet dans INFO */

#endif /* OTA_PROTOCOL_H */
