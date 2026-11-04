/**
 ******************************************************************************
 * @file    ymodem.c
 * @brief   Réception YMODEM (voir ymodem.h).
 *
 *  YMODEM découpe un fichier en blocs numérotés protégés par un CRC. Le
 *  récepteur acquitte chaque bloc (ACK) ou en demande la répétition (NAK) :
 *  un octet perdu ou abîmé ne corrompt pas le fichier. Le module (on_data)
 *  reçoit les blocs dans l'ordre, chacun une seule fois.
 ******************************************************************************
 */
#include <string.h>
#include <stdlib.h>
#include "ymodem.h"

/* Octets de contrôle du protocole */
#define SOH        0x01U               /* début d'un bloc de 128 octets  */
#define STX        0x02U               /* début d'un bloc de 1024 octets */
#define EOT        0x04U               /* fin du fichier                 */
#define ACK        0x06U               /* bloc reçu sans erreur          */
#define NAK        0x15U               /* bloc à renvoyer                */
#define CAN        0x18U               /* annulation (deux de suite)     */
#define CRC_REQ    0x43U               /* 'C' : demande de transfert avec CRC */

#define C_INTERVAL_MS      2000U       /* répétition de 'C' avant le début     */
#define BYTE_TIMEOUT_MS    1000U       /* entre deux octets d'un même bloc     */
#define PACKET_TIMEOUT_MS 10000U       /* attente du bloc suivant              */
#define END_TIMEOUT_MS     3000U       /* attente du bloc 0 de fin de lot      */
#define PURGE_SILENCE_MS    300U
#define MAX_ERRORS           10U
#define MAX_NOISE          4096U       /* octets parasites tolérés avant le début */

typedef enum
{
  PK_DATA,
  PK_EOT,
  PK_CAN,
  PK_TIMEOUT,
  PK_BAD,
  PK_NOISE
} pk_t;

static uint8_t s_buf[1024];            /* bloc en cours de réception */

/* Voir ymodem.h : calcul bit à bit, poids fort en premier */
uint16_t ymodem_crc16(const uint8_t *data, uint32_t len)
{
  uint16_t crc = 0U;
  uint32_t i;
  int      b;

  for (i = 0U; i < len; i++)
  {
    crc ^= (uint16_t)((uint16_t)data[i] << 8);
    for (b = 0; b < 8; b++)
    {
      crc = ((crc & 0x8000U) != 0U) ? (uint16_t)((uint16_t)(crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

/* Lit un octet (ou -1 après ms millisecondes) par la fonction de l'appelant */
static int rx(const ymodem_cfg_t *cfg, uint32_t ms)
{
  return cfg->getc(ms, cfg->io);
}

/* Envoie un octet de contrôle par la fonction de l'appelant */
static void tx(const ymodem_cfg_t *cfg, uint8_t c)
{
  cfg->putc(c, cfg->io);
}

/* Jette les octets reçus jusqu'à un silence de PURGE_SILENCE_MS (après une
 * erreur, pour se resynchroniser sur le début du bloc suivant)         */
static void purge(const ymodem_cfg_t *cfg)
{
  uint32_t n = 0U;
  while ((rx(cfg, PURGE_SILENCE_MS) >= 0) && (n++ < 8192U))
  {
    /* on vide jusqu'à un silence */
  }
}

/* Annule le transfert : plusieurs CAN envoyés à l'émetteur */
static void cancel(const ymodem_cfg_t *cfg)
{
  tx(cfg, CAN);
  tx(cfg, CAN);
  tx(cfg, CAN);
  purge(cfg);
}

/* Lit la fin d'un bloc dont l'octet de tête (SOH ou STX) vient d'être reçu. */
static pk_t read_body(const ymodem_cfg_t *cfg, int head, uint16_t *len, uint8_t *blk)
{
  uint16_t size = (head == (int)STX) ? 1024U : 128U;
  uint16_t i;
  uint16_t crc;
  int      b;
  int      nb;
  int      c;

  b = rx(cfg, BYTE_TIMEOUT_MS);
  nb = rx(cfg, BYTE_TIMEOUT_MS);
  if ((b < 0) || (nb < 0))
  {
    return PK_BAD;
  }
  for (i = 0U; i < size; i++)
  {
    c = rx(cfg, BYTE_TIMEOUT_MS);
    if (c < 0)
    {
      return PK_BAD;
    }
    s_buf[i] = (uint8_t)c;
  }
  c = rx(cfg, BYTE_TIMEOUT_MS);
  if (c < 0)
  {
    return PK_BAD;
  }
  crc = (uint16_t)((uint16_t)c << 8);
  c = rx(cfg, BYTE_TIMEOUT_MS);
  if (c < 0)
  {
    return PK_BAD;
  }
  crc |= (uint16_t)c;
  if ((((uint8_t)b ^ (uint8_t)nb) != 0xFFU) || (ymodem_crc16(s_buf, size) != crc))
  {
    return PK_BAD;
  }
  *len = size;
  *blk = (uint8_t)b;
  return PK_DATA;
}

/* Attend le prochain élément : bloc de données, EOT, annulation ou silence */
static pk_t read_packet(const ymodem_cfg_t *cfg, uint32_t timeout_ms, uint16_t *len, uint8_t *blk)
{
  int c = rx(cfg, timeout_ms);

  if (c < 0)
  {
    return PK_TIMEOUT;
  }
  if ((c == (int)SOH) || (c == (int)STX))
  {
    return read_body(cfg, c, len, blk);
  }
  if (c == (int)EOT)
  {
    return PK_EOT;
  }
  if (c == (int)CAN)
  {
    return (rx(cfg, BYTE_TIMEOUT_MS) == (int)CAN) ? PK_CAN : PK_NOISE;
  }
  return PK_NOISE;
}

/* Bloc 0 : « nom\0taille [date mode ...]\0 » */
static void parse_header_block(uint16_t len, ymodem_result_t *res)
{
  uint16_t i = 0U;
  uint16_t k = 0U;

  while ((i < len) && (s_buf[i] != 0U))
  {
    if (k < (sizeof(res->name) - 1U))
    {
      res->name[k++] = (char)s_buf[i];
    }
    i++;
  }
  res->name[k] = '\0';
  res->size = 0U;
  if ((i + 1U) < len)
  {
    char     num[12];
    uint16_t j = 0U;
    i++;
    while ((i < len) && (s_buf[i] >= (uint8_t)'0') && (s_buf[i] <= (uint8_t)'9') && (j < (sizeof(num) - 1U)))
    {
      num[j++] = (char)s_buf[i++];
    }
    num[j] = '\0';
    if (j > 0U)
    {
      res->size = (uint32_t)strtoul(num, NULL, 10);
    }
  }
}

/* Voir ymodem.h ; le déroulement est décrit en tête de ymodem.h */
int ymodem_receive(const ymodem_cfg_t *cfg, ymodem_result_t *res)
{
  uint16_t len = 0U;
  uint8_t  blk = 0U;
  uint8_t  expected;
  uint32_t errors = 0U;
  uint32_t noise = 0U;
  uint32_t t_start;
  uint32_t t_c;
  bool     eot = false;
  pk_t     pk;

  memset(res, 0, sizeof(*res));

  /* 1) Attente du bloc 0 : 'C' toutes les C_INTERVAL_MS */
  t_start = cfg->tick(cfg->io);
  t_c = t_start - C_INTERVAL_MS;
  for (;;)
  {
    uint32_t now = cfg->tick(cfg->io);
    int      c;

    if ((now - t_start) >= cfg->start_timeout_ms)
    {
      return YM_ERR_NOSTART;
    }
    if ((now - t_c) >= C_INTERVAL_MS)
    {
      tx(cfg, CRC_REQ);
      t_c = now;
    }
    c = rx(cfg, 100U);
    if (c < 0)
    {
      continue;
    }
    if ((c == (int)SOH) || (c == (int)STX))
    {
      pk = read_body(cfg, c, &len, &blk);
      if (pk == PK_DATA)
      {
        break;
      }
      if (++errors > MAX_ERRORS)
      {
        cancel(cfg);
        return YM_ERR_PROTOCOL;
      }
      purge(cfg);
      t_c = cfg->tick(cfg->io) - C_INTERVAL_MS;   /* 'C' immédiat : bloc 0 à renvoyer */
      continue;
    }
    if (c == (int)CAN)
    {
      if (rx(cfg, BYTE_TIMEOUT_MS) == (int)CAN)
      {
        return YM_ERR_REMOTE_CANCEL;
      }
      continue;
    }
    if ((c == 0x03) || (c == 0x1B) || (c == (int)'q') || (c == (int)'Q'))
    {
      return YM_ERR_USER_CANCEL;           /* Ctrl-C, Échap ou q au clavier */
    }
    if (++noise > MAX_NOISE)
    {
      return YM_ERR_PROTOCOL;
    }
    /* autres octets (fin de ligne de la commande...) : ignorés */
  }

  /* 2) Bloc 0 : nom et taille du fichier */
  if (blk != 0U)
  {
    cancel(cfg);
    return YM_ERR_NOTYMODEM;
  }
  parse_header_block(len, res);
  if (res->name[0] == '\0')
  {
    tx(cfg, ACK);
    return YM_ERR_NOFILE;
  }
  if ((cfg->on_file != NULL) && (cfg->on_file(res->name, res->size, cfg->ctx) != 0))
  {
    cancel(cfg);
    return YM_ERR_SINK;
  }
  tx(cfg, ACK);
  tx(cfg, CRC_REQ);

  /* 3) Blocs de données */
  expected = 1U;
  errors = 0U;
  for (;;)
  {
    pk = read_packet(cfg, PACKET_TIMEOUT_MS, &len, &blk);
    if (pk == PK_DATA)
    {
      if (blk == expected)
      {
        uint32_t n = len;
        if (res->size > 0U)
        {
          uint32_t left = res->size - res->received;
          n = (n < left) ? n : left;            /* bourrage du dernier bloc retiré */
        }
        if ((n > 0U) && (cfg->on_data != NULL) && (cfg->on_data(s_buf, n, cfg->ctx) != 0))
        {
          cancel(cfg);
          return YM_ERR_SINK;
        }
        res->received += n;
        res->blocks++;
        expected++;
        errors = 0U;
        tx(cfg, ACK);
      }
      else if (blk == (uint8_t)(expected - 1U))
      {
        res->retries++;                         /* bloc répété : notre ACK a été perdu */
        tx(cfg, ACK);
        if (blk == 0U)
        {
          tx(cfg, CRC_REQ);
        }
      }
      else
      {
        cancel(cfg);
        return YM_ERR_PROTOCOL;                 /* bloc hors séquence */
      }
    }
    else if (pk == PK_EOT)
    {
      if (!eot)
      {
        eot = true;
        tx(cfg, NAK);                           /* premier EOT : confirmation demandée */
      }
      else
      {
        tx(cfg, ACK);
        break;
      }
    }
    else if (pk == PK_CAN)
    {
      return YM_ERR_REMOTE_CANCEL;
    }
    else
    {
      if (++errors > MAX_ERRORS)
      {
        cancel(cfg);
        return (pk == PK_TIMEOUT) ? YM_ERR_TIMEOUT : YM_ERR_PROTOCOL;
      }
      if (pk != PK_TIMEOUT)
      {
        purge(cfg);
      }
      res->retries++;
      tx(cfg, NAK);
    }
  }

  /* 4) Fin du lot : bloc 0 vide (un seul fichier accepté) */
  tx(cfg, CRC_REQ);
  pk = read_packet(cfg, END_TIMEOUT_MS, &len, &blk);
  if ((pk == PK_DATA) && (blk == 0U))
  {
    tx(cfg, ACK);
    if (s_buf[0] != 0U)
    {
      cancel(cfg);                              /* fichier suivant refusé */
    }
  }
  else if (pk == PK_EOT)
  {
    tx(cfg, ACK);
  }
  else
  {
    /* émetteur ne terminant pas le lot : sans conséquence */
  }

  if ((res->size > 0U) && (res->received != res->size))
  {
    return YM_ERR_SIZE;
  }
  return YM_OK;
}

/* Voir ymodem.h */
const char *ymodem_status_str(int st)
{
  switch (st)
  {
    case YM_OK:                return "OK";
    case YM_ERR_NOSTART:       return "aucun transfert reçu (délai dépassé)";
    case YM_ERR_TIMEOUT:       return "l'émetteur ne répond plus";
    case YM_ERR_REMOTE_CANCEL: return "transfert annulé par l'émetteur";
    case YM_ERR_USER_CANCEL:   return "annulé au clavier";
    case YM_ERR_PROTOCOL:      return "trop d'erreurs de transmission";
    case YM_ERR_SINK:          return "fichier refusé";
    case YM_ERR_SIZE:          return "taille reçue différente de la taille annoncée";
    case YM_ERR_NOTYMODEM:     return "protocole XMODEM reçu : choisir YMODEM dans le terminal";
    case YM_ERR_NOFILE:        return "aucun fichier envoyé";
    default:                   return "erreur inconnue";
  }
}
