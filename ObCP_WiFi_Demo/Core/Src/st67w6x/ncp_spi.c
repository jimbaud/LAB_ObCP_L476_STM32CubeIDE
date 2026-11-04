/**
 ******************************************************************************
 * @file    ncp_spi.c
 * @brief   Transport SPI vers le ST67W611M1 (voir le format dans ncp_spi.h).
 *
 *  Implémentation volontairement simple (sans RTOS ni DMA) : chaque
 *  transaction est bloquante et bornée par des délais de garde.
 *  Protocole repris du pilote officiel ST67W6X_Network_Driver
 *  (STMicroelectronics, licence BSD-3-Clause), fichier spi_iface.c.
 ******************************************************************************
 */
#include <string.h>
#include "ncp_spi.h"
#include "ncp_port.h"

#define SPI_MAGIC              0x55AAU   /* signature de début de trame */
/* Arrondi au multiple de 4 supérieur : 13 -> 16, 16 -> 16 (le module
 * échange toujours des multiples de 4 octets).                        */
#define ALIGN4(x)              ((uint16_t)(((x) + 3U) & ~3U))

#define RDY_TIMEOUT_MS         2000U  /* attente RDY=1 après CS=1           */
#define ACK_TIMEOUT_MS          200U  /* attente RDY=0 (acquittement)       */
#define SEND_TIMEOUT_MS        3000U  /* durée max pour faire accepter une trame */
#define SEND_MAX_ATTEMPTS        20U

/* Tampons d'émission et de réception d'une transaction (en-tête + charge
 * utile). +4 : marge pour l'alignement sur 4 octets. Variables « static » :
 * visibles seulement dans ce fichier, conservées entre deux appels.     */
static uint8_t s_tx[NCP_SPI_HDR_LEN + NCP_SPI_MTU + 4U];
static uint8_t s_rx[NCP_SPI_HDR_LEN + NCP_SPI_MTU + 4U];

static ncp_spi_rx_cb_t s_cb;             /* destinataire des trames reçues  */
static bool            s_peer_stalled;   /* dernier état rx_stall du module */
static bool            s_busy;           /* transaction en cours            */
static ncp_spi_stats_t s_stats;

/* -------------------------------------------------------------------------- */
/* Écrit l'en-tête de 8 octets d'une trame de len octets (format : ncp_spi.h) */
static void put_header(uint8_t *h, uint16_t len)
{
  h[0] = (uint8_t)(SPI_MAGIC & 0xFFU);
  h[1] = (uint8_t)(SPI_MAGIC >> 8);
  h[2] = (uint8_t)(len & 0xFFU);
  h[3] = (uint8_t)(len >> 8);
  h[4] = 0U;                         /* version 0, rx_stall 0, flags 0 */
  h[5] = NCP_SPI_TYPE_AT;
  h[6] = 0U;
  h[7] = 0U;
}

/* Attend que la broche RDY prenne le niveau voulu ; -1 au bout du délai */
static int wait_rdy(bool level, uint32_t timeout_ms)
{
  uint32_t t0 = ncp_port_tick();
  while (ncp_port_rdy() != level)
  {
    if ((ncp_port_tick() - t0) >= timeout_ms)
    {
      return -1;
    }
  }
  return 0;
}

/* Une transaction complète. data == NULL : lecture seule.
 * *accepted = true si la charge utile émise a été acceptée par le module.
 *
 * Étapes : 1) CS = 1 et attente de RDY = 1 (le module est prêt) ;
 *          2) échange des en-têtes (et de notre charge utile) ;
 *          3) analyse de l'en-tête reçu : magic, longueur, rx_stall ;
 *          4) lecture du reste de la trame du module s'il en reste ;
 *          5) attente de RDY = 0 (acquittement) puis CS = 0 ;
 *          6) remise de la trame reçue au callback (ncp_at.c).          */
static int transaction(const uint8_t *data, uint16_t len, bool *accepted)
{
  uint16_t send_len = ((data != NULL) && !s_peer_stalled) ? len : 0U;
  uint16_t xfer     = ALIGN4(NCP_SPI_HDR_LEN + send_len);
  uint16_t plen     = 0U;
  uint8_t  type     = 0U;
  int      ret      = NCP_SPI_OK;

  *accepted = false;

  ncp_port_cs(true);
  if (wait_rdy(true, RDY_TIMEOUT_MS) != 0)
  {
    ncp_port_cs(false);
    s_stats.timeouts++;
    return NCP_SPI_ERR_TIMEOUT;
  }

  /* 2) en-tête + charge utile, complétés par des zéros jusqu'à xfer */
  put_header(s_tx, send_len);
  if (send_len > 0U)
  {
    memcpy(&s_tx[NCP_SPI_HDR_LEN], data, send_len);
    memset(&s_tx[NCP_SPI_HDR_LEN + send_len], 0, (size_t)(xfer - NCP_SPI_HDR_LEN - send_len));
  }

  if (ncp_port_spi_txrx(s_tx, s_rx, xfer) != 0)
  {
    s_stats.io_errors++;
    ret = NCP_SPI_ERR_IO;
    goto end;
  }

  /* 3) en-tête reçu : octets assemblés en nombres de 16 bits (petit-boutiste) */
  {
    uint16_t magic = (uint16_t)(s_rx[0] | ((uint16_t)s_rx[1] << 8));
    bool     stall = ((s_rx[4] >> 2) & 1U) != 0U;
    plen = (uint16_t)(s_rx[2] | ((uint16_t)s_rx[3] << 8));
    type = s_rx[5];

    if (magic != SPI_MAGIC)
    {
      s_stats.hdr_errors++;
      s_stats.last_bad_magic = magic;
      plen = 0U;
      ret = NCP_SPI_ERR_HDR;
      goto end;
    }

    if (plen > NCP_SPI_MTU)
    {
      /* Trame trop longue : on la lit par morceaux pour rester synchronisé,
       * puis on l'ignore.                                                 */
      uint32_t remain = (uint32_t)ALIGN4(NCP_SPI_HDR_LEN + plen) - xfer;
      s_stats.hdr_errors++;
      while (remain > 0U)
      {
        uint16_t chunk = (remain > NCP_SPI_MTU) ? (uint16_t)NCP_SPI_MTU : (uint16_t)remain;
        memset(s_tx, 0, chunk);
        if (ncp_port_spi_txrx(s_tx, &s_rx[NCP_SPI_HDR_LEN], chunk) != 0)
        {
          s_stats.io_errors++;
          break;
        }
        remain -= chunk;
      }
      plen = 0U;
      ret = NCP_SPI_ERR_LEN;
      goto end;
    }

    if ((uint16_t)(NCP_SPI_HDR_LEN + plen) > xfer)
    {
      /* Reliquat de la trame du module (lecture avec octets de bourrage nuls,
       * plus robuste qu'une réception seule à fréquence élevée).          */
      uint16_t remain = ALIGN4(NCP_SPI_HDR_LEN + plen - xfer);
      memset(s_tx, 0, remain);
      if (ncp_port_spi_txrx(s_tx, &s_rx[xfer], remain) != 0)
      {
        s_stats.io_errors++;
        plen = 0U;
        ret = NCP_SPI_ERR_IO;
        goto end;
      }
    }

    if (send_len > 0U)
    {
      if (!stall)
      {
        *accepted = true;
        s_stats.tx_frames++;
        s_stats.tx_bytes += send_len;
      }
      else
      {
        s_stats.stalls++;          /* le module n'a pas pu prendre la trame */
      }
    }
    s_peer_stalled = stall;
  }

end:
  /* Le module abaisse RDY pour acquitter l'en-tête, puis on relâche CS. */
  (void)wait_rdy(false, ACK_TIMEOUT_MS);
  ncp_port_cs(false);

  if ((ret == NCP_SPI_OK) && (plen > 0U))
  {
    s_stats.rx_frames++;
    s_stats.rx_bytes += plen;
    if (s_cb != NULL)
    {
      s_cb(type, &s_rx[NCP_SPI_HDR_LEN], plen);
    }
  }
  return ret;
}

/* -------------------------------------------------------------------------- */
/* Voir ncp_spi.h */
void ncp_spi_init(ncp_spi_rx_cb_t cb)
{
  s_cb = cb;
  s_peer_stalled = false;
  s_busy = false;
  memset(&s_stats, 0, sizeof(s_stats));
  ncp_port_cs(false);
}

/* Voir ncp_spi.h : jusqu'à SEND_MAX_ATTEMPTS transactions en SEND_TIMEOUT_MS,
 * tant que le module refuse la trame (rx_stall).                         */
int ncp_spi_send(const uint8_t *data, uint16_t len)
{
  uint32_t t0;
  uint32_t attempts = 0U;
  int      ret = NCP_SPI_ERR_TIMEOUT;

  if ((data == NULL) || (len == 0U) || (len > NCP_SPI_TX_MAX))
  {
    return NCP_SPI_ERR_PARAM;
  }
  if (s_busy)
  {
    return NCP_SPI_ERR_BUSY;
  }
  s_busy = true;

  t0 = ncp_port_tick();
  while (attempts++ < SEND_MAX_ATTEMPTS)
  {
    bool accepted = false;
    ret = transaction(data, len, &accepted);
    if ((ret == NCP_SPI_OK) && accepted)
    {
      break;
    }
    if (ret == NCP_SPI_OK)
    {
      ret = NCP_SPI_ERR_TIMEOUT;   /* module saturé : on réessaie */
    }
    if ((ncp_port_tick() - t0) >= SEND_TIMEOUT_MS)
    {
      break;
    }
  }

  s_busy = false;
  return ret;
}

/* Voir ncp_spi.h : appelée par at_poll() à chaque tour de boucle */
int ncp_spi_poll(void)
{
  bool accepted;
  int  ret;

  if (s_busy || !ncp_port_rdy())
  {
    return 0;
  }
  s_busy = true;
  ret = transaction(NULL, 0U, &accepted);
  s_busy = false;
  return (ret == NCP_SPI_OK) ? 1 : ret;
}

/* Voir ncp_spi.h */
const ncp_spi_stats_t *ncp_spi_stats(void)
{
  return &s_stats;
}
