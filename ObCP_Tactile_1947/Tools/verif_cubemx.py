#!/usr/bin/env python3
"""Contrôle ciblé du contrat ObCP après Generate Code (Python 3, sans paquet)."""
import argparse
import re
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = r"${workspace_loc:/${ProjName}/Linker/ObCP_Flash.ld}"
BASE = 0x08000000
FIRMWARE_END = 0x08080000
STORE_START = 0x080DB800
STORE_BYTES = 144 * 1024

PINS = {
    "PA5": "SPI1_SCK", "PA6": "SPI1_MISO", "PA7": "SPI1_MOSI",
    "PB8": "I2C1_SCL", "PB9": "I2C1_SDA",
    "PB6": "GPIO_Output", "PC7": "GPIO_Output", "PB5": "GPIO_Output",
    "PA9": "GPIO_Output",
    "PC10": "SPI3_SCK", "PC11": "SPI3_MISO", "PC12": "SPI3_MOSI",
    "PB1": "TIM1_CH3N", "PC5": "GPIO_Output", "PC13": "GPIO_Input",
}
LABELS = {
    "PB6": "TFT_CS", "PC7": "TFT_DC", "PB5": "SD_CS",
    "PA9": "TOUCH_SPI_CS",
    "PC5": "ACC_CS", "PC6": "LED_RED", "PC8": "LED_GREEN",
    "PB1": "PWM_MOTOR", "PC13": "USER_BUTTON",
}


def read_ioc():
    """Lire les couples clé/valeur CubeMX sans modifier le fichier de configuration."""
    lines = (ROOT / "ObCP_Tactile_1947.ioc").read_text(encoding="utf-8").splitlines()
    return dict(line.split("=", 1) for line in lines if "=" in line)


def linker_options(restore):
    """Lire les deux options -T de CubeIDE, puis les rétablir si restore est vrai.

    Le remplacement porte seulement sur la valeur des options identifiées dans le XML.
    Le reste du texte est préservé afin de ne pas reformater les métadonnées Eclipse.
    """
    path = ROOT / ".cproject"
    text = path.read_text(encoding="utf-8")
    root = ET.fromstring(text)
    options = root.findall(".//option[@superClass='com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.linker.option.script']")
    if restore and len(options) == 2 and any(option.get("value") != SCRIPT for option in options):
        # Modifier seulement les deux valeurs -T. Garder le reste des métadonnées.
        for option in options:
            identifier = option.attrib["id"]
            text, n = re.subn(
                r'(<option\b[^>]*\bid="' + re.escape(identifier) + r'"[^>]*\bvalue=")[^"]+(")',
                lambda m: m[1] + SCRIPT + m[2], text,
            )
            if n != 1:
                raise ValueError(f"Option de liaison ambiguë {identifier}")
        path.write_text(text, encoding="utf-8")
        print("Script de liaison rétabli en Debug et Release.")
        root = ET.fromstring(text)
        options = root.findall(".//option[@superClass='com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.linker.option.script']")
    return options


def image_span(path):
    """Valider un Intel HEX et renvoyer adresses min/max et nombre d’octets chargés.

    Une ligne contient longueur, adresse locale, type, données et somme de contrôle.
    Les enregistrements de type 2/4 changent la base des adresses suivantes. Tous les
    octets doivent rester en banque 1 et le vecteur Reset doit viser du code Thumb.
    Une ValueError décrit la première anomalie trouvée.
    """
    high = 0
    loaded = {}
    eof = False
    for line in path.read_text(encoding="ascii").splitlines():
        if eof or not line.startswith(":"):
            raise ValueError("Structure Intel HEX invalide")
        record = bytes.fromhex(line[1:])
        if len(record) != record[0] + 5 or sum(record) % 256:
            raise ValueError("Somme de contrôle Intel HEX invalide")
        offset = int.from_bytes(record[1:3], "big")
        kind = record[3]
        data = record[4:-1]
        if kind == 0:
            address = high + offset
            if not BASE <= address <= address + len(data) <= FIRMWARE_END:
                raise ValueError(f"Données programmées hors banque 1 à {address:#010x}")
            for i, value in enumerate(data):
                loaded[address + i] = value
        elif kind == 4:
            high = int.from_bytes(data, "big") << 16
        elif kind == 2:
            high = int.from_bytes(data, "big") << 4
        elif kind == 1:
            eof = True
        elif kind not in (3, 5):
            raise ValueError(f"Enregistrement Intel HEX inconnu {kind}")
    if not eof or min(loaded, default=0) != BASE:
        raise ValueError("Image vide ou vecteurs de démarrage absents")
    reset = int.from_bytes(bytes(loaded[BASE + i] for i in range(4, 8)), "little")
    if not (reset & 1 and BASE <= reset < FIRMWARE_END):
        raise ValueError("Vecteur de démarrage Thumb hors image")
    return min(loaded), max(loaded), len(loaded)


def main():
    """Vérifier brochage, DMA, crochets et mémoire, puis afficher les écarts éventuels.

    Sans --retablir-lien, le contrôle est en lecture seule. --hex et --map ajoutent
    l’inspection d’un résultat de compilation au contrôle du projet CubeMX/CubeIDE.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--retablir-lien", action="store_true")
    parser.add_argument("--hex", type=Path)
    parser.add_argument("--map", type=Path)
    args = parser.parse_args()
    issues = []
    ioc = read_ioc()
    # Un changement de ces réglages exige aussi de revoir le pilote concerné.
    contract = {
        "ProjectManager.KeepUserCode": "true",
        "ProjectManager.TargetToolchain": "STM32CubeIDE",
        "PA9.PinState": "GPIO_PIN_SET",
        "SPI1.BaudRatePrescaler": "SPI_BAUDRATEPRESCALER_8",
        "TIM1.Period": "1023",
        "TIM1.RepetitionCounter": "3",
        "RCC.SYSCLKFreq_VALUE": "64000000",
        "RCC.APB2Freq_Value": "64000000",
        "Dma.SPI1_TX.0.Instance": "DMA1_Channel3",
        "Dma.TIM1_UP.1.Instance": "DMA1_Channel6",
        "Dma.TIM1_UP.1.Mode": "DMA_CIRCULAR",
    }
    contract.update({f"{pin}.Signal": value for pin, value in PINS.items()})
    contract.update({f"{pin}.GPIO_Label": label for pin, label in LABELS.items()})
    for key, expected in contract.items():
        if ioc.get(key) != expected:
            issues.append(f".ioc {key} attendu {expected}, relevé {ioc.get(key)}")
    options = linker_options(args.retablir_lien)
    if len(options) != 2 or any(option.get("value") != SCRIPT for option in options):
        issues.append("Sélectionner Linker/ObCP_Flash.ld dans les deux configurations CubeIDE")

    sources = ROOT / "Core/Src"
    main_c = (sources / "main.c").read_text(encoding="utf-8")
    irq_c = (sources / "stm32l4xx_it.c").read_text(encoding="utf-8")
    for marker, call in (("Includes", '#include "obcp_board.h"'),
                         ("2", "obcp_start();"), ("3", "obcp_task();")):
        user = re.search(r"USER CODE BEGIN " + marker + r"\s*\*/(.*?)USER CODE END " + marker,
                         main_c, re.S)
        if not user or call not in user[1]:
            issues.append(f"Crochet {marker} de main.c absent ou incomplet")
    for term in ("obcp_flash_ecc_nmi()", "DMA1_Channel3_IRQHandler", "DMA1_Channel6_IRQHandler"):
        if term not in irq_c:
            issues.append(f"Interruption ou crochet absent dans stm32l4xx_it.c {term}")
    if not re.search(r"MX_DMA_Init\(\);.*MX_SPI1_Init\(\);.*MX_TIM1_Init\(\);", main_c, re.S):
        issues.append("Initialiser le DMA avant SPI1 et TIM1")
    gpio_c = (sources / "gpio.c").read_text(encoding="utf-8")
    main_h = (ROOT / "Core/Inc/main.h").read_text(encoding="utf-8")
    if ("HAL_GPIO_WritePin(TOUCH_SPI_CS_GPIO_Port, TOUCH_SPI_CS_Pin, GPIO_PIN_SET);" not in gpio_c or
            "#define TOUCH_SPI_CS_Pin GPIO_PIN_9" not in main_h or
            "#define TOUCH_SPI_CS_GPIO_Port GPIOA" not in main_h):
        issues.append("Le CS tactile doit être PA9 et démarrer à 1")
    for name, terms in {
        "spi.c": ["DMA1_Channel3", "DMA_REQUEST_1", "SPI_BAUDRATEPRESCALER_8"],
        "tim.c": ["DMA1_Channel6", "DMA_REQUEST_7", "TIM_DMA_ID_UPDATE", "RepetitionCounter = 3",
                  "Period = 1023", "DMA_PDATAALIGN_HALFWORD", "DMA_MDATAALIGN_HALFWORD"],
    }.items():
        content = (sources / name).read_text(encoding="utf-8")
        for term in terms:
            if term not in content:
                issues.append(f"{name} paramètre généré absent {term}")
    if args.map:
        content = args.map.read_text(encoding="utf-8", errors="replace")
        match = re.search(r"^STORAGE\s+(0x[\da-f]+)\s+(0x[\da-f]+)", content, re.I | re.M)
        if not match or tuple(int(v, 16) for v in match.groups()) != (STORE_START, STORE_BYTES):
            issues.append("Réserve STORAGE non conforme dans le fichier .map")
    if args.hex:
        try:
            begin, end, count = image_span(args.hex)
            print(f"HEX {begin:#010x} à {end:#010x}, {count} octets en banque 1.")
        except (ValueError, KeyError) as exc:
            issues.append(str(exc))
    for issue in issues:
        print("ÉCART", issue)
    print(f"Bilan {len(issues)} écart(s).")
    return bool(issues)


if __name__ == "__main__":
    raise SystemExit(main())
