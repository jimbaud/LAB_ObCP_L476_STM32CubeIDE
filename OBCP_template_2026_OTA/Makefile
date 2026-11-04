# ----------------------------------------------------------------------------
# Compilation en ligne de commande (Linux, macOS, WSL) avec GNU Arm Embedded
# Toolchain (arm-none-eabi-gcc), sans STM32CubeIDE.
#
#   make              version Release (optimisée -Os), dans build/Release
#   make DEBUG=1      version Debug (-O0 -g3), dans build/Debug
#   make clean        efface le dossier build
#
# Les options sont celles du projet STM32CubeIDE (.cproject) : mêmes
# fichiers, mêmes chemins d'en-têtes, mêmes définitions, même script
# d'édition de liens. STM32CubeIDE n'utilise pas ce fichier.
# ----------------------------------------------------------------------------

TARGET  := OBCP_template_2026
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
BUILD := build/$(CONFIG)

# Dossiers de sources déclarés dans .cproject
SRC_DIRS := Core Drivers Middlewares USB_DEVICE BlueNRG-MS
C_SRCS   := $(shell find $(SRC_DIRS) -name '*.c')
S_SRCS   := $(shell find $(SRC_DIRS) -name '*.s')

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
# -u _printf_float : snprintf("%.2f") pour les accélérations (newlib-nano)
LDFLAGS := $(MCU) -TSTM32L476RGTX_FLASH.ld --specs=nosys.specs --specs=nano.specs \
           -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--gc-sections -static \
           -u _printf_float -Wl,--start-group -lc -lm -Wl,--end-group

OBJS := $(patsubst %.c,$(BUILD)/obj/%.o,$(C_SRCS)) $(patsubst %.s,$(BUILD)/obj/%.o,$(S_SRCS))

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin
	@$(SIZE) $(BUILD)/$(TARGET).elf

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf build

-include $(OBJS:.o=.d)

.PHONY: all clean
