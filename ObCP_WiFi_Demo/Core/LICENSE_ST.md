# Licence des fichiers générés par STM32CubeMX

Les fichiers suivants sont générés par STM32CubeMX à partir de `ObCP_WiFi_Demo.ioc` ou
copiés par lui depuis le paquet STM32Cube FW_L4 (licence BSD-3-Clause de
STMicroelectronics, texte ci-dessous) ; le code de la démo n'y figure que dans les
sections « USER CODE » :
`Core/Src/main.c`, `gpio.c`, `adc.c`, `rtc.c`, `spi.c`, `tim.c`, `stm32l4xx_it.c`,
`stm32l4xx_hal_msp.c`, `system_stm32l4xx.c`, `syscalls.c`, `sysmem.c`, les en-têtes
correspondants de `Core/Inc/` (dont `main.h` et `stm32l4xx_hal_conf.h`),
`Core/Startup/startup_stm32l476rgtx.s`, `STM32L476RGTX_FLASH.ld` et `STM32L476RGTX_RAM.ld`.

Les fichiers générés de `USB_DEVICE/` dérivent des modèles de la bibliothèque STM32 USB
Device : ils suivent sa licence (SLA0044, voir
`Middlewares/ST/STM32_USB_Device_Library/LICENSE.txt`).

Les autres fichiers de `Core/` (`board.c`, `cdc_port.c`, `fault_handlers.c`,
`uart_bridge.c`, `ncp_port_obcp.c`, `Core/Src/app/` et leurs en-têtes) sont écrits pour
la démo ; le pilote `Core/Src/st67w6x/` et la bibliothèque `Core/Src/qrcodegen/` ont
leur propre notice dans leur dossier.

---

Copyright 2017 STMicroelectronics.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.