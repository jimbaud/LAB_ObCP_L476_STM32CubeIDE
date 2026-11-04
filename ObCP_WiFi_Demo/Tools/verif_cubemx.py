#!/usr/bin/env python3
"""
Vérification du projet après une régénération par STM32CubeMX
==============================================================

    python3 Tools/verif_cubemx.py            (depuis le dossier du projet)
    python3 Tools/verif_cubemx.py <dossier>  (dossier du projet)

Le code de la démo est dans des fichiers que CubeMX ne génère pas (Core/Src/
board.c, cdc_port.c, fault_handlers.c, uart_bridge.c, ncp_port_obcp.c,
Core/Src/app/, Core/Src/st67w6x/, Core/Src/qrcodegen/ et leurs en-têtes dans
Core/Inc/). Il est relié au code généré par quelques lignes placées dans les
sections « USER CODE », que CubeMX conserve à chaque régénération.

Ce script vérifie que ces points d'accroche sont toujours là, que les
réglages dont la démo dépend sont toujours dans le .ioc (périphériques,
étiquettes des broches, gestionnaires de faute non générés), que les
sections USER CODE sont bien formées et qu'aucun fichier de la démo n'est
déclaré comme « généré » dans .mxproject (CubeMX l'effacerait).
Code de retour : 0 si tout est correct, 1 sinon.
"""
import re
import sys
from pathlib import Path

PRJ = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent)

# (fichier, section USER CODE, texte attendu dans la section)
HOOKS = [
    ("Core/Src/main.c", "Includes", '#include "board.h"'),
    ("Core/Src/main.c", "Includes", '#include "app.h"'),
    ("Core/Src/main.c", "1", "board_early_init();"),
    ("Core/Src/main.c", "Init", "board_lse_start();"),
    ("Core/Src/main.c", "2", "board_post_init();"),
    ("Core/Src/main.c", "2", "app_init();"),
    ("Core/Src/main.c", "WHILE", "app_loop();"),
    ("Core/Src/main.c", "Error_Handler_Debug", "board_error_blink();"),
    ("Core/Src/stm32l4xx_it.c", "PFP", "void fault_nmi(void);"),
    ("Core/Src/stm32l4xx_it.c", "NonMaskableInt_IRQn 0", "fault_nmi();"),
    ("USB_DEVICE/App/usbd_cdc_if.c", "INCLUDE", '#include "cdc_port.h"'),
    ("USB_DEVICE/App/usbd_cdc_if.c", "3", "cdc_port_itf_init()"),
    ("USB_DEVICE/App/usbd_cdc_if.c", "4", "cdc_port_itf_deinit()"),
    ("USB_DEVICE/App/usbd_cdc_if.c", "5", "cdc_port_itf_control(cmd, pbuf, length)"),
    ("USB_DEVICE/App/usbd_cdc_if.c", "6", "cdc_port_itf_receive(Buf, Len)"),
    ("USB_DEVICE/App/usbd_cdc_if.c", "7", "cdc_port_write(Buf, Len)"),
    ("USB_DEVICE/App/usbd_cdc_if.c", "13", "cdc_port_itf_tx_complete(Buf, Len, epnum)"),
    ("USB_DEVICE/Target/usbd_conf.c", "Includes", '#include "cdc_port.h"'),
    ("USB_DEVICE/Target/usbd_conf.c", "2", "cdc_port_on_suspend(true);"),
    ("USB_DEVICE/Target/usbd_conf.c", "3", "cdc_port_on_suspend(false);"),
]

# Fonctions d'initialisation générées dont la démo a besoin (périphérique -> fichier)
INITS = [("MX_GPIO_Init", "GPIO", "Core/Src/gpio.c"), ("MX_SPI1_Init", "SPI1 (module Wi-Fi)", "Core/Src/spi.c"),
         ("MX_SPI3_Init", "SPI3 (accéléromètre)", "Core/Src/spi.c"),
         ("MX_ADC1_Init", "ADC1 (température, VREFINT)", "Core/Src/adc.c"),
         ("MX_TIM1_Init", "TIM1 (sortie PWM)", "Core/Src/tim.c"), ("MX_TIM3_Init", "TIM3 (LED RVB)", "Core/Src/tim.c"),
         ("MX_RTC_Init", "RTC", "Core/Src/rtc.c"), ("MX_USB_DEVICE_Init", "USB_DEVICE (CDC)", "USB_DEVICE/App/usb_device.c")]

# Étiquettes (User Label) des broches utilisées par Core/Inc/board.h
LABELS = ["User1", "BLE_Reset", "NCP_UART_TX", "NCP_UART_RX", "ACC_CS", "PWM_Motor", "NCP_BOOT", "Led_Red",
          "Led_Green", "Led_Blue", "NCP_SW1", "NCP_RDY", "NCP_CHIP_EN", "NCP_CS"]

# Fichiers de la démo (jamais générés par CubeMX)
OWN = ["Core/Src/board.c", "Core/Src/cdc_port.c", "Core/Src/fault_handlers.c", "Core/Src/uart_bridge.c",
       "Core/Src/ncp_port_obcp.c", "Core/Inc/board.h", "Core/Inc/cdc_port.h", "Core/Inc/uart_bridge.h",
       "Core/Src/app/app.c", "Core/Src/st67w6x/ncp.c", "Core/Src/qrcodegen/qrcodegen.c"]

FAULTS = ["HardFault_Handler", "MemManage_Handler", "BusFault_Handler", "UsageFault_Handler"]
NVIC_FAULTS = ["HardFault_IRQn", "MemoryManagement_IRQn", "BusFault_IRQn", "UsageFault_IRQn"]

errors = []
notes = []


def read(rel):
    p = PRJ / rel
    return p.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n") if p.is_file() else None


def sections(text, rel):
    """Sections USER CODE d'un fichier : {nom: [contenu, ...]} ; vérifie l'imbrication."""
    res = {}
    stack = []
    for m in re.finditer(r"/\* USER CODE (BEGIN|END) (.+?) \*/", text):
        kind, name = m.group(1), m.group(2)
        if kind == "BEGIN":
            if stack:
                errors.append(f"{rel} : section « {name} » ouverte dans « {stack[-1][0]} »")
            stack.append((name, m.end()))
        else:
            if not stack or stack[-1][0] != name:
                errors.append(f"{rel} : fin de section « {name} » inattendue")
                continue
            res.setdefault(name, []).append(text[stack[-1][1]:m.start()])
            stack.pop()
    for name, _ in stack:
        errors.append(f"{rel} : section « {name} » jamais refermée")
    return res


# ---- 1. Points d'accroche dans les sections USER CODE ----------------------------
cache = {}
for rel, sec, txt in HOOKS:
    if rel not in cache:
        t = read(rel)
        cache[rel] = sections(t, rel) if t is not None else None
    secs = cache[rel]
    if secs is None:
        errors.append(f"{rel} : fichier absent (périphérique désactivé dans CubeMX ?)")
    elif sec not in secs:
        errors.append(f"{rel} : section « USER CODE BEGIN {sec} » absente")
    elif not any(txt in body for body in secs[sec]):
        errors.append(f"{rel} : « {txt} » manque dans la section USER CODE {sec}")
ok_hooks = len(HOOKS) - sum(1 for e in errors if "manque" in e or "absente" in e)

# ---- 2. Périphériques et étiquettes ----------------------------------------------
main_c = read("Core/Src/main.c") or ""
for fn, what, rel in INITS:
    if not re.search(r"^\s*" + fn + r"\(\);", main_c, re.M):
        errors.append(f"main.c : {fn}() n'est plus appelée : {what} a été désactivé dans CubeMX ou "
                      f"son appel retiré (Project Manager > Advanced Settings)")
    if read(rel) is None:
        errors.append(f"{rel} absent : {what}")
main_h = read("Core/Inc/main.h") or ""
for lab in LABELS:
    if not re.search(r"#define\s+" + lab + r"_Pin\b", main_h):
        errors.append(f"main.h : étiquette de broche « {lab} » absente (renommée dans CubeMX ?)")

# ---- 3. Gestionnaires de faute ----------------------------------------------------
it_c = read("Core/Src/stm32l4xx_it.c") or ""
for fn in FAULTS:
    if re.search(r"^void\s+" + fn + r"\s*\(", it_c, re.M):
        errors.append(f"stm32l4xx_it.c : {fn}() est générée alors qu'elle est définie dans fault_handlers.c "
                      f"(NVIC > Code generation : décocher « Generate IRQ handler »)")
iocs = sorted(PRJ.glob("*.ioc"))
if len(iocs) != 1:
    errors.append(f"un seul fichier .ioc attendu à la racine du projet ({len(iocs)} trouvé(s))")
else:
    ioc = iocs[0].read_text(encoding="utf-8", errors="replace")
    for irq in NVIC_FAULTS:
        m = re.search(r"^NVIC\." + irq + r"=(.*)$", ioc, re.M)
        fields = m.group(1).strip().split("\\:") if m else []
        if len(fields) < 6 or fields[5] != "false":
            errors.append(f"{iocs[0].name} : NVIC.{irq} : la génération du gestionnaire doit rester désactivée")
    for ip in ("ADC1", "RTC", "SPI1", "SPI3", "TIM1", "TIM3", "USB_DEVICE", "USB_OTG_FS"):
        if not re.search(r"^Mcu\.IP\d+=" + ip + r"\s*$", ioc, re.M):
            errors.append(f"{iocs[0].name} : périphérique {ip} absent")

# ---- 4. Sections USER CODE bien formées dans tous les fichiers générés ----------------
for p in sorted(list((PRJ / "Core").rglob("*.[ch]")) + list((PRJ / "USB_DEVICE").rglob("*.[ch]"))):
    rel = p.relative_to(PRJ).as_posix()
    if rel not in cache:
        sections(read(rel), rel)

# ---- 5. Fichiers de la démo : présents, jamais listés comme générés -----------------
mx = read(".mxproject") or ""
for rel in OWN:
    if read(rel) is None:
        errors.append(f"{rel} : fichier de la démo absent")
own_all = [p.relative_to(PRJ).as_posix() for p in (PRJ / "Core").rglob("*.[ch]")]
for rel in own_all:
    if rel.replace("/", "\\") in mx and not any(g in rel for g in (
            "main.", "gpio.", "adc.", "rtc.", "spi.", "tim.", "stm32l4xx_it.", "stm32l4xx_hal_msp.",
            "stm32l4xx_hal_conf.", "system_stm32l4xx.")):
        errors.append(f".mxproject liste {rel} comme fichier généré : CubeMX pourrait l'effacer")

# ---- Bilan -------------------------------------------------------------------------
print(f"Projet : {PRJ}")
print(f"Points d'accroche dans les sections USER CODE : {ok_hooks}/{len(HOOKS)} présents")
if errors:
    print(f"{len(errors)} problème(s) :")
    for e in errors:
        print("  - " + e)
    sys.exit(1)
print("Périphériques, étiquettes des broches et gestionnaires de faute : conformes")
print("Projet prêt à compiler.")
