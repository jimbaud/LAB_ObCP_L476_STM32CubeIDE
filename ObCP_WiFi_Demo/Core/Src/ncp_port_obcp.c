/**
 ******************************************************************************
 * @file    ncp_port_obcp.c
 * @brief   Adaptation du pilote ST67W6X_Lite à l'ObCP (voir
 *          ncp_port.h et le brochage dans board.h).
 *
 *  C'est le seul fichier du pilote qui touche au matériel : quatre broches
 *  (CHIP_EN, BOOT, CS, RDY), le bus SPI1 (initialisé par MX_SPI1_Init(),
 *  généré par CubeMX) et l'horloge système (SysTick, 1 ms). Pour porter le
 *  pilote sur une autre carte, seul ce fichier est à réécrire.
 ******************************************************************************
 */
#include "ncp_port.h"
#include "board.h"

void ncp_port_chip_enable(bool on)
{
  HAL_GPIO_WritePin(NCP_EN_PORT, NCP_EN_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void ncp_port_boot(bool on)
{
  HAL_GPIO_WritePin(NCP_BOOT_PORT, NCP_BOOT_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void ncp_port_cs(bool active)
{
  HAL_GPIO_WritePin(NCP_CS_PORT, NCP_CS_PIN, active ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool ncp_port_rdy(void)
{
  return HAL_GPIO_ReadPin(NCP_RDY_PORT, NCP_RDY_PIN) == GPIO_PIN_SET;
}

/* Transfert par la HAL (mode bloquant, sans DMA) : la fonction ne rend la
 * main qu'une fois les len octets échangés ou le délai de garde écoulé.  */
int ncp_port_spi_txrx(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
  /* 10 Mbit/s : 2 Ko en ~1,6 ms ; marge large pour le délai de garde */
  uint32_t timeout = 20U + ((uint32_t)len / 256U);

  if (len == 0U)
  {
    return 0;
  }
  return (HAL_SPI_TransmitReceive(&hspi1, (uint8_t *)tx, rx, len, timeout) == HAL_OK) ? 0 : -1;
}

uint32_t ncp_port_tick(void)
{
  return HAL_GetTick();
}

void ncp_port_delay(uint32_t ms)
{
  HAL_Delay(ms);
}
