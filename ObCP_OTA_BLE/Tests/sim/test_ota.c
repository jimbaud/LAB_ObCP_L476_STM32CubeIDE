/**
 * @file    test_ota.c
 * @brief   Tests unitaires, sur PC, du cœur du chargeur : crc32.c,
 *          app_image.c et ota.c (les fichiers RÉELS du chargeur), avec la
 *          flash simulée de fake_app_flash.c.
 *
 * Compilation et exécution : voir Tests/run_tests.sh (gcc avec
 * AddressSanitizer et UndefinedBehaviorSanitizer, qui détectent les
 * débordements de tableau et les opérations indéfinies du C).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_image.h"
#include "crc32.h"
#include "loader_config.h"
#include "ota.h"
#include "ota_protocol.h"
#include "sim_api.h"

/* ------------------------------------------------------------------------- */
/* Mini-cadre de test                                                        */
/* ------------------------------------------------------------------------- */
static int checks = 0;
static int failures = 0;

#define CHECK(cond) do {                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("  ECHEC ligne %d : %s\n", __LINE__, #cond);              \
        }                                                                    \
    } while (0)

/* Réponses envoyées par ota.c (fonction « notify »). */
typedef struct { uint8_t code; uint32_t value; } response_t;
static response_t responses[4096];
static int nb_responses = 0;

static void capture(uint8_t code, uint32_t value)
{
    if (nb_responses < (int)(sizeof(responses) / sizeof(responses[0])))
    {
        responses[nb_responses].code = code;
        responses[nb_responses].value = value;
    }
    nb_responses++;
}

static response_t last_response(void)
{
    response_t none = { 0xFF, 0xFFFFFFFFU };
    return (nb_responses > 0) ? responses[nb_responses - 1] : none;
}

static void reset_all(uint8_t fill)
{
    sim_flash_reset(fill);
    ota_init(capture);
    nb_responses = 0;
}

/* ------------------------------------------------------------------------- */
/* Fabrication d'images de test                                              */
/* ------------------------------------------------------------------------- */
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Image plausible : pile en RAM, Reset_Handler dans l'image, octets pseudo-aléatoires. */
static uint8_t *make_image(uint32_t size, uint32_t seed)
{
    uint8_t *img = malloc(size);
    uint32_t x = seed * 2654435761U + 1U;
    for (uint32_t i = 0; i < size; i++)
    {
        x = x * 1103515245U + 12345U;
        img[i] = (uint8_t)(x >> 16);
    }
    put_u32(&img[0], 0x20018000U);                       /* pile : fin de la SRAM1 */
    put_u32(&img[4], APP_ADDRESS + (size > 9U ? 9U : 1U)); /* Reset_Handler impair */
    return img;
}

static void send_start(uint32_t size, uint32_t crc)
{
    uint8_t cmd[OTA_START_LEN] = { OTA_CMD_START };
    put_u32(&cmd[1], size);
    put_u32(&cmd[5], crc);
    ota_on_control(cmd, sizeof(cmd));
}

static void send_cmd(uint8_t code)
{
    ota_on_control(&code, 1);
}

/* Envoie l'image comme le fait obcp_ota.py : paquets de 19 octets qui ne
 * chevauchent jamais deux blocs de 1024 octets. */
static uint8_t send_image(const uint8_t *img, uint32_t size, uint8_t seq)
{
    uint8_t packet[OTA_DATA_MAX_LEN];
    for (uint32_t block = 0; block < size; block += OTA_BLOCK_SIZE)
    {
        uint32_t block_end = (block + OTA_BLOCK_SIZE < size) ? block + OTA_BLOCK_SIZE : size;
        for (uint32_t pos = block; pos < block_end; pos += OTA_DATA_PAYLOAD)
        {
            uint32_t n = (block_end - pos < OTA_DATA_PAYLOAD) ? block_end - pos : OTA_DATA_PAYLOAD;
            packet[0] = seq++;
            memcpy(&packet[1], &img[pos], n);
            ota_on_data(packet, (uint16_t)(n + 1U));
        }
    }
    return seq;
}

static int flash_equals(const uint8_t *img, uint32_t size)
{
    const uint8_t *f = sim_flash_data();
    if (memcmp(f, img, size) != 0)
    {
        return 0;
    }
    /* Le reste du dernier double mot doit être à 0xFF (bourrage). */
    for (uint32_t i = size; i < ((size + 7U) & ~7U); i++)
    {
        if (f[i] != 0xFFU) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Tests                                                                     */
/* ------------------------------------------------------------------------- */
static void test_crc32(void)
{
    printf("CRC-32\n");
    const uint8_t v[] = "123456789";
    CHECK(crc32_update(0, v, 9) == 0xCBF43926U);          /* valeur de référence */
    CHECK(crc32_update(0, v, 0) == 0U);
    CHECK(crc32_update(crc32_update(0, v, 4), v + 4, 5) == 0xCBF43926U);  /* par morceaux */
    uint8_t zeros[32] = {0};
    CHECK(crc32_update(0, zeros, 32) == 0x190A55ADU);
}

static void test_vectors(void)
{
    printf("Table des vecteurs\n");
    CHECK(app_image_vectors_ok(0x20018000U, 0x08080305U, 7032U));
    CHECK(app_image_vectors_ok(0x20020000U, 0x08080009U, 16U));   /* fin de l'alias SRAM2 */
    CHECK(app_image_vectors_ok(0x10008000U, 0x08080009U, 16U));   /* fin de la SRAM2       */
    CHECK(!app_image_vectors_ok(0x20018002U, 0x08080305U, 7032U)); /* pile non alignée     */
    CHECK(!app_image_vectors_ok(0x20000000U, 0x08080305U, 7032U)); /* pile au tout début   */
    CHECK(!app_image_vectors_ok(0x30000000U, 0x08080305U, 7032U)); /* pile hors RAM        */
    CHECK(!app_image_vectors_ok(0xFFFFFFFFU, 0xFFFFFFFFU, 7032U)); /* flash effacée        */
    CHECK(!app_image_vectors_ok(0x20018000U, 0x08080304U, 7032U)); /* adresse paire        */
    CHECK(!app_image_vectors_ok(0x20018000U, 0x08000305U, 7032U)); /* lié pour 0x08000000  */
    CHECK(!app_image_vectors_ok(0x20018000U, 0x08081B79U, 7032U)); /* hors de l'image      */
}

static void test_transfer_ok(uint32_t size)
{
    printf("Transfert complet de %u octets\n", size);
    reset_all(0x5A);                         /* « ancienne » flash quelconque */
    uint8_t *img = make_image(size, size);
    uint32_t crc = crc32_update(0, img, size);

    send_start(size, crc);
    CHECK(last_response().code == OTA_RSP_READY);
    CHECK(last_response().value == size);
    CHECK(ota_get_state() == OTA_STATE_RECEIVING);
    CHECK(!app_image_is_installed());        /* page 0 effacée dès START */

    int before = nb_responses;
    send_image(img, size, 0);
    int expected_acks = (int)((size + OTA_BLOCK_SIZE - 1U) / OTA_BLOCK_SIZE);
    CHECK(nb_responses - before == expected_acks);
    for (int i = 0; i < expected_acks; i++)
    {
        uint32_t want = ((uint32_t)(i + 1) * OTA_BLOCK_SIZE < size) ? (uint32_t)(i + 1) * OTA_BLOCK_SIZE : size;
        CHECK(responses[before + i].code == OTA_RSP_ACK);
        CHECK(responses[before + i].value == want);
    }
    CHECK(ota_get_state() == OTA_STATE_COMPLETE);
    CHECK(!app_image_is_installed());        /* toujours invalide avant END */
    CHECK(ota_take_activity());
    CHECK(!ota_take_activity());             /* « take » : lu une seule fois */

    send_cmd(OTA_CMD_END);
    CHECK(last_response().code == OTA_RSP_DONE);
    CHECK(last_response().value == crc);
    CHECK(ota_get_state() == OTA_STATE_DONE);
    CHECK(app_image_is_installed());
    CHECK(flash_equals(img, size));
    CHECK(!sim_flash_is_unlocked());         /* flash reverrouillée */
    /* Le double mot 0 est écrit EN DERNIER. */
    CHECK(sim_flash_first_dword_write_rank() == (long)sim_flash_program_count() - 1);

    send_cmd(OTA_CMD_RUN);
    CHECK(ota_take_run_request());
    CHECK(!ota_take_run_request());
    free(img);
}

static void test_errors(void)
{
    uint32_t size = 5000U;
    uint8_t *img = make_image(size, 7U);
    uint32_t crc = crc32_update(0, img, size);
    uint8_t packet[OTA_DATA_MAX_LEN] = {0};

    printf("Taille refusée\n");
    reset_all(0xFF);
    send_start(0, 0);              CHECK(last_response().code == OTA_ERR_SIZE);
    send_start(7, 0);              CHECK(last_response().code == OTA_ERR_SIZE);
    send_start(APP_MAX_SIZE + 1U, 0); CHECK(last_response().code == OTA_ERR_SIZE);
    CHECK(last_response().value == APP_MAX_SIZE + 1U);
    CHECK(sim_flash_erase_count() == 0U);   /* rien n'a été effacé */

    printf("Commandes mal formées\n");
    reset_all(0xFF);
    uint8_t short_start[5] = { OTA_CMD_START, 1, 2, 3, 4 };
    ota_on_control(short_start, sizeof(short_start));
    CHECK(last_response().code == OTA_ERR_COMMAND);
    send_cmd(0x7E);                CHECK(last_response().code == OTA_ERR_COMMAND);
    ota_on_control(NULL, 0);       CHECK(last_response().code == OTA_ERR_COMMAND);
    uint8_t long_end[2] = { OTA_CMD_END, 0 };
    ota_on_control(long_end, 2);   CHECK(last_response().code == OTA_ERR_COMMAND);

    printf("Données hors transfert\n");
    reset_all(0xFF);
    ota_on_data(packet, 10);       CHECK(last_response().code == OTA_ERR_STATE);

    printf("Paquets de longueur invalide\n");
    reset_all(0xFF);
    send_start(size, crc);
    ota_on_data(packet, 1);        CHECK(last_response().code == OTA_ERR_COMMAND);
    send_start(size, crc);
    ota_on_data(packet, 21);       CHECK(last_response().code == OTA_ERR_COMMAND);

    printf("Paquet perdu (numéro de séquence)\n");
    reset_all(0xFF);
    send_start(size, crc);
    packet[0] = 0; memcpy(&packet[1], img, 19); ota_on_data(packet, 20);
    packet[0] = 2; memcpy(&packet[1], img + 19, 19); ota_on_data(packet, 20);   /* le n°1 manque */
    CHECK(last_response().code == OTA_ERR_SEQUENCE);
    CHECK(last_response().value == 1U);
    CHECK(ota_get_state() == OTA_STATE_ERROR);
    int n = nb_responses;
    packet[0] = 1; ota_on_data(packet, 20);  /* en erreur : ignoré sans réponse */
    CHECK(nb_responses == n);
    CHECK(!sim_flash_is_unlocked());

    printf("Nouveau START après une erreur\n");
    send_start(size, crc);
    CHECK(last_response().code == OTA_RSP_READY);
    send_image(img, size, 0);
    send_cmd(OTA_CMD_END);
    CHECK(last_response().code == OTA_RSP_DONE);
    CHECK(flash_equals(img, size));

    printf("Trop de données\n");
    reset_all(0xFF);
    send_start(size, crc);
    uint8_t seq = send_image(img, size, 0);
    packet[0] = seq; ota_on_data(packet, 5);
    CHECK(last_response().code == OTA_ERR_OVERFLOW);
    CHECK(last_response().value == size + 4U);
    reset_all(0xFF);
    send_start(20, crc);
    packet[0] = 0; ota_on_data(packet, 20);
    packet[0] = 1; ota_on_data(packet, 20);   /* 38 octets pour 20 annoncés */
    CHECK(last_response().code == OTA_ERR_OVERFLOW);

    printf("END trop tôt\n");
    reset_all(0xFF);
    send_start(size, crc);
    packet[0] = 0; memcpy(&packet[1], img, 19); ota_on_data(packet, 20);
    send_cmd(OTA_CMD_END);
    CHECK(last_response().code == OTA_ERR_INCOMPLETE);
    CHECK(last_response().value == 19U);
    CHECK(!app_image_is_installed());
    reset_all(0xFF);
    send_cmd(OTA_CMD_END);
    CHECK(last_response().code == OTA_ERR_STATE);

    printf("CRC faux\n");
    reset_all(0xFF);
    send_start(size, crc ^ 1U);
    send_image(img, size, 0);
    send_cmd(OTA_CMD_END);
    CHECK(last_response().code == OTA_ERR_CRC);
    CHECK(last_response().value == crc);
    CHECK(!app_image_is_installed());        /* le double mot 0 n'a pas été écrit */

    printf("Programme lié à la mauvaise adresse\n");
    reset_all(0xFF);
    uint8_t *bad = make_image(size, 9U);
    put_u32(&bad[4], 0x08000189U);           /* Reset_Handler en banque 1 */
    uint32_t bad_crc = crc32_update(0, bad, size);
    send_start(size, bad_crc);
    send_image(bad, size, 0);
    send_cmd(OTA_CMD_END);
    CHECK(last_response().code == OTA_ERR_IMAGE);
    CHECK(last_response().value == 0x08000189U);
    CHECK(!app_image_is_installed());
    free(bad);

    printf("Panne d'effacement\n");
    reset_all(0xFF);
    sim_flash_fail_next_erase();
    send_start(size, crc);
    CHECK(last_response().code == OTA_ERR_FLASH);
    CHECK(ota_get_state() == OTA_STATE_ERROR);

    printf("Panne d'écriture au milieu\n");
    reset_all(0xFF);
    sim_flash_fail_program_at(2048);
    send_start(size, crc);
    send_image(img, size, 0);
    CHECK(ota_get_state() == OTA_STATE_ERROR);
    int flash_errors = 0;
    for (int i = 0; i < nb_responses && i < 4096; i++)
    {
        if (responses[i].code == OTA_ERR_FLASH) flash_errors++;
    }
    CHECK(flash_errors == 1);
    CHECK(!app_image_is_installed());

    printf("ABORT et déconnexion pendant un transfert\n");
    reset_all(0xFF);
    send_start(size, crc);
    send_image(img, 2048, 0);
    send_cmd(OTA_CMD_ABORT);
    CHECK(ota_get_state() == OTA_STATE_IDLE);
    CHECK(!sim_flash_is_unlocked());
    CHECK(!app_image_is_installed());
    send_start(size, crc);
    send_image(img, 3000, 0);
    ota_on_disconnect();
    CHECK(ota_get_state() == OTA_STATE_IDLE);
    CHECK(!app_image_is_installed());

    printf("Une ancienne application est effacée par START\n");
    reset_all(0xFF);
    uint8_t *old = make_image(4096, 3U);
    sim_flash_write_raw(0, old, 4096);
    CHECK(app_image_is_installed());
    send_start(size, crc);
    CHECK(!app_image_is_installed());
    ota_on_disconnect();                     /* transfert abandonné */
    CHECK(!app_image_is_installed());        /* le chargeur restera actif */
    free(old);

    printf("RUN gardé après déconnexion, IDENTIFY\n");
    reset_all(0xFF);
    send_cmd(OTA_CMD_RUN);
    ota_on_disconnect();
    CHECK(ota_take_run_request());
    send_cmd(OTA_CMD_IDENTIFY);
    CHECK(ota_take_identify_request());
    CHECK(!ota_take_identify_request());
    CHECK(nb_responses == 0);                /* ni RUN ni IDENTIFY ne répondent */

    free(img);
}

static void test_info(void)
{
    printf("Caractéristique INFO\n");
    uint8_t info[OTA_INFO_LEN];
    reset_all(0xFF);
    ota_build_info(info, 2);
    CHECK(info[0] == OTA_PROTOCOL_VERSION);
    CHECK(info[1] == LOADER_VERSION_MAJOR && info[2] == LOADER_VERSION_MINOR);
    CHECK(info[3] == 0);                       /* pas d'application */
    CHECK((info[4] | info[5] << 8 | info[6] << 16 | (uint32_t)info[7] << 24) == APP_ADDRESS);
    CHECK((info[8] | info[9] << 8 | info[10] << 16 | (uint32_t)info[11] << 24) == APP_MAX_SIZE);
    CHECK((info[12] | info[13] << 8) == OTA_BLOCK_SIZE);
    CHECK(info[14] == OTA_DATA_PAYLOAD);
    CHECK(info[15] == 2);
    /* Octets de diagnostic : remplis par ble_ota.c, laissés à 0 par ota.c */
    CHECK(OTA_INFO_LEN == 18U);
    CHECK(info[OTA_INFO_DISCONNECT_REASON] == 0 && info[OTA_INFO_MODULE_RESTART] == 0);
    uint8_t *img = make_image(64, 1U);
    sim_flash_write_raw(0, img, 64);
    ota_build_info(info, 1);
    CHECK(info[3] == 1);                       /* application présente */
    free(img);
}

int main(void)
{
    test_crc32();
    test_vectors();
    const uint32_t sizes[] = { 8, 9, 19, 20, 1023, 1024, 1025, 4864, 7032, 65536, 100003, APP_MAX_SIZE };
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        test_transfer_ok(sizes[i]);
    }
    test_errors();
    test_info();
    printf("\n%d vérifications, %d échec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
