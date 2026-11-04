# Tests sur PC (gcc natif) du pilote ST67W6X_Lite et de l'application.
# Les sources sont celles du projet (Core/Src/st67w6x, Core/Src/app,
# Core/Src/cdc_port.c...) ; leurs en-têtes sont tous dans Core/Inc.
#   make -C Tests          # compile et exécute tous les tests
#   make -C Tests verbose  # test du pilote avec journal du trafic AT simulé
#
#   test_driver : pilote face au module simulé (trames SPI, AT, évènements,
#                 mise à jour du module par AT+OTASEND)
#   test_app    : calendrier, utilitaires, HTTP, configuration, réception YMODEM,
#                 couleurs de la LED RVB, accéléromètre LIS3DH (SPI simulé)
#   test_usb    : port COM virtuel USB de l'ObCP (tampons, contrôle de flux, DTR)
#   test_sim    : application complète (4 démos, ObCP, module ota) face au module simulé ;
#                 journal dans build/sim_transcript.txt
#   test_boot   : démarrage automatique (web.auto, mqtt.auto) avec une configuration
#                 déjà enregistrée : box joignable, sans box, box injoignable
#   check_ota_py.py : essai de bout en bout de Tools/maj_module_ota.py (et de sz)
#                 face à l'application en temps réel sur un pseudo-terminal
#   check-cubemx  : Tools/verif_cubemx.py (code de la démo bien relié au code
#                 généré par CubeMX, à relancer après chaque régénération)

CC      ?= gcc
BUILD   := build
DRV     := ../Core/Src/st67w6x
APP     := ../Core/Src/app
QR      := ../Core/Src/qrcodegen
CFLAGS  := -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined \
           -fno-sanitize-recover=undefined -I. -DHOST_TEST
INC     := -I../Core/Inc

DRV_SRC := $(DRV)/ncp.c $(DRV)/ncp_spi.c $(DRV)/ncp_at.c $(DRV)/ncp_wifi.c $(DRV)/ncp_net.c $(DRV)/ncp_mqtt.c \
           $(DRV)/ncp_fwu.c
APP_PURE := $(APP)/timeutil.c $(APP)/util.c $(APP)/http_parse.c $(APP)/cfg_codec.c \
            $(APP)/ymodem.c $(APP)/color.c $(APP)/lis3dh.c $(APP)/bridge_ctl.c
APP_FULL := $(APP_PURE) $(APP)/app.c $(APP)/cli.c $(APP)/demo_scan.c $(APP)/demo_net.c \
            $(APP)/demo_web.c $(APP)/demo_mqtt.c $(APP)/web_page.c $(APP)/qr_term.c \
            $(APP)/module_ota.c \
            $(QR)/qrcodegen.c
HOST_SRC := host/host_stubs.c host/host_console.c

.PHONY: all run verbose clean check-web check-ota-py check-cubemx
all: run

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_driver: test_driver.c sim_module.c sha256.c $(DRV_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ $^ -lm

$(BUILD)/test_app: test_app.c ymodem_sender.c $(APP_PURE) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ $^ -lm

# Application complète : la fausse HAL (host/) remplace celle de ST
$(BUILD)/test_sim: test_sim.c sim_module.c sha256.c ymodem_sender.c $(HOST_SRC) $(DRV_SRC) $(APP_FULL) | $(BUILD)
	$(CC) $(CFLAGS) -Ihost $(INC) -o $@ $^ -lm

# Démarrage automatique (web.auto, mqtt.auto) : configuration enregistrée avant app_init()
$(BUILD)/test_boot: test_boot.c sim_module.c sha256.c ymodem_sender.c $(HOST_SRC) $(DRV_SRC) $(APP_FULL) | $(BUILD)
	$(CC) $(CFLAGS) -Ihost $(INC) -o $@ $^ -lm

# Port COM virtuel USB : cdc_port.c et les rappels générés (usbd_cdc_if.c) face à
# une classe CDC simulée (usbfake/, placé avant les en-têtes du projet).
$(BUILD)/test_usb: test_usb.c ../Core/Src/cdc_port.c ../USB_DEVICE/App/usbd_cdc_if.c usbfake/usbd_cdc.h usbfake/usbd_core.h | $(BUILD)
	$(CC) $(CFLAGS) -Iusbfake -I../USB_DEVICE/App $(INC) -o $@ test_usb.c ../Core/Src/cdc_port.c ../USB_DEVICE/App/usbd_cdc_if.c

# Application en temps réel sur un pseudo-terminal (essai du script Tools/maj_module_ota.py)
$(BUILD)/pty_sim: pty_sim.c sim_module.c sha256.c $(HOST_SRC) $(DRV_SRC) $(APP_FULL) | $(BUILD)
	$(CC) $(CFLAGS) -DSIM_REALTIME -Ihost $(INC) -o $@ $^ -lm

run: $(BUILD)/test_driver $(BUILD)/test_app $(BUILD)/test_usb $(BUILD)/test_sim $(BUILD)/test_boot $(BUILD)/pty_sim
	./$(BUILD)/test_driver
	./$(BUILD)/test_app
	./$(BUILD)/test_usb
	./$(BUILD)/test_sim
	@echo "Démarrage automatique (configuration enregistrée, 3 scénarios)"
	./$(BUILD)/test_boot box
	./$(BUILD)/test_boot sans-box
	./$(BUILD)/test_boot box-absente
	@$(MAKE) --no-print-directory check-web
	@$(MAKE) --no-print-directory check-ota-py
	@$(MAKE) --no-print-directory check-cubemx

# Points d'accroche du code de la démo dans les fichiers générés par CubeMX
check-cubemx:
	@if command -v python3 >/dev/null 2>&1; then python3 ../Tools/verif_cubemx.py ..; fi

# Essai de bout en bout du script de mise à jour (si python3 et pyserial présents)
check-ota-py: $(BUILD)/pty_sim
	@if command -v python3 >/dev/null 2>&1; then python3 check_ota_py.py; fi

# Validation des réponses produites par la simulation (si python3 / node présents)
check-web:
	@if command -v python3 >/dev/null 2>&1; then \
	  for f in $(BUILD)/sim_*.json; do python3 -m json.tool $$f >/dev/null || exit 1; done; \
	  echo "JSON du serveur web et de la télémétrie : valides"; fi
	@if command -v node >/dev/null 2>&1; then \
	  sed -n 's/.*<script>\(.*\)<\/script>.*/\1/p' $(BUILD)/sim_index.html > $(BUILD)/sim_page.js && \
	  node --check $(BUILD)/sim_page.js && echo "JavaScript de la page web : syntaxe valide"; fi

verbose: $(BUILD)/test_driver
	./$(BUILD)/test_driver -v

clean:
	rm -rf $(BUILD)
