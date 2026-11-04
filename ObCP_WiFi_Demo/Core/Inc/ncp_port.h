/**
 ******************************************************************************
 * @file    ncp_port.h
 * @brief   Couche d'adaptation matérielle du pilote ST67W6X_Lite.
 *
 *          Le pilote (ncp_spi.c, ncp_at.c, ...) ne dépend que de ces quelques
 *          fonctions : il suffit de les réimplémenter pour porter le pilote sur
 *          une autre carte STM32. Sur l'ObCP, elles sont écrites dans
 *          Core/Src/ncp_port_obcp.c (broches nommées dans Core/Inc/board.h) ;
 *          pour les tests sur PC, dans Tests/sim_module.c (module simulé).
 *
 *          Signaux du ST67W611M1 (tous actifs à l'état HAUT) :
 *            - CHIP_EN : alimentation logique / reset du module
 *            - BOOT    : 0 = mode mission (fonctionnement normal)
 *            - SPI_CS  : sélection SPI (et réveil du module)
 *            - SPI_RDY : le module demande / accepte une transaction SPI
 ******************************************************************************
 */
#ifndef NCP_PORT_H
#define NCP_PORT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief  Commande la broche CHIP_EN (true : module en marche, false : en reset). */
void     ncp_port_chip_enable(bool on);

/** @brief  Commande la broche BOOT (true pendant un reset : chargeur en ROM du module). */
void     ncp_port_boot(bool on);

/** @brief  Commande la sélection SPI (true : CS actif, à l'état haut sur ce module). */
void     ncp_port_cs(bool active);

/**
 * @brief  Lit la broche RDY du module.
 * @retval true si le module a des données à envoyer ou accepte une transaction.
 */
bool     ncp_port_rdy(void);

/**
 * @brief  Transfert SPI full-duplex : len octets émis depuis tx pendant que
 *         len octets sont reçus dans rx (mode 0, poids fort en premier).
 * @param  tx   octets à émettre.
 * @param  rx   tampon des octets reçus (même longueur).
 * @param  len  nombre d'octets.
 * @retval 0 si OK, une valeur négative en cas d'erreur.
 */
int      ncp_port_spi_txrx(const uint8_t *tx, uint8_t *rx, uint16_t len);

/** @brief  Temps écoulé depuis le démarrage, en millisecondes (HAL_GetTick()). */
uint32_t ncp_port_tick(void);

/**
 * @brief  Attente de ms millisecondes (attente active : les interruptions,
 *         dont celles de l'USB, continuent d'être servies).
 */
void     ncp_port_delay(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* NCP_PORT_H */
