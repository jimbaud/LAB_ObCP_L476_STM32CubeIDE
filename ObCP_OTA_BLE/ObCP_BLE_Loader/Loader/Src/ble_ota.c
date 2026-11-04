/**
 ******************************************************************************
 * @file    ble_ota.c
 * @brief   Service GATT « ObCP OTA » sur le module BlueNRG-MS (voir ble_ota.h).
 ******************************************************************************
 *
 *  Les fonctions aci_xxx() et hci_xxx() viennent de la bibliothèque ST
 *  X-CUBE-BLE1 (dossier Middlewares/ST/BlueNRG-MS). Chacune envoie une
 *  commande au module par SPI et attend sa réponse ; elles renvoient
 *  BLE_STATUS_SUCCESS (0) si tout s'est bien passé.
 *  Documentation : manuel de programmation PM0237 « BlueNRG-MS ACI commands ».
 *
 *  LES « HANDLES »
 *  ---------------
 *  Dans la base de données GATT du module, chaque élément reçoit un numéro,
 *  son « handle ». aci_gatt_add_char() renvoie le handle de la DÉCLARATION
 *  d'une caractéristique ; sa VALEUR est au handle suivant (+1) et, pour une
 *  caractéristique qui notifie, le descripteur CCCD (que le PC écrit pour
 *  activer les notifications) est au handle +2.
 *
 *  ROBUSTESSE (version 1.1 du chargeur)
 *  ------------------------------------
 *   - Les commandes envoyées au module sont protégées contre l'interruption
 *     du module (spi_send_guarded) : sans cela, une lecture faite par
 *     l'interruption pouvait s'intercaler au milieu d'un envoi.
 *   - Avant de redémarrer (commande RUN), la carte laisse le PC se
 *     déconnecter lui-même (ble_ota_disconnect).
 *   - Si le module redémarre tout seul (plantage, baisse d'alimentation...),
 *     il est entièrement réinitialisé et la carte redevient visible.
 *   - La cause de la dernière coupure est publiée dans INFO : après s'être
 *     reconnecté, le PC peut l'afficher (diagnostic).
 ******************************************************************************
 */

#include <string.h>

#include "main.h"
#include "hci.h"
#include "hci_le.h"
#include "hci_tl.h"
#include "sm.h"
#include "bluenrg_aci.h"
#include "bluenrg_gap.h"
#include "bluenrg_gatt_server.h"
#include "bluenrg_utils.h"

#include "ble_ota.h"
#include "boot.h"
#include "crc32.h"
#include "loader_config.h"
#include "ota.h"
#include "ota_protocol.h"

/* Longueur du nom : "ObCP-OTA-" (9 caractères) + 4 chiffres hexadécimaux. */
#define NAME_LEN            ((uint8_t)(sizeof(LOADER_NAME_PREFIX) - 1U + 4U))

/* Longueur maximale d'une valeur de caractéristique (MTU 23 - 3).          */
#define CHAR_MAX_LEN        20U

/* Temps maximal pour réussir à envoyer une notification quand les tampons
 * d'émission du module sont pleins.                                       */
#define NOTIFY_RETRY_MS     200U

/* UUID (définis dans ota_protocol.h). */
static const uint8_t service_uuid[16] = OTA_UUID_SERVICE;
static const uint8_t ctrl_uuid[16]    = OTA_UUID_CTRL;
static const uint8_t data_uuid[16]    = OTA_UUID_DATA;
static const uint8_t info_uuid[16]    = OTA_UUID_INFO;

/* État de la partie Bluetooth (remis à zéro à chaque initialisation). */
static struct
{
    bool     ready;               /* module initialisé                     */
    bool     connected;           /* un PC est connecté                    */
    bool     advertise_pending;   /* annonces à (re)lancer                 */
    bool     module_restarted;    /* le module a redémarré : tout refaire  */
    uint32_t last_adv_try;        /* instant du dernier essai d'annonce    */
    uint16_t conn_handle;         /* identifiant de la connexion           */
    uint16_t service_handle;      /* handle du service ObCP OTA            */
    uint16_t ctrl_handle;         /* déclaration CTRL (valeur +1, CCCD +2) */
    uint16_t data_handle;         /* déclaration DATA (valeur +1)          */
    uint16_t info_handle;         /* déclaration INFO (valeur +1)          */
    uint8_t  board_addr[6];       /* adresse BLE propre à la carte         */
    char     name[NAME_LEN + 1U]; /* "ObCP-OTA-XXXX" + zéro final          */
} ble;

/* Diagnostic publié dans INFO (octets 16 et 17). Cette structure est à part
 * de « ble » : elle n'est pas effacée quand le module est réinitialisé.   */
static struct
{
    uint8_t disconnect_reason;    /* code de la dernière déconnexion       */
    uint8_t module_restart;       /* raison du dernier redémarrage imprévu */
} diag;

/* ------------------------------------------------------------------------- */
/* Fonctions internes                                                        */
/* ------------------------------------------------------------------------- */

/* Envoie une commande au module, à l'abri de l'interruption du module.
 *
 * La fonction d'envoi du pilote de ST (HCI_TL_SPI_Send) écrit sur le bus SPI
 * sans masquer l'interruption EXTI12. Si le module signale un événement
 * pendant cet envoi, la routine d'interruption lit le bus SPI au beau milieu
 * de notre écriture : les deux échanges s'entremêlent et la commande peut
 * être perdue (ou l'interruption peut tourner en boucle, le bus étant
 * occupé). On masque donc l'interruption le temps de l'envoi. Un événement
 * signalé entre-temps n'est pas perdu : il reste en attente et sera lu dès
 * que l'interruption est démasquée.                                         */
static int32_t spi_send_guarded(uint8_t *buffer, uint16_t size)
{
    HAL_NVIC_DisableIRQ(BLE_IRQ_EXTI_IRQn);
    int32_t result = HCI_TL_SPI_Send(buffer, size);
    HAL_NVIC_EnableIRQ(BLE_IRQ_EXTI_IRQn);
    return result;
}

/* Remplace la fonction d'envoi du pilote par spi_send_guarded() (les
 * autres fonctions restent celles du pilote). La substitution est faite ici,
 * dans notre code, et non dans hci_tl_interface.c : ce fichier est régénéré
 * par CubeMX, la modification y serait perdue.                             */
static void use_guarded_spi_send(void)
{
    tHciIO io;

    memset(&io, 0, sizeof(io));
    io.Init    = HCI_TL_SPI_Init;
    io.DeInit  = HCI_TL_SPI_DeInit;
    io.Reset   = HCI_TL_SPI_Reset;
    io.Receive = HCI_TL_SPI_Receive;
    io.Send    = spi_send_guarded;
    io.GetTick = BSP_GetTick;
    hci_register_io_bus(&io);
}

/* Met à jour la caractéristique INFO (lue par le PC avant le transfert). */
static void refresh_info(void)
{
    uint8_t info[OTA_INFO_LEN];

    ota_build_info(info, (uint8_t)boot_get_reason());
    info[OTA_INFO_DISCONNECT_REASON] = diag.disconnect_reason;
    info[OTA_INFO_MODULE_RESTART]    = diag.module_restart;
    (void)aci_gatt_update_char_value(ble.service_handle, ble.info_handle,
                                     0U, OTA_INFO_LEN, info);
}

/* Envoie une réponse au PC : notification de 5 octets sur CTRL.
 * C'est la fonction « notify » donnée au cœur du transfert (ota.c).        */
static void send_response(uint8_t code, uint32_t value)
{
    uint8_t message[OTA_RSP_LEN];
    uint32_t start = HAL_GetTick();
    tBleStatus ret;

    /* Avant une réponse qui change l'état (READY après l'effacement, DONE
     * après la validation, erreurs), on met INFO à jour : l'application est
     * devenue invalide ou valide. On le fait AVANT la notification, car dès
     * qu'il la reçoit, le PC peut envoyer des données ; or il vaut mieux
     * n'envoyer aucune commande au module pendant que les données arrivent
     * (voir l'explication en tête de ota.c). Les ACK, eux, ne changent rien. */
    if (code != OTA_RSP_ACK)
    {
        refresh_info();
    }

    message[0] = code;
    message[1] = (uint8_t)(value);          /* petit-boutiste */
    message[2] = (uint8_t)(value >> 8);
    message[3] = (uint8_t)(value >> 16);
    message[4] = (uint8_t)(value >> 24);

    /* Modifier la valeur de CTRL envoie automatiquement une notification au
     * PC (s'il les a activées). Si les tampons d'émission du module sont
     * pleins, il répond « ressources insuffisantes » : on réessaie.        */
    do
    {
        ret = aci_gatt_update_char_value(ble.service_handle, ble.ctrl_handle,
                                         0U, OTA_RSP_LEN, message);
    } while ((ret == BLE_STATUS_INSUFFICIENT_RESOURCES)
             && ((HAL_GetTick() - start) < NOTIFY_RETRY_MS));
}

/* Une adresse « vide » (que des 0 ou que des 0xFF) n'est pas utilisable. */
static bool address_is_blank(const uint8_t addr[6])
{
    bool all_zero = true;
    bool all_ones = true;

    for (int i = 0; i < 6; i++)
    {
        all_zero = all_zero && (addr[i] == 0x00U);
        all_ones = all_ones && (addr[i] == 0xFFU);
    }
    return all_zero || all_ones;
}

/* Choisit l'adresse BLE de la carte.
 * Comme le programme modèle de l'ObCP, on lit l'adresse aléatoire propre à
 * chaque module BLE : chaque carte a ainsi une adresse différente, ce qui est
 * indispensable en salle de TP. Si la lecture échoue, on fabrique une
 * adresse à partir du numéro de série unique du STM32 (96 bits à UID_BASE). */
static void choose_board_address(void)
{
    uint8_t len_out = 0U;
    tBleStatus ret = aci_hal_read_config_data(CONFIG_DATA_RANDOM_ADDRESS,
                                              CONFIG_DATA_PUBADDR_LEN,
                                              &len_out, ble.board_addr);

    if ((ret != BLE_STATUS_SUCCESS) || (len_out != CONFIG_DATA_PUBADDR_LEN)
        || address_is_blank(ble.board_addr))
    {
        uint32_t hash = crc32_update(0U, (const uint8_t *)UID_BASE, 12U);
        ble.board_addr[0] = (uint8_t)(hash);
        ble.board_addr[1] = (uint8_t)(hash >> 8);
        ble.board_addr[2] = (uint8_t)(hash >> 16);
        ble.board_addr[3] = (uint8_t)(hash >> 24);
        ble.board_addr[4] = 0xB7U;
        ble.board_addr[5] = 0xC0U;       /* 2 bits de poids fort à 1 */
    }
}

/* Construit le nom "ObCP-OTA-XXXX" : XXXX = deux derniers octets de
 * l'adresse, tels qu'affichés à la fin de « C1:22:33:44:A1:B2 ».          */
static void build_name(void)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t prefix_len = sizeof(LOADER_NAME_PREFIX) - 1U;

    memcpy(ble.name, LOADER_NAME_PREFIX, prefix_len);
    ble.name[prefix_len + 0U] = hex[ble.board_addr[1] >> 4];
    ble.name[prefix_len + 1U] = hex[ble.board_addr[1] & 0x0FU];
    ble.name[prefix_len + 2U] = hex[ble.board_addr[0] >> 4];
    ble.name[prefix_len + 3U] = hex[ble.board_addr[0] & 0x0FU];
    ble.name[NAME_LEN] = '\0';
}

/* Crée le service et ses trois caractéristiques dans le module. */
static bool add_ota_service(void)
{
    /* Nombre de cases (« attributs ») réservées pour le service :
     * 1 (service) + 3 (CTRL : déclaration, valeur, CCCD)
     * + 2 (DATA) + 2 (INFO) = 8 ; on prévoit un peu de marge.              */
    const uint8_t max_attributes = 10U;

    if (aci_gatt_add_serv(UUID_TYPE_128, service_uuid, PRIMARY_SERVICE,
                          max_attributes, &ble.service_handle) != BLE_STATUS_SUCCESS)
    {
        return false;
    }

    /* CTRL : le PC écrit les commandes (avec accusé ATT) et reçoit les
     * réponses par notification. GATT_NOTIFY_ATTRIBUTE_WRITE demande au
     * module de nous prévenir à chaque écriture.                          */
    if (aci_gatt_add_char(ble.service_handle, UUID_TYPE_128, ctrl_uuid,
                          CHAR_MAX_LEN, CHAR_PROP_WRITE | CHAR_PROP_NOTIFY,
                          ATTR_PERMISSION_NONE, GATT_NOTIFY_ATTRIBUTE_WRITE,
                          16U, 1U, &ble.ctrl_handle) != BLE_STATUS_SUCCESS)
    {
        return false;
    }

    /* DATA : écriture SANS réponse, la plus rapide en BLE.                 */
    if (aci_gatt_add_char(ble.service_handle, UUID_TYPE_128, data_uuid,
                          CHAR_MAX_LEN, CHAR_PROP_WRITE_WITHOUT_RESP,
                          ATTR_PERMISSION_NONE, GATT_NOTIFY_ATTRIBUTE_WRITE,
                          16U, 1U, &ble.data_handle) != BLE_STATUS_SUCCESS)
    {
        return false;
    }

    /* INFO : lecture seule, longueur fixe (OTA_INFO_LEN octets).           */
    if (aci_gatt_add_char(ble.service_handle, UUID_TYPE_128, info_uuid,
                          OTA_INFO_LEN, CHAR_PROP_READ,
                          ATTR_PERMISSION_NONE, GATT_DONT_NOTIFY_EVENTS,
                          16U, 0U, &ble.info_handle) != BLE_STATUS_SUCCESS)
    {
        return false;
    }

    return true;
}

/* Lance les annonces : la carte devient visible et connectable. */
static void start_advertising(void)
{
    /* Données d'annonce : le nom complet (type AD 0x09 suivi des lettres). */
    char local_name[NAME_LEN + 1U];
    local_name[0] = AD_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(&local_name[1], ble.name, NAME_LEN);

    /* Réponse au scan : l'UUID du service, au format [longueur][type][16 octets].
     * Il ne tient pas dans l'annonce elle-même (31 octets au plus), d'où la
     * réponse au scan, que le PC demande automatiquement.                   */
    uint8_t scan_response[18];
    scan_response[0] = 17U;                                 /* type + UUID */
    scan_response[1] = AD_TYPE_128_BIT_SERV_UUID_CMPLT_LIST;
    memcpy(&scan_response[2], service_uuid, sizeof(service_uuid));
    (void)hci_le_set_scan_resp_data(sizeof(scan_response), scan_response);

    tBleStatus ret = aci_gap_set_discoverable(ADV_IND,
                                              LOADER_ADV_INTERVAL_MIN,
                                              LOADER_ADV_INTERVAL_MAX,
                                              PUBLIC_ADDR, NO_WHITE_LIST_USE,
                                              (uint8_t)(NAME_LEN + 1U), local_name,
                                              0U, NULL, 0U, 0U);

    /* « Commande refusée » signifie que les annonces tournent déjà.        */
    if ((ret == BLE_STATUS_SUCCESS) || (ret == ERR_COMMAND_DISALLOWED))
    {
        ble.advertise_pending = false;   /* sinon nouvel essai plus tard */
    }
}

/* La connexion vient de se terminer (le PC s'est déconnecté, ou la liaison
 * radio est tombée) : on note la cause pour le diagnostic, on abandonne le
 * transfert en cours et on redevient visible.                             */
static void connection_ended(uint8_t reason)
{
    ble.connected = false;
    ble.advertise_pending = true;
    diag.disconnect_reason = reason;
    ota_on_disconnect();
    refresh_info();                        /* le PC verra la cause en INFO */
}

/* Une écriture du PC vient d'arriver dans une de nos caractéristiques. */
static void on_attribute_modified(uint16_t handle, const uint8_t *data, uint8_t len)
{
    if (handle == (uint16_t)(ble.ctrl_handle + 1U))          /* valeur de CTRL */
    {
        ota_on_control(data, len);
    }
    else if (handle == (uint16_t)(ble.data_handle + 1U))     /* valeur de DATA */
    {
        ota_on_data(data, len);
    }
    else if (handle == (uint16_t)(ble.ctrl_handle + 2U))     /* CCCD de CTRL */
    {
        /* Le PC vient d'activer les notifications : il est prêt. On lui
         * demande des échanges plus fréquents pour accélérer le transfert. */
        if ((len > 0U) && ((data[0] & 0x01U) != 0U))
        {
            (void)aci_l2cap_connection_parameter_update_request(
                ble.conn_handle, LOADER_CONN_INTERVAL_MIN, LOADER_CONN_INTERVAL_MAX,
                LOADER_CONN_LATENCY, LOADER_CONN_TIMEOUT);
        }
    }
}

/* Fonction appelée par la bibliothèque (hci_user_evt_proc) pour chaque
 * événement reçu du module. pData pointe sur le paquet HCI brut.         */
static void ble_event_handler(void *pData)
{
    hci_uart_pckt  *packet = pData;
    hci_event_pckt *event  = (hci_event_pckt *)packet->data;

    if (packet->type != HCI_EVENT_PKT)
    {
        return;                            /* ce n'est pas un événement */
    }

    switch (event->evt)
    {
    case EVT_DISCONN_COMPLETE:             /* fin de la connexion */
    {
        evt_disconn_complete *dc = (evt_disconn_complete *)event->data;
        /* status différent de 0 : une demande de déconnexion a échoué et
         * la connexion continue ; il n'y a rien à faire.                 */
        if (dc->status == BLE_STATUS_SUCCESS)
        {
            connection_ended(dc->reason);
        }
        break;
    }

    case EVT_LE_META_EVENT:                /* événements propres au BLE */
    {
        evt_le_meta_event *meta = (evt_le_meta_event *)event->data;
        if (meta->subevent == EVT_LE_CONN_COMPLETE)
        {
            evt_le_connection_complete *cc = (evt_le_connection_complete *)meta->data;
            if (cc->status == BLE_STATUS_SUCCESS)
            {
                ble.connected = true;
                ble.conn_handle = cc->handle;
            }
            else
            {
                ble.advertise_pending = true;   /* connexion ratée : on se remontre */
            }
        }
        break;
    }

    case EVT_VENDOR:                       /* événements propres au BlueNRG */
    {
        evt_blue_aci *blue = (evt_blue_aci *)event->data;
        if (blue->ecode == EVT_BLUE_GATT_ATTRIBUTE_MODIFIED)
        {
            evt_gatt_attr_modified_IDB05A1 *modified =
                (evt_gatt_attr_modified_IDB05A1 *)blue->data;
            on_attribute_modified(modified->attr_handle, modified->att_data,
                                  modified->data_length);
        }
        else if ((blue->ecode == EVT_BLUE_HAL_INITIALIZED) && ble.ready)
        {
            /* Le module envoie cet événement à chaque démarrage. Une fois
             * l'initialisation terminée (ble.ready), il signifie que le
             * module a redémarré tout seul (plantage, chien de garde,
             * baisse d'alimentation...) : il a perdu la connexion, notre
             * service et ses réglages. On ne lui envoie plus rien ici ;
             * ble_ota_process() refera toute l'initialisation.            */
            evt_hal_initialized *init = (evt_hal_initialized *)blue->data;
            diag.module_restart = init->reason_code;
            if (ble.connected)
            {
                ble.connected = false;
                ota_on_disconnect();
            }
            ble.module_restarted = true;
        }
        break;
    }

    default:
        break;                             /* événement sans intérêt ici */
    }
}

/* Lit les événements en attente et les traite.
 *
 * Filet de sécurité : l'interruption du module est déclenchée sur le FRONT
 * montant de sa broche IRQ. Si la file de la bibliothèque était pleine au
 * moment de ce front, l'interruption n'a pas pu tout lire et aucun nouveau
 * front n'arrivera tant que la broche reste à 1. On regarde donc ici si la
 * broche est encore à 1 et, si oui, on fait la lecture nous-mêmes
 * (interruption masquée pour ne pas être lus deux fois).                  */
static void poll_events(void)
{
    if (HAL_GPIO_ReadPin(BLE_IRQ_GPIO_Port, BLE_IRQ_Pin) == GPIO_PIN_SET)
    {
        HAL_NVIC_DisableIRQ(BLE_IRQ_EXTI_IRQn);
        hci_tl_lowlevel_isr();
        HAL_NVIC_EnableIRQ(BLE_IRQ_EXTI_IRQn);
    }
    hci_user_evt_proc();
}

/* Attend la fin de la connexion (événement EVT_DISCONN_COMPLETE), au plus
 * timeout_ms millisecondes, en continuant à traiter les événements.       */
static void wait_disconnection(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while (ble.connected && ((HAL_GetTick() - start) < timeout_ms))
    {
        poll_events();
    }
}

/* ------------------------------------------------------------------------- */
/* Fonctions publiques                                                       */
/* ------------------------------------------------------------------------- */

bool ble_ota_init(void)
{
    uint8_t  hw_version = 0U;
    uint16_t fw_version = 0U;
    uint16_t gap_service = 0U;
    uint16_t dev_name_char = 0U;
    uint16_t appearance_char = 0U;
    uint8_t  loader_addr[6];

    memset(&ble, 0, sizeof(ble));          /* « diag », lui, est conservé */
    ota_init(send_response);               /* le cœur répondra par send_response */

    /* 1) Liaison SPI, broches du module, reset matériel du module, et
     *    enregistrement de notre fonction de traitement des événements.
     *    Puis remplacement de la fonction d'envoi par sa version protégée. */
    hci_init(ble_event_handler, NULL);
    use_guarded_spi_send();

    /* 2) Version du module (lecture facultative, utile au débogage), puis
     *    reset logiciel : l'adresse ne peut être changée que juste après.  */
    (void)getBlueNRGVersion(&hw_version, &fw_version);
    (void)hci_reset();
    HAL_Delay(100U);

    /* 3) Adresse et nom. Le chargeur utilise l'adresse de la carte avec
     *    l'octet de poids fort (le premier affiché, C1 dans C1:22:33:44:A1:B2)
     *    légèrement modifié : bit 0 inversé, soit C0:22:33:44:A1:B2. Le PC
     *    voit ainsi deux appareils distincts (l'application et le chargeur)
     *    et ne confond pas leurs services, qu'il garde parfois en mémoire. */
    choose_board_address();
    build_name();
    memcpy(loader_addr, ble.board_addr, sizeof(loader_addr));
    loader_addr[5] ^= 0x01U;
    if (aci_hal_write_config_data(CONFIG_DATA_PUBADDR_OFFSET, CONFIG_DATA_PUBADDR_LEN,
                                  loader_addr) != BLE_STATUS_SUCCESS)
    {
        return false;
    }

    /* 4) Serveur GATT et rôle « périphérique » (la carte attend qu'on se
     *    connecte à elle), puis nom de l'appareil.                         */
    if (aci_gatt_init() != BLE_STATUS_SUCCESS)
    {
        return false;
    }
    if (aci_gap_init_IDB05A1(GAP_PERIPHERAL_ROLE_IDB05A1, 0U, NAME_LEN, &gap_service,
                             &dev_name_char, &appearance_char) != BLE_STATUS_SUCCESS)
    {
        return false;
    }
    if (aci_gatt_update_char_value(gap_service, dev_name_char, 0U, NAME_LEN,
                                   ble.name) != BLE_STATUS_SUCCESS)
    {
        return false;
    }

    /* 5) Notre service, la puissance d'émission et la caractéristique INFO. */
    if (!add_ota_service())
    {
        return false;
    }
    (void)aci_hal_set_tx_power_level(LOADER_TX_HIGH_POWER, LOADER_TX_PA_LEVEL);
    refresh_info();

    /* 6) Le module envoie un événement « module démarré » après chacun de
     *    ses resets (étapes 1 et 2). On lit maintenant les événements reçus
     *    pendant l'initialisation : ble.ready vaut encore false, ils sont
     *    donc ignorés et ne seront pas pris pour un redémarrage imprévu.   */
    hci_user_evt_proc();

    /* 7) Les annonces seront lancées au premier appel de ble_ota_process(). */
    ble.ready = true;
    ble.advertise_pending = true;
    ble.last_adv_try = HAL_GetTick() - LOADER_ADV_RETRY_MS;
    return true;
}

void ble_ota_process(void)
{
    if (!ble.ready)
    {
        return;
    }

    poll_events();

    /* Le module a redémarré tout seul : on refait toute l'initialisation
     * (la cause reste notée dans « diag », le PC la lira dans INFO).      */
    if (ble.module_restarted)
    {
        (void)ble_ota_init();
        return;
    }

    if (ble.advertise_pending && !ble.connected
        && ((HAL_GetTick() - ble.last_adv_try) >= LOADER_ADV_RETRY_MS))
    {
        ble.last_adv_try = HAL_GetTick();
        start_advertising();
    }
}

bool ble_ota_is_ready(void)
{
    return ble.ready;
}

bool ble_ota_is_connected(void)
{
    return ble.connected;
}

void ble_ota_disconnect(void)
{
    if (!ble.ready || !ble.connected)
    {
        return;
    }

    /* 1) On laisse d'abord le PC se déconnecter lui-même. Pourquoi ? La
     *    commande RUN est une écriture « avec accusé de réception » : le
     *    module renvoie l'accusé au PC à l'échange radio suivant. Si la
     *    carte coupait la liaison tout de suite, le module pourrait la
     *    couper AVANT d'avoir envoyé cet accusé, et le PC croirait à une
     *    panne (Windows signale alors une « opération annulée »).         */
    wait_disconnection(LOADER_PC_DISCONNECT_WAIT_MS);

    /* 2) Le PC est toujours connecté : c'est la carte qui coupe.         */
    if (ble.connected)
    {
        (void)aci_gap_terminate(ble.conn_handle, HCI_OE_USER_ENDED_CONNECTION);
        wait_disconnection(LOADER_DISCONNECT_WAIT_MS);
    }
}

const char *ble_ota_get_name(void)
{
    return ble.name;
}
