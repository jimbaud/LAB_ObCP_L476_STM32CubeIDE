/**
 ******************************************************************************
 * @file    config_store.c
 * @brief   Réglages enregistrés dans l'EEPROM M95640 (voir config_store.h).
 *
 *  Commandes de l'EEPROM (fiche du M95640, § 6) : chaque échange commence
 *  par EEPROM_CS à 0 et finit par EEPROM_CS à 1.
 *    WREN  0x06              autorise l'écriture suivante
 *    RDSR  0x05 -> état      bit 0 (WIP) à 1 : écriture interne en cours
 *    READ  0x03 A15..A8 A7..A0 -> données...
 *    WRITE 0x02 A15..A8 A7..A0 données... (32 octets au plus, dans une page)
 *  Après WRITE, l'EEPROM programme ses cellules seule (5 ms au plus) : on
 *  interroge le registre d'état jusqu'à ce que WIP repasse à 0.
 ******************************************************************************
 */
#include "config_store.h"

#include <string.h>

#include "app_config.h"
#include "main.h"                     /* HAL, EEPROM_CS, BLE_IRQ */
#include "stm32l4xx_nucleo_bus.h"     /* BSP_SPI2_SendRecv : bus du module et de l'EEPROM */

#define EE_WREN        0x06U
#define EE_RDSR        0x05U
#define EE_READ        0x03U
#define EE_WRITE       0x02U
#define EE_STATUS_WIP  0x01U

#define BLOCK_SIZE     32U
#define BLOCK_VERSION  1U
#define DATA_LEN       12U
#define WRITE_TIMEOUT_MS 20U          /* 5 ms annoncées par la fiche, marge */

static const uint8_t k_magic[4] = { 'M', 'E', 'S', 'H' };

/* -------------------------------------------------------------------------- */
/* Accès au bus SPI2 partagé avec le module Bluetooth                         */
/* -------------------------------------------------------------------------- */

/* Sélectionne l'EEPROM (EEPROM_CS à 0). L'interruption du module est
 * masquée pendant l'échange : le module ne peut pas utiliser le bus. */
static void select_eeprom(void)
{
  HAL_NVIC_DisableIRQ(BLE_IRQ_EXTI_IRQn);  /* le module n'utilise plus le bus */
  HAL_GPIO_WritePin(EEPROM_CS_GPIO_Port, EEPROM_CS_Pin, GPIO_PIN_RESET);
}

/* Libère l'EEPROM (EEPROM_CS à 1) et rend le bus au module. */
static void release_eeprom(void)
{
  HAL_GPIO_WritePin(EEPROM_CS_GPIO_Port, EEPROM_CS_Pin, GPIO_PIN_SET);
  HAL_NVIC_EnableIRQ(BLE_IRQ_EXTI_IRQn);
}

/* Échange len octets (BLOCK_SIZE + 3 au plus) : envoie tx, reçoit dans
 * rx (NULL : octets reçus ignorés). */
static void transfer(uint8_t *tx, uint8_t *rx, uint16_t len)
{
  uint8_t dummy[BLOCK_SIZE + 3U];

  (void)BSP_SPI2_SendRecv(tx, (rx != NULL) ? rx : dummy, len);
}

/* Lit le registre d'état de l'EEPROM (commande RDSR). */
static uint8_t read_status(void)
{
  uint8_t tx[2] = { EE_RDSR, 0xFFU };
  uint8_t rx[2] = { 0U, 0U };

  select_eeprom();
  transfer(tx, rx, 2U);
  release_eeprom();
  return rx[1];
}

/* Attend la fin de l'écriture interne. false : l'EEPROM ne répond pas. */
static bool wait_ready(void)
{
  uint32_t start = HAL_GetTick();

  do
  {
    if ((read_status() & EE_STATUS_WIP) == 0U)
    {
      return true;
    }
  } while ((HAL_GetTick() - start) < WRITE_TIMEOUT_MS);
  return false;
}

/* Lit le bloc de 32 octets des réglages (commande READ). */
static void read_block(uint8_t *block)
{
  uint8_t tx[3U + BLOCK_SIZE];
  uint8_t rx[3U + BLOCK_SIZE];

  memset(tx, 0xFF, sizeof(tx));
  tx[0] = EE_READ;
  tx[1] = (uint8_t)(CONFIG_EEPROM_ADDRESS >> 8);
  tx[2] = (uint8_t)CONFIG_EEPROM_ADDRESS;
  select_eeprom();
  transfer(tx, rx, (uint16_t)sizeof(tx));
  release_eeprom();
  memcpy(block, &rx[3], BLOCK_SIZE);
}

/* Écrit le bloc de 32 octets (WREN, WRITE), attend la fin de l'écriture
 * interne, puis relit le bloc pour le vérifier. */
static store_status_t write_block(const uint8_t *block)
{
  uint8_t wren = EE_WREN;
  uint8_t tx[3U + BLOCK_SIZE];
  uint8_t check[BLOCK_SIZE];

  if (!wait_ready())
  {
    return STORE_NO_EEPROM;
  }
  select_eeprom();
  transfer(&wren, NULL, 1U);
  release_eeprom();

  tx[0] = EE_WRITE;
  tx[1] = (uint8_t)(CONFIG_EEPROM_ADDRESS >> 8);
  tx[2] = (uint8_t)CONFIG_EEPROM_ADDRESS;
  memcpy(&tx[3], block, BLOCK_SIZE);
  select_eeprom();
  transfer(tx, NULL, (uint16_t)sizeof(tx));
  release_eeprom();

  if (!wait_ready())
  {
    return STORE_NO_EEPROM;
  }
  read_block(check);
  return (memcmp(check, block, BLOCK_SIZE) == 0) ? STORE_OK : STORE_VERIFY_FAILED;
}

/* -------------------------------------------------------------------------- */
/* Format du bloc                                                             */
/* -------------------------------------------------------------------------- */

/* Voir config_store.h */
uint16_t config_store_crc16(const uint8_t *data, uint32_t len)
{
  uint16_t crc = 0xFFFFU;

  for (uint32_t i = 0U; i < len; i++)
  {
    crc ^= (uint16_t)((uint16_t)data[i] << 8);
    for (int bit = 0; bit < 8; bit++)
    {
      crc = ((crc & 0x8000U) != 0U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

/* Range les réglages dans un bloc : « MESH », version, longueur, données,
 * CRC-16 sur les 30 premiers octets. */
static void encode(const stored_config_t *c, uint8_t *block)
{
  uint16_t crc;

  memset(block, 0, BLOCK_SIZE);
  memcpy(block, k_magic, sizeof(k_magic));
  block[4]  = BLOCK_VERSION;
  block[5]  = DATA_LEN;
  block[6]  = c->id;
  block[7]  = c->net;
  block[8]  = (uint8_t)c->groups;
  block[9]  = (uint8_t)(c->groups >> 8);
  block[10] = c->ttl;
  block[11] = c->relay ? 1U : 0U;
  block[12] = c->suppress;
  block[13] = (uint8_t)c->rssi_min;
  block[14] = (uint8_t)c->tx_dbm;
  block[15] = c->hello_s;
  block[16] = c->auto_s;
  block[17] = c->flash ? 1U : 0U;
  crc = config_store_crc16(block, BLOCK_SIZE - 2U);
  block[30] = (uint8_t)crc;
  block[31] = (uint8_t)(crc >> 8);
}

/* Relit les réglages d'un bloc ; faux si le bloc n'est pas valable
 * (signature, version, longueur ou CRC). */
static bool decode(const uint8_t *block, stored_config_t *c)
{
  uint16_t crc = (uint16_t)(block[30] | ((uint16_t)block[31] << 8));

  if ((memcmp(block, k_magic, sizeof(k_magic)) != 0) || (block[4] != BLOCK_VERSION)
      || (block[5] != DATA_LEN) || (crc != config_store_crc16(block, BLOCK_SIZE - 2U)))
  {
    return false;
  }
  c->id       = block[6];
  c->net      = block[7];
  c->groups   = (uint16_t)(block[8] | ((uint16_t)block[9] << 8));
  c->ttl      = block[10];
  c->relay    = (block[11] != 0U);
  c->suppress = block[12];
  c->rssi_min = (int8_t)block[13];
  c->tx_dbm   = (int8_t)block[14];
  c->hello_s  = block[15];
  c->auto_s   = block[16];
  c->flash    = (block[17] != 0U);
  return true;
}

/* -------------------------------------------------------------------------- */
/* Fonctions publiques                                                        */
/* -------------------------------------------------------------------------- */

/* Voir config_store.h */
store_status_t config_store_load(stored_config_t *c)
{
  uint8_t block[BLOCK_SIZE];

  read_block(block);
  return decode(block, c) ? STORE_OK : STORE_EMPTY;
}

/* Voir config_store.h */
store_status_t config_store_save(const stored_config_t *c)
{
  uint8_t block[BLOCK_SIZE];

  encode(c, block);
  return write_block(block);
}

/* Voir config_store.h */
store_status_t config_store_erase(void)
{
  uint8_t block[BLOCK_SIZE];

  memset(block, 0xFF, sizeof(block));     /* état d'une EEPROM neuve */
  return write_block(block);
}

/* Voir config_store.h */
const char *config_store_status_text(store_status_t s)
{
  switch (s)
  {
    case STORE_OK:            return "fait";
    case STORE_EMPTY:         return "aucun réglage enregistré";
    case STORE_NO_EEPROM:     return "l'EEPROM ne répond pas";
    case STORE_VERIFY_FAILED: return "relecture différente : EEPROM absente ou défaillante";
    default:                  return "erreur inconnue";
  }
}
