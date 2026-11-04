/* Remplaçant PC minimal de usbd_core.h (bibliothèque USB de ST) pour
 * Core/Src/cdc_port.c : arrêt de l'USB avant un redémarrage. */
#ifndef FAKE_USBD_CORE_H
#define FAKE_USBD_CORE_H
#include "usbd_cdc.h"
USBD_StatusTypeDef USBD_Stop(USBD_HandleTypeDef *pdev);
USBD_StatusTypeDef USBD_DeInit(USBD_HandleTypeDef *pdev);
#endif
