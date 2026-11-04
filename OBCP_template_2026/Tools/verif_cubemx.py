#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verif_cubemx.py - Vérifie le projet après une régénération par STM32CubeMX
==========================================================================

Utilisation (depuis le dossier du projet, celui qui contient le .ioc) :

    python Tools/verif_cubemx.py
    python Tools/verif_cubemx.py C:\\chemin\\vers\\OBCP_template_2026

Pourquoi ce script ?
    STM32CubeMX réécrit les fichiers qu'il génère (main.c, gpio.c, tim.c,
    usbd_cdc_if.c...) chaque fois que l'on clique sur « Generate Code ».
    Il ne garde que ce qui est écrit entre les commentaires
    /* USER CODE BEGIN xxx */ et /* USER CODE END xxx */.
    Le programme de l'ObCP est donc rangé dans ses propres fichiers
    (app.c, commands.c, ble_uart.c...) que CubeMX ne touche jamais, et
    seules quelques lignes « crochets » sont placées dans les sections
    USER CODE des fichiers générés. Ce script vérifie que tout est en place.

Ce que le script vérifie :
    1. les crochets dans les sections USER CODE (app_init, app_loop...) ;
    2. les réglages du fichier .ioc dont dépend le programme (noms des
       broches, timers, interruptions, options de génération) ;
    3. le code généré : chaque nom de broche utilisé par l'application
       existe bien dans main.h ;
    4. les fichiers de l'application sont présents, et CubeMX ne les
       considère pas comme des fichiers générés (il pourrait les effacer) ;
    5. chaque fonction de rappel de la HAL (HAL_..._Callback) n'est
       écrite qu'une fois (sinon : erreur « multiple definition »).

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
HOOKS = [
    ("Core/Src/main.c", "Includes", '#include "app.h"',
     "déclare app_init() et app_loop()"),
    ("Core/Src/main.c", "Includes", '#include "fault.h"',
     "déclare fault_signal()"),
    ("Core/Src/main.c", "2", "app_init();",
     "démarre l'application (sorties, accéléromètre, Bluetooth)"),
    ("Core/Src/main.c", "WHILE", "while (1)",
     "boucle principale (ligne écrite par CubeMX, à garder)"),
    ("Core/Src/main.c", "3", "app_loop();",
     "un tour de la boucle principale"),
    ("Core/Src/main.c", "Error_Handler_Debug", "fault_signal(FAULT_ERROR_HANDLER);",
     "signale une erreur grave par la LED rouge"),
    ("Core/Src/stm32l4xx_it.c", "Includes", '#include "fault.h"',
     "déclare fault_signal()"),
    ("Core/Src/stm32l4xx_it.c", "NonMaskableInt_IRQn 0", "fault_signal(FAULT_NMI);",
     "signale une NMI par la LED rouge"),
    ("Core/Src/stm32l4xx_it.c", "HardFault_IRQn 0", "fault_signal(FAULT_HARDFAULT);",
     "signale une HardFault par la LED rouge"),
    ("Core/Src/stm32l4xx_it.c", "MemoryManagement_IRQn 0", "fault_signal(FAULT_MEMMANAGE);",
     "signale une MemManage fault par la LED rouge"),
    ("Core/Src/stm32l4xx_it.c", "BusFault_IRQn 0", "fault_signal(FAULT_BUSFAULT);",
     "signale une BusFault par la LED rouge"),
    ("Core/Src/stm32l4xx_it.c", "UsageFault_IRQn 0", "fault_signal(FAULT_USAGEFAULT);",
     "signale une UsageFault par la LED rouge"),
    ("USB_DEVICE/App/usbd_cdc_if.c", "INCLUDE", '#include "usb_console.h"',
     "déclare les fonctions du module usb_console"),
    ("USB_DEVICE/App/usbd_cdc_if.c", "3", "usb_console_on_init();",
     "prévient usb_console que le PC a ouvert la liaison"),
    ("USB_DEVICE/App/usbd_cdc_if.c", "4", "usb_console_on_deinit();",
     "prévient usb_console que la liaison est fermée"),
    ("USB_DEVICE/App/usbd_cdc_if.c", "6", "usb_console_on_receive(Buf, *Len);",
     "confie chaque paquet reçu du PC à usb_console"),
    ("USB_DEVICE/App/usbd_cdc_if.c", "7", "usb_console_write(Buf, Len)",
     "CDC_Transmit_FS() range le texte dans la file d'envoi"),
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
     "le paquet suivant doit être autorisé par usb_console.c, une fois le "
     "paquet courant traité (contrôle de flux) : retirez cette ligne"),
]

# --------------------------------------------------------------------------
# 2. Réglages du fichier .ioc
#    (clé, valeur attendue, gravité, explication si la valeur diffère)
#    gravité : "ERREUR" (le programme ne marchera pas) ou "ATTENTION"
# --------------------------------------------------------------------------
LABEL_HELP = ("dans CubeMX : clic droit sur la broche > Enter User Label ; "
              "le code utilise ce nom (main.h)")
IOC_SETTINGS = [
    # Options de génération (onglet Project Manager)
    ("ProjectManager.KeepUserCode", "true", "ERREUR",
     "Project Manager > Code Generator > « Keep User Code when re-generating » "
     "doit être coché, sinon les sections USER CODE sont effacées"),
    ("ProjectManager.CoupleFile", "true", "ERREUR",
     "Project Manager > Code Generator > « Generate peripheral initialization as "
     "a pair of '.c/.h' files » doit être coché (l'application inclut tim.h, spi.h)"),
    ("ProjectManager.TargetToolchain", "STM32CubeIDE", "ERREUR",
     "Project Manager > Project > Toolchain/IDE doit rester « STM32CubeIDE »"),
    ("ProjectManager.HeapSize", "0x800", "ATTENTION",
     "taille du tas (printf avec %f en a besoin) : gardez au moins 0x800"),
    ("ProjectManager.StackSize", "0x1000", "ATTENTION",
     "taille de la pile : gardez au moins 0x1000"),
    # Base de temps de la HAL : SysTick (sinon CubeMX écrit dans main.c un
    # deuxième HAL_TIM_PeriodElapsedCallback, déjà présent dans hal_callbacks.c)
    ("VP_SYS_VS_Systick.Mode", "SysTick", "ERREUR",
     "System Core > SYS > Timebase Source doit rester « SysTick »"),
    # Noms des broches (User Label)
    ("PC13.GPIO_Label", "USER_BUTTON", "ERREUR", LABEL_HELP),
    ("PC2.GPIO_Label", "BLE_RST", "ERREUR", LABEL_HELP),
    ("PC3.GPIO_Label", "EEPROM_CS", "ATTENTION", LABEL_HELP),
    ("PC5.GPIO_Label", "ACC_CS", "ERREUR", LABEL_HELP),
    ("PB1.GPIO_Label", "PWM_MOTOR", "ERREUR", LABEL_HELP),
    ("PB2.GPIO_Label", "BLE_CS", "ERREUR", LABEL_HELP),
    ("PB12.GPIO_Label", "BLE_IRQ", "ERREUR", LABEL_HELP),
    ("PC6.GPIO_Label", "LED_RED", "ERREUR", LABEL_HELP),
    ("PC8.GPIO_Label", "LED_GREEN", "ERREUR", LABEL_HELP),
    ("PC9.GPIO_Label", "LED_BLUE", "ERREUR", LABEL_HELP),
    # Broches
    ("PC13.GPIO_ModeDefaultEXTI", "GPIO_MODE_IT_FALLING", "ERREUR",
     "le bouton (PC13) doit déclencher une interruption sur front descendant"),
    ("PC5.PinState", "GPIO_PIN_SET", "ERREUR",
     "ACC_CS (PC5) doit démarrer à 1 : accéléromètre non sélectionné"),
    ("PC3.PinState", "GPIO_PIN_SET", "ATTENTION",
     "EEPROM_CS (PC3) doit démarrer à 1 : mémoire EEPROM non sélectionnée"),
    ("PB2.PinState", "GPIO_PIN_SET", "ERREUR",
     "BLE_CS (PB2) doit démarrer à 1 : module Bluetooth non sélectionné"),
    # Timers
    ("TIM3.Prescaler", "0", "ATTENTION",
     "TIM3 (LED) : prescaler 0 -> PWM à 64 MHz / 65536 = 977 Hz"),
    ("TIM3.Period", "65535", "ATTENTION",
     "TIM3 (LED) : période 65535 -> les valeurs 0 à 65535 des commandes r, v, b "
     "couvrent tout le réglage (avec une autre valeur, la LED est à 100 % dès "
     "que la commande dépasse la période)"),
    ("TIM7.Prescaler", "6399", "ERREUR",
     "TIM7 : 64 MHz / (6399 + 1) = 10 kHz"),
    ("TIM7.Period", "9999", "ERREUR",
     "TIM7 : 10 kHz / (9999 + 1) = 1 interruption par seconde (start timer)"),
    # Classe USB
    ("USB_DEVICE.CLASS_NAME_FS", "CDC", "ERREUR",
     "Middleware > USB_DEVICE > Class For FS IP doit rester « Communication "
     "Device Class (Virtual Port Com) »"),
]

# Interruptions qui doivent être autorisées (System Core > NVIC)
NVIC_ENABLED = [
    ("NVIC.EXTI15_10_IRQn", "bouton PC13 et ligne IRQ du module Bluetooth PB12"),
    ("NVIC.TIM7_IRQn", "interruption 1 s du timer TIM7"),
    ("NVIC.OTG_FS_IRQn", "USB"),
]

# Version des outils (simple information)
IOC_INFO = [
    ("MxCube.Version", "version de STM32CubeMX"),
    ("ProjectManager.FirmwarePackage", "paquet logiciel STM32L4"),
    ("Mcu.ThirdParty0", "paquet Bluetooth X-CUBE-BLE1"),
]

# --------------------------------------------------------------------------
# 4. Fichiers de l'application (jamais générés par CubeMX)
# --------------------------------------------------------------------------
APP_MODULES = ["app", "accelerometer", "ble_uart", "button", "commands",
               "fault", "fifo", "line_builder", "outputs", "timer_1s",
               "usb_console"]
APP_FILES = (["Core/Src/%s.c" % m for m in APP_MODULES] +
             ["Core/Src/hal_callbacks.c"] +
             ["Core/Inc/%s.h" % m for m in APP_MODULES] +
             ["Core/Inc/app_config.h"])

# Dossiers du projet où chercher les fonctions de rappel (pas Drivers/ :
# les versions « __weak » de la HAL y sont normales)
CALLBACK_DIRS = ["Core/Src", "USB_DEVICE", "BlueNRG-MS"]


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
    for key, role in NVIC_ENABLED:
        value = ioc.get(key, "")
        if value.split("\\:")[0] == "true":
            rep.ok("%s autorisée (%s)" % (key, role))
        else:
            rep.error("%s non autorisée (%s)" % (key, role),
                      "System Core > NVIC : cochez la case « Enabled » de cette interruption")


def check_generated(root, rep):
    print("\n3. Noms des broches dans le code généré (main.h)")
    main_h = os.path.join(root, "Core/Inc/main.h")
    if not os.path.isfile(main_h):
        rep.error("Core/Inc/main.h absent", "relancez la génération du code dans CubeMX")
        return
    defined = set(re.findall(r"^#define\s+(\w+)", read_text(main_h), re.MULTILINE))
    used = {}
    for rel in APP_FILES:
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


def check_app_files(root, rep):
    print("\n4. Fichiers de l'application")
    mx = os.path.join(root, ".mxproject")
    generated = read_text(mx).replace("\\", "/") if os.path.isfile(mx) else ""
    for rel in APP_FILES:
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
                      "gardez-en une seule (celles de l'application sont dans "
                      "Core/Src/hal_callbacks.c), sinon : « multiple definition »")
    for name in ("HAL_GPIO_EXTI_Callback", "HAL_TIM_PeriodElapsedCallback"):
        files = where.get(name, [])
        if files == ["Core/Src/hal_callbacks.c"]:
            rep.ok("%s : Core/Src/hal_callbacks.c" % name)
        elif not files:
            rep.error("%s introuvable" % name,
                      "Core/Src/hal_callbacks.c manque : le bouton et le timer 1 s ne marchent plus")


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
    check_app_files(root, rep)
    check_callbacks(root, rep)
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
