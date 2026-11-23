# =============================================================================
#  Réseau maillé Bluetooth - ObCP (STM32L476RG) + module BlueNRG-M0 ou SPBTLE-RF
#
#  Compilation en ligne de commande (alternative à STM32CubeIDE) :
#     make                  -> build/Release/ObCP_BLE_Mesh_Demo.elf / .hex / .bin
#     make DEBUG=1          -> build/Debug/...      (-O0 -g3, comme la configuration Debug)
#     make OTA=1            -> build/Release_OTA/... version rangée à 0x08080000,
#                              chargée par Bluetooth derrière le chargeur ObCP-OTA
#     make flash            -> programmation par USB (chargeur DFU de ST) avec
#                              STM32_Programmer_CLI : ObCP en mode Prog + Reset
#     make test             -> tests sur PC (gcc natif), voir Tests/Makefile
#     make binaires         -> versions USB et OTA copiées dans Binaries/
#     make clean
#  Prérequis : GNU Arm Embedded Toolchain (arm-none-eabi-gcc) dans le PATH,
#  ou PREFIX=/chemin/vers/arm-none-eabi-
#
#  Mêmes fichiers, mêmes chemins d'en-têtes, mêmes définitions et même script
#  d'édition de liens que le projet STM32CubeIDE (.cproject), qui n'utilise
#  pas ce fichier.
# =============================================================================

TARGET  := ObCP_BLE_Mesh_Demo
PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

ifeq ($(DEBUG),1)
  CONFIG := Debug
  OPT    := -O0 -g3
  DEFS   := -DDEBUG
else
  CONFIG := Release
  OPT    := -Os -g0
  DEFS   :=
endif
# Version chargée par Bluetooth : seul le script d'édition de liens change
# (ORIGIN = 0x8080000) ; le programme reconnaît sa version tout seul.
ifeq ($(OTA),1)
  CONFIG   := $(CONFIG)_OTA
  LDSCRIPT := STM32L476RGTX_FLASH_OTA.ld
else
  LDSCRIPT := STM32L476RGTX_FLASH.ld
endif
BUILD := build/$(CONFIG)

# Dossiers de sources déclarés dans .cproject (Source Location)
SRC_DIRS := Core Drivers Middlewares USB_DEVICE BlueNRG-MS
C_SRCS   := $(shell find $(SRC_DIRS) -name '*.c')
S_SRCS   := $(shell find $(SRC_DIRS) -name '*.s')

# Chemins d'inclusion : ceux que CubeMX écrit dans .cproject (le code de la
# démo n'en demande aucun autre, ses en-têtes sont dans Core/Inc).
INCLUDES := \
  -ICore/Inc \
  -IDrivers/STM32L4xx_HAL_Driver/Inc \
  -IDrivers/STM32L4xx_HAL_Driver/Inc/Legacy \
  -IDrivers/CMSIS/Device/ST/STM32L4xx/Include \
  -IDrivers/CMSIS/Include \
  -IMiddlewares/ST/BlueNRG-MS/utils \
  -IMiddlewares/ST/BlueNRG-MS/includes \
  -IMiddlewares/ST/BlueNRG-MS/hci/hci_tl_patterns/Basic \
  -IBlueNRG-MS/Target \
  -IUSB_DEVICE/App \
  -IUSB_DEVICE/Target \
  -IMiddlewares/ST/STM32_USB_Device_Library/Core/Inc \
  -IMiddlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc

MCU     := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS  := $(MCU) -std=gnu11 $(OPT) $(DEFS) -DUSE_HAL_DRIVER -DSTM32L476xx $(INCLUDES) \
           -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP --specs=nano.specs
ASFLAGS := $(MCU) $(DEFS) -x assembler-with-cpp --specs=nano.specs
# -u _printf_float : option « Use float with printf » cochée dans .cproject
LDFLAGS := $(MCU) -T$(LDSCRIPT) --specs=nosys.specs --specs=nano.specs \
           -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--gc-sections -static \
           -u _printf_float -Wl,--print-memory-usage \
           -Wl,--start-group -lc -lm -Wl,--end-group

OBJS := $(patsubst %.c,$(BUILD)/obj/%.o,$(C_SRCS)) $(patsubst %.s,$(BUILD)/obj/%.o,$(S_SRCS))

.PHONY: all size flash test binaires clean

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin size

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: %.s
	@mkdir -p $(dir $@)
	@echo "AS  $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	@echo "LD  $@"
	@$(CC) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	@$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	@$(OBJCOPY) -O binary $< $@

size: $(BUILD)/$(TARGET).elf
	@$(SIZE) $<

# Le fichier .elf porte ses adresses : 0x08000000 (version USB) ou
# 0x08080000 (version OTA, sans effacer le chargeur placé à 0x08000000).
flash: $(BUILD)/$(TARGET).elf
	STM32_Programmer_CLI -c port=usb1 -w $< -v -s

test:
	$(MAKE) -C Tests

# Programmes précompilés du dossier Binaries/ (versions USB et OTA, Release)
binaires:
	$(MAKE) --no-print-directory
	$(MAKE) --no-print-directory OTA=1
	cp build/Release/$(TARGET).hex build/Release/$(TARGET).bin build/Release/$(TARGET).elf Binaries/
	cp build/Release_OTA/$(TARGET).hex Binaries/$(TARGET)_OTA.hex
	cp build/Release_OTA/$(TARGET).elf Binaries/$(TARGET)_OTA.elf

clean:
	rm -rf build
	$(MAKE) -C Tests clean

-include $(OBJS:.o=.d)
