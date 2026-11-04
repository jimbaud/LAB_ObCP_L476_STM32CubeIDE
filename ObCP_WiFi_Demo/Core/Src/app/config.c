/**
 ******************************************************************************
 * @file    config.c
 * @brief   Configuration en flash (voir config.h).
 *
 *  La STM32L476RG possède 2 banques de 512 Ko (pages de 2 Ko) : la page de
 *  configuration est en banque 2 alors que le programme s'exécute depuis la
 *  banque 1, ce qui permet d'écrire sans bloquer l'exécution (RWW).
 *  Programmation par double mot (64 bits) avec ECC.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "app_config.h"
#include "board.h"
#include "util.h"

cfg_t g_cfg;

static volatile bool s_ecc_error;
static char          s_ap_ssid[33];
static char          s_hostname[33];
static char          s_root[64];
static char          s_client_id[32];

/* Banque physique contenant l'adresse (tient compte d'une éventuelle
 * permutation des banques par l'option BFB2).                         */
static uint32_t flash_bank(uint32_t addr)
{
  bool upper = (addr >= (FLASH_BASE + FLASH_BANK_SIZE));
  bool swap = (READ_BIT(SYSCFG->MEMRMP, SYSCFG_MEMRMP_FB_MODE) != 0U);
  return (upper != swap) ? FLASH_BANK_2 : FLASH_BANK_1;
}

/* Numéro de la page (2 Ko) dans sa banque */
static uint32_t flash_page(uint32_t addr)
{
  if (addr >= (FLASH_BASE + FLASH_BANK_SIZE))
  {
    return (addr - (FLASH_BASE + FLASH_BANK_SIZE)) / FLASH_PAGE_SIZE;
  }
  return (addr - FLASH_BASE) / FLASH_PAGE_SIZE;
}

/* Voir config.h */
bool config_ecc_nmi(void)
{
  if ((FLASH->ECCR & FLASH_ECCR_ECCD) != 0U)
  {
    FLASH->ECCR |= FLASH_ECCR_ECCD;       /* acquittement (écriture de 1) */
    s_ecc_error = true;
    return true;
  }
  return false;
}

/* Voir config.h : lecture directe de la flash, comme de la mémoire */
int config_load(void)
{
  int st;

  s_ecc_error = false;
  st = cfg_unpack(&g_cfg, (const uint8_t *)CONFIG_FLASH_ADDR, CFG_BLOB_MAX);
  if (s_ecc_error)
  {
    st = CFG_UNPACK_CRC;                  /* page abîmée (coupure pendant une écriture ?) */
  }
  if (st != CFG_UNPACK_OK)
  {
    cfg_defaults(&g_cfg);
  }
  return st;
}

/* Voir config.h */
bool config_stored(void)
{
  cfg_t tmp;
  bool  ok;

  s_ecc_error = false;
  ok = (cfg_unpack(&tmp, (const uint8_t *)CONFIG_FLASH_ADDR, CFG_BLOB_MAX) == CFG_UNPACK_OK);
  return ok && !s_ecc_error;
}

/* Voir config.h. La flash ne s'écrit qu'après effacement (tous les bits à 1),
 * par pages entières ; il faut la déverrouiller (HAL_FLASH_Unlock) avant. */
int config_erase(void)
{
  FLASH_EraseInitTypeDef er = { 0 };
  uint32_t               page_err = 0U;
  HAL_StatusTypeDef      st;

  /* Sécurité : la dernière page de la flash ne doit pas contenir de code
   * (programme de plus de 1022 Ko, impossible en pratique : environ 140 Ko). */
  if (board_image_end() > CONFIG_FLASH_ADDR)
  {
    return -1;
  }
  er.TypeErase = FLASH_TYPEERASE_PAGES;
  er.Banks     = flash_bank(CONFIG_FLASH_ADDR);
  er.Page      = flash_page(CONFIG_FLASH_ADDR);
  er.NbPages   = 1U;

  (void)HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
  st = HAL_FLASHEx_Erase(&er, &page_err);
  (void)HAL_FLASH_Lock();
  return (st == HAL_OK) ? 0 : -1;
}

/* Voir config.h */
int config_save(void)
{
  static uint8_t    blob[CFG_BLOB_MAX];
  size_t            n = cfg_pack(&g_cfg, blob, sizeof(blob));
  size_t            i;
  HAL_StatusTypeDef st = HAL_OK;

  if (n == 0U)
  {
    return -1;
  }
  if (config_erase() != 0)
  {
    return -2;
  }
  (void)HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
  for (i = 0U; (i < n) && (st == HAL_OK); i += 8U)
  {
    uint64_t dw;
    memcpy(&dw, &blob[i], sizeof(dw));
    st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, CONFIG_FLASH_ADDR + i, dw);
  }
  (void)HAL_FLASH_Lock();
  if (st != HAL_OK)
  {
    return -3;
  }
  return (memcmp((const void *)CONFIG_FLASH_ADDR, blob, n) == 0) ? 0 : -4;
}

/* -------------------------------------------------------------------------- */
/* Voir config.h */
const char *config_ap_ssid(void)
{
  if (g_cfg.ap_ssid[0] != '\0')
  {
    return g_cfg.ap_ssid;
  }
  (void)snprintf(s_ap_ssid, sizeof(s_ap_ssid), AP_SSID_PREFIX "%s", board_uid_short());
  return s_ap_ssid;
}

/* Voir config.h */
const char *config_hostname(void)
{
  if (g_cfg.hostname[0] != '\0')
  {
    return g_cfg.hostname;
  }
  (void)snprintf(s_hostname, sizeof(s_hostname), HOSTNAME_PREFIX "%s", board_uid_short());
  return s_hostname;
}

/* Voir config.h (les '/' finaux sont retirés) */
const char *config_mqtt_root(void)
{
  size_t n;

  if (g_cfg.mqtt_topic[0] != '\0')
  {
    (void)util_strlcpy(s_root, g_cfg.mqtt_topic, sizeof(s_root));
    n = strlen(s_root);
    while ((n > 0U) && (s_root[n - 1U] == '/'))
    {
      s_root[--n] = '\0';                 /* pas de '/' final */
    }
    return s_root;
  }
  (void)snprintf(s_root, sizeof(s_root), MQTT_TOPIC_PREFIX "%s", board_uid_short());
  return s_root;
}

/* Voir config.h */
const char *config_mqtt_client_id(void)
{
  (void)snprintf(s_client_id, sizeof(s_client_id), HOSTNAME_PREFIX "%s", board_uid_short());
  return s_client_id;
}
