# Simulateur PC d'ObCP Adventure (SDL2) : MÊME moteur, MÊMES niveaux et MÊME
# rendu par bandes que la carte (fichiers de Core/Src). La souris joue le rôle
# du doigt sur l'écran tactile ; le clavier sert de référence.
#
#   make -C Simulator            -> build/obcp_adventure_sim   (Linux, macOS,
#                                   ou Windows avec MSYS2 : paquets gcc, make, SDL2)
#   make -C Simulator run        -> compile puis lance le jeu
#   make -C Simulator smoke      -> 120 pas sans fenêtre (vérification rapide)
#   make -C Simulator windows SDL2_MINGW=/chemin/SDL2-2.x.y/x86_64-w64-mingw32
#                                -> build/windows/ObCP_Adventure_Simulateur.exe
#                                   (compilation croisée MinGW-w64, SDL2 intégrée :
#                                   un seul fichier .exe, sans DLL ; une console
#                                   s'ouvre à côté du jeu pour les messages ;
#                                   sans date dans l'en-tête : deux compilations
#                                   des mêmes sources donnent le même fichier)
#
# Prérequis : SDL2 et son outil sdl2-config (paquet libsdl2-dev sous Debian/Ubuntu,
# brew install sdl2 sous macOS, mingw-w64-ucrt-x86_64-SDL2 sous MSYS2).

CC      ?= gcc
BUILD   := build
SRC     := ../Core/Src
CFLAGS  := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -I../Core/Inc
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)

# Modules portables du jeu (aucun n'inclut la HAL de ST)
PORTABLE := $(SRC)/engine/oa_game.c $(SRC)/engine/oa_physics.c $(SRC)/engine/oa_ui.c \
            $(SRC)/graphics/oa_render.c $(SRC)/graphics/oa_assets.c \
            $(SRC)/services/oa_input.c $(SRC)/services/oa_calibration.c $(SRC)/services/oa_storage.c \
            $(SRC)/content/oa_levels.c

# Compilation croisée pour Windows 64 bits
MINGW      ?= x86_64-w64-mingw32-
SDL2_MINGW ?= ../../SDL2-2.30.9/x86_64-w64-mingw32
WIN_LIBS   := -L$(SDL2_MINGW)/lib -Wl,-Bstatic -lSDL2 -Wl,-Bdynamic \
              -lm -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 \
              -lsetupapi -lversion -luuid -static-libgcc -Wl,--no-insert-timestamp

.PHONY: all run smoke windows clean
all: $(BUILD)/obcp_adventure_sim

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/obcp_adventure_sim: main_sdl.c $(PORTABLE) | $(BUILD)
	@if [ -z "$(SDL_LIBS)" ]; then echo "SDL2 introuvable (sdl2-config) : installer SDL2, voir l'en-tête de ce Makefile"; exit 1; fi
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $^ $(SDL_LIBS)

run: $(BUILD)/obcp_adventure_sim
	cd $(BUILD) && ./obcp_adventure_sim

smoke: $(BUILD)/obcp_adventure_sim
	cd $(BUILD) && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./obcp_adventure_sim --headless --frames 120

windows: main_sdl.c $(PORTABLE)
	mkdir -p $(BUILD)/windows
	$(MINGW)gcc -std=c11 -O2 -Wall -Wextra -s -I../Core/Inc -I$(SDL2_MINGW)/include/SDL2 \
	    -o $(BUILD)/windows/ObCP_Adventure_Simulateur.exe $^ $(WIN_LIBS)

clean:
	rm -rf $(BUILD)
