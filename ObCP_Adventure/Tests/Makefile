# Tests sur PC (gcc natif) du jeu : les sources sont celles du projet
# (Core/Src/engine, graphics, services, content, devices, app) ; leurs en-têtes
# sont tous dans Core/Inc. Aucun fichier de ST (HAL, USB) n'est compilé ici :
# le matériel est remplacé par des doublures (bus SPI/I2C, temps, écran).
#   make -C Tests            # compile et exécute tous les tests
#   make -C Tests captures   # recrée les captures d'écran de la documentation
#   make -C Tests bench      # trafic SPI estimé pour 10 niveaux (CSV)
#
#   test_engine      : physique, collisions, niveaux, états, entrées, journal
#                      EEPROM (coupures simulées), pilotes tactiles et
#                      accéléromètres sur bus simulé, protection des tampons
#   test_calibration : assistant d'étalonnage (5 points, neutre), délais
#   test_ui          : navigation tactile, brouillon des réglages, pause,
#                      appui long, centrage des textes (pixels RGB565)
#   test_app         : ordonnanceur oa_app.c complet avec un STMPE610 simulé
#   check-cubemx     : Tools/verif_cubemx.py (code du jeu bien relié au code
#                      généré par CubeMX, à relancer après chaque génération)

CC      ?= gcc
BUILD   := build
SRC     := ../Core/Src
CFLAGS  := -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
           -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
INC     := -I../Core/Inc

# Modules portables : aucun n'inclut la HAL (ils se compilent tels quels sur PC)
PORTABLE := $(SRC)/engine/oa_game.c $(SRC)/engine/oa_physics.c $(SRC)/engine/oa_ui.c \
            $(SRC)/graphics/oa_render.c $(SRC)/graphics/oa_assets.c \
            $(SRC)/services/oa_input.c $(SRC)/services/oa_calibration.c $(SRC)/services/oa_storage.c \
            $(SRC)/devices/oa_touch.c $(SRC)/devices/oa_accel.c $(SRC)/devices/oa_eeprom.c \
            $(SRC)/content/oa_levels.c

TESTS := test_engine test_calibration test_ui test_app

.PHONY: all run captures bench check-cubemx clean
all: run

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_engine: test_engine.c $(PORTABLE) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ $^

$(BUILD)/test_calibration: test_calibration.c $(PORTABLE) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ $^

$(BUILD)/test_ui: test_ui.c $(PORTABLE) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ $^

# Ordonnanceur réel (oa_app.c) : test_app.c fournit les fonctions de oa_board.h
$(BUILD)/test_app: test_app.c $(SRC)/app/oa_app.c $(PORTABLE) | $(BUILD)
	$(CC) $(CFLAGS) -DOA_APP_TEST $(INC) -o $@ $^

$(BUILD)/benchmark_render: benchmark_render.c $(PORTABLE) | $(BUILD)
	$(CC) -std=c11 -O2 -Wall -Wextra $(INC) -o $@ $^

run: $(addprefix $(BUILD)/,$(TESTS))
	./$(BUILD)/test_engine
	./$(BUILD)/test_calibration
	./$(BUILD)/test_ui
	./$(BUILD)/test_app
	@$(MAKE) --no-print-directory check-cubemx

# Captures PPM du rendu réel (menus, réglages, pause, étalonnage) ; Tools/capture_ui_docs.py
# les convertit en PNG pour Doc/images.
captures: $(BUILD)/test_ui $(BUILD)/test_app
	mkdir -p $(BUILD)/captures
	./$(BUILD)/test_ui $(BUILD)/captures
	./$(BUILD)/test_app $(BUILD)/captures/app_etalonnage.ppm

bench: $(BUILD)/benchmark_render
	./$(BUILD)/benchmark_render $(BUILD)/render_pc.csv
	@echo "Résultat : Tests/$(BUILD)/render_pc.csv"

check-cubemx:
	@if command -v python3 >/dev/null 2>&1; then python3 ../Tools/verif_cubemx.py ..; fi

clean:
	rm -rf $(BUILD)
