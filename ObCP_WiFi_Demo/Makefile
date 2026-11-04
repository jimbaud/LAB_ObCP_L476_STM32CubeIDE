# =============================================================================
#  Démonstrateur Wi-Fi 6 ST67W611M1 - ObCP (STM32L476RG) + X-NUCLEO-67W61M1
#
#  Compilation en ligne de commande (alternative à STM32CubeIDE) :
#     make                  -> build/ObCP_WiFi_Demo.elf / .hex / .bin
#     make flash            -> programmation par USB (chargeur DFU de ST) avec
#                              STM32_Programmer_CLI : ObCP en mode Prog + Reset,
#                              ou commande « dfu » dans le terminal
#     make test             -> tests unitaires sur PC (gcc natif)
#     make clean
#  Prérequis : GNU Arm Embedded Toolchain (arm-none-eabi-gcc) dans le PATH,
#  ou PREFIX=/chemin/vers/arm-none-eabi-
# =============================================================================

TARGET   := ObCP_WiFi_Demo
BUILD    := build
PREFIX   ?= arm-none-eabi-
CC       := $(PREFIX)gcc
OBJCOPY  := $(PREFIX)objcopy
SIZE     := $(PREFIX)size

# Mêmes fichiers que le projet STM32CubeIDE : tout ce qui est sous Core/ (code
# généré par CubeMX et code de la démo), USB_DEVICE/, Drivers/ et Middlewares/.
C_SRC := $(wildcard Core/Src/*.c) \
         $(wildcard Core/Src/app/*.c) \
         $(wildcard Core/Src/st67w6x/*.c) \
         $(wildcard Core/Src/qrcodegen/*.c) \
         $(wildcard USB_DEVICE/App/*.c) \
         $(wildcard USB_DEVICE/Target/*.c) \
         $(wildcard Middlewares/ST/STM32_USB_Device_Library/Core/Src/*.c) \
         $(wildcard Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/*.c) \
         $(wildcard Drivers/STM32L4xx_HAL_Driver/Src/*.c)
ASM_SRC := Core/Startup/startup_stm32l476rgtx.s

# Chemins d'inclusion : uniquement ceux que CubeMX génère (le code de la démo
# n'en demande aucun autre, ses en-têtes sont dans Core/Inc).
INCLUDES := -ICore/Inc \
            -IUSB_DEVICE/App \
            -IUSB_DEVICE/Target \
            -IMiddlewares/ST/STM32_USB_Device_Library/Core/Inc \
            -IMiddlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc \
            -IDrivers/STM32L4xx_HAL_Driver/Inc \
            -IDrivers/STM32L4xx_HAL_Driver/Inc/Legacy \
            -IDrivers/CMSIS/Device/ST/STM32L4xx/Include \
            -IDrivers/CMSIS/Include

DEFS  := -DUSE_HAL_DRIVER -DSTM32L476xx -DNDEBUG
MCU   := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
OPT   ?= -Os

CFLAGS  := $(MCU) $(DEFS) $(INCLUDES) $(OPT) -g3 -std=gnu11 \
           -Wall -Wextra -Wno-unused-parameter \
           -ffunction-sections -fdata-sections -fstack-usage -MMD -MP
ASFLAGS := $(MCU) -x assembler-with-cpp -g3
LDFLAGS := $(MCU) -TSTM32L476RGTX_FLASH.ld --specs=nano.specs --specs=nosys.specs \
           -Wl,-Map=$(BUILD)/$(TARGET).map,--cref -Wl,--gc-sections \
           -Wl,--print-memory-usage -Wl,--start-group -lc -lm -Wl,--end-group

OBJS := $(addprefix $(BUILD)/,$(C_SRC:.c=.o)) $(addprefix $(BUILD)/,$(ASM_SRC:.s=.o))

.PHONY: all clean flash test size

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin size

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	@echo "AS  $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) STM32L476RGTX_FLASH.ld
	@echo "LD  $@"
	@$(CC) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	@$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	@$(OBJCOPY) -O binary $< $@

size: $(BUILD)/$(TARGET).elf
	@$(SIZE) $<

flash: $(BUILD)/$(TARGET).elf
	STM32_Programmer_CLI -c port=usb1 -w $< -v -s

test:
	$(MAKE) -C Tests

clean:
	rm -rf $(BUILD)
	$(MAKE) -C Tests clean

-include $(OBJS:.o=.d)
