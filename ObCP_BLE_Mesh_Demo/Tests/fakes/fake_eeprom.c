/**
 ******************************************************************************
 * @file    Tests/fakes/fake_eeprom.c
 * @brief   Simulation sur PC de l'EEPROM SPI M95640 de l'ObCP (8 Ko, pages
 *          de 32 octets), sur le bus SPI2 partagé avec le module Bluetooth :
 *          commandes WREN, RDSR, READ, WRITE ; écriture effective à la
 *          remontée de CS, puis 3 lectures d'état « occupée » (WIP).
 ******************************************************************************
 */
#include <string.h>
#include "fake_board.h"
#include "stm32l4xx_nucleo_bus.h"

bool    fake_eeprom_present = true;
uint8_t fake_eeprom_mem[8192];
int     fake_eeprom_writes;
int     fake_eeprom_irq_violations;
int     fake_ota_enter_count;
const uint32_t g_pfnVectors[1];        /* voir fakes/obcp_ota_request.h */

static bool     s_selected;
static bool     s_wel;              /* écriture autorisée (WREN) */
static int      s_busy_reads;       /* lectures d'état « occupée » restantes */
static int      s_index;            /* rang de l'octet dans l'échange */
static uint8_t  s_opcode;
static uint16_t s_addr;
static uint8_t  s_page[32];
static uint16_t s_page_base;
static int      s_page_count;

void fake_eeprom_reset(void)
{
  memset(fake_eeprom_mem, 0xFF, sizeof(fake_eeprom_mem));
  s_wel = false;
  s_busy_reads = 0;
}

void fake_eeprom_cs(bool low)
{
  if (low && !s_selected)
  {
    s_index      = 0;
    s_page_count = 0;
  }
  if (!low && s_selected && (s_opcode == 0x02U) && s_wel && (s_page_count > 0))
  {
    for (int i = 0; i < s_page_count; i++)        /* écriture dans la page */
    {
      uint16_t a = (uint16_t)(s_page_base | ((s_addr + i) & 0x1FU));
      fake_eeprom_mem[a & 0x1FFFU] = s_page[i];
    }
    fake_eeprom_writes++;
    s_wel        = false;
    s_busy_reads = 3;
  }
  s_selected = low;
}

static uint8_t xfer(uint8_t mosi)
{
  uint8_t miso = 0xFFU;

  if (!fake_eeprom_present)
  {
    return 0xFFU;                                 /* rien ne répond : MISO à 1 */
  }
  if (s_index == 0)
  {
    s_opcode = mosi;
    if (mosi == 0x06U)
    {
      s_wel = true;
    }
  }
  else if (s_opcode == 0x05U)                     /* RDSR */
  {
    miso = (uint8_t)((s_wel ? 0x02U : 0x00U) | ((s_busy_reads > 0) ? 0x01U : 0x00U));
    if (s_busy_reads > 0)
    {
      s_busy_reads--;
    }
  }
  else if ((s_opcode == 0x03U) || (s_opcode == 0x02U))
  {
    if (s_index == 1)
    {
      s_addr = (uint16_t)(mosi << 8);
    }
    else if (s_index == 2)
    {
      s_addr      = (uint16_t)(s_addr | mosi);
      s_page_base = (uint16_t)(s_addr & 0x1FE0U);
    }
    else if (s_opcode == 0x03U)
    {
      miso = fake_eeprom_mem[(s_addr + (s_index - 3)) & 0x1FFF];
    }
    else if (s_page_count < 32)
    {
      s_page[s_page_count++] = mosi;
    }
  }
  s_index++;
  return miso;
}

/* Bus SPI2 : seul l'EEPROM est simulée ici (le module Bluetooth est simulé
 * au niveau des commandes ACI, dans fake_bluenrg.c). */
int32_t BSP_SPI2_SendRecv(uint8_t *pTxData, uint8_t *pRxData, uint16_t Length)
{
  if (!s_selected)
  {
    return BSP_ERROR_NONE;
  }
  if (fake_exti_enabled)
  {
    fake_eeprom_irq_violations++;                 /* l'interruption du module pourrait couper l'échange */
  }
  for (uint16_t i = 0U; i < Length; i++)
  {
    pRxData[i] = xfer(pTxData[i]);
  }
  return BSP_ERROR_NONE;
}
