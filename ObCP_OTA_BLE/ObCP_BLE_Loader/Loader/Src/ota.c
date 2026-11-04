/**
 ******************************************************************************
 * @file    ota.c
 * @brief   Cœur du transfert OTA (voir ota.h pour la machine à états).
 ******************************************************************************
 *
 *  COMMENT LES OCTETS ARRIVENT EN FLASH
 *  ------------------------------------
 *  Le PC envoie le programme par paquets de 19 octets, mais la flash ne
 *  s'écrit que par double mot de 8 octets. Les octets reçus sont donc
 *  rangés un par un dans un petit tampon de 8 octets (« dword ») ; dès qu'il
 *  est plein, il est écrit en flash et l'on recommence.
 *
 *  Exception volontaire : le TOUT PREMIER double mot (pointeur de pile et
 *  adresse de Reset_Handler) est gardé en RAM et n'est écrit qu'à la fin,
 *  après la vérification du CRC. Tant qu'il n'est pas écrit, la zone
 *  application commence par 0xFFFFFFFF et le chargeur la considère comme
 *  vide : un transfert interrompu ne peut pas lancer un programme incomplet.
 *
 *  POURQUOI LE PC ATTEND UN ACCUSÉ TOUS LES 1024 OCTETS ?
 *  ------------------------------------------------------
 *  Les événements du module BLE sont rangés dans une petite file d'attente
 *  (5 places dans la bibliothèque X-CUBE-BLE1). Pendant l'envoi d'une
 *  commande au module (une notification par exemple), cette bibliothèque
 *  peut abandonner des événements si la file est pleine. Le PC s'arrête donc
 *  après chaque bloc et attend l'accusé (ACK) : quand le chargeur envoie
 *  l'ACK, aucun paquet de données n'est en route, rien ne peut se perdre.
 *  Le numéro de séquence et le CRC final restent là comme filets de sécurité.
 ******************************************************************************
 */

#include <string.h>

#include "ota.h"
#include "app_flash.h"
#include "app_image.h"
#include "crc32.h"
#include "loader_config.h"

/* ------------------------------------------------------------------------- */
/* Variables du module, regroupées dans une structure.                       */
/* Le mot-clé « static » les rend invisibles des autres fichiers : seules    */
/* les fonctions de ota.c peuvent les modifier.                              */
/* ------------------------------------------------------------------------- */
static struct
{
    ota_notify_fn notify;          /* fonction de réponse au PC               */
    ota_state_t   state;           /* état courant                            */
    uint32_t      size;            /* taille annoncée par START               */
    uint32_t      expected_crc;    /* CRC-32 annoncé par START                */
    uint32_t      received;        /* octets reçus depuis START               */
    uint8_t       next_seq;        /* numéro de séquence attendu              */
    uint8_t       dword[8];        /* double mot en cours de remplissage      */
    uint8_t       dword_fill;      /* nombre d'octets déjà dans dword         */
    uint32_t      dword_offset;    /* position du double mot dans l'image     */
    uint8_t       first_dword[8];  /* 8 premiers octets, écrits en dernier    */
    bool          run_request;     /* commande RUN reçue                      */
    bool          identify_request;/* commande IDENTIFY reçue                 */
    bool          activity;        /* un ACK vient d'être envoyé              */
} ota;

/* ------------------------------------------------------------------------- */
/* Petites fonctions utilitaires                                             */
/* ------------------------------------------------------------------------- */

/* Lit un entier de 32 bits petit-boutiste (octet de poids faible en premier). */
static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* Écrit un entier de 32 bits en petit-boutiste. */
static void write_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value);
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

/* Envoie une réponse au PC (si une fonction de réponse est enregistrée). */
static void respond(uint8_t code, uint32_t value)
{
    if (ota.notify != NULL)
    {
        ota.notify(code, value);
    }
}

/* Remet à zéro les compteurs d'un transfert (sans toucher à la flash). */
static void reset_session(void)
{
    ota.state        = OTA_STATE_IDLE;
    ota.size         = 0U;
    ota.expected_crc = 0U;
    ota.received     = 0U;
    ota.next_seq     = 0U;
    ota.dword_fill   = 0U;
    ota.dword_offset = 0U;
    memset(ota.dword, 0xFF, sizeof(ota.dword));
    memset(ota.first_dword, 0xFF, sizeof(ota.first_dword));
}

/* Passe en erreur : reverrouille la flash et prévient le PC. */
static void fail(uint8_t error_code, uint32_t value)
{
    ota.state = OTA_STATE_ERROR;
    app_flash_lock();
    respond(error_code, value);
}

/* Range le double mot complet : en RAM s'il s'agit du premier (il sera écrit
 * en dernier), en flash sinon. Renvoie false si l'écriture flash échoue.   */
static bool store_dword(void)
{
    if (ota.dword_offset == 0U)
    {
        memcpy(ota.first_dword, ota.dword, sizeof(ota.first_dword));
    }
    else if (!app_flash_program(ota.dword_offset, ota.dword))
    {
        return false;
    }

    ota.dword_offset += 8U;
    ota.dword_fill = 0U;
    memset(ota.dword, 0xFF, sizeof(ota.dword));
    return true;
}

/* ------------------------------------------------------------------------- */
/* Traitement des commandes                                                  */
/* ------------------------------------------------------------------------- */

/* START : [0x01][taille sur 4 octets][CRC-32 sur 4 octets] */
static void handle_start(const uint8_t *data, uint16_t len)
{
    if (len != OTA_START_LEN)
    {
        fail(OTA_ERR_COMMAND, len);
        return;
    }

    uint32_t size = read_u32(&data[1]);
    uint32_t crc  = read_u32(&data[5]);

    reset_session();                 /* START recommence toujours de zéro */

    /* Il faut au moins la table des vecteurs (8 octets) et pas plus que la
     * banque 2.                                                           */
    if ((size < 8U) || (size > APP_MAX_SIZE))
    {
        fail(OTA_ERR_SIZE, size);
        return;
    }

    /* Effacement des pages qui recevront le programme. Comme la page 0 est
     * effacée, l'ancienne application devient invalide dès maintenant.    */
    if (!app_flash_erase(size))
    {
        fail(OTA_ERR_FLASH, app_flash_last_error());
        return;
    }

    ota.size         = size;
    ota.expected_crc = crc;
    ota.state        = OTA_STATE_RECEIVING;
    respond(OTA_RSP_READY, size);
}

/* END : toutes les données sont arrivées, on vérifie puis on valide. */
static void handle_end(void)
{
    if (ota.state != OTA_STATE_COMPLETE)
    {
        if (ota.state == OTA_STATE_RECEIVING)
        {
            fail(OTA_ERR_INCOMPLETE, ota.received);      /* END trop tôt */
        }
        else
        {
            fail(OTA_ERR_STATE, (uint32_t)ota.state);
        }
        return;
    }

    /* 1) CRC de l'image : les 8 premiers octets sont en RAM, le reste est
     *    relu directement dans la flash (on vérifie ainsi ce qui a vraiment
     *    été écrit, pas seulement ce qui a été reçu).                      */
    uint32_t crc = crc32_update(0U, ota.first_dword, 8U);
    crc = crc32_update(crc, app_flash_read(8U), ota.size - 8U);
    if (crc != ota.expected_crc)
    {
        fail(OTA_ERR_CRC, crc);
        return;
    }

    /* 2) La table des vecteurs doit correspondre à un programme lié pour
     *    l'adresse 0x08080000 (sinon il planterait au démarrage).          */
    uint32_t initial_sp    = read_u32(&ota.first_dword[0]);
    uint32_t reset_handler = read_u32(&ota.first_dword[4]);
    if (!app_image_vectors_ok(initial_sp, reset_handler, ota.size))
    {
        fail(OTA_ERR_IMAGE, reset_handler);
        return;
    }

    /* 3) Validation : écriture du premier double mot, puis verrouillage.   */
    if (!app_flash_program(0U, ota.first_dword))
    {
        fail(OTA_ERR_FLASH, app_flash_last_error());
        return;
    }
    app_flash_lock();
    app_flash_flush_caches();

    /* 4) Relecture de contrôle.                                           */
    if ((memcmp(app_flash_read(0U), ota.first_dword, 8U) != 0) || !app_image_is_installed())
    {
        fail(OTA_ERR_FLASH, 0U);
        return;
    }

    ota.state = OTA_STATE_DONE;
    respond(OTA_RSP_DONE, crc);
}

/* ------------------------------------------------------------------------- */
/* Fonctions publiques (déclarées dans ota.h)                                */
/* ------------------------------------------------------------------------- */

void ota_init(ota_notify_fn notify)
{
    memset(&ota, 0, sizeof(ota));
    ota.notify = notify;
    reset_session();
}

void ota_on_control(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U))
    {
        fail(OTA_ERR_COMMAND, 0U);
        return;
    }

    switch (data[0])
    {
    case OTA_CMD_START:
        handle_start(data, len);
        break;

    case OTA_CMD_END:
        if (len != 1U)
        {
            fail(OTA_ERR_COMMAND, len);
        }
        else
        {
            handle_end();
        }
        break;

    case OTA_CMD_ABORT:
        reset_session();             /* retour à IDLE, sans réponse */
        app_flash_lock();
        break;

    case OTA_CMD_RUN:
        ota.run_request = true;      /* le redémarrage est fait par loader.c */
        break;

    case OTA_CMD_IDENTIFY:
        ota.identify_request = true; /* le clignotement est fait par loader.c */
        break;

    default:
        fail(OTA_ERR_COMMAND, data[0]);   /* code de commande inconnu */
        break;
    }
}

void ota_on_data(const uint8_t *data, uint16_t len)
{
    /* Les paquets ne sont attendus que pendant la réception.               */
    if (ota.state != OTA_STATE_RECEIVING)
    {
        if (ota.state == OTA_STATE_COMPLETE)
        {
            fail(OTA_ERR_OVERFLOW, ota.received + ((len > 0U) ? (uint32_t)len - 1U : 0U));
        }
        else if (ota.state != OTA_STATE_ERROR)
        {
            fail(OTA_ERR_STATE, (uint32_t)ota.state);
        }
        /* En état ERROR on ignore sans répondre : l'erreur a déjà été
         * signalée et le PC va s'arrêter.                                  */
        return;
    }

    /* Un paquet contient le numéro de séquence puis 1 à 19 octets.        */
    if ((data == NULL) || (len < 2U) || (len > OTA_DATA_MAX_LEN))
    {
        fail(OTA_ERR_COMMAND, len);
        return;
    }

    /* Numéro de séquence : il doit suivre exactement le précédent.        */
    if (data[0] != ota.next_seq)
    {
        fail(OTA_ERR_SEQUENCE, ota.next_seq);
        return;
    }
    ota.next_seq++;                  /* 255 + 1 repasse à 0 (uint8_t) */

    const uint8_t *payload = &data[1];
    uint32_t count = (uint32_t)len - 1U;

    if ((ota.received + count) > ota.size)
    {
        fail(OTA_ERR_OVERFLOW, ota.received + count);
        return;
    }

    /* Rangement octet par octet dans le double mot en cours.              */
    for (uint32_t i = 0U; i < count; i++)
    {
        ota.dword[ota.dword_fill] = payload[i];
        ota.dword_fill++;
        if ((ota.dword_fill == 8U) && !store_dword())
        {
            fail(OTA_ERR_FLASH, app_flash_last_error());
            return;
        }
    }
    ota.received += count;

    /* Dernier octet reçu : on complète le dernier double mot avec 0xFF
     * (la valeur d'une flash effacée) et on l'écrit.                       */
    if (ota.received == ota.size)
    {
        if ((ota.dword_fill > 0U) && !store_dword())
        {
            fail(OTA_ERR_FLASH, app_flash_last_error());
            return;
        }
        ota.state = OTA_STATE_COMPLETE;
    }

    /* Accusé de réception à chaque bloc complet, et à la fin.             */
    if (((ota.received % OTA_BLOCK_SIZE) == 0U) || (ota.received == ota.size))
    {
        ota.activity = true;
        respond(OTA_RSP_ACK, ota.received);
    }
}

void ota_on_disconnect(void)
{
    /* Un transfert inachevé est abandonné ; la flash est reverrouillée.
     * Un programme déjà validé (état DONE) reste bien sûr installé.
     * Une commande RUN déjà reçue reste valable : le PC se déconnecte
     * souvent juste après l'avoir envoyée.                                */
    reset_session();
    app_flash_lock();
}

ota_state_t ota_get_state(void)
{
    return ota.state;
}

uint32_t ota_get_received(void)
{
    return ota.received;
}

uint32_t ota_get_size(void)
{
    return ota.size;
}

bool ota_take_run_request(void)
{
    bool request = ota.run_request;
    ota.run_request = false;
    return request;
}

bool ota_take_identify_request(void)
{
    bool request = ota.identify_request;
    ota.identify_request = false;
    return request;
}

bool ota_take_activity(void)
{
    bool activity = ota.activity;
    ota.activity = false;
    return activity;
}

void ota_build_info(uint8_t info[OTA_INFO_LEN], uint8_t boot_reason)
{
    memset(info, 0, OTA_INFO_LEN);
    info[0] = (uint8_t)OTA_PROTOCOL_VERSION;
    info[1] = (uint8_t)LOADER_VERSION_MAJOR;
    info[2] = (uint8_t)LOADER_VERSION_MINOR;
    info[3] = app_image_is_installed() ? 1U : 0U;
    write_u32(&info[4], APP_ADDRESS);
    write_u32(&info[8], APP_MAX_SIZE);
    info[12] = (uint8_t)(OTA_BLOCK_SIZE & 0xFFU);
    info[13] = (uint8_t)(OTA_BLOCK_SIZE >> 8);
    info[14] = (uint8_t)OTA_DATA_PAYLOAD;
    info[15] = boot_reason;
}
