/**
 ******************************************************************************
 * @file    usb_console.c
 * @brief   Port COM virtuel USB : réception des lignes, émission par tampon
 *          (voir usb_console.h pour le principe).
 *
 *  Variables partagées avec les interruptions USB : elles sont « volatile »
 *  (le compilateur les relit à chaque usage) et les opérations qui en
 *  modifient plusieurs à la fois se font interruptions masquées (section
 *  critique, voir irq_lock()), pour qu'une interruption ne tombe jamais au
 *  milieu.
 ******************************************************************************
 */
#include "usb_console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "fifo.h"
#include "usbd_cdc_if.h"          /* bibliothèque USB de ST, classe CDC */

/* Structure de gestion du périphérique USB, définie dans usb_device.c
 * (code généré par CubeMX). */
extern USBD_HandleTypeDef hUsbDeviceFS;

/* -------------------------------------------------------------------------- */
/* Sections critiques                                                         */
/* -------------------------------------------------------------------------- */

/* Masque les interruptions et rend l'état précédent (registre PRIMASK du
 * Cortex-M4), pour que irq_unlock() le rétablisse tel qu'il était : on peut
 * ainsi appeler ces fonctions même quand les interruptions sont déjà
 * masquées (par exemple depuis une interruption).                         */
static inline uint32_t irq_lock(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static inline void irq_unlock(uint32_t primask)
{
  __set_PRIMASK(primask);
}

/* -------------------------------------------------------------------------- */
/* État                                                                       */
/* -------------------------------------------------------------------------- */

/* Émission : tampon circulaire, et morceau en cours d'envoi (le pilote USB
 * lit ces octets pendant le transfert : ils ne doivent pas bouger).        */
static uint8_t  s_tx_storage[USB_TX_FIFO_SIZE];
static fifo_t   s_tx = FIFO_INIT(s_tx_storage);
static uint8_t  s_tx_chunk[USB_TX_CHUNK_SIZE];

/* État de la liaison, modifié par les interruptions USB. */
static volatile bool     s_configured;  /* énumération terminée */
static volatile bool     s_suspended;   /* bus en veille */
static volatile bool     s_link_up;     /* évènement « USB (re)connecté » */
static volatile uint32_t s_session;     /* change à chaque (dé)configuration */
static volatile bool     s_line_reset;  /* ligne en cours à oublier (nouvelle session) */

/* Réception : paquet en attente de lecture (dans le tampon du pilote USB). */
static uint8_t *volatile s_rx_buf;      /* adresse du paquet */
static volatile uint32_t s_rx_len;      /* nombre d'octets, 0 = pas de paquet */
static volatile uint32_t s_rx_session;  /* session à laquelle appartient le paquet */
static uint32_t          s_rx_pos;      /* prochain octet à lire */
static bool              s_rx_started;  /* lecture du paquet commencée */
static bool              s_rx_whole;    /* paquet = commande complète (programme) */
static line_builder_t    s_line;        /* ligne en cours d'assemblage */

/* -------------------------------------------------------------------------- */
/* Réception                                                                  */
/* -------------------------------------------------------------------------- */

/* Copie la ligne assemblée dans la destination de l'appelant. */
static void copy_line(char *line, size_t size, line_status_t status)
{
  if (size == 0U)
  {
    return;
  }
  if (status == LINE_READY)
  {
    (void)strncpy(line, s_line.text, size - 1U);
    line[size - 1U] = '\0';
  }
  else
  {
    line[0] = '\0';                     /* ligne rejetée : rien à copier */
  }
}

/* Paquet entièrement lu : on autorise l'arrivée du suivant. */
static void finish_packet(void)
{
  uint32_t primask = irq_lock();

  s_rx_len     = 0U;
  s_rx_started = false;
  /* Réarmer la réception, sauf si l'USB a été déconnecté puis reconnecté
   * entre-temps : la bibliothèque l'a alors déjà réarmée (USBD_CDC_Init). */
  if (s_configured && (s_rx_session == s_session))
  {
    (void)USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  }
  irq_unlock(primask);
}

/* Voir usb_console.h */
line_status_t usb_console_read_line(char *line, size_t size)
{
  uint32_t      len = s_rx_len;         /* une seule lecture de la variable partagée */
  line_status_t status;

  if (s_line_reset)
  {
    s_line_reset = false;
    line_builder_reset(&s_line);        /* reste d'une ligne d'avant la reconnexion */
  }
  if (len == 0U)
  {
    return LINE_NONE;                   /* aucun paquet en attente */
  }

  if (!s_rx_started)
  {
    s_rx_started = true;
    s_rx_pos     = 0U;
    /* Paquet de plusieurs caractères qui commence une ligne : envoyé par un
     * programme, sa fin vaut fin de ligne. Un caractère seul : frappe. */
    s_rx_whole   = (len > 1U) && line_builder_is_empty(&s_line);
  }

  while (s_rx_pos < len)
  {
    char c = (char)s_rx_buf[s_rx_pos];
    s_rx_pos++;
    status = line_builder_put(&s_line, c);
    if (status != LINE_NONE)
    {
      copy_line(line, size, status);
      return status;                    /* la suite du paquet au prochain appel */
    }
  }

  if (s_rx_whole)
  {
    s_rx_whole = false;
    status = line_builder_end(&s_line);
    if (status != LINE_NONE)
    {
      copy_line(line, size, status);
      return status;
    }
  }

  finish_packet();
  return LINE_NONE;
}

/* -------------------------------------------------------------------------- */
/* Émission                                                                   */
/* -------------------------------------------------------------------------- */

/* Voir usb_console.h */
bool usb_console_write(const uint8_t *data, uint16_t len)
{
  uint32_t primask;
  bool     ok;

  if (len == 0U)
  {
    return true;
  }
  if (!usb_console_is_ready())
  {
    return false;                       /* pas de PC pour lire : abandon */
  }
  primask = irq_lock();
  ok = fifo_write(&s_tx, data, len);    /* tout ou rien */
  irq_unlock(primask);
  return ok;
}

/* Voir usb_console.h */
bool usb_console_print(const char *text)
{
  size_t len = strlen(text);

  if (len > 0xFFFFU)
  {
    return false;
  }
  return usb_console_write((const uint8_t *)text, (uint16_t)len);
}

/* Voir usb_console.h */
bool usb_console_printf(const char *format, ...)
{
  char    text[128];
  va_list args;
  int     n;

  va_start(args, format);
  n = vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  if (n < 0)
  {
    return false;
  }
  return usb_console_print(text);       /* texte tronqué à 127 caractères si besoin */
}

/* Voir usb_console.h */
void usb_console_process(void)
{
  const USBD_CDC_HandleTypeDef *hcdc;
  uint16_t                      n;
  uint32_t                      primask;

  if (!usb_console_is_ready())
  {
    return;
  }
  hcdc = (const USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
  if ((hcdc == NULL) || (hcdc->TxState != 0U))
  {
    return;                             /* transfert précédent pas terminé */
  }

  primask = irq_lock();
  n = fifo_peek(&s_tx, 0U, s_tx_chunk, (uint16_t)sizeof(s_tx_chunk));
  irq_unlock(primask);
  if (n == 0U)
  {
    return;                             /* rien à envoyer */
  }

  /* Le pilote envoie les n octets en paquets de 64 octets, puis remet
   * TxState à 0 (dans une interruption) quand le PC a tout reçu. Les octets
   * ne quittent le tampon qu'une fois le transfert accepté.             */
  (void)USBD_CDC_SetTxBuffer(&hUsbDeviceFS, s_tx_chunk, n);
  if (USBD_CDC_TransmitPacket(&hUsbDeviceFS) == (uint8_t)USBD_OK)
  {
    primask = irq_lock();
    fifo_drop(&s_tx, n);
    irq_unlock(primask);
  }
}

/* Voir usb_console.h */
bool usb_console_tx_pending(void)
{
  const USBD_CDC_HandleTypeDef *hcdc;
  uint32_t                      primask;
  bool                          pending;

  if (!usb_console_is_ready())
  {
    return false;                       /* plus de PC : rien ne partira */
  }
  hcdc = (const USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
  primask = irq_lock();
  pending = (fifo_used(&s_tx) > 0U) || ((hcdc != NULL) && (hcdc->TxState != 0U));
  irq_unlock(primask);
  return pending;
}

/* -------------------------------------------------------------------------- */
/* État de la liaison                                                         */
/* -------------------------------------------------------------------------- */

/* Voir usb_console.h */
bool usb_console_is_ready(void)
{
  return s_configured && !s_suspended;
}

/* Voir usb_console.h */
bool usb_console_take_link_up_event(void)
{
  uint32_t primask = irq_lock();
  bool     event   = s_link_up;

  s_link_up = false;
  irq_unlock(primask);
  return event && usb_console_is_ready();
}

/* -------------------------------------------------------------------------- */
/* Rappels de la bibliothèque USB (appelés en interruption)                   */
/* -------------------------------------------------------------------------- */

/* Voir usb_console.h */
void usb_console_on_init(void)
{
  s_session++;
  s_rx_len     = 0U;                    /* paquet d'une session précédente : oublié */
  s_line_reset = true;
  fifo_clear(&s_tx);                    /* messages d'avant la connexion : oubliés */
  s_suspended  = false;
  s_configured = true;
  s_link_up    = true;
}

/* Voir usb_console.h */
void usb_console_on_deinit(void)
{
  s_configured = false;
  s_session++;
  s_rx_len     = 0U;
  s_link_up    = false;
  fifo_clear(&s_tx);
}

/* Voir usb_console.h */
void usb_console_on_receive(uint8_t *buf, uint32_t len)
{
  if (len == 0U)
  {
    /* Paquet vide (le PC termine ainsi un envoi multiple de 64 octets) :
     * rien à lire, on réarme tout de suite la réception.               */
    (void)USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return;
  }
  s_rx_buf     = buf;
  s_rx_session = s_session;
  s_rx_len     = len;                   /* écrit en dernier : le paquet est prêt */
}

/* Voir usb_console.h */
void usb_console_on_suspend(void)
{
  s_suspended = true;
  s_link_up   = false;
  fifo_clear(&s_tx);                    /* plus personne pour lire */
}

/* Voir usb_console.h */
void usb_console_on_resume(void)
{
  s_suspended = false;
  if (s_configured)
  {
    s_link_up = true;                   /* comme la version 2024 : USB_Connected = 1 */
  }
}
