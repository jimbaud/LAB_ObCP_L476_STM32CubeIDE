/**
 ******************************************************************************
 * @file    app_flash.c
 * @brief   Accès à la flash de la zone application avec la bibliothèque HAL.
 ******************************************************************************
 *
 *  Fonctions HAL utilisées (voir le manuel UM1884 de la HAL STM32L4) :
 *   - HAL_FLASH_Unlock() / HAL_FLASH_Lock()    : (dé)verrouillage ;
 *   - HAL_FLASHEx_Erase()                      : effacement de pages ;
 *   - HAL_FLASH_Program()                      : écriture d'un double mot ;
 *   - HAL_FLASH_GetError()                     : code de la dernière erreur.
 ******************************************************************************
 */

#include <string.h>

#include "main.h"            /* HAL et définitions du STM32L476 */
#include "app_flash.h"
#include "loader_config.h"

/* Vérifications faites par le compilateur : si quelqu'un modifie
 * loader_config.h de façon incohérente, la compilation s'arrête ici avec un
 * message clair au lieu de produire un chargeur défectueux.               */
_Static_assert(LOADER_FLASH_PAGE_SIZE == FLASH_PAGE_SIZE,
               "LOADER_FLASH_PAGE_SIZE doit valoir FLASH_PAGE_SIZE (2 Ko)");
_Static_assert(APP_ADDRESS == (FLASH_BANK1_END + 1UL),
               "La zone application doit commencer au debut de la banque 2");
_Static_assert((APP_ADDRESS + APP_MAX_SIZE - 1UL) == FLASH_END,
               "La zone application doit finir a la fin de la flash");

static uint32_t last_error = 0U;   /* mémorise la cause du dernier échec */

bool app_flash_erase(uint32_t size)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0U;      /* reçoit le numéro de la page en défaut */

    if ((size == 0U) || (size > APP_MAX_SIZE))
    {
        return false;
    }

    /* Nombre de pages nécessaires, arrondi au-dessus :
     * par exemple 2049 octets demandent 2 pages.                           */
    uint32_t nb_pages = (size + LOADER_FLASH_PAGE_SIZE - 1U) / LOADER_FLASH_PAGE_SIZE;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks     = FLASH_BANK_2;          /* banque 2 = zone application   */
    erase.Page      = 0U;                    /* première page de la banque 2  */
    erase.NbPages   = nb_pages;

    HAL_FLASH_Unlock();

    /* Des indicateurs d'erreur peuvent rester levés depuis le démarrage
     * (OPTVERR en particulier) ; tant qu'ils sont présents, la HAL refuse
     * de programmer. On les efface donc avant de commencer.               */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        last_error = HAL_FLASH_GetError();
        HAL_FLASH_Lock();
        return false;
    }

    last_error = 0U;
    return true;                             /* la flash reste déverrouillée */
}

bool app_flash_program(uint32_t offset, const uint8_t data[8])
{
    uint64_t double_word;

    /* Refus de toute écriture mal alignée ou hors de la zone application. */
    if (((offset % 8U) != 0U) || (offset > (APP_MAX_SIZE - 8U)))
    {
        return false;
    }

    /* Les 8 octets reçus sont recopiés dans un entier de 64 bits. Le
     * Cortex-M4 étant petit-boutiste, l'octet data[0] se retrouvera bien à
     * l'adresse la plus basse une fois écrit en flash.                     */
    memcpy(&double_word, data, sizeof(double_word));

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, APP_ADDRESS + offset,
                          double_word) != HAL_OK)
    {
        last_error = HAL_FLASH_GetError();
        return false;
    }
    return true;
}

const uint8_t *app_flash_read(uint32_t offset)
{
    return (const uint8_t *)(APP_ADDRESS + offset);
}

void app_flash_lock(void)
{
    HAL_FLASH_Lock();
}

void app_flash_flush_caches(void)
{
    /* Séquence décrite dans le manuel de référence RM0351 (chapitre de la
     * mémoire flash, partie sur les caches) : désactiver le cache de
     * données, le remettre à zéro, puis le réactiver.                      */
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();
}

uint32_t app_flash_last_error(void)
{
    return last_error;
}
