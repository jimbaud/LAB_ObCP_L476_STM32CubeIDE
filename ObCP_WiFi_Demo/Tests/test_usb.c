/*
 * Tests sur PC du port COM virtuel de l'ObCP (Core/Src/cdc_port.c, appelé par
 * les rappels de USB_DEVICE/App/usbd_cdc_if.c générés par CubeMX) :
 * tampons circulaires, contrôle de flux en réception, découpage des envois,
 * ouverture / fermeture du terminal (DTR), codage de ligne. La classe CDC de
 * ST est remplacée par usbfake/usbd_cdc.h : chaque envoi et chaque réarmement
 * du point d'accès OUT est enregistré.
 *   make -C Tests && ./Tests/build/test_usb
 */
#include <stdio.h>
#include <string.h>
#include "usbd_cdc_if.h"
#include "usbd_core.h"
#include "cdc_port.h"

static int g_fail, g_pass;
#define CHECK(c) do { if (c) { g_pass++; } else { g_fail++; printf("  ECHEC %s:%d : %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- classe CDC simulée -------------------------------------------------------- */
uint32_t           fake_primask;
USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t *s_txbuf;
static uint32_t s_txlen;
static bool     s_busy;
static int      s_transmits;
static int      s_rearms;
static uint8_t *s_rxbuf;
static uint8_t  s_host_rx[16384];          /* octets reçus par le PC */
static bool     s_in_isr;                  /* rappel appelé « dans l'interruption USB » */
static size_t   s_host_rx_len;

uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff, uint32_t length)
{
  (void)pdev;
  s_txbuf = pbuff;
  s_txlen = length;
  return USBD_OK;
}

uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff)
{
  (void)pdev;
  s_rxbuf = pbuff;
  return USBD_OK;
}

uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *pdev)
{
  (void)pdev;
  CHECK((fake_primask != 0U) || s_in_isr); /* interruptions masquées ou interruption USB */
  if (s_busy)
  {
    return USBD_BUSY;
  }
  s_busy = true;
  s_transmits++;
  return USBD_OK;
}

uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *pdev)
{
  (void)pdev;
  s_rearms++;
  return USBD_OK;
}

static int s_usb_stops;

USBD_StatusTypeDef USBD_Stop(USBD_HandleTypeDef *pdev)
{
  (void)pdev;
  s_usb_stops++;
  return USBD_OK;
}

USBD_StatusTypeDef USBD_DeInit(USBD_HandleTypeDef *pdev)
{
  (void)pdev;
  s_usb_stops += 10;
  return USBD_OK;
}

/* Fin d'un transfert IN : le PC « reçoit » les octets */
static void host_complete(void)
{
  uint32_t len = s_txlen;
  CHECK(s_busy);
  memcpy(&s_host_rx[s_host_rx_len], s_txbuf, s_txlen);
  s_host_rx_len += s_txlen;
  s_busy = false;
  s_in_isr = true;
  (void)USBD_Interface_fops_FS.TransmitCplt(s_txbuf, &len, 1U);
  s_in_isr = false;
}

static void host_drain(void)
{
  int guard = 0;
  while (s_busy && (guard++ < 1000))
  {
    host_complete();
  }
}

static void set_dtr(bool on)
{
  USBD_SetupReqTypedef req = { 0x21U, CDC_SET_CONTROL_LINE_STATE, (uint16_t)(on ? 3U : 0U), 0U, 0U };
  s_in_isr = true;
  (void)USBD_Interface_fops_FS.Control(CDC_SET_CONTROL_LINE_STATE, (uint8_t *)&req, 0U);
  s_in_isr = false;
}

static void host_send(const uint8_t *d, uint32_t n)
{
  uint8_t pkt[64];
  memcpy(pkt, d, n);
  memcpy(s_rxbuf, pkt, n);               /* la classe reçoit dans le tampon fourni */
  s_in_isr = true;
  (void)USBD_Interface_fops_FS.Receive(s_rxbuf, &n);
  s_in_isr = false;
}

int main(void)
{
  uint8_t  big[6000];
  uint8_t  pkt[64];
  uint8_t  lc[7];
  size_t   i;
  size_t   n;
  int      rearms;

  printf("Tests du port COM virtuel USB (PC)\n");
  for (i = 0U; i < sizeof(big); i++)
  {
    big[i] = (uint8_t)(i * 7U + 3U);
  }

  printf("- avant énumération : tampon conservé\n");
  CHECK(!cdc_port_configured() && !cdc_port_open());
  n = cdc_port_write((const uint8_t *)"Bonjour\n", 8U);
  CHECK(n == 8U);
  CHECK(s_transmits == 0);

  printf("- énumération puis ouverture du terminal (DTR)\n");
  CHECK(USBD_Interface_fops_FS.Init() == 0);
  CHECK(s_rxbuf != NULL);
  CHECK(cdc_port_configured());
  CHECK(!cdc_port_open());
  CHECK(s_transmits == 0);                /* pas d'envoi tant que le terminal est fermé */
  set_dtr(true);
  CHECK(cdc_port_open() && cdc_port_dtr());
  CHECK(cdc_port_open_count() == 1U);
  CHECK(s_transmits == 1);                /* le message de démarrage part à l'ouverture */
  host_drain();
  CHECK((s_host_rx_len == 8U) && (memcmp(s_host_rx, "Bonjour\n", 8U) == 0));
  CHECK(cdc_port_tx_empty());
  CHECK(fake_primask == 0U);

  printf("- envoi long : découpage en transferts de 512 octets, tampon circulaire\n");
  s_host_rx_len = 0U;
  s_transmits = 0;
  {
    size_t sent = 0U;
    while (sent < sizeof(big))
    {
      size_t k = cdc_port_write(&big[sent], sizeof(big) - sent);
      sent += k;
      CHECK(s_txlen <= 512U);
      host_complete();                    /* le PC lit au fur et à mesure */
    }
    host_drain();
  }
  CHECK(s_host_rx_len == sizeof(big));
  CHECK(memcmp(s_host_rx, big, sizeof(big)) == 0);
  CHECK(s_transmits >= 12);

  printf("- tampon plein, terminal fermé : écriture partielle\n");
  set_dtr(false);
  CHECK(!cdc_port_open());
  s_host_rx_len = 0U;
  n = cdc_port_write(big, sizeof(big));
  CHECK(n == 4095U);                      /* 4 Ko - 1 */
  CHECK(cdc_port_tx_free() == 0U);
  CHECK(!s_busy);
  set_dtr(true);                          /* réouverture : tout part */
  CHECK(cdc_port_open_count() == 2U);
  host_drain();
  CHECK((s_host_rx_len == 4095U) && (memcmp(s_host_rx, big, 4095U) == 0));

  printf("- oubli des octets non envoyés (passerelle)\n");
  s_host_rx_len = 0U;
  set_dtr(false);
  (void)cdc_port_write(big, 100U);
  cdc_port_tx_discard();
  CHECK(cdc_port_tx_empty());
  set_dtr(true);
  host_drain();
  CHECK(s_host_rx_len == 0U);
  (void)cdc_port_write(big, 600U);        /* transfert de 512 en cours puis oubli */
  CHECK(s_busy && (s_txlen == 512U));
  cdc_port_tx_discard();
  host_drain();
  CHECK(s_host_rx_len == 512U);           /* seuls les octets déjà partis arrivent */

  printf("- envoi malgré DTR à 0 (passerelle « module boot »)\n");
  s_host_rx_len = 0U;
  set_dtr(false);
  cdc_port_set_always_send(true);
  (void)cdc_port_write((const uint8_t *)"OK", 2U);
  host_drain();
  CHECK((s_host_rx_len == 2U) && (memcmp(s_host_rx, "OK", 2U) == 0));
  cdc_port_set_always_send(false);
  set_dtr(true);

  printf("- réception : contrôle de flux par paquets de 64 octets\n");
  rearms = s_rearms;
  for (i = 0U; i < 64U; i++)
  {
    pkt[i] = (uint8_t)i;
  }
  for (i = 0U; i < 31U; i++)              /* 31 x 64 = 1984 octets : il reste 63 octets libres */
  {
    host_send(pkt, 64U);
  }
  CHECK(cdc_port_rx_count() == 1984U);
  CHECK(s_rearms == rearms + 30);         /* plus de réarmement après le 31e paquet */
  CHECK(cdc_port_getc() == 0);            /* lecture d'un octet : 64 libres -> réarmé */
  CHECK(s_rearms == rearms + 31);
  host_send(pkt, 64U);                    /* le paquet tient dans la place réservée */
  CHECK(cdc_port_rx_count() == 2047U);
  CHECK(s_rearms == rearms + 31);
  {
    uint8_t buf[2048];
    size_t  got = cdc_port_read(buf, sizeof(buf));
    CHECK(got == 2047U);
    CHECK((buf[0] == 1U) && (buf[62] == 63U) && (buf[63] == 0U));   /* ordre conservé */
  }
  CHECK(s_rearms == rearms + 32);
  CHECK(cdc_port_getc() == -1);
  host_send((const uint8_t *)"x", 1U);
  cdc_port_rx_flush();
  CHECK(cdc_port_rx_count() == 0U);

  printf("- réception sans DTR : le terminal est considéré ouvert\n");
  set_dtr(false);
  CHECK(!cdc_port_open());
  n = cdc_port_open_count();
  host_send((const uint8_t *)"help\r", 5U);
  CHECK(cdc_port_open());
  CHECK(cdc_port_open_count() == n + 1U);
  cdc_port_rx_flush();

  printf("- codage de ligne (débit choisi par le PC)\n");
  CHECK(cdc_port_baudrate() == 115200U);
  lc[0] = 0x80U; lc[1] = 0x84U; lc[2] = 0x1EU; lc[3] = 0x00U;          /* 2 000 000 bit/s */
  lc[4] = 0U; lc[5] = 0U; lc[6] = 8U;
  (void)USBD_Interface_fops_FS.Control(CDC_SET_LINE_CODING, lc, 7U);
  CHECK(cdc_port_baudrate() == 2000000U);
  CHECK(cdc_port_line_coding_count() == 1U);
  memset(lc, 0, sizeof(lc));
  (void)USBD_Interface_fops_FS.Control(CDC_GET_LINE_CODING, lc, 7U);
  CHECK((lc[0] == 0x80U) && (lc[2] == 0x1EU) && (lc[6] == 8U));

  printf("- veille de l'hôte, réinitialisation USB\n");
  s_in_isr = true;
  cdc_port_on_suspend(true);
  s_in_isr = false;
  CHECK(!cdc_port_configured() && !cdc_port_open());
  (void)cdc_port_write((const uint8_t *)"zz", 2U);
  CHECK(!s_busy);
  s_in_isr = true;
  cdc_port_on_suspend(false);
  s_in_isr = false;
  CHECK(cdc_port_open());
  host_drain();
  (void)USBD_Interface_fops_FS.DeInit();
  CHECK(!cdc_port_configured() && !cdc_port_open() && !cdc_port_dtr());
  for (i = 0U; i < 40U; i++)              /* données anciennes : tampon presque plein */
  {
    uint32_t len = 50U;
    (void)USBD_Interface_fops_FS.Init();
    memcpy(s_rxbuf, pkt, 50U);
    (void)USBD_Interface_fops_FS.Receive(s_rxbuf, &len);
  }
  (void)USBD_Interface_fops_FS.Init();     /* moins de 64 octets libres : tampon vidé */
  CHECK(cdc_port_rx_count() == 0U);
  CHECK(fake_primask == 0U);

  printf("- CDC_Transmit_FS (fonction publique générée par CubeMX) passe par le tampon\n");
  set_dtr(true);
  host_drain();
  n = s_host_rx_len;
  CHECK(CDC_Transmit_FS((uint8_t *)"CubeMX", 6U) == USBD_OK);
  host_drain();
  CHECK((s_host_rx_len == n + 6U) && (memcmp(&s_host_rx[n], "CubeMX", 6U) == 0));
  s_busy = true;                          /* transfert en cours : la donnée attend dans le tampon */
  CHECK(CDC_Transmit_FS((uint8_t *)"!", 1U) == USBD_OK);
  s_busy = false;
  CHECK(!cdc_port_tx_empty());

  printf("- arrêt de l'USB avant un redémarrage\n");
  cdc_port_usb_stop();
  CHECK(s_usb_stops == 11);

  printf("%d vérifications réussies, %d échec(s)\n", g_pass, g_fail);
  return (g_fail == 0) ? 0 : 1;
}
