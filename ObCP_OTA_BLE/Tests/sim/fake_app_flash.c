/**
 * @file    fake_app_flash.c
 * @brief   Flash SIMULÉE pour les tests sur PC : remplace app_flash.c (qui
 *          utilise la HAL du STM32) par un tableau de 512 Ko en mémoire.
 *
 * La simulation respecte les règles de la vraie flash du STM32L4 :
 *  - effacement par pages de 2 Ko (octets remis à 0xFF) ;
 *  - écriture par double mot de 8 octets, adresse multiple de 8 ;
 *  - écriture refusée si la flash est verrouillée ou si le double mot n'est
 *    pas effacé (la vraie flash lèverait l'erreur PROGERR) ;
 * et permet d'injecter des pannes (effacement ou écriture qui échoue).
 */

#include <string.h>

#include "app_flash.h"
#include "loader_config.h"
#include "sim_api.h"

#define SIM_ERR_LOCKED     0x01U   /* écriture alors que la flash est verrouillée */
#define SIM_ERR_NOT_ERASED 0x08U   /* équivalent de HAL_FLASH_ERROR_PROG          */
#define SIM_ERR_INJECTED   0x80U   /* panne injectée par le test                  */

static uint8_t  flash[APP_MAX_SIZE];
static bool     unlocked = false;
static uint32_t last_error = 0U;
static int      fail_erase = 0;          /* 1 : le prochain effacement échoue        */
static long     fail_program_at = -1;    /* position du double mot qui échouera      */
static uint32_t erase_count = 0U;        /* nombre d'effacements demandés           */
static uint32_t program_count = 0U;      /* nombre de doubles mots écrits           */
static long     first_dword_write = -1;  /* rang de l'écriture du double mot 0      */

/* ---------- API de app_flash.h ------------------------------------------- */

bool app_flash_erase(uint32_t size)
{
    if ((size == 0U) || (size > APP_MAX_SIZE))
    {
        return false;
    }
    erase_count++;
    unlocked = true;
    if (fail_erase)
    {
        fail_erase = 0;
        last_error = SIM_ERR_INJECTED;
        unlocked = false;
        return false;
    }
    uint32_t pages = (uint32_t)((size + LOADER_FLASH_PAGE_SIZE - 1U) / LOADER_FLASH_PAGE_SIZE);
    memset(flash, 0xFF, pages * LOADER_FLASH_PAGE_SIZE);
    last_error = 0U;
    return true;
}

bool app_flash_program(uint32_t offset, const uint8_t data[8])
{
    if (((offset % 8U) != 0U) || (offset > (APP_MAX_SIZE - 8U)))
    {
        return false;
    }
    if (!unlocked)
    {
        last_error = SIM_ERR_LOCKED;
        return false;
    }
    if ((fail_program_at >= 0) && ((long)offset == fail_program_at))
    {
        fail_program_at = -1;
        last_error = SIM_ERR_INJECTED;
        return false;
    }
    for (uint32_t i = 0U; i < 8U; i++)
    {
        if (flash[offset + i] != 0xFFU)
        {
            last_error = SIM_ERR_NOT_ERASED;
            return false;
        }
    }
    memcpy(&flash[offset], data, 8U);
    if (offset == 0U)
    {
        first_dword_write = (long)program_count;
    }
    program_count++;
    return true;
}

const uint8_t *app_flash_read(uint32_t offset)
{
    return &flash[offset];
}

void app_flash_lock(void)
{
    unlocked = false;
}

void app_flash_flush_caches(void)
{
}

uint32_t app_flash_last_error(void)
{
    return last_error;
}

/* ---------- Fonctions réservées aux tests (sim_api.h) ------------------- */

void sim_flash_reset(uint8_t fill)
{
    memset(flash, fill, sizeof(flash));
    unlocked = false;
    last_error = 0U;
    fail_erase = 0;
    fail_program_at = -1;
    erase_count = 0U;
    program_count = 0U;
    first_dword_write = -1;
}

void sim_flash_write_raw(uint32_t offset, const uint8_t *data, uint32_t len)
{
    memcpy(&flash[offset], data, len);
}

const uint8_t *sim_flash_data(void)
{
    return flash;
}

void sim_flash_fail_next_erase(void)
{
    fail_erase = 1;
}

void sim_flash_fail_program_at(long offset)
{
    fail_program_at = offset;
}

bool sim_flash_is_unlocked(void)
{
    return unlocked;
}

uint32_t sim_flash_erase_count(void)
{
    return erase_count;
}

uint32_t sim_flash_program_count(void)
{
    return program_count;
}

long sim_flash_first_dword_write_rank(void)
{
    return first_dword_write;
}
