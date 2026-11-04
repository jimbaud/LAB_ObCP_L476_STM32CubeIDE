# =============================================================================
#  Tests sur PC du programme de l'ObCP (sans carte)
# =============================================================================
#  Le code de Core/Src est compilé pour le PC ; le matériel (HAL, module BLE,
#  USB, accéléromètre) est remplacé par des « doublures » (dossier fakes/).
#
#    make            compile et lance les trois séries de tests
#    make unit       tests unitaires (fifo, lignes, analyse des commandes)
#    make app        programme complet sur la carte simulée (USB, BLE...)
#    make compare    même scénario joué par les versions 2024 et 2026
#    make demo2024   démonstration d'un débordement de tableau de 2024
#    make clean      efface le dossier build/
#
#  Il faut gcc, make et python3 (Linux, macOS, WSL ou MSYS2 sous Windows).
#  Sans les « sanitizers » (MSYS2...) :  make SAN=
# =============================================================================

CC     = gcc
PYTHON ?= python3
SAN    ?= -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS ?= -std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter

BUILD := build

# Dossiers d'en-têtes du projet (les mêmes que dans STM32CubeIDE)
PROJECT_INC := \
  -I../Core/Inc \
  -I../BlueNRG-MS/Target \
  -I../USB_DEVICE/App \
  -I../USB_DEVICE/Target \
  -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc \
  -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc \
  -I../Middlewares/ST/BlueNRG-MS/includes \
  -I../Middlewares/ST/BlueNRG-MS/utils \
  -I../Middlewares/ST/BlueNRG-MS/hci/hci_tl_patterns/Basic

# fakes/ en premier : son stm32l4xx_hal.h remplace celui de ST
INC := -Ifakes -I. $(PROJECT_INC)

FAKES := fakes/fake_hal.c fakes/fake_bluenrg.c

APP_SRC := \
  ../Core/Src/app.c \
  ../Core/Src/accelerometer.c \
  ../Core/Src/ble_uart.c \
  ../Core/Src/button.c \
  ../Core/Src/commands.c \
  ../Core/Src/fifo.c \
  ../Core/Src/hal_callbacks.c \
  ../Core/Src/line_builder.c \
  ../Core/Src/ota_switch.c \
  ../Core/Src/outputs.c \
  ../Core/Src/timer_1s.c \
  ../Core/Src/usb_console.c \
  ../USB_DEVICE/App/usbd_cdc_if.c

UNIT_SRC := test_unit.c check.c \
  ../Core/Src/fifo.c ../Core/Src/line_builder.c ../Core/Src/commands.c

REF_SRC := \
  reference_2024/Src/Driver_OBCP.c \
  reference_2024/Src/BLE_service.c \
  reference_2024/Src/LIS3DSH.c

.PHONY: all run unit app compare demo2024 clean

all: run

run: unit app compare
	@echo
	@echo "=== Tous les tests sont passés ==="

$(BUILD):
	mkdir -p $(BUILD)

# --- 1. Tests unitaires ------------------------------------------------------
$(BUILD)/test_unit: $(UNIT_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) $(INC) $(UNIT_SRC) -lm -o $@

unit: $(BUILD)/test_unit
	@echo
	@echo "=== Tests unitaires ==="
	./$(BUILD)/test_unit

# --- 2. Programme complet sur la carte simulée --------------------------------
$(BUILD)/test_app: test_app.c check.c $(APP_SRC) $(FAKES) fakes/fake_usb.c | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) $(INC) test_app.c check.c $(APP_SRC) $(FAKES) fakes/fake_usb.c -lm -o $@

app: $(BUILD)/test_app
	@echo
	@echo "=== Programme complet sur la carte simulée ==="
	./$(BUILD)/test_app

# --- 3. Comparaison avec la version 2024 -------------------------------------
# Version 2024 : ses propres en-têtes (reference_2024/Inc) passent avant ceux
# de Core/Inc. Elle est compilée sans avertissements (-w) : c'est l'ancien code,
# conservé tel quel pour la comparaison.
$(BUILD)/compare_2024: compare/scenario.c compare/adapter_2024.c $(REF_SRC) $(FAKES) | $(BUILD)
	$(CC) -std=gnu11 -g -O1 -w $(SAN) -Ifakes -Icompare -Ireference_2024/Inc $(PROJECT_INC) \
	  compare/scenario.c compare/adapter_2024.c $(REF_SRC) $(FAKES) -lm -o $@

$(BUILD)/compare_2026: compare/scenario.c compare/adapter_2026.c $(APP_SRC) $(FAKES) fakes/fake_usb.c | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -Icompare $(INC) \
	  compare/scenario.c compare/adapter_2026.c $(APP_SRC) $(FAKES) fakes/fake_usb.c -lm -o $@

compare: $(BUILD)/compare_2024 $(BUILD)/compare_2026
	@echo
	@echo "=== Comparaison avec la version 2024 ==="
	./$(BUILD)/compare_2024 > $(BUILD)/journal_2024.txt
	./$(BUILD)/compare_2026 > $(BUILD)/journal_2026.txt
	$(PYTHON) compare/compare.py $(BUILD)/journal_2024.txt $(BUILD)/journal_2026.txt $(BUILD)/differences.md

# --- Démonstration : « start timer » tapé touche par touche -------------------
# La version 2024 écrit après la fin de UsbBuffer2[10] : AddressSanitizer
# arrête le programme (résultat attendu, d'où le « - » qui ignore l'erreur).
$(BUILD)/demo_2024: compare/demo_overflow.c compare/adapter_2024.c $(REF_SRC) $(FAKES) | $(BUILD)
	$(CC) -std=gnu11 -g -O0 -w $(SAN) -Ifakes -Icompare -Ireference_2024/Inc $(PROJECT_INC) \
	  compare/demo_overflow.c compare/adapter_2024.c $(REF_SRC) $(FAKES) -lm -o $@

$(BUILD)/demo_2026: compare/demo_overflow.c compare/adapter_2026.c $(APP_SRC) $(FAKES) fakes/fake_usb.c | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -Icompare $(INC) \
	  compare/demo_overflow.c compare/adapter_2026.c $(APP_SRC) $(FAKES) fakes/fake_usb.c -lm -o $@

demo2024: $(BUILD)/demo_2024 $(BUILD)/demo_2026
	@echo
	./$(BUILD)/demo_2026
	@echo
	-./$(BUILD)/demo_2024

clean:
	rm -rf $(BUILD)
