/**
 ******************************************************************************
 * @file    Tests/fakes/fake_usb.c
 * @brief   Remplaçant PC de la bibliothèque USB de ST (classe CDC) et du PC
 *          qui lit et écrit le port COM virtuel. Utilise le vrai
 *          USB_DEVICE/App/usbd_cdc_if.c (généré par CubeMX) : les tests
 *          vérifient donc aussi les lignes ajoutées dans ses sections
 *          USER CODE.
 ******************************************************************************
 */
#include <string.h>
#include "fake_usb.h"
#include "usbd_cdc_if.h"
#include "usb_console.h"

USBD_HandleTypeDef hUsbDeviceFS;
static USBD_CDC_HandleTypeDef s_hcdc;
static bool s_rx_armed;

int  fake_usb_rearm_count;
int  fake_usb_transfers;
bool fake_usb_host_reads = true;

/* Paquets en attente côté PC */
#define HOST_QUEUE 64
static char   s_host_pkt[HOST_QUEUE][64];
static size_t s_host_len[HOST_QUEUE];
static size_t s_host_head, s_host_count;

/* Octets reçus par le PC */
static char   s_host_rx[65536];
static size_t s_host_rx_len;

/* Transfert IN en cours */
static uint8_t s_inflight[4096];
static size_t  s_inflight_len;

/* ---- Fonctions de la bibliothèque USB utilisées par le programme --------- */
uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff, uint32_t length)
{
  (void)pdev;
  s_hcdc.TxBuffer = pbuff;
  s_hcdc.TxLength = length;
  return USBD_OK;
}

uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff)
{
  (void)pdev;
  s_hcdc.RxBuffer = pbuff;
  return USBD_OK;
}

uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *pdev)
{
  (void)pdev;
  if (hUsbDeviceFS.pClassData == NULL)
  {
    return USBD_FAIL;
  }
  if (s_hcdc.TxState != 0U)
  {
    return USBD_BUSY;
  }
  s_hcdc.TxState = 1U;
  memcpy(s_inflight, s_hcdc.TxBuffer, s_hcdc.TxLength);
  s_inflight_len = s_hcdc.TxLength;
  fake_usb_transfers++;
  return USBD_OK;
}

uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *pdev)
{
  (void)pdev;
  if (hUsbDeviceFS.pClassData == NULL)
  {
    return USBD_FAIL;
  }
  fake_usb_rearm_count++;
  s_rx_armed = true;
  return USBD_OK;
}

/* ---- Côté PC -------------------------------------------------------------- */
void fake_usb_connect(void)
{
  memset(&s_hcdc, 0, sizeof(s_hcdc));
  hUsbDeviceFS.pClassData = &s_hcdc;
  hUsbDeviceFS.dev_state  = USBD_STATE_CONFIGURED;
  (void)USBD_Interface_fops_FS.Init();
  s_rx_armed = true;                     /* USBD_CDC_Init arme la réception */
}

void fake_usb_disconnect(void)
{
  (void)USBD_Interface_fops_FS.DeInit();
  hUsbDeviceFS.pClassData = NULL;
  hUsbDeviceFS.dev_state  = USBD_STATE_DEFAULT;
  s_rx_armed     = false;
  s_inflight_len = 0U;
}

void fake_usb_suspend(void)
{
  usb_console_on_suspend();
}

void fake_usb_resume(void)
{
  usb_console_on_resume();
}

void fake_usb_host_send(const char *data, size_t len)
{
  size_t slot;

  if ((s_host_count >= HOST_QUEUE) || (len > 64U) || (hUsbDeviceFS.pClassData == NULL))
  {
    return;                              /* port absent : le PC ne peut rien envoyer */
  }
  slot = (s_host_head + s_host_count) % HOST_QUEUE;
  memcpy(s_host_pkt[slot], data, len);
  s_host_len[slot] = len;
  s_host_count++;
  (void)fake_usb_pump();
}

void fake_usb_host_send_str(const char *text)
{
  fake_usb_host_send(text, strlen(text));
}

bool fake_usb_pump(void)
{
  static uint32_t len;

  if ((s_host_count == 0U) || !s_rx_armed || (hUsbDeviceFS.pClassData == NULL))
  {
    return false;
  }
  s_rx_armed = false;
  /* Le paquet est écrit au début du tampon de réception, sans effacer la
   * suite : comme sur la carte (tampon UserRxBufferFS réutilisé). */
  memcpy(s_hcdc.RxBuffer, s_host_pkt[s_host_head], s_host_len[s_host_head]);
  len = (uint32_t)s_host_len[s_host_head];
  s_host_head = (s_host_head + 1U) % HOST_QUEUE;
  s_host_count--;
  s_hcdc.RxLength = len;
  (void)USBD_Interface_fops_FS.Receive(s_hcdc.RxBuffer, &len);
  return true;
}

size_t fake_usb_host_pending(void)
{
  return s_host_count;
}

void fake_usb_host_read(void)
{
  if (!fake_usb_host_reads || (s_hcdc.TxState == 0U))
  {
    return;
  }
  if (s_host_rx_len + s_inflight_len < sizeof(s_host_rx))
  {
    memcpy(&s_host_rx[s_host_rx_len], s_inflight, s_inflight_len);
    s_host_rx_len += s_inflight_len;
  }
  s_inflight_len = 0U;
  s_hcdc.TxState = 0U;                   /* fin du transfert (USBD_CDC_DataIn) */
}

size_t fake_usb_host_take(char *out, size_t size)
{
  size_t n = (s_host_rx_len < size - 1U) ? s_host_rx_len : size - 1U;

  memcpy(out, s_host_rx, n);
  out[n] = '\0';
  memmove(s_host_rx, &s_host_rx[n], s_host_rx_len - n);
  s_host_rx_len -= n;
  return n;
}
