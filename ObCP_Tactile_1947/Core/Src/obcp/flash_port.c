/**
 * @file flash_port.c
 * @brief Adaptation du stockage aux banques et à l’ECC STM32L476.
 * @ingroup obcp_storage
 *
 * Les adresses logiques sont des offsets dans la réserve du linker. La programmation porte
 * sur huit octets alignés. Les opérations physiques sont synchrones et exécutées dans la
 * boucle principale. Le seul chemin d’interruption est le signal ECCD en NMI.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
/** @def BANK_2_ADDRESS
 * @brief Début physique de la banque 2 à 0x08080000 sur le L476 de 1 Mio.
 */
/** @var __obcp_storage_start
 * @brief Symbole du linker. Son adresse, et non sa valeur, désigne le début de la réserve.
 */
/** @var __obcp_storage_end
 * @brief Symbole du linker dont l'adresse est la borne exclusive de la réserve.
 */
/** @var ecc_failed
 * @brief Indicateur volatile posé en NMI si une lecture déclenche une erreur ECCD.
 */

#include "obcp_board.h"
#include "obcp_flash.h"

#include "main.h"
#include <string.h>

#define BANK_2_ADDRESS UINT32_C(0x08080000)

extern const uint8_t __obcp_storage_start;
extern const uint8_t __obcp_storage_end;
static volatile bool ecc_failed;

/*
 * Vérifier la géométrie réelle avant de modifier la flash.
 *
 * Contrôle capacité 1 Mio, absence de permutation des banques, pages de 2 Kio et bornes de
 * la réserve. Ces contrôles relient les hypothèses du linker au composant lu.
 */
bool obcp_flash_ready(void)
{
    uintptr_t start = (uintptr_t)&__obcp_storage_start;
    uintptr_t end = (uintptr_t)&__obcp_storage_end;
    /* Sur le L476 à 1 Mio, les deux banques font 512 Kio. DUALBANK
     * concerne les variantes 256/512 Kio. FB_MODE doit rester à zéro. */
    return *(const volatile uint16_t *)FLASHSIZE_BASE == 1024U &&
           (SYSCFG->MEMRMP & SYSCFG_MEMRMP_FB_MODE) == 0U &&
           FLASH_PAGE_SIZE == OBCP_FLASH_PAGE &&
           start >= BANK_2_ADDRESS && end <= UINT32_C(0x08100000) &&
           end - start == OBCP_STORAGE_BYTES && start % OBCP_FLASH_PAGE == 0U;
}

/*
 * Reconnaître une NMI provoquée par une erreur ECC double.
 *
 * Le bit ECCD est acquitté et un indicateur volatile mémorise l'erreur pour la lecture en
 * cours. Les autres causes de NMI restent à traiter par le gestionnaire généré.
 */
bool obcp_flash_ecc_nmi(void)
{
    if ((FLASH->ECCR & FLASH_ECCR_ECCD) == 0U) {
        return false;
    }
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ECCD);
    ecc_failed = true;
    return true;
}

/**
 * @brief Vérifier une plage d'offsets dans la réserve.
 *
 * Le test utilise count inférieur à la taille restante, après le contrôle d'offset. Cette
 * forme évite un débordement de offset + count.
 * @param[in] offset Offset en octets depuis le début de la réserve flash.
 * @param[in] count Nombre d’octets de l’opération ou de la transaction.
 * @return true si toute la plage reste dans la réserve.
 */
static bool in_range(uint32_t offset, size_t count)
{
    return offset <= OBCP_STORAGE_BYTES && count <= OBCP_STORAGE_BYTES - offset;
}

/**
 * @brief Lire une plage mémoire et surveiller son ECC.
 *
 * Calcule l'adresse à partir du symbole du linker, copie vers le tampon puis consulte
 * l'indicateur NMI. Le contexte opaque du port n'est pas utilisé.
 * @param[in] context Contexte opaque transmis sans allocation. Le port physique peut ne pas
 *   l’utiliser.
 * @param[in] offset Offset en octets depuis le début de la réserve flash.
 * @param[out] data Tampon de destination valide. Sa capacité est donnée par size, capacity
 *   ou count.
 * @param[in] count Nombre d’octets de l’opération ou de la transaction.
 * @return true si les bornes et la lecture ECC sont valides.
 */
static bool flash_read(void *context, uint32_t offset, void *data, size_t count)
{
    (void)context;
    if (!in_range(offset, count)) {
        return false;
    }
    ecc_failed = false;
    const void *source = (const void *)((uintptr_t)&__obcp_storage_start + offset);
    memcpy(data, source, count);
    return !ecc_failed;
}

/**
 * @brief Effacer un nombre entier de pages en banque 2.
 *
 * Contrôle géométrie et alignement, déverrouille la flash, convertit l'adresse en numéro
 * de page de banque 2, efface puis reverrouille.
 * @param[in] context Contexte opaque transmis sans allocation. Le port physique peut ne pas
 *   l’utiliser.
 * @param[in] offset Offset en octets depuis le début de la réserve flash.
 * @param[in] count Nombre d’octets de l’opération ou de la transaction.
 * @return true si tous les contrôles et l'effacement réussissent.
 */
static bool flash_erase(void *context, uint32_t offset, uint32_t count)
{
    (void)context;
    if (!obcp_flash_ready() || !in_range(offset, count) ||
        offset % OBCP_FLASH_PAGE != 0U || count == 0U || count % OBCP_FLASH_PAGE != 0U ||
        HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }
    uint32_t addr = (uint32_t)(uintptr_t)&__obcp_storage_start + offset;
    FLASH_EraseInitTypeDef params = {
        .TypeErase = FLASH_TYPEERASE_PAGES, .Banks = FLASH_BANK_2,
        .Page = (addr - BANK_2_ADDRESS) / OBCP_FLASH_PAGE,
        .NbPages = count / OBCP_FLASH_PAGE
    };
    uint32_t bad_page;
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
    HAL_StatusTypeDef result = HAL_FLASHEx_Erase(&params, &bad_page);
    (void)HAL_FLASH_Lock();
    return result == HAL_OK;
}

/**
 * @brief Programmer exactement un double mot aligné.
 *
 * Copie les huit octets vers un uint64_t local pour respecter l'alignement, effectue la
 * programmation puis reverrouille le contrôleur.
 * @param[in] context Contexte opaque transmis sans allocation. Le port physique peut ne pas
 *   l’utiliser.
 * @param[in] offset Offset en octets depuis le début de la réserve flash.
 * @param[in] bytes Huit octets source du double mot à programmer.
 * @return true si la programmation HAL réussit.
 */
static bool flash_program(void *context, uint32_t offset, const uint8_t bytes[8])
{
    (void)context;
    if (!obcp_flash_ready() || !in_range(offset, 8U) || (offset & 7U) != 0U ||
        HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }
    uint64_t word;
    memcpy(&word, bytes, sizeof(word));
    uint32_t addr = (uint32_t)(uintptr_t)&__obcp_storage_start + offset;
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
    HAL_StatusTypeDef result = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, word);
    (void)HAL_FLASH_Lock();
    return result == HAL_OK;
}

/*
 * Construire le port utilisé par storage.c.
 *
 * Le port contient les trois callbacks synchrones. Un port nul est renvoyé si les
 * hypothèses de placement ne correspondent pas au composant.
 */
Storage obcp_flash_storage(void)
{
    if (!obcp_flash_ready()) {
        return (Storage){0};
    }
    return (Storage){NULL, flash_read, flash_erase, flash_program};
}
