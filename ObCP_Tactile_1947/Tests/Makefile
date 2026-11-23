# Banc PC : seuls les modèles et les ports simulables sont liés.
# La HAL STM32 réelle est remplacée par mock_hal pour les tests des contrôleurs tactiles.
# ASan détecte les accès mémoire invalides ; UBSan détecte plusieurs comportements indéfinis.
# SAN peut être adapté au compilateur du poste. Aucune carte ni sonde ST-LINK n’est nécessaire.
CC ?= cc
SRC := app gfx input calibration paint maze breakout race audio storage logo
BASE := ../Core/Src/obcp
FLAGS := -std=c11 -Wall -Wextra -Werror -Wshadow -g -O1 -I../Core/Inc
SAN ?= -fsanitize=address,undefined -fno-omit-frame-pointer
OUT := ../build/host
TEST := $(OUT)/test_obcp
TOUCH_TEST := $(OUT)/test_touch_bus
TOUCH_SRC := touch_port ft6206 stmpe610 tsc2007

.PHONY: test captures
$(TEST): test_obcp.c $(addprefix $(BASE)/,$(addsuffix .c,$(SRC))) $(wildcard ../Core/Inc/obcp_*.h)
	mkdir -p $(OUT)
	$(CC) $(FLAGS) $(SAN) $(filter %.c,$^) -o $@

$(TOUCH_TEST): test_touch_bus.c $(addprefix $(BASE)/,$(addsuffix .c,$(TOUCH_SRC))) $(wildcard mock_hal/*.h) $(BASE)/touch_drivers.h ../Core/Inc/obcp_touch.h ../Core/Inc/obcp_panel.h
	mkdir -p $(OUT)
	$(CC) -Imock_hal $(FLAGS) $(SAN) $(filter %.c,$^) -o $@

test: $(TEST) $(TOUCH_TEST)
	ASAN_OPTIONS=detect_leaks=0 $(TEST)
	ASAN_OPTIONS=detect_leaks=0 $(TOUCH_TEST)

captures: $(TEST)
	mkdir -p $(OUT)/captures
	ASAN_OPTIONS=detect_leaks=0 $(TEST) $(OUT)/captures
