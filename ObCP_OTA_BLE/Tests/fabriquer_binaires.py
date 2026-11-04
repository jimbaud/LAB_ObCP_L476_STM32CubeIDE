#!/usr/bin/env python3
"""Compile les projets (configuration Release) et remplit le dossier Binaires/ :

  ObCP_BLE_Loader.hex / .elf        le chargeur BLE (à programmer une fois, en DFU)
  ObCP_OTA_Demo_v1.elf / .hex       démonstration, LED verte
  ObCP_OTA_Demo_v2.elf / .hex       démonstration, LED bleue (pour tester un 2e transfert)
  ObCP_Chargeur_et_Demo.hex         chargeur + démo v1 en un seul fichier (DFU)
"""
import os
import shutil
import subprocess
import sys

ICI = os.path.dirname(os.path.abspath(__file__))
RACINE = os.path.dirname(ICI)
SORTIE = os.path.join(RACINE, "Binaires")
BUILD = os.path.join(ICI, "build")


def compiler(projet, *options):
    cmd = [sys.executable, os.path.join(ICI, "build_arm.py"), os.path.join(RACINE, projet), "--release"]
    r = subprocess.run(cmd + list(options), capture_output=True, text=True)
    if r.returncode != 0 or "avertissements de compilation : 0" not in r.stdout:
        sys.exit("échec de compilation de %s\n%s%s" % (projet, r.stdout, r.stderr))
    print(r.stdout.strip().splitlines()[-3])
    return os.path.join(BUILD, projet, "Release", projet)


def fusionner_hex(premier, second, destination):
    """Met deux fichiers Intel HEX bout à bout (un seul enregistrement de fin)."""
    lignes = []
    for i, chemin in enumerate((premier, second)):
        with open(chemin) as f:
            for ligne in f.read().split():
                type_enr = ligne[7:9]
                if type_enr == "01":                   # fin de fichier : une seule, à la fin
                    continue
                if type_enr == "05" and i == 1:        # adresse de démarrage : celle du chargeur
                    continue
                lignes.append(ligne)
    lignes.append(":00000001FF")
    with open(destination, "w", newline="\r\n") as f:
        f.write("\n".join(lignes) + "\n")


def main():
    os.makedirs(SORTIE, exist_ok=True)
    chargeur = compiler("ObCP_BLE_Loader")
    for ext in (".hex", ".elf"):
        shutil.copy(chargeur + ext, os.path.join(SORTIE, "ObCP_BLE_Loader" + ext))

    for version in (1, 2):
        demo = compiler("ObCP_OTA_Demo", "--nos-dossiers", "Core/Src/demo.c",
                        "--extra=-DDEMO_VERSION=%d" % version)
        for ext in (".hex", ".elf"):
            shutil.copy(demo + ext, os.path.join(SORTIE, "ObCP_OTA_Demo_v%d%s" % (version, ext)))

    fusionner_hex(os.path.join(SORTIE, "ObCP_BLE_Loader.hex"), os.path.join(SORTIE, "ObCP_OTA_Demo_v1.hex"),
                  os.path.join(SORTIE, "ObCP_Chargeur_et_Demo.hex"))
    print("Binaires :", ", ".join(sorted(os.listdir(SORTIE))))


if __name__ == "__main__":
    main()
