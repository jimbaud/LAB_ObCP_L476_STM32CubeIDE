/**
 ******************************************************************************
 * @file    app_image.c
 * @brief   Tests de validité d'une application (voir app_image.h).
 ******************************************************************************
 */

#include "app_image.h"
#include "app_flash.h"
#include "loader_config.h"

/* Lit un mot de 32 bits rangé en petit-boutiste à l'adresse p. On assemble
 * les octets un par un : cela fonctionne quelle que soit l'adresse (même non
 * alignée) et sur n'importe quel processeur (utile pour les tests sur PC).  */
static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

bool app_image_vectors_ok(uint32_t initial_sp, uint32_t reset_handler,
                          uint32_t image_size)
{
    /* 1) Le pointeur de pile doit désigner la RAM et être aligné sur 4 octets.
     *    On accepte la fin exacte d'une zone (la pile descend : le premier
     *    mot empilé sera juste en dessous).                                */
    bool sp_in_main_ram = (initial_sp > RAM_MAIN_START) && (initial_sp <= RAM_MAIN_END);
    bool sp_in_sram2    = (initial_sp > RAM_SRAM2_START) && (initial_sp <= RAM_SRAM2_END);
    bool sp_aligned     = ((initial_sp & 0x3UL) == 0UL);

    /* 2) Reset_Handler doit être dans l'image, et en mode Thumb (bit 0 = 1). */
    uint32_t code_start = APP_ADDRESS;
    uint32_t code_end   = APP_ADDRESS + image_size;      /* exclu */
    bool reset_in_image = (reset_handler >= code_start) && (reset_handler < code_end);
    bool reset_is_thumb = ((reset_handler & 0x1UL) == 1UL);

    return (sp_in_main_ram || sp_in_sram2) && sp_aligned
           && reset_in_image && reset_is_thumb;
}

bool app_image_is_installed(void)
{
    const uint8_t *vectors = app_flash_read(0U);   /* début de la banque 2 */

    return app_image_vectors_ok(read_u32(&vectors[0]), read_u32(&vectors[4]),
                                APP_MAX_SIZE);
}
