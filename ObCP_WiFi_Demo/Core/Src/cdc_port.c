/**
 ******************************************************************************
 * @file    cdc_port.c
 * @brief   Port COM virtuel de l'ObCP (voir cdc_port.h).
 *
 *          Les rappels cdc_port_itf_* s'exécutent dans l'interruption USB
 *          (appelés par usbd_cdc_if.c) ; les autres fonctions dans la boucle
 *          principale. Les index des tampons circulaires ne sont modifiés que
 *          d'un seul côté (tête par l'écrivain, queue par le lecteur) ; les
 *          décisions de (re)démarrage de transfert se font interruptions
 *          masquées.
 *
 *          Tampon circulaire (ou « anneau ») : tableau dans lequel l'écrivain
 *          avance l'index « head » et le lecteur l'index « tail » ; arrivés en
 *          fin de tableau, ils repartent au début. Le tampon est vide quand
 *          head == tail. Avec une taille puissance de 2, le retour au début
 *          s'écrit simplement (index + 1) & (taille - 1).
 ******************************************************************************
 */
#include <string.h>
#include "cdc_port.h"
#include "usbd_core.h"
#include "usbd_cdc.h"

#define TX_SIZE      4096U                 /* puissances de 2 */
#define RX_SIZE      2048U
#define TX_CHUNK      512U                 /* taille maximale d'un transfert IN */
#define PKT_SIZE     CDC_DATA_FS_OUT_PACKET_SIZE   /* paquet USB reçu : 64 octets */

extern USBD_HandleTypeDef hUsbDeviceFS;    /* usb_device.c (généré) */

static uint8_t           s_tx[TX_SIZE];
static volatile uint32_t s_tx_head;        /* écrit par la boucle principale */
static volatile uint32_t s_tx_tail;        /* avancé à la fin de chaque transfert */
static volatile uint32_t s_tx_inflight;    /* octets en cours d'envoi (0 = libre) */

static uint8_t           s_rx[RX_SIZE];
static volatile uint32_t s_rx_head;        /* écrit dans l'interruption */
static volatile uint32_t s_rx_tail;
static volatile bool     s_rx_armed;
__ALIGN_BEGIN static uint8_t s_rx_pkt[PKT_SIZE] __ALIGN_END;

static volatile bool     s_configured;
static volatile bool     s_suspended;
static volatile bool     s_dtr;
static volatile bool     s_open;
static volatile bool     s_always_send;
static volatile uint32_t s_open_count;
static volatile uint32_t s_lc_count;
static uint8_t           s_line_coding[7] = { 0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08 };  /* 115200 8N1 */

/* Section critique : masque les interruptions et rend l'état précédent */
static uint32_t irq_save(void)
{
  uint32_t pm = __get_PRIMASK();
  __disable_irq();
  return pm;
}

/* Fin de section critique : rétablit l'état rendu par irq_save() */
static void irq_restore(uint32_t pm)
{
  __set_PRIMASK(pm);
}

/* Place libre dans le tampon de réception (une case reste toujours vide) */
static uint32_t rx_free(void)
{
  return (RX_SIZE - 1U) - ((s_rx_head - s_rx_tail) & (RX_SIZE - 1U));
}

/* Le terminal vient d'ouvrir le port : compté une fois par ouverture */
static void mark_open(void)
{
  if (!s_open)
  {
    s_open = true;
    s_open_count++;
  }
}

/* ---- Émission ------------------------------------------------------------------ */
/* Lance un transfert USB vers le PC avec les octets en attente (au plus
 * TX_CHUNK, sans dépasser la fin du tableau), si rien n'est déjà en cours.
 * À appeler interruptions masquées ou depuis l'interruption USB.        */
static void tx_start_locked(void)
{
  uint32_t head = s_tx_head;
  uint32_t tail = s_tx_tail;
  uint32_t len;

  if (!s_configured || s_suspended || (!s_open && !s_always_send) || (s_tx_inflight != 0U) || (head == tail))
  {
    return;
  }
  len = (head - tail) & (TX_SIZE - 1U);
  if (len > (TX_SIZE - tail))
  {
    len = TX_SIZE - tail;                  /* partie contiguë jusqu'à la fin du tampon */
  }
  if (len > TX_CHUNK)
  {
    len = TX_CHUNK;
  }
  s_tx_inflight = len;
  (void)USBD_CDC_SetTxBuffer(&hUsbDeviceFS, &s_tx[tail], len);
  if (USBD_CDC_TransmitPacket(&hUsbDeviceFS) != USBD_OK)
  {
    s_tx_inflight = 0U;                    /* classe occupée : nouvel essai plus tard */
  }
}

/* Voir cdc_port.h */
void cdc_port_tx_kick(void)
{
  uint32_t pm = irq_save();
  tx_start_locked();
  irq_restore(pm);
}

/* Voir cdc_port.h */
size_t cdc_port_tx_free(void)
{
  return (TX_SIZE - 1U) - ((s_tx_head - s_tx_tail) & (TX_SIZE - 1U));
}

/* Voir cdc_port.h : copie dans l'anneau, puis publication du nouvel index
 * (__DMB : les octets sont bien écrits avant que l'index ne change).    */
size_t cdc_port_write(const uint8_t *data, size_t n)
{
  size_t   room = cdc_port_tx_free();
  uint32_t head = s_tx_head;
  size_t   i;

  if (n > room)
  {
    n = room;
  }
  for (i = 0U; i < n; i++)
  {
    s_tx[head] = data[i];
    head = (head + 1U) & (TX_SIZE - 1U);
  }
  __DMB();
  s_tx_head = head;
  cdc_port_tx_kick();
  return n;
}

/* Voir cdc_port.h */
void cdc_port_set_always_send(bool on)
{
  s_always_send = on;
  cdc_port_tx_kick();
}

/* Voir cdc_port.h : seuls les octets déjà confiés à l'USB sont conservés */
void cdc_port_tx_discard(void)
{
  uint32_t pm = irq_save();
  s_tx_head = (s_tx_tail + s_tx_inflight) & (TX_SIZE - 1U);
  irq_restore(pm);
}

/* Voir cdc_port.h */
bool cdc_port_tx_empty(void)
{
  return (s_tx_head == s_tx_tail) && (s_tx_inflight == 0U);
}

/* ---- Réception ----------------------------------------------------------------- */
/* Réautorise la réception d'un paquet USB dès qu'il y a de la place pour lui :
 * tant que ce n'est pas fait, le PC attend (contrôle de flux par NAK).   */
static void rx_rearm_if_room(void)
{
  uint32_t pm;

  if (s_rx_armed || (rx_free() < PKT_SIZE))
  {
    return;
  }
  pm = irq_save();
  if (!s_rx_armed && s_configured && (rx_free() >= PKT_SIZE))
  {
    s_rx_armed = true;
    (void)USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  }
  irq_restore(pm);
}

/* Voir cdc_port.h */
int cdc_port_getc(void)
{
  int c;

  if (s_rx_tail == s_rx_head)
  {
    rx_rearm_if_room();
    return -1;
  }
  c = s_rx[s_rx_tail];
  __DMB();
  s_rx_tail = (s_rx_tail + 1U) & (RX_SIZE - 1U);
  rx_rearm_if_room();
  return c;
}

/* Voir cdc_port.h */
size_t cdc_port_read(uint8_t *buf, size_t max)
{
  size_t n = 0U;

  while ((n < max) && (s_rx_tail != s_rx_head))
  {
    buf[n++] = s_rx[s_rx_tail];
    __DMB();
    s_rx_tail = (s_rx_tail + 1U) & (RX_SIZE - 1U);
  }
  rx_rearm_if_room();
  return n;
}

/* Voir cdc_port.h */
size_t cdc_port_rx_count(void)
{
  return (s_rx_head - s_rx_tail) & (RX_SIZE - 1U);
}

/* Voir cdc_port.h */
void cdc_port_rx_flush(void)
{
  s_rx_tail = s_rx_head;
  rx_rearm_if_room();
}

/* ---- État (voir cdc_port.h) ----------------------------------------------------- */
bool cdc_port_configured(void)
{
  return s_configured && !s_suspended;
}

bool cdc_port_open(void)
{
  return s_configured && !s_suspended && s_open;
}

uint32_t cdc_port_open_count(void)
{
  return s_open_count;
}

bool cdc_port_dtr(void)
{
  return s_dtr;
}

/* Débit : 4 premiers octets du codage de ligne USB, poids faible d'abord */
uint32_t cdc_port_baudrate(void)
{
  return (uint32_t)s_line_coding[0] | ((uint32_t)s_line_coding[1] << 8) |
         ((uint32_t)s_line_coding[2] << 16) | ((uint32_t)s_line_coding[3] << 24);
}

uint32_t cdc_port_line_coding_count(void)
{
  return s_lc_count;
}

/* Voir cdc_port.h (appelée par usbd_conf.c, interruption USB) */
void cdc_port_on_suspend(bool suspended)
{
  s_suspended = suspended;
  if (!suspended)
  {
    tx_start_locked();
  }
}

/* Voir cdc_port.h */
void cdc_port_usb_stop(void)
{
  (void)USBD_Stop(&hUsbDeviceFS);         /* résistance de tirage de D+ retirée */
  (void)USBD_DeInit(&hUsbDeviceFS);
}

/* ---- Rappels de la classe CDC (interruption USB, via usbd_cdc_if.c) ------------------ */
/* Le PC a configuré la carte : tampons de la classe CDC désignés */
int8_t cdc_port_itf_init(void)
{
  s_tx_inflight = 0U;
  (void)USBD_CDC_SetTxBuffer(&hUsbDeviceFS, s_tx, 0U);
  (void)USBD_CDC_SetRxBuffer(&hUsbDeviceFS, s_rx_pkt);
  if (rx_free() < PKT_SIZE)
  {
    s_rx_tail = s_rx_head;                 /* données anciennes non lues : abandonnées */
  }
  s_rx_armed   = true;                     /* la classe arme le point d'accès OUT après ce rappel */
  s_suspended  = false;
  s_configured = true;
  return (int8_t)USBD_OK;
}

/* USB arrêté ou câble débranché */
int8_t cdc_port_itf_deinit(void)
{
  s_configured  = false;
  s_tx_inflight = 0U;                      /* transfert interrompu : les octets restent dans le tampon */
  s_rx_armed    = false;
  s_dtr         = false;
  s_open        = false;
  return (int8_t)USBD_OK;
}

/* Requêtes du PC : codage de ligne (débit...) et lignes DTR / RTS.
 * DTR passe à 1 quand un terminal ouvre le port, à 0 quand il le ferme. */
int8_t cdc_port_itf_control(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
  switch (cmd)
  {
    case CDC_SET_LINE_CODING:
      if (length >= 7U)
      {
        memcpy(s_line_coding, pbuf, 7U);
        s_lc_count++;
      }
      break;

    case CDC_GET_LINE_CODING:
      memcpy(pbuf, s_line_coding, 7U);
      break;

    case CDC_SET_CONTROL_LINE_STATE:
    {
      const USBD_SetupReqTypedef *req = (const USBD_SetupReqTypedef *)(void *)pbuf;
      bool dtr = (req->wValue & 0x0001U) != 0U;

      if (dtr && !s_dtr)
      {
        mark_open();                       /* terminal ouvert : envoi du contenu du tampon */
      }
      else if (!dtr && s_dtr)
      {
        s_open = false;                    /* terminal fermé */
      }
      else
      {
        /* pas de changement */
      }
      s_dtr = dtr;
      tx_start_locked();
      break;
    }

    default:
      break;
  }
  return (int8_t)USBD_OK;
}

/* Paquet reçu du PC : copié dans l'anneau de réception */
int8_t cdc_port_itf_receive(uint8_t *buf, uint32_t *len)
{
  uint32_t n = *len;
  uint32_t head = s_rx_head;
  uint32_t i;

  if (n > PKT_SIZE)
  {
    n = PKT_SIZE;
  }
  for (i = 0U; i < n; i++)                 /* place garantie : le point d'accès n'est armé */
  {                                        /* que s'il reste au moins PKT_SIZE octets libres */
    s_rx[head] = buf[i];
    head = (head + 1U) & (RX_SIZE - 1U);
  }
  s_rx_head = head;
  if (n > 0U)
  {
    mark_open();
  }
  if (rx_free() >= PKT_SIZE)
  {
    s_rx_armed = true;
    (void)USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  }
  else
  {
    s_rx_armed = false;                    /* hôte mis en attente jusqu'à la lecture */
  }
  tx_start_locked();
  return (int8_t)USBD_OK;
}

/* Transfert vers le PC terminé : les octets envoyés sont libérés, la suite part */
int8_t cdc_port_itf_tx_complete(uint8_t *buf, uint32_t *len, uint8_t epnum)
{
  (void)buf;
  (void)len;
  (void)epnum;
  s_tx_tail = (s_tx_tail + s_tx_inflight) & (TX_SIZE - 1U);
  s_tx_inflight = 0U;
  tx_start_locked();
  return (int8_t)USBD_OK;
}
