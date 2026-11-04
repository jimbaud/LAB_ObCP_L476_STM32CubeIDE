/**
 ******************************************************************************
 * @file    ncp_fwu.c
 * @brief   Mise à jour du firmware du ST67W611M1 par le lien SPI (voir ncp_fwu.h).
 *
 *  Utilisé par la commande « module ota » (module_ota.c) : le fichier arrive
 *  du PC par blocs de 128 ou 1024 octets (YMODEM) ; ncp_fwu_write() les
 *  regroupe dans s_buf et envoie au module un bloc de 2048 octets dès qu'il
 *  est plein (le premier envoi est l'en-tête de 512 octets, exigé seul).
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "ncp_fwu.h"
#include "ncp_at.h"
#include "ncp_port.h"

#define FWU_CHUNK          2048U    /* multiple de 256 o (recommandation ST)      */
#define FWU_CMD_TIMEOUT    5000U    /* AT+OTASTART, confirmation d'un AT+OTASEND  */
#define FWU_FIN_TIMEOUT   10000U    /* réponse à AT+OTAFIN                         */

static const char FWU_MAGIC[] = "QCC74X_OTAVer1.0";   /* début d'un fichier .ota */

static uint8_t  s_buf[FWU_CHUNK];   /* bloc en cours de remplissage           */
static uint16_t s_n;                /* octets déjà dans s_buf                 */
static bool     s_active;           /* session ouverte (AT+OTASTART=1)        */
static bool     s_header_sent;      /* en-tête de 512 octets déjà transmis    */
static uint32_t s_sent;             /* total transmis au module               */

/* Copie un champ texte de l'en-tête (terminé par 0 ou bourrage 0xFF). */
static bool copy_text(char *dst, size_t dst_len, const uint8_t *src, size_t src_len)
{
  size_t i;
  for (i = 0U; (i < src_len) && (i < (dst_len - 1U)); i++)
  {
    uint8_t c = src[i];
    if ((c == 0U) || (c == 0xFFU))
    {
      break;
    }
    if ((c < 0x20U) || (c > 0x7EU))
    {
      return false;
    }
    dst[i] = (char)c;
  }
  dst[i] = '\0';
  return true;
}

/* Voir ncp_fwu.h ; champs aux positions décrites en tête de ncp_fwu.h */
bool ncp_fwu_parse_header(const uint8_t *h, size_t len, ncp_fwu_header_t *out)
{
  size_t k;

  if ((h == NULL) || (out == NULL) || (len < 96U) || (memcmp(h, FWU_MAGIC, sizeof(FWU_MAGIC) - 1U) != 0))
  {
    return false;
  }
  memset(out, 0, sizeof(*out));
  memcpy(out->type, &h[16], 4U);
  out->type[4] = '\0';
  for (k = 4U; (k > 0U) && (out->type[k - 1U] == ' '); k--)
  {
    out->type[k - 1U] = '\0';
  }
  if ((strcmp(out->type, "RAW") != 0) && (strcmp(out->type, "XZ") != 0))
  {
    return false;
  }
  out->length = (uint32_t)h[20] | ((uint32_t)h[21] << 8) | ((uint32_t)h[22] << 16) | ((uint32_t)h[23] << 24);
  if ((out->length == 0U) || (out->length > NCP_FWU_MAX_IMAGE))
  {
    return false;
  }
  if (!copy_text(out->module, sizeof(out->module), &h[32], 16U) ||
      !copy_text(out->version, sizeof(out->version), &h[48], 16U) || (out->version[0] == '\0'))
  {
    return false;
  }
  memcpy(out->sha256, &h[64], sizeof(out->sha256));
  return true;
}

/* Voir ncp_fwu.h */
int ncp_fwu_begin(void)
{
  int st;

  s_active = false;
  s_n = 0U;
  s_header_sent = false;
  s_sent = 0U;
  (void)at_cmd("AT+OTASTART=0", NULL, NULL, NULL, FWU_CMD_TIMEOUT);   /* session précédente éventuelle */
  st = at_cmd("AT+OTASTART=1", NULL, NULL, NULL, FWU_CMD_TIMEOUT);
  if (st == AT_OK)
  {
    s_active = true;
  }
  return st;
}

/* Envoie les n premiers octets de s_buf au module (AT+OTASEND) */
static int send_buf(uint16_t n)
{
  char cmd[24];
  int  st;

  (void)snprintf(cmd, sizeof(cmd), "AT+OTASEND=%u", (unsigned)n);
  st = at_cmd_data(cmd, s_buf, n, FWU_CMD_TIMEOUT);
  if (st == AT_OK)
  {
    s_sent += n;
  }
  return st;
}

/* Voir ncp_fwu.h : remplit s_buf jusqu'à la taille visée (512 puis 2048),
 * l'envoie, recommence avec la suite des données.                        */
int ncp_fwu_write(const uint8_t *data, uint32_t len)
{
  if (!s_active)
  {
    return NCP_FWU_ERR_STATE;
  }
  while (len > 0U)
  {
    uint16_t target = s_header_sent ? (uint16_t)FWU_CHUNK : (uint16_t)NCP_FWU_HEADER_SIZE;
    uint32_t room = (uint32_t)target - s_n;
    uint32_t k = (len < room) ? len : room;

    memcpy(&s_buf[s_n], data, k);
    s_n = (uint16_t)(s_n + k);
    data = &data[k];
    len -= k;
    if (s_n == target)
    {
      int st = send_buf(s_n);
      if (st != AT_OK)
      {
        return st;
      }
      s_n = 0U;
      s_header_sent = true;
    }
  }
  return AT_OK;
}

/* Voir ncp_fwu.h */
int ncp_fwu_end(void)
{
  if (!s_active || !s_header_sent)
  {
    return NCP_FWU_ERR_STATE;          /* image plus courte que son en-tête */
  }
  if (s_n > 0U)
  {
    int st = send_buf(s_n);
    s_n = 0U;
    if (st != AT_OK)
    {
      return st;
    }
  }
  return AT_OK;
}

/* Voir ncp_fwu.h */
int ncp_fwu_abort(void)
{
  s_active = false;
  s_n = 0U;
  return at_cmd("AT+OTASTART=0", NULL, NULL, NULL, FWU_CMD_TIMEOUT);
}

/* Voir ncp_fwu.h */
int ncp_fwu_finish(uint32_t ready_timeout_ms)
{
  int st;

  if (!s_active)
  {
    return NCP_FWU_ERR_STATE;
  }
  /* laisse arriver d'éventuelles confirmations tardives du dernier envoi
   * avant d'ouvrir la commande suivante                                    */
  ncp_port_delay(20U);
  at_poll();
  at_clear_ready();
  st = at_cmd("AT+OTAFIN", NULL, NULL, NULL, FWU_FIN_TIMEOUT);
  s_active = false;
  if (st == AT_ERROR)
  {
    return NCP_FWU_ERR_REJECTED;
  }
  if ((st != AT_OK) && (st != AT_TIMEOUT))
  {
    return st;
  }
  /* AT_TIMEOUT : le module a pu redémarrer sans répondre OK ; « ready » tranche */
  return (at_wait_ready(ready_timeout_ms) == AT_OK) ? AT_OK : AT_TIMEOUT;
}

/* Voir ncp_fwu.h */
uint32_t ncp_fwu_sent(void)
{
  return s_sent;
}

/* Voir ncp_fwu.h */
bool ncp_fwu_active(void)
{
  return s_active;
}
