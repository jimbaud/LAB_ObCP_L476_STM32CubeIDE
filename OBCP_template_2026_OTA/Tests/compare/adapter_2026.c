/**
 ******************************************************************************
 * @file    Tests/compare/adapter_2026.c
 * @brief   Version 2026 (Core/Src) branchée sur la carte simulée.
 ******************************************************************************
 */
#include "adapter.h"
#include "app.h"
#include "fake_usb.h"

const char *adapter_name = "2026";

void adapter_init(void)
{
  app_init();
}

void adapter_loop(void)
{
  app_loop();                    /* __WFI() simulé : le temps avance de 1 ms */
  fake_usb_host_read();
  (void)fake_usb_pump();
}

void adapter_usb_connect(void)
{
  fake_usb_connect();
}

void adapter_usb_send(const char *data, size_t len)
{
  fake_usb_host_send(data, len);
}

size_t adapter_usb_take(char *out, size_t size)
{
  return fake_usb_host_take(out, size);
}
