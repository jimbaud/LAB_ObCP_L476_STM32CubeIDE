/**
 ******************************************************************************
 * @file    app_config.h
 * @brief   Réglages de l'application ObCP : version, nom Bluetooth, tailles
 *          des tampons, temporisations.
 *
 *  Tous les « nombres magiques » du programme sont rassemblés ici, avec leur
 *  unité et la raison de leur valeur. Pour changer un réglage, on modifie ce
 *  fichier seulement : les modules (usb_console.c, ble_uart.c...) utilisent
 *  ces noms au lieu d'écrire les valeurs en dur.
 *
 *  Ce fichier n'est pas généré par STM32CubeMX : une régénération du projet
 *  ne le modifie pas.
 ******************************************************************************
 * @author  Joël Imbaud - SUPMICROTECH-ENSMM (version 2 du programme modèle)
 ******************************************************************************
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ========================================================================== */
/* Identification du programme                                                */
/* ========================================================================== */

/** Version du programme (majeure.mineure.correctif). La version 2.0.0 reprend
 *  les commandes de OBCP_template_2024 et corrige ses défauts (voir README).
 *  La version 2.1.0 se charge par Bluetooth (chargeur ObCP-OTA) et ajoute la
 *  commande « #ota ». */
#define APP_VERSION                "2.1.0"

/** Nom de la carte vu par un téléphone en Bluetooth (paquet de publicité et
 *  caractéristique « Device Name »). 20 caractères au plus : le paquet de
 *  publicité ne fait que 31 octets. C'est le nom que cherchent les étudiants
 *  et l'application Android « ObCP Control » : ne le changer qu'en
 *  connaissance de cause. */
#define BLE_DEVICE_NAME            "OBCP-CROC"

/* ========================================================================== */
/* Commandes                                                                  */
/* ========================================================================== */

/** Longueur maximale d'une ligne de commande, en caractères (sans le '\0'
 *  final). La plus longue commande, « start timer », en compte 11 : 64 laisse
 *  de la marge pour de futures commandes. Une ligne plus longue est rejetée
 *  avec le message « False command ». */
#define CMD_LINE_MAX               64U

/** Plus grande valeur acceptée par les commandes r, v, b et m (16 bits). */
#define CMD_VALUE_MAX              65535U

/* ========================================================================== */
/* Port COM virtuel (USB)                                                     */
/* ========================================================================== */

/** Taille du tampon d'émission USB, en octets. Il garde les messages en
 *  attente d'envoi vers le PC (l'aide, la plus longue réponse, fait environ
 *  450 octets). Doit être une puissance de 2 (voir fifo.h). */
#define USB_TX_FIFO_SIZE           1024U

/** Nombre maximal d'octets confiés en une fois au pilote USB. Le pilote les
 *  découpe lui-même en paquets de 64 octets (taille d'un paquet USB « full
 *  speed » pour la classe CDC). */
#define USB_TX_CHUNK_SIZE          256U

/* ========================================================================== */
/* Bluetooth Low Energy (module BlueNRG-M0)                                   */
/* ========================================================================== */

/** Nombre maximal d'octets d'une notification BLE avec le MTU par défaut
 *  (23 octets, dont 3 d'en-tête ATT) : c'est la taille des morceaux envoyés
 *  au téléphone. Elle est aussi la taille des caractéristiques TX et RX. */
#define BLE_CHUNK_SIZE             20U

/** Taille du tampon d'émission BLE, en octets (messages en attente d'envoi,
 *  2 octets de longueur en plus par message). Puissance de 2 (voir fifo.h). */
#define BLE_TX_FIFO_SIZE           1024U

/** Nombre de messages reçus du téléphone gardés en attente de traitement. */
#define BLE_RX_QUEUE_LEN           8U

/** Nombre maximal de notifications envoyées à chaque tour de boucle : limite
 *  le temps passé dans l'envoi pour que l'USB et le bouton restent servis. */
#define BLE_CHUNKS_PER_LOOP        4U

/** Attente, en ms, avant de redemander l'envoi d'une notification quand le
 *  module a répondu « plus de tampon libre » et n'a pas encore signalé qu'un
 *  tampon s'est libéré (évènement EVT_BLUE_GATT_TX_POOL_AVAILABLE). */
#define BLE_TX_RETRY_MS            20U

/** Attente, en ms, avant de relancer la publicité (« advertising ») si le
 *  module l'a refusée, par exemple juste après une déconnexion. */
#define BLE_ADV_RETRY_MS           200U

/* ========================================================================== */
/* Chargement par Bluetooth (commande « #ota », voir ota_switch.h)            */
/* ========================================================================== */

/** Après « #ota » : temps maximal, en ms, laissé à la réponse pour partir
 *  vers le PC et le téléphone avant le redémarrage dans le chargeur. Il ne
 *  sert que si le PC ne lit plus le port COM (sinon, la réponse part en
 *  quelques millisecondes). */
#define OTA_REPLY_TIMEOUT_MS       500U

/** Après « #ota » : temps maximal, en ms, pour fermer proprement la
 *  connexion Bluetooth (et prévenir le PC : « BLE disconnected ») avant ce
 *  redémarrage. Au total, le passage au chargeur prend donc 1 s au plus. */
#define OTA_DISCONNECT_TIMEOUT_MS  500U

/* ========================================================================== */
/* Bouton utilisateur et mesures                                              */
/* ========================================================================== */

/** Durée, en ms, pendant laquelle le bouton doit rester stable pour qu'un
 *  appui (ou un relâchement) soit pris en compte : filtre les rebonds du
 *  contact mécanique, qui durent quelques millisecondes. */
#define BUTTON_DEBOUNCE_MS         30U

/* ========================================================================== */
/* Économie d'énergie                                                         */
/* ========================================================================== */

/** 1 : à la fin de chaque tour de boucle, le processeur s'endort (instruction
 *  WFI, « Wait For Interrupt ») jusqu'à la prochaine interruption (au plus
 *  1 ms grâce au SysTick). La consommation baisse de quelques mA, ce qui
 *  compte sur batterie. 0 : la boucle tourne sans arrêt (comportement de la
 *  version 2024). */
#define APP_SLEEP_WHEN_IDLE        1

#endif /* APP_CONFIG_H */
