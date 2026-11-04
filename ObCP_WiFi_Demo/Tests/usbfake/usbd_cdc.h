/* Remplaçant PC minimal de la classe CDC de la bibliothèque USB de ST, pour
 * tester les tampons et le contrôle de flux de Core/Src/cdc_port.c (appelé par
 * USB_DEVICE/App/usbd_cdc_if.c, généré par CubeMX). */
#ifndef FAKE_USBD_CDC_H
#define FAKE_USBD_CDC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define __ALIGN_BEGIN
#define __ALIGN_END
#define __get_PRIMASK()      (fake_primask)
#define __disable_irq()      do { fake_primask = 1U; } while (0)
#define __set_PRIMASK(x)     do { fake_primask = (x); } while (0)
#define __DMB()              do { } while (0)
extern uint32_t fake_primask;

typedef enum { USBD_OK = 0, USBD_BUSY, USBD_EMEM, USBD_FAIL } USBD_StatusTypeDef;

typedef struct
{
  uint8_t  bmRequest;
  uint8_t  bRequest;
  uint16_t wValue;
  uint16_t wIndex;
  uint16_t wLength;
} USBD_SetupReqTypedef;

typedef struct { int dummy; } USBD_HandleTypeDef;

typedef struct _USBD_CDC_Itf
{
  int8_t (* Init)(void);
  int8_t (* DeInit)(void);
  int8_t (* Control)(uint8_t cmd, uint8_t *pbuf, uint16_t length);
  int8_t (* Receive)(uint8_t *Buf, uint32_t *Len);
  int8_t (* TransmitCplt)(uint8_t *Buf, uint32_t *Len, uint8_t epnum);
} USBD_CDC_ItfTypeDef;

#define CDC_DATA_FS_OUT_PACKET_SIZE   64U
#define CDC_SET_LINE_CODING           0x20U
#define CDC_GET_LINE_CODING           0x21U
#define CDC_SET_CONTROL_LINE_STATE    0x22U

uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff, uint32_t length);
uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff);
uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *pdev);
uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *pdev);

#endif
