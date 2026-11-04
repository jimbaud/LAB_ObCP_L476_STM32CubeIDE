/**
 ******************************************************************************
 * @file    Tests/compare/adapter_2024.c
 * @brief   Version 2024 (Tests/reference_2024 : Driver_OBCP.c, BLE_service.c,
 *          LIS3DSH.c) branchée sur la carte simulée. Le démarrage et la boucle
 *          reprennent les sections USER CODE de son main.c.
 *
 *  Le PC simulé lit instantanément tout ce que la carte envoie : c'est le
 *  cas le plus favorable à cette version (son CDC_Transmit_FS perdait les
 *  messages envoyés pendant un transfert en cours).
 ******************************************************************************
 */
#include <string.h>
#include "adapter.h"
#include "includes.h"

const char *adapter_name = "2024";

/* Tampon de réception USB, réutilisé à chaque paquet sans être effacé,
 * comme UserRxBufferFS dans usbd_cdc_if.c. */
static uint8_t  s_rx_buffer[2048];
static uint32_t s_rx_len;

/* Ce que le PC a reçu */
static char   s_host[65536];
static size_t s_host_len;

/* Remplace CDC_Transmit_FS de usbd_cdc_if.c (PC qui lit tout de suite). */
uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len)
{
  if (s_host_len + Len < sizeof(s_host))
  {
    memcpy(&s_host[s_host_len], Buf, Len);
    s_host_len += Len;
  }
  return 0U;   /* USBD_OK */
}

void adapter_init(void)
{
  LIS3DSH_InitTypeDef myAccConfigDef;

  /* USER CODE 2 du main.c de 2024 */
  MX_BlueNRG_MS_Init();
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
  HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
  myAccConfigDef.dataRate   = LIS3DSH_DATARATE_1;
  myAccConfigDef.fullScale  = LIS3DSH_FULLSCALE_2;
  myAccConfigDef.enableAxes = LIS3DSH_XYZ_ENABLE;
  LIS3DSH_Init(&hspi3, &myAccConfigDef);
}

void adapter_loop(void)
{
  /* boucle while (1) du main.c de 2024 */
  Bluetooh_LE_Process();
  receiveDataUSB();
  HAL_Delay(0U);                 /* le temps passe (1 ms) entre deux tours */
}

void adapter_usb_connect(void)
{
  USB_Connected = 1;             /* positionné par HAL_PCD_ResumeCallback en 2024 */
}

void adapter_usb_send(const char *data, size_t len)
{
  /* CDC_Receive_FS de 2024 */
  memcpy(s_rx_buffer, data, len);
  s_rx_len   = (uint32_t)len;
  UsbBuffer  = s_rx_buffer;
  USB_length = &s_rx_len;
  RxUSB_OK   = 1;
}

size_t adapter_usb_take(char *out, size_t size)
{
  size_t n = (s_host_len < size - 1U) ? s_host_len : size - 1U;

  memcpy(out, s_host, n);
  out[n] = '\0';
  memmove(s_host, &s_host[n], s_host_len - n);
  s_host_len -= n;
  return n;
}
