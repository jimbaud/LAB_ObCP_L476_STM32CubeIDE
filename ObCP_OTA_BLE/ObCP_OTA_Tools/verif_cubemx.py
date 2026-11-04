# -*- coding: utf-8 -*-
"""
verif_cubemx.py - Vérifie qu'un projet STM32CubeIDE est prêt pour le
chargement par Bluetooth, en particulier après une régénération du code
par STM32CubeMX (fichier .ioc).

    python verif_cubemx.py CHEMIN_DU_PROJET

Le type de projet est reconnu automatiquement :
  - le CHARGEUR (projet ObCP_BLE_Loader : dossier Loader/) ;
  - une APPLICATION chargée par Bluetooth (tout autre projet).

Chaque point est affiché avec OK, ATTENTION (recommandation) ou ERREUR (le
projet ne fonctionnera pas). Code de retour : 0 sans erreur, 1 sinon.
"""

from __future__ import annotations

import os
import re
import sys

resultats = []          # (niveau, message)


def ok(message):
    """Enregistre un point vérifié avec succès."""
    resultats.append(("OK", message))


def attention(message):
    """Enregistre une recommandation (le projet fonctionnera quand même)."""
    resultats.append(("ATTENTION", message))


def erreur(message):
    """Enregistre une erreur (le projet ne fonctionnera pas)."""
    resultats.append(("ERREUR", message))


def lire(chemin):
    """Contenu d'un fichier texte, ou None s'il n'existe pas."""
    try:
        with open(chemin, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return None


def section(texte, nom):
    """Contenu d'une section /* USER CODE BEGIN nom */ ... /* USER CODE END nom */."""
    m = re.search(r"/\*\s*USER CODE BEGIN %s\s*\*/(.*?)/\*\s*USER CODE END %s\s*\*/"
                  % (re.escape(nom), re.escape(nom)), texte or "", re.S)
    return m.group(1) if m else None


def sans_commentaires(texte):
    """Retire les commentaires C (pour ne pas confondre un appel et un commentaire)."""
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", texte or "", flags=re.S)


def zone_flash(projet):
    """(origine, taille) de la zone FLASH du script d'édition de liens."""
    for nom in ("STM32L476RGTX_FLASH.ld",):
        texte = lire(os.path.join(projet, nom))
        if texte:
            m = re.search(r"^\s*FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+)\s*,\s*LENGTH\s*=\s*(\d+)\s*K",
                          texte, re.M)
            if m:
                return int(m.group(1), 16), int(m.group(2)) * 1024
    return None


def verifier_chargeur(projet, main_c):
    """Vérifications propres au projet du chargeur (ObCP_BLE_Loader)."""
    # 1) Appels du chargeur dans les sections USER CODE de main.c
    attendus = [("1", "boot_check_and_jump();"), ("2", "loader_init();"), ("3", "loader_process();")]
    for nom, appel in attendus:
        contenu = sans_commentaires(section(main_c, nom))
        if appel in contenu:
            ok("main.c : %s dans USER CODE %s" % (appel, nom))
        else:
            erreur("main.c : %s manque dans la section USER CODE BEGIN %s" % (appel, nom))
    inclusions = section(main_c, "Includes") or ""
    for entete in ("boot.h", "loader.h"):
        if entete in inclusions:
            ok("main.c : #include \"%s\"" % entete)
        else:
            erreur("main.c : #include \"%s\" manque dans USER CODE BEGIN Includes" % entete)

    # 2) Réglages de compilation (.cproject)
    cproject = lire(os.path.join(projet, ".cproject")) or ""
    nb_config = cproject.count("<cconfiguration ")
    if cproject.count('value="../Loader/Inc"') >= nb_config > 0:
        ok(".cproject : chemin d'inclusion ../Loader/Inc (%d configurations)" % nb_config)
    else:
        erreur(".cproject : ajoutez ../Loader/Inc aux chemins d'inclusion (Properties > C/C++ Build > "
               "Settings > MCU GCC Compiler > Include paths), pour Debug et Release")
    if cproject.count('kind="sourcePath" name="Loader"') >= nb_config > 0:
        ok(".cproject : dossier source Loader")
    else:
        erreur(".cproject : le dossier Loader n'est pas un dossier source (clic droit > "
               "Resource Configurations > Exclude from build... ne doit pas l'exclure)")

    # 3) Script d'édition de liens
    zone = zone_flash(projet)
    if zone and zone[0] == 0x08000000 and zone[1] <= 512 * 1024:
        ok("STM32L476RGTX_FLASH.ld : FLASH en banque 1 (0x08000000, %d Ko)" % (zone[1] // 1024))
    elif zone and zone[0] == 0x08000000:
        attention("STM32L476RGTX_FLASH.ld : FLASH de %d Ko ; limitez-la à 512K (banque 1) pour que le "
                  "chargeur ne puisse jamais déborder sur la zone application" % (zone[1] // 1024))
    else:
        erreur("STM32L476RGTX_FLASH.ld : le chargeur doit commencer à 0x8000000")

    # 4) Étiquettes des broches (main.h), utilisées par le code du chargeur
    main_h = lire(os.path.join(projet, "Core", "Inc", "main.h")) or ""
    for etiquette in ("USER_BUTTON_Pin", "BLE_RST_Pin", "BLE_IRQ_Pin", "BLE_IRQ_EXTI_IRQn",
                      "LED_RED_Pin", "LED_GREEN_Pin", "LED_BLUE_Pin"):
        if re.search(r"#define\s+%s\b" % etiquette, main_h):
            ok("main.h : %s" % etiquette)
        else:
            erreur("main.h : %s absent ; dans CubeMX, l'étiquette (User Label) de la broche doit "
                   "être %s" % (etiquette, etiquette.rsplit("_", 1)[0]))
    gpio_c = lire(os.path.join(projet, "Core", "Src", "gpio.c")) or ""
    if re.search(r"HAL_GPIO_WritePin\(\s*EEPROM_CS_GPIO_Port\s*,\s*EEPROM_CS_Pin\s*,\s*GPIO_PIN_SET\s*\)", gpio_c):
        ok("gpio.c : EEPROM (PC3) désélectionnée au démarrage")
    else:
        attention("gpio.c : PC3 (EEPROM_CS) devrait être une sortie au niveau haut (bus SPI partagé)")

    # 5) Interruption du module BLE
    it_c = lire(os.path.join(projet, "Core", "Src", "stm32l4xx_it.c")) or ""
    if "HAL_EXTI_IRQHandler(&H_EXTI_12)" in it_c:
        ok("stm32l4xx_it.c : interruption EXTI12 du module BLE")
    else:
        erreur("stm32l4xx_it.c : HAL_EXTI_IRQHandler(&H_EXTI_12) absent (X-CUBE-BLE1 mal configuré ?)")

    # 6) Fichiers du chargeur
    manquants = [f for f in ("boot.c", "ble_ota.c", "ota.c", "app_flash.c", "app_image.c",
                             "crc32.c", "leds.c", "loader.c")
                 if not os.path.exists(os.path.join(projet, "Loader", "Src", f))]
    if manquants:
        erreur("fichiers manquants dans Loader/Src : %s" % ", ".join(manquants))
    else:
        ok("dossier Loader complet")


def verifier_application(projet, main_c):
    """Vérifications pour une application chargée par Bluetooth."""
    # 1) Script d'édition de liens : LA modification indispensable
    zone = zone_flash(projet)
    if zone is None:
        erreur("STM32L476RGTX_FLASH.ld introuvable ou ligne FLASH non reconnue")
    elif zone[0] == 0x08080000 and zone[1] <= 512 * 1024:
        ok("STM32L476RGTX_FLASH.ld : programme placé en banque 2 (0x08080000, %d Ko)" % (zone[1] // 1024))
    elif zone[0] == 0x08000000:
        erreur("STM32L476RGTX_FLASH.ld : le programme est placé à 0x08000000. Remplacez la ligne FLASH par\n"
               "            FLASH (rx) : ORIGIN = 0x8080000, LENGTH = 512K")
    else:
        erreur("STM32L476RGTX_FLASH.ld : FLASH doit commencer à 0x8080000 avec 512K au plus")

    # 2) Table des vecteurs
    if "obcp_ota_init_vectors();" in sans_commentaires(section(main_c, "1")):
        ok("main.c : obcp_ota_init_vectors() dans USER CODE 1")
    else:
        attention("main.c : ajoutez obcp_ota_init_vectors(); dans USER CODE BEGIN 1 (recommandé, "
                  "indispensable pour déboguer avec une sonde)")
    if os.path.exists(os.path.join(projet, "Core", "Inc", "obcp_ota_request.h")):
        ok("Core/Inc/obcp_ota_request.h présent")
    elif "obcp_ota" in (main_c or ""):
        erreur("main.c utilise obcp_ota_request.h, mais le fichier n'est pas dans Core/Inc")
    system_c = sans_commentaires(lire(os.path.join(projet, "Core", "Src", "system_stm32l4xx.c")))
    if re.search(r"#define\s+USER_VECT_TAB_ADDRESS", system_c):
        attention("system_stm32l4xx.c : USER_VECT_TAB_ADDRESS est défini ; VECT_TAB_OFFSET doit alors "
                  "valoir 0x00080000, sinon les interruptions iraient dans le chargeur")


def main(argv=None):
    """Programme principal ; renvoie 0 sans erreur, 1 sinon, 2 si mal appelé."""
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 1 or not os.path.isdir(argv[0]):
        print(__doc__)
        return 2
    projet = os.path.abspath(argv[0])
    main_c = lire(os.path.join(projet, "Core", "Src", "main.c"))
    if main_c is None:
        print("ERREUR : %s n'est pas un projet STM32CubeIDE (Core/Src/main.c introuvable)" % projet)
        return 1
    chargeur = os.path.isdir(os.path.join(projet, "Loader"))
    print("Projet %s (%s)" % (os.path.basename(projet),
                              "chargeur BLE" if chargeur else "application chargée par Bluetooth"))
    if chargeur:
        verifier_chargeur(projet, main_c)
    else:
        verifier_application(projet, main_c)
    for niveau, message in resultats:
        print("  %-9s %s" % (niveau, message))
    nb_erreurs = sum(1 for niveau, _ in resultats if niveau == "ERREUR")
    nb_attention = sum(1 for niveau, _ in resultats if niveau == "ATTENTION")
    print("Bilan : %d erreur(s), %d recommandation(s)." % (nb_erreurs, nb_attention))
    return 1 if nb_erreurs else 0


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    sys.exit(main())
