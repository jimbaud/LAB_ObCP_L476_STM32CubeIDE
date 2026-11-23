/**
 ******************************************************************************
 * @file    app_config.h
 * @brief   Réglages du programme de démonstration du réseau maillé :
 *          version, radio Bluetooth, réseau, temporisations, tampons.
 *
 *  Tous les « nombres magiques » du programme sont rassemblés ici, avec leur
 *  unité et la raison de leur valeur. Pour changer un réglage, on modifie ce
 *  fichier seulement : les modules (mesh.c, mesh_radio.c, node.c...)
 *  utilisent ces noms au lieu d'écrire les valeurs en dur.
 *
 *  Beaucoup de ces réglages sont aussi modifiables pendant le
 *  fonctionnement, depuis le terminal (commandes ttl, relais, suppr...) :
 *  les valeurs ci-dessous sont alors les valeurs « par défaut », celles
 *  d'une carte neuve ou après la commande « oublie ».
 *
 *  Ce fichier n'est pas généré par STM32CubeMX : une régénération du projet
 *  ne le modifie pas.
 ******************************************************************************
 * @author  Joël Imbaud - SUPMICROTECH-ENSMM
 ******************************************************************************
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ========================================================================== */
/* Identification du programme                                                */
/* ========================================================================== */

/** Nom et version du programme (majeure.mineure.correctif), affichés par la
 *  commande « info » et au branchement du câble USB. */
#define APP_NAME                   "ObCP BLE Mesh Demo"
#define APP_VERSION                "1.2.1"

/** Zone de la flash où le chargeur Bluetooth ObCP-OTA range le programme
 *  qu'il reçoit (banque 2, 512 ko). Le même code source donne deux
 *  versions, qui ne diffèrent que par le script d'édition de liens :
 *    - version USB : STM32L476RGTX_FLASH.ld, programme à 0x08000000,
 *      chargé par le câble USB (configurations Debug et Release) ;
 *    - version OTA : STM32L476RGTX_FLASH_OTA.ld, programme à 0x08080000,
 *      chargé par Bluetooth derrière le chargeur (« make OTA=1 »).
 *  Le programme reconnaît sa version à l'adresse de sa table des vecteurs
 *  (voir app_is_ota() dans app.c) : aucun autre réglage à changer. */
#define APP_OTA_FLASH_START        0x08080000UL
#define APP_OTA_FLASH_END          0x08100000UL

/* ========================================================================== */
/* Terminal (port COM virtuel USB)                                            */
/* ========================================================================== */

/** Longueur maximale d'une ligne de commande, en caractères (sans le '\0'
 *  final). « msg 17 » suivi d'un texte de 14 caractères en compte 21 : 80
 *  laisse de la marge. Une ligne plus longue est rejetée entière. */
#define CMD_LINE_MAX               80U

/** Taille du tampon d'émission USB, en octets : messages en attente d'envoi
 *  vers le PC. Avec le journal des trames (« journal on ») ou l'outil PC
 *  (« pc on »), plusieurs dizaines de lignes peuvent arriver d'un coup.
 *  Puissance de 2 (voir fifo.h). */
#define USB_TX_FIFO_SIZE           4096U

/** Nombre maximal d'octets confiés en une fois au pilote USB (il les
 *  découpe lui-même en paquets de 64 octets). */
#define USB_TX_CHUNK_SIZE          256U

/* ========================================================================== */
/* Radio : module Bluetooth BlueNRG-M0 ou SPBTLE-RF (mesh_radio.c)            */
/* ========================================================================== */
/* Les durées de la norme Bluetooth s'expriment en pas de 0,625 ms : une
 * valeur N vaut N x 0,625 ms (160 = 100 ms).                                 */

/** Intervalle entre deux « évènements de publicité » pendant l'émission
 *  d'une trame, en pas de 0,625 ms. 160 = 100 ms : c'est le minimum permis
 *  par la norme Bluetooth 4.1 pour une publicité « non connectable »
 *  (ADV_NONCONN_IND). À chaque évènement, la trame part sur les trois
 *  canaux de publicité (37, 38 et 39). */
#define RADIO_ADV_INTERVAL         160U

/** Durée d'émission d'une trame, en ms : la publicité reste active pendant
 *  ce temps, puis s'arrête. 320 ms laissent passer 3 évènements de
 *  publicité (à 0, 100 et 200 ms environ, plus un retard aléatoire de 0 à
 *  10 ms ajouté par le module ; retard fixe de 5 ms en mode simultané) : un
 *  récepteur a ainsi trois occasions d'entendre la trame. Plus long = plus
 *  sûr, mais moins de trames par seconde. */
#define RADIO_TX_TIME_MS           320U

/** Écoute (« scan ») en mode alterné, le mode par défaut (voir
 *  mesh_radio.h) : le module écoute un canal pendant SCAN_WINDOW, toutes les
 *  SCAN_INTERVAL (pas de 0,625 ms), en changeant de canal à chaque fois.
 *  80 / 80 : écoute continue, 50 ms sur chaque canal ; elle ne s'arrête que
 *  pendant les émissions de la carte. */
#define RADIO_SCAN_INTERVAL        80U
#define RADIO_SCAN_WINDOW          80U

/** Écoute en mode simultané (commande « radio simultane », pile en mode 4).
 *  Le BlueNRG-MS loge alors écoute et publicité dans une même base de temps,
 *  dont la période est l'intervalle d'écoute (PM0237, § 4.2). Il n'accepte
 *  d'émettre pendant l'écoute que si :
 *    - l'intervalle de publicité plus 5 ms est un multiple entier de cette
 *      période : ici 100 ms + 5 ms = 105 ms = 168 pas, une période ;
 *    - l'écoute laisse libre le créneau d'un évènement de publicité, 14,6 ms,
 *      plus les marges entre créneaux (1,25 ms) : ici 105 - 80 = 25 ms.
 *  Sinon il refuse (code 0x12) : c'est ce qui arrivait avec la version 1.2.0,
 *  qui écoutait 40 ms toutes les 50 ms. 168 / 128 : 80 ms d'écoute toutes
 *  les 105 ms, soit 76 % du temps. */
#define RADIO_SIM_SCAN_INTERVAL    168U
#define RADIO_SIM_SCAN_WINDOW      128U

/** Mode de la pile du module (UM1865, table 285) : le mode 4 permet
 *  d'émettre pendant l'écoute (« simultaneous advertising and scanning »),
 *  utilisé par la commande « radio simultane ». Il existe à partir de la
 *  pile 7.1b, codée 0x0712 (format 0xJJMN de getBlueNRGVersion, lettre
 *  comptée à partir de a = 1). Les SPBTLE-RF (cartes d'avant 2024) sont
 *  livrés avec la pile 7.1e, les BlueNRG-M0 avec une pile 7.2 ou plus
 *  récente : tous l'acceptent. Le programme le demande au démarrage quand la
 *  pile le permet, mais émet par défaut en mode alterné. */
#define RADIO_STACK_MODE           4U
#define RADIO_STACK_MODE_MIN_FW    0x0712U

/** Nombre de trames reçues gardées en attente de traitement par la boucle
 *  principale (rapports de publicité retenus par mesh_radio.c). */
#define RADIO_RX_QUEUE_LEN         16U

/** Attente, en ms, avant un nouvel essai quand le module refuse de lancer
 *  ou d'arrêter l'écoute ou la publicité. */
#define RADIO_RETRY_MS             50U

/** Puissance d'émission par défaut, en dBm. Valeurs possibles (table du
 *  module, UM1865) : -18, -15, -11, -8, -5, -2, 2, 5 et 8 dBm, les mêmes
 *  pour les deux modules (puissance conduite ; au réglage maximal, la
 *  puissance rayonnée annoncée est de +4 dBm pour le SPBTLE-RF et de +6 dBm
 *  pour le BlueNRG-M0). -2 dBm est la puissance du programme modèle
 *  OBCP_template_2026. Pour obtenir
 *  plusieurs sauts dans une salle, baisser la puissance (commande
 *  « puissance -18 ») et/ou filtrer les trames faibles (« rssimin »). */
#define RADIO_TX_POWER_DEFAULT     (-2)

/* ========================================================================== */
/* Réseau maillé (mesh.c)                                                     */
/* ========================================================================== */

/** Numéro de réseau par défaut (0 à 255). Les cartes réglées sur des numéros
 *  différents s'ignorent : plusieurs groupes d'étudiants peuvent travailler
 *  dans la même salle (commande « reseau »). */
#define MESH_NET_DEFAULT           0U

/** TTL (« Time To Live ») par défaut d'une trame émise : nombre de relais
 *  qu'elle peut encore traverser. 4 : la trame peut traverser 4 relais,
 *  donc atteindre des nœuds à 5 bonds radio de son origine. Valeurs
 *  permises : 0 (voisins directs seulement) à MESH_TTL_MAX. */
#define MESH_TTL_DEFAULT           4U
#define MESH_TTL_MAX               15U

/** Annulation des relais inutiles (« suppression ») : un nœud qui a entendu
 *  au moins N autres relais de la même trame pendant son attente ne la
 *  relaie pas (ses voisins l'ont sûrement déjà reçue). 0 : pas
 *  d'annulation, inondation complète (chaque nœud relaie chaque trame une
 *  fois). Commande « suppr ». */
#define MESH_SUPPRESS_DEFAULT      3U

/** Attente aléatoire avant de relayer une trame, en ms (tirée entre MIN et
 *  MAX). Elle évite que tous les voisins relaient au même instant (les
 *  trames se brouilleraient) et laisse le temps d'entendre les autres
 *  relais (voir MESH_SUPPRESS_DEFAULT). */
#define MESH_RELAY_DELAY_MIN_MS    20U
#define MESH_RELAY_DELAY_MAX_MS    300U

/** Seuil de puissance reçue (RSSI, en dBm) en dessous duquel une trame est
 *  ignorée, comme si elle n'avait pas été entendue. MESH_RSSI_MIN_OFF : pas
 *  de seuil. Sert à réduire la portée sur une table pour obtenir plusieurs
 *  sauts (commande « rssimin -60 »). */
#define MESH_RSSI_MIN_OFF          (-127)
#define MESH_RSSI_MIN_DEFAULT      MESH_RSSI_MIN_OFF

/** Période d'émission des trames HELLO (présence et liste des voisins), en
 *  secondes, avec un écart aléatoire de ±20 %. 0 : pas de HELLO. Les HELLO
 *  servent à la carte du réseau (outil PC) et à la détection des numéros en
 *  double. Commande « hello ». */
#define MESH_HELLO_PERIOD_S        20U

/** Mémoire des trames déjà vues (« cache ») : nombre de trames retenues et
 *  durée pendant laquelle on s'en souvient, en ms. Une trame reçue une
 *  seconde fois (par un autre chemin) pendant ce temps n'est ni traitée ni
 *  relayée une seconde fois. Avec un numéro d'ordre sur 8 bits, un nœud
 *  devrait émettre plus de 256 trames en 30 s pour que deux trames se
 *  confondent : impossible (3 trames par seconde au plus). */
#define MESH_CACHE_SIZE            32U
#define MESH_CACHE_LIFETIME_MS     30000U

/** Nombre de relais en attente (pendant leur délai aléatoire). */
#define MESH_PENDING_MAX           8U

/** File d'émission : nombre de trames en attente de leur tour d'émission
 *  (une trame occupe la radio RADIO_TX_TIME_MS). */
#define MESH_TXQ_LEN               8U

/** Nouvel essai d'émission quand la radio refuse, et nombre d'essais avant
 *  d'abandonner la trame. */
#define MESH_TX_RETRY_MS           50U
#define MESH_TX_ATTEMPTS           3U

/** Voisins directs (cartes entendues sans relais) : nombre retenu et durée
 *  au-delà de laquelle un voisin silencieux est oublié, en ms. */
#define MESH_NEIGHBORS_MAX         16U
#define MESH_NEIGHBOR_TIMEOUT_MS   60000U

/** Nœuds connus du réseau (origines des trames reçues, voisins ou non) :
 *  nombre retenu et durée d'oubli, en ms. */
#define MESH_NODES_MAX             32U
#define MESH_NODE_TIMEOUT_MS       120000U

/* ========================================================================== */
/* Démonstration (node.c)                                                     */
/* ========================================================================== */

/** Délai d'attente d'une réponse à « ping » ou « trace », en ms. */
#define NODE_REPLY_TIMEOUT_MS      5000U

/** Réponse à une demande adressée à un groupe ou à tous (ping, mesure) :
 *  chaque nœud attend un délai aléatoire de 0 à NODE_REPLY_SPREAD_MS avant
 *  de répondre, sinon toutes les réponses partiraient en même temps et se
 *  brouilleraient (« implosion » des réponses). */
#define NODE_REPLY_SPREAD_MS       2000U

/** En mode alterné (voir mesh_radio.h), une carte n'écoute pas pendant
 *  qu'elle émet : l'émetteur d'une demande (ping, trace, mesure) serait
 *  encore sourd si la réponse partait tout de suite. Les réponses attendent
 *  alors au moins la fin de son émission (RADIO_TX_TIME_MS), plus une marge
 *  pour la reprise de son écoute. En mode simultané : pas d'attente. */
#define NODE_REPLY_MIN_ALT_MS      (RADIO_TX_TIME_MS + 50U)

/** Nombre de pings et de traces suivis en même temps, et de réponses
 *  différées en attente. */
#define NODE_PING_SLOTS            4U
#define NODE_DEFERRED_SLOTS        6U

/** Envoi automatique des mesures (accélération, température) à tous, en
 *  secondes. 0 : pas d'envoi automatique (réglage « auto »). */
#define NODE_MEASURE_PERIOD_S      0U

/** Durée par défaut de l'identification (« ident »), en secondes. */
#define NODE_IDENT_DEFAULT_S       5U

/* ========================================================================== */
/* LED tricolore (led_fx.c)                                                   */
/* ========================================================================== */

/** Durée de l'éclair de la LED à chaque trame relayée ou reçue, en ms. */
#define LED_FLASH_MS               60U

/** Durée d'affichage de la couleur d'une vague (commande « vague »), en ms. */
#define LED_WAVE_MS                1500U

/** Battement de cœur : un bref éclat vert très faible toutes les
 *  LED_HEARTBEAT_MS montre que la carte fonctionne quand la LED est
 *  éteinte. 0 : pas de battement. */
#define LED_HEARTBEAT_MS           3000U

/* ========================================================================== */
/* Mémoire EEPROM (config_store.c)                                            */
/* ========================================================================== */

/** Adresse, dans l'EEPROM M95640 (8 ko), du bloc de réglages enregistrés par
 *  la commande « sauve ». Un bloc de 32 octets aligné sur une page, à la fin
 *  de la mémoire, pour ne pas gêner un autre programme qui utiliserait le
 *  début. */
#define CONFIG_EEPROM_ADDRESS      0x1FC0U

/* ========================================================================== */
/* Bouton utilisateur                                                         */
/* ========================================================================== */

/** Durée, en ms, pendant laquelle le bouton doit rester stable pour qu'un
 *  appui (ou un relâchement) soit pris en compte (anti-rebond). */
#define BUTTON_DEBOUNCE_MS         30U

/* ========================================================================== */
/* Économie d'énergie                                                         */
/* ========================================================================== */

/** 1 : à la fin de chaque tour de boucle, le processeur s'endort (WFI)
 *  jusqu'à la prochaine interruption (au plus 1 ms grâce au SysTick). */
#define APP_SLEEP_WHEN_IDLE        1

#endif /* APP_CONFIG_H */
