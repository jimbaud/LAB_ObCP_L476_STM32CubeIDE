# Même cible, sources et script de liaison que le projet CubeIDE.
# make -j       -> Release ; make DEBUG=1 -j -> Debug.
# := évalue immédiatement ; ?= laisse le poste utilisateur fournir sa valeur.
TARGET := ObCP_Tactile_1947
PREFIX ?= arm-none-eabi-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE := $(PREFIX)size
MCU := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
LD_SCRIPT := Linker/ObCP_Flash.ld
# Un sysroot GNU Arm non installé peut être fourni ici par l'environnement.
SPECS ?= --specs=nano.specs --specs=nosys.specs
NEWLIB_FLAGS ?=

ifeq ($(DEBUG),1)
CONFIG := Debug
OPT := -O0 -g3 -DDEBUG
else
CONFIG := Release
OPT := -Os -g0
endif
OUT := build/$(CONFIG)
HAL := Drivers/STM32L4xx_HAL_Driver
CMSIS := Drivers/CMSIS
CPPFLAGS := -DSTM32L476xx -DUSE_HAL_DRIVER -ICore/Inc -I$(HAL)/Inc \
    -I$(HAL)/Inc/Legacy -I$(CMSIS)/Include -I$(CMSIS)/Device/ST/STM32L4xx/Include
CFLAGS := $(MCU) $(OPT) -std=gnu11 -Wall -ffunction-sections -fdata-sections -fstack-usage
LDFLAGS := $(MCU) $(NEWLIB_FLAGS) $(SPECS) -T$(LD_SCRIPT) -static \
    -Wl,--gc-sections,-Map=$(OUT)/$(TARGET).map
# Chaque source est compilée séparément. -MMD/-MP conservent les dépendances vers les en-têtes.
SRC := $(wildcard Core/Src/*.c Core/Src/obcp/*.c $(HAL)/Src/*.c)
ASM := $(wildcard Core/Startup/*.s)
OBJS := $(patsubst %.c,$(OUT)/obj/%.o,$(SRC)) $(patsubst %.s,$(OUT)/obj/%.o,$(ASM))

.PHONY: all clean
all: $(OUT)/$(TARGET).elf $(OUT)/$(TARGET).hex $(OUT)/$(TARGET).bin
	$(SIZE) $(OUT)/$(TARGET).elf

$(OUT)/obj/Core/Src/obcp/%.o: CFLAGS += -Wextra -Werror
$(OUT)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(NEWLIB_FLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/obj/%.o: %.s
	@mkdir -p $(@D)
	$(CC) $(MCU) -c $< -o $@

# Le linker rassemble les objets, résout les appels et applique les bornes de Linker/ObCP_Flash.ld.
$(OUT)/$(TARGET).elf: $(OBJS) $(LD_SCRIPT)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

# ELF sert au débogage. HEX garde les adresses et ses contrôles ; BIN est la suite brute des octets.
$(OUT)/$(TARGET).hex: $(OUT)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(OUT)/$(TARGET).bin: $(OUT)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf build

-include $(OBJS:.o=.d)
