# =============================================================================
#  Tests sur PC du programme de démonstration du réseau maillé (sans carte)
# =============================================================================
#  Le code de Core/Src est compilé pour le PC ; le matériel (HAL, module BLE,
#  USB, accéléromètre, EEPROM) est remplacé par des « doublures » (fakes/).
#
#    make            compile et lance les trois séries de tests
#    make unit       tests unitaires (trames, adresses, réglages, commandes)
#    make network    réseau de cartes simulées (inondation, TTL, doublons...)
#    make app        programme complet sur une carte simulée
#    make simu       simulateur pédagogique (réglages : make simu ARGS="...")
#    make clean      efface le dossier build/
#
#  Il faut gcc, make (Linux, macOS, WSL ou MSYS2 sous Windows).
#  Sans les « sanitizers » (MSYS2...) :  make SAN=
# =============================================================================

CC     = gcc
SAN    ?= -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS ?= -std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter
ARGS   ?=

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

# fakes/ en premier : ses stm32l4xx_hal.h et obcp_ota_request.h remplacent
# ceux du projet
INC := -Ifakes -I. $(PROJECT_INC)

MESH_SRC := ../Core/Src/mesh.c ../Core/Src/mesh_pdu.c

APP_SRC := \
  ../Core/Src/app.c \
  ../Core/Src/accelerometer.c \
  ../Core/Src/button.c \
  ../Core/Src/commands.c \
  ../Core/Src/config_store.c \
  ../Core/Src/fifo.c \
  ../Core/Src/hal_callbacks.c \
  ../Core/Src/led_fx.c \
  ../Core/Src/line_builder.c \
  ../Core/Src/mesh.c \
  ../Core/Src/mesh_pdu.c \
  ../Core/Src/mesh_radio.c \
  ../Core/Src/node.c \
  ../Core/Src/outputs.c \
  ../Core/Src/report.c \
  ../Core/Src/settings.c \
  ../Core/Src/timer_1s.c \
  ../Core/Src/usb_console.c \
  ../USB_DEVICE/App/usbd_cdc_if.c

FAKES := fakes/fake_hal.c fakes/fake_usb.c fakes/fake_bluenrg.c fakes/fake_eeprom.c

.PHONY: all run unit network app simu clean

all: run

run: unit network app
	@echo
	@echo "=== Tous les tests sont passés ==="

$(BUILD):
	mkdir -p $(BUILD)

# --- 1. Tests unitaires -------------------------------------------------------
$(BUILD)/test_unit: test_unit.c check.c $(APP_SRC) $(FAKES) | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) $(INC) test_unit.c check.c $(APP_SRC) $(FAKES) -lm -o $@

unit: $(BUILD)/test_unit
	@echo
	@echo "=== Tests unitaires ==="
	./$(BUILD)/test_unit

# --- 2. Réseau de cartes simulées --------------------------------------------
$(BUILD)/test_network: test_network.c sim_net.c check.c $(MESH_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -I. -I../Core/Inc test_network.c sim_net.c check.c $(MESH_SRC) -lm -o $@

network: $(BUILD)/test_network
	@echo
	@echo "=== Réseau de cartes simulées ==="
	./$(BUILD)/test_network

# --- 3. Programme complet sur une carte simulée -------------------------------
$(BUILD)/test_app: test_app.c check.c $(APP_SRC) $(FAKES) | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) $(INC) test_app.c check.c $(APP_SRC) $(FAKES) -lm -o $@

app: $(BUILD)/test_app
	@echo
	@echo "=== Programme complet sur une carte simulée ==="
	./$(BUILD)/test_app

# --- Simulateur pédagogique ---------------------------------------------------
$(BUILD)/simu_reseau: simu_reseau.c sim_net.c $(MESH_SRC) | $(BUILD)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -I../Core/Inc simu_reseau.c sim_net.c $(MESH_SRC) -lm -o $@

simu: $(BUILD)/simu_reseau
	./$(BUILD)/simu_reseau $(ARGS)

clean:
	rm -rf $(BUILD)
