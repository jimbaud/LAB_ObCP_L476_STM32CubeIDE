#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verif_cubemx.py - Vérifie le projet ObCP Adventure après une génération par STM32CubeMX
=====================================================================================

Utilisation (depuis le dossier du projet, celui qui contient le .ioc) :

    python Tools/verif_cubemx.py
    python Tools/verif_cubemx.py C:\\chemin\\vers\\ObCP_Adventure

Pourquoi ce script ?
    STM32CubeMX réécrit les fichiers qu'il génère (main.c, gpio.c, spi.c,
    usbd_cdc_if.c...) chaque fois que l'on clique sur « Generate Code ».
    Il ne garde que ce qui est écrit entre les commentaires
    /* USER CODE BEGIN xxx */ et /* USER CODE END xxx */.
    Le jeu est donc rangé dans ses propres fichiers (Core/Src/app, engine,
    graphics, services, devices, content, oa_board.c, usb_console.c) que
    CubeMX ne touche jamais, et seules quelques lignes « crochets » sont
    placées dans les sections USER CODE des fichiers générés. Ce script
    vérifie que tout est en place.

Ce que le script vérifie :
    1. les crochets dans les sections USER CODE (oa_app_init, oa_app_task...) ;
    2. les réglages du fichier .ioc dont dépend le jeu (noms et niveaux de
       repos des broches, SPI, I2C, DMA, TIM2, horloge, USB, interruptions,
       options de génération) ;
    3. le code généré : appels des fonctions d'initialisation dans le bon
       ordre (DMA avant SPI1) et noms des broches utilisés par le jeu ;
    4. les fichiers du jeu sont présents, et CubeMX ne les considère pas
       comme des fichiers générés (il pourrait les effacer) ;
    5. chaque fonction de rappel de la HAL (HAL_..._Callback) n'est écrite
       qu'une fois (sinon : erreur « multiple definition ») ;
    6. les modules portables (moteur, rendu, services, pilotes) n'appellent
       pas la HAL : ils sont aussi compilés par le simulateur et les tests PC.

Il ne modifie aucun fichier. Code de retour : 0 si tout va bien, 1 sinon.
Il n'utilise que la bibliothèque standard de Python 3 (rien à installer).
"""
import glob
import os
import re
import sys

# --------------------------------------------------------------------------
# 1. Crochets attendus dans les sections USER CODE des fichiers générés
#    (fichier, nom de la section, texte attendu, rôle de la ligne)
# --------------------------------------------------------------------------
FAULT_HELP = "allume la LED rouge et arrête la carte (oa_board.c)"
HOOKS = [
    ("Core/Src/main.c", "Includes", '#include "oa_app.h"',
     "déclare oa_app_init() et oa_app_task()"),
    ("Core/Src/main.c", "Includes", '#include "oa_board.h"',
     "déclare oa_board_fatal()"),
    ("Core/Src/main.c", "2", "oa_app_init();",
     "démarre le jeu (base de temps, sauvegarde, tactile, écran)"),
    ("Core/Src/main.c", "WHILE", "while (1)",
     "super-boucle (ligne écrite par CubeMX, à garder)"),
    ("Core/Src/main.c", "3", "oa_app_task();",
     "un tour de la super-boucle"),
    ("Core/Src/main.c", "Error_Handler_Debug", "oa_board_fatal();",
     "signale une erreur d'initialisation par la LED rouge"),
    ("Core/Src/stm32l4xx_it.c", "Includes", '#include "oa_board.h"',
     "déclare oa_board_fatal()"),
    ("Core/Src/stm32l4xx_it.c", "NonMaskableInt_IRQn 0", "oa_board_fatal();", FAULT_HELP),
    ("Core/Src/stm32l4xx_it.c", "HardFault_IRQn 0", "oa_board_fatal();", FAULT_HELP),
    ("Core/Src/stm32l4xx_it.c", "MemoryManagement_IRQn 0", "oa_board_fatal();", FAULT_HELP),
    ("Core/Src/stm32l4xx_it.c", "BusFault_IRQn 0", "oa_board_fatal();", FAULT_HELP),
    ("Core/Src/stm32l4xx_it.c", "UsageFault_IRQn 0", "oa_board_fatal();", FAULT_HELP),
    ("USB_DEVICE/App/usbd_cdc_if.c", "INCLUDE", '#include "usb_console.h"',
     "déclare les fonctions du module usb_console"),
    ("USB_DEVICE/App/usbd_cdc_if.c", "3", "usb_console_on_init();",
     "prévient usb_console que le PC a ouvert la liaison"),
    ("USB_DEVICE/App/usbd_cdc_if.c", "4", "usb_console_on_deinit();",
     "prévient usb_console que la liaison est fermée"),
    ("USB_DEVICE/App/usbd_cdc_if.c", "6", "usb_console_on_receive(Buf, *Len);",
     "confie chaque paquet reçu du PC à usb_console"),
    ("USB_DEVICE/App/usbd_cdc_if.c", "7", "usb_console_write(Buf, Len)",
     "CDC_Transmit_FS() passe par le tampon d'émission de usb_console"),
    ("USB_DEVICE/Target/usbd_conf.c", "Includes", '#include "usb_console.h"',
     "déclare les fonctions du module usb_console"),
    ("USB_DEVICE/Target/usbd_conf.c", "2", "usb_console_on_suspend();",
     "prévient usb_console de la mise en veille du bus USB"),
    ("USB_DEVICE/Target/usbd_conf.c", "3", "usb_console_on_resume();",
     "prévient usb_console du réveil du bus USB"),
]

# Lignes qui ne doivent PAS revenir dans une section (texte, raison)
FORBIDDEN = [
    ("USB_DEVICE/App/usbd_cdc_if.c", "6", "USBD_CDC_ReceivePacket",
     "usb_console_on_receive() autorise déjà le paquet suivant : retirez cette "
     "ligne (code par défaut de CubeMX), sinon la réception est réarmée deux fois"),
    ("Core/Src/main.c", "2", "HAL_TIM_Base_Start",
     "TIM2 est démarré par oa_app_init() (oa_board_start_timebase) : inutile ici"),
]

# --------------------------------------------------------------------------
# 2. Réglages du fichier .ioc
#    (clé, valeur attendue, gravité, explication si la valeur diffère)
#    gravité : "ERREUR" (le jeu ne marchera pas) ou "ATTENTION"
# --------------------------------------------------------------------------
LABEL_HELP = ("dans CubeMX : clic droit sur la broche > Enter User Label ; "
              "oa_board.c utilise ce nom (main.h)")
IDLE_HELP = ("GPIO > broche > « GPIO output level » : High. Une sélection à 0 au "
             "démarrage fait répondre un composant sur un bus partagé")
IOC_SETTINGS = [
    # Options de génération (onglet Project Manager)
    ("ProjectManager.KeepUserCode", "true", "ERREUR",
     "Project Manager > Code Generator > « Keep User Code when re-generating » "
     "doit être coché, sinon les sections USER CODE sont effacées"),
    ("ProjectManager.CoupleFile", "true", "ERREUR",
     "Project Manager > Code Generator > « Generate peripheral initialization as "
     "a pair of '.c/.h' files » doit être coché (oa_board.c inclut spi.h, i2c.h...)"),
    ("ProjectManager.TargetToolchain", "STM32CubeIDE", "ERREUR",
     "Project Manager > Project > Toolchain/IDE doit rester « STM32CubeIDE »"),
    ("ProjectManager.HeapSize", "0x200", "ATTENTION",
     "taille du tas : le jeu n'alloue rien, 0x200 suffit pour la bibliothèque C"),
    ("ProjectManager.StackSize", "0x2000", "ATTENTION",
     "taille de la pile : gardez au moins 0x2000 (8 Ko)"),
    ("VP_SYS_VS_Systick.Mode", "SysTick", "ATTENTION",
     "System Core > SYS > Timebase Source : « SysTick » conseillé"),
    # Horloge : TIM2 (1 MHz) et l'I2C (100 kHz) sont calculés pour 80 MHz
    ("RCC.SYSCLKFreq_VALUE", "80000000", "ERREUR",
     "Clock Configuration : HCLK doit rester à 80 MHz (TIM2 et I2C1 en dépendent)"),
    ("RCC.USBFreq_Value", "48000000", "ERREUR",
     "Clock Configuration : l'USB demande 48 MHz (PLLSAI1)"),
    # Noms des broches (User Label)
    ("PB6.GPIO_Label", "LCD_CS", "ERREUR", LABEL_HELP),
    ("PC7.GPIO_Label", "LCD_DC", "ERREUR", LABEL_HELP),
    ("PA9.GPIO_Label", "TOUCH_CS", "ERREUR", LABEL_HELP),
    ("PC5.GPIO_Label", "ACC_CS", "ERREUR", LABEL_HELP),
    ("PC3.GPIO_Label", "EEPROM_CS", "ERREUR", LABEL_HELP),
    ("PC13.GPIO_Label", "USER_BUTTON", "ERREUR", LABEL_HELP),
    ("PC6.GPIO_Label", "LED_RED", "ERREUR", LABEL_HELP),
    ("PC8.GPIO_Label", "LED_GREEN", "ERREUR", LABEL_HELP),
    ("PC9.GPIO_Label", "LED_BLUE", "ERREUR", LABEL_HELP),
    ("PB5.GPIO_Label", "SD_CS", "ATTENTION", LABEL_HELP),
    ("PB2.GPIO_Label", "BLE_CS", "ATTENTION", LABEL_HELP),
    ("PC2.GPIO_Label", "BLE_RST", "ATTENTION", LABEL_HELP),
    # Niveaux de repos des sélections (bus partagés)
    ("PB6.PinState", "GPIO_PIN_SET", "ERREUR", "LCD_CS (PB6) : " + IDLE_HELP),
    ("PA9.PinState", "GPIO_PIN_SET", "ERREUR", "TOUCH_CS (PA9) : " + IDLE_HELP),
    ("PB5.PinState", "GPIO_PIN_SET", "ERREUR",
     "SD_CS (PB5) : la carte microSD du shield est sur SPI1 ; " + IDLE_HELP),
    ("PC5.PinState", "GPIO_PIN_SET", "ERREUR", "ACC_CS (PC5) : " + IDLE_HELP),
    ("PC3.PinState", "GPIO_PIN_SET", "ERREUR", "EEPROM_CS (PC3) : " + IDLE_HELP),
    ("PB2.PinState", "GPIO_PIN_SET", "ERREUR",
     "BLE_CS (PB2) : le module Bluetooth partage SPI2 avec l'EEPROM ; " + IDLE_HELP),
    # Bus
    ("PA5.Signal", "SPI1_SCK", "ERREUR", "écran et STMPE610 du shield sur SPI1 (D13)"),
    ("PA6.Signal", "SPI1_MISO", "ERREUR", "écran et STMPE610 du shield sur SPI1 (D12)"),
    ("PA7.Signal", "SPI1_MOSI", "ERREUR", "écran et STMPE610 du shield sur SPI1 (D11)"),
    ("PB13.Signal", "SPI2_SCK", "ERREUR", "EEPROM M95640 sur SPI2"),
    ("PB14.Signal", "SPI2_MISO", "ERREUR", "EEPROM M95640 sur SPI2"),
    ("PB15.Signal", "SPI2_MOSI", "ERREUR", "EEPROM M95640 sur SPI2"),
    ("PC10.Signal", "SPI3_SCK", "ERREUR", "accéléromètre sur SPI3"),
    ("PC11.Signal", "SPI3_MISO", "ERREUR", "accéléromètre sur SPI3"),
    ("PC12.Signal", "SPI3_MOSI", "ERREUR", "accéléromètre sur SPI3"),
    ("PB8.Signal", "I2C1_SCL", "ERREUR", "tactiles FT62xx / TSC2007 sur I2C1 (SCL du connecteur R3)"),
    ("PB9.Signal", "I2C1_SDA", "ERREUR", "tactiles FT62xx / TSC2007 sur I2C1 (SDA du connecteur R3)"),
    # DMA de l'écran
    ("Dma.SPI1_TX.0.Instance", "DMA1_Channel3", "ERREUR",
     "SPI1_TX doit rester sur DMA1 canal 3 (DMA Settings > Add > SPI1_TX)"),
    ("Dma.SPI1_TX.0.Direction", "DMA_MEMORY_TO_PERIPH", "ERREUR",
     "SPI1_TX : sens Memory To Peripheral"),
    ("Dma.SPI1_TX.0.Mode", "DMA_NORMAL", "ERREUR",
     "SPI1_TX : mode Normal (une bande par transfert), pas Circular"),
    ("Dma.SPI1_TX.0.MemInc", "DMA_MINC_ENABLE", "ERREUR",
     "SPI1_TX : incrément de l'adresse mémoire activé"),
    # Base de temps en microsecondes
    ("TIM2.Prescaler", "79", "ERREUR",
     "TIM2 : 80 MHz / (79 + 1) = 1 MHz, oa_board_us() compte des microsecondes"),
    ("TIM2.Period", "4294967295", "ERREUR",
     "TIM2 : période maximale (compteur libre de 32 bits)"),
    # Classe USB
    ("USB_DEVICE.CLASS_NAME_FS", "CDC", "ERREUR",
     "Middleware > USB_DEVICE > Class For FS IP doit rester « Communication "
     "Device Class (Virtual Port Com) »"),
]

# Interruptions qui doivent être autorisées (System Core > NVIC)
NVIC_ENABLED = [
    ("NVIC.DMA1_Channel3_IRQn", "fin d'envoi d'une bande vers l'écran"),
    ("NVIC.OTG_FS_IRQn", "USB"),
]

# Fonctions d'initialisation générées, dans l'ordre d'appel attendu dans main.c
INIT_ORDER = ["MX_GPIO_Init", "MX_DMA_Init", "MX_SPI1_Init", "MX_SPI2_Init", "MX_SPI3_Init",
              "MX_I2C1_Init", "MX_TIM2_Init", "MX_USART2_UART_Init", "MX_USB_DEVICE_Init"]

# Version des outils (simple information)
IOC_INFO = [
    ("MxCube.Version", "version de STM32CubeMX"),
    ("ProjectManager.FirmwarePackage", "paquet logiciel STM32L4"),
]

# --------------------------------------------------------------------------
# 4. Fichiers du jeu (jamais générés par CubeMX)
# --------------------------------------------------------------------------
PORTABLE_DIRS = ["Core/Src/app", "Core/Src/engine", "Core/Src/graphics", "Core/Src/services",
                 "Core/Src/devices", "Core/Src/content"]
GAME_FILES = (["Core/Src/oa_board.c", "Core/Src/usb_console.c",
               "Core/Src/app/oa_app.c",
               "Core/Src/engine/oa_game.c", "Core/Src/engine/oa_physics.c", "Core/Src/engine/oa_ui.c",
               "Core/Src/graphics/oa_render.c", "Core/Src/graphics/oa_assets.c",
               "Core/Src/services/oa_input.c", "Core/Src/services/oa_calibration.c",
               "Core/Src/services/oa_storage.c",
               "Core/Src/devices/oa_lcd.c", "Core/Src/devices/oa_touch.c",
               "Core/Src/devices/oa_accel.c", "Core/Src/devices/oa_eeprom.c",
               "Core/Src/content/oa_levels.c"] +
              ["Core/Inc/%s.h" % m for m in ("oa_app", "oa_board", "oa_engine", "oa_config", "oa_ui",
                                             "oa_render", "oa_assets", "oa_input", "oa_calibration",
                                             "oa_storage", "oa_devices", "usb_console")])

# Dossiers du projet où chercher les fonctions de rappel (pas Drivers/ :
# les versions « __weak » de la HAL y sont normales)
CALLBACK_DIRS = ["Core/Src", "USB_DEVICE"]
GAME_CALLBACKS = {"HAL_SPI_TxCpltCallback": "Core/Src/oa_board.c",
                  "HAL_SPI_ErrorCallback": "Core/Src/oa_board.c"}


# ==========================================================================
class Report:
    """Compte et affiche les résultats."""

    def __init__(self):
        self.errors = 0
        self.warnings = 0

    def ok(self, text):
        print("  [OK]        " + text)

    def error(self, text, hint=""):
        self.errors += 1
        print("  [ERREUR]    " + text)
        if hint:
            print("              -> " + hint)

    def warning(self, text, hint=""):
        self.warnings += 1
        print("  [ATTENTION] " + text)
        if hint:
            print("              -> " + hint)

    def info(self, text):
        print("  [INFO]      " + text)


def read_text(path):
    """Lit un fichier texte (UTF-8 ou ASCII), fins de ligne unifiées."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read().replace("\r\n", "\n")


USER_CODE = re.compile(r"/\*\s*USER CODE BEGIN (.+?)\s*\*/(.*?)/\*\s*USER CODE END \1\s*\*/",
                       re.DOTALL)


def user_sections(text):
    """Rend {nom de section: contenu} des sections USER CODE d'un fichier."""
    return {m.group(1): m.group(2) for m in USER_CODE.finditer(text)}


def strip_comments(code):
    """Retire les commentaires C (pour ne pas compter une ligne mise en commentaire)."""
    code = re.sub(r"/\*.*?\*/", " ", code, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", code)


def read_ioc(path):
    """Rend {clé: valeur} du fichier .ioc (format « clé=valeur »)."""
    values = {}
    for line in read_text(path).split("\n"):
        if "=" in line and not line.startswith("#"):
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    return values


# ==========================================================================
def check_hooks(root, rep):
    print("\n1. Crochets dans les sections USER CODE")
    cache = {}
    for rel, section, needle, role in HOOKS:
        path = os.path.join(root, rel)
        if rel not in cache:
            cache[rel] = user_sections(read_text(path)) if os.path.isfile(path) else None
        sections = cache[rel]
        where = "%s, section USER CODE %s" % (rel, section)
        if sections is None:
            rep.error("%s : fichier absent" % rel,
                      "relancez la génération du code dans CubeMX (Generate Code)")
        elif section not in sections:
            rep.error("%s : section introuvable" % where,
                      "les commentaires USER CODE BEGIN/END ont été effacés ou modifiés")
        elif needle not in strip_comments(sections[section]):
            rep.error("%s : « %s » manque (%s)" % (where, needle, role),
                      "remettez cette ligne entre /* USER CODE BEGIN %s */ et "
                      "/* USER CODE END %s */" % (section, section))
        else:
            rep.ok("%s : %s" % (where, needle))
    for rel, section, needle, why in FORBIDDEN:
        sections = cache.get(rel)
        if sections and section in sections and needle in strip_comments(sections[section]):
            rep.error("%s, section USER CODE %s : « %s » ne doit pas y être" % (rel, section, needle),
                      why)


def check_ioc(root, rep):
    print("\n2. Réglages du fichier .ioc")
    iocs = glob.glob(os.path.join(root, "*.ioc"))
    if len(iocs) != 1:
        rep.error("il faut exactement un fichier .ioc dans %s (trouvés : %d)" % (root, len(iocs)))
        return
    ioc = read_ioc(iocs[0])
    rep.info("fichier %s" % os.path.basename(iocs[0]))
    for key, label in IOC_INFO:
        rep.info("%s : %s" % (label, ioc.get(key, "?")))
    name = ioc.get("ProjectManager.ProjectName", "")
    if name + ".ioc" != os.path.basename(iocs[0]):
        rep.warning("nom du projet « %s » différent du nom du fichier .ioc" % name,
                    "Project Manager > Project > Project Name doit être le nom du fichier .ioc")
    script = ioc.get("ProjectManager.UAScriptAfterPath", "")
    if script:
        rep.warning("script lancé après la génération : %s" % script,
                    "aucun script n'est nécessaire : videz « After Code Generation » "
                    "(Project Manager > Code Generator > User Actions)")
    for key, expected, level, why in IOC_SETTINGS:
        value = ioc.get(key)
        if value == expected:
            rep.ok("%s = %s" % (key, expected))
            continue
        text = "%s = %s (attendu : %s)" % (key, value if value is not None else "absent", expected)
        if level == "ERREUR":
            rep.error(text, why)
        else:
            rep.warning(text, why)
    state = ioc.get("PC2.PinState", "GPIO_PIN_RESET")
    if state == "GPIO_PIN_RESET":
        rep.ok("PC2 (BLE_RST) à 0 au démarrage : module Bluetooth maintenu en reset")
    else:
        rep.warning("PC2.PinState = %s" % state,
                    "BLE_RST doit rester à 0 : le jeu n'utilise pas le Bluetooth")
    for key, role in NVIC_ENABLED:
        value = ioc.get(key, "")
        if value.split("\\:")[0] == "true":
            rep.ok("%s autorisée (%s)" % (key, role))
        else:
            rep.error("%s non autorisée (%s)" % (key, role),
                      "System Core > NVIC : cochez la case « Enabled » de cette interruption")


def check_generated(root, rep):
    print("\n3. Code généré : initialisations et noms des broches")
    main_c = os.path.join(root, "Core/Src/main.c")
    if os.path.isfile(main_c):
        code = strip_comments(read_text(main_c))
        body = code[code.find("int main(void)"):]
        positions = []
        for fn in INIT_ORDER:
            m = re.search(r"^\s*" + fn + r"\(\);", body, re.MULTILINE)
            if not m:
                rep.error("main.c : %s() n'est plus appelée" % fn,
                          "le périphérique a été désactivé dans CubeMX, ou son appel retiré "
                          "(Project Manager > Advanced Settings)")
            else:
                positions.append((m.start(), fn))
        found = [fn for _, fn in sorted(positions)]
        if "MX_DMA_Init" in found and "MX_SPI1_Init" in found and \
                found.index("MX_DMA_Init") > found.index("MX_SPI1_Init"):
            rep.error("main.c : MX_DMA_Init() est appelée après MX_SPI1_Init()",
                      "Project Manager > Advanced Settings : placez MX_DMA_Init juste après "
                      "MX_GPIO_Init, sinon le DMA de l'écran ne fonctionne pas")
        elif len(found) == len(INIT_ORDER):
            rep.ok("main.c : %d initialisations, MX_DMA_Init() avant MX_SPI1_Init()" % len(found))
    else:
        rep.error("Core/Src/main.c absent", "relancez la génération du code dans CubeMX")
    main_h = os.path.join(root, "Core/Inc/main.h")
    if not os.path.isfile(main_h):
        rep.error("Core/Inc/main.h absent", "relancez la génération du code dans CubeMX")
        return
    defined = set(re.findall(r"^#define\s+(\w+)", read_text(main_h), re.MULTILINE))
    used = {}
    for rel in GAME_FILES:
        path = os.path.join(root, rel)
        if os.path.isfile(path):
            code = strip_comments(read_text(path))
            for name in re.findall(r"\b([A-Z][A-Z0-9_]*_(?:Pin|GPIO_Port|EXTI_IRQn))\b", code):
                if name != "GPIO_Pin":          # paramètre des fonctions de la HAL
                    used.setdefault(name, rel)
    for name in sorted(used):
        if name in defined:
            rep.ok("%s (utilisé par %s)" % (name, used[name]))
        else:
            rep.error("%s utilisé par %s mais absent de main.h" % (name, used[name]),
                      "le nom (User Label) de la broche a changé dans CubeMX : "
                      "remettez l'ancien nom ou adaptez le code")


def check_game_files(root, rep):
    print("\n4. Fichiers du jeu")
    mx = os.path.join(root, ".mxproject")
    generated = read_text(mx).replace("\\", "/") if os.path.isfile(mx) else ""
    for rel in GAME_FILES:
        if not os.path.isfile(os.path.join(root, rel)):
            rep.error("%s absent" % rel, "récupérez-le dans l'archive du projet")
        elif re.search(r"(^|[;=/])" + re.escape(rel) + r"($|;)", generated, re.MULTILINE):
            rep.error("%s est listé dans .mxproject comme fichier généré" % rel,
                      "CubeMX pourrait l'effacer : ne donnez pas à vos fichiers le nom "
                      "d'un fichier que CubeMX génère")
        else:
            rep.ok(rel)


def check_callbacks(root, rep):
    print("\n5. Fonctions de rappel de la HAL écrites une seule fois")
    definition = re.compile(r"^[ \t]*(?:__weak[ \t]+)?void[ \t]+(HAL_\w*Callback)[ \t]*\([^;{]*\)\s*\{",
                            re.MULTILINE)
    where = {}
    for folder in CALLBACK_DIRS:
        for path in glob.glob(os.path.join(root, folder, "**", "*.c"), recursive=True):
            code = strip_comments(read_text(path))
            for name in definition.findall(code):
                where.setdefault(name, []).append(os.path.relpath(path, root).replace("\\", "/"))
    for name in sorted(where):
        files = where[name]
        if len(files) > 1:
            rep.error("%s écrite dans %d fichiers : %s" % (name, len(files), ", ".join(files)),
                      "gardez-en une seule (celles du jeu sont dans Core/Src/oa_board.c), "
                      "sinon : « multiple definition »")
    for name, rel in GAME_CALLBACKS.items():
        files = where.get(name, [])
        if files == [rel]:
            rep.ok("%s : %s" % (name, rel))
        elif not files:
            rep.error("%s introuvable" % name,
                      "%s manque : l'écran ne serait plus libéré après un envoi DMA" % rel)


def check_portability(root, rep):
    print("\n6. Modules portables (compilés aussi par le simulateur et les tests PC)")
    forbidden = re.compile(r'#\s*include\s*"(main\.h|stm32l4xx_hal[^"]*|spi\.h|i2c\.h|tim\.h|usart\.h)"'
                           r'|\bHAL_[A-Za-z]\w*\s*\(')
    count = 0
    for folder in PORTABLE_DIRS:
        for path in sorted(glob.glob(os.path.join(root, folder, "*.c"))):
            rel = os.path.relpath(path, root).replace("\\", "/")
            m = forbidden.search(strip_comments(read_text(path)))
            count += 1
            if m:
                rep.warning("%s utilise la HAL (« %s »)" % (rel, m.group(0).strip()),
                            "passez par une fonction de oa_board.h, sinon le simulateur et "
                            "les tests PC ne compilent plus")
    rep.ok("%d fichiers vérifiés dans %s" % (count, ", ".join(d.split("/")[-1] for d in PORTABLE_DIRS)))


def main():
    if len(sys.argv) > 1:
        root = os.path.abspath(sys.argv[1])
    else:
        here = os.path.dirname(os.path.abspath(__file__))
        root = os.path.dirname(here) if os.path.basename(here) == "Tools" else os.getcwd()
    print("Vérification du projet : %s" % root)
    rep = Report()
    check_hooks(root, rep)
    check_ioc(root, rep)
    check_generated(root, rep)
    check_game_files(root, rep)
    check_callbacks(root, rep)
    check_portability(root, rep)
    print("\nBilan : %d erreur(s), %d avertissement(s)" % (rep.errors, rep.warnings))
    if rep.errors == 0:
        print("Le projet est prêt à être compilé (Project > Build Project).")
    return 1 if rep.errors else 0


if __name__ == "__main__":
    # Affichage correct des accents dans l'invite de commandes Windows
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    sys.exit(main())
