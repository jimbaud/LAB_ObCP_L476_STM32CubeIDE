#!/usr/bin/env python3
"""Compile un projet STM32CubeIDE de l'ObCP en dehors de l'IDE, avec les mêmes
options que la configuration Debug (ou Release) de STM32CubeIDE.

Utilisé pour vérifier que les projets compilent sans avertissement ; les
étudiants, eux, compilent normalement dans STM32CubeIDE (marteau).

    python3 build_arm.py ../ObCP_BLE_Loader            (Debug, -O0)
    python3 build_arm.py ../ObCP_OTA_Demo --release    (Release, -Os)

Le script lit les chemins d'inclusion et les dossiers sources dans le fichier
.cproject du projet : ce sont donc bien les réglages de l'IDE qui sont testés.
"""
import argparse
import os
import subprocess
import sys
import xml.etree.ElementTree as ET

CC = "arm-none-eabi-gcc"
CPU = ["-mcpu=cortex-m4", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard", "-mthumb"]


def lire_cproject(projet, config):
    """Renvoie (includes, defines, dossiers_sources, script_ld) de la configuration."""
    arbre = ET.parse(os.path.join(projet, ".cproject"))
    for cfg in arbre.iter("cconfiguration"):
        module = cfg.find("storageModule")
        if module is None or module.get("name") != config:
            continue
        includes, defines, sources, script = [], [], [], None
        for opt in cfg.iter("option"):
            sup = opt.get("superClass", "")
            if sup.endswith("c.compiler.option.includepaths"):
                includes = [v.get("value") for v in opt.iter("listOptionValue")]
            elif sup.endswith("c.compiler.option.definedsymbols"):
                defines = [v.get("value") for v in opt.iter("listOptionValue")]
            elif sup.endswith("c.linker.option.script"):
                script = opt.get("value")
        for entry in cfg.iter("entry"):
            if entry.get("kind") == "sourcePath":
                sources.append(entry.get("name"))
        return includes, defines, sources, script
    sys.exit("configuration %s introuvable dans .cproject" % config)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("projet")
    ap.add_argument("--release", action="store_true")
    ap.add_argument("--extra", default="", help="options supplémentaires pour nos fichiers")
    ap.add_argument("--nos-dossiers", default="Loader,App",
                    help="dossiers compilés avec -Wextra (notre code)")
    args = ap.parse_args()

    projet = os.path.abspath(args.projet)
    nom = os.path.basename(projet)
    config = "Release" if args.release else "Debug"
    includes, defines, sources, script = lire_cproject(projet, config)
    script = script.replace("${workspace_loc:/${ProjName}/", projet + "/").rstrip("}")

    sortie = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", nom, config)
    os.makedirs(sortie, exist_ok=True)

    fichiers = []
    for dossier in sources:
        for racine, _, noms in os.walk(os.path.join(projet, dossier)):
            for n in sorted(noms):
                if n.endswith((".c", ".s")):
                    fichiers.append(os.path.join(racine, n))

    opt = ["-Os"] if args.release else ["-O0", "-g3"]
    communs = CPU + ["-std=gnu11"] + opt + ["-D" + d for d in defines] + \
        ["-I" + os.path.normpath(os.path.join(projet, i.replace("../", ""))) for i in includes] + \
        ["-ffunction-sections", "-fdata-sections", "-Wall", "-fstack-usage", "--specs=nano.specs"]
    # dossiers (ou fichiers) qui contiennent NOTRE code : avertissements renforcés
    nos_chemins = [os.path.join(projet, d) for d in args.nos_dossiers.split(",")]

    def est_a_nous(fichier):
        return any(fichier == c or fichier.startswith(c + os.sep) for c in nos_chemins)

    objets, avertissements = [], 0
    for f in fichiers:
        rel = os.path.relpath(f, projet)
        obj = os.path.join(sortie, rel.replace(os.sep, "_") + ".o")
        cmd = [CC] + communs + ["-c", f, "-o", obj]
        if f.endswith(".s"):
            cmd = [CC] + CPU + ["-g3", "-c", "-x", "assembler-with-cpp", f, "-o", obj]
        elif est_a_nous(f) or f.endswith(os.path.join("Core", "Src", "main.c")):
            cmd += ["-Wextra", "-Wshadow", "-Wstrict-prototypes", "-Wmissing-prototypes"] + args.extra.split()
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0 or r.stderr.strip():
            print("---", rel)
            print(r.stderr)
            avertissements += r.stderr.count("warning:")
            if r.returncode != 0:
                sys.exit("ECHEC de compilation")
        objets.append(obj)

    elf = os.path.join(sortie, nom + ".elf")
    lien = [CC] + CPU + objets + ["-T" + script, "--specs=nosys.specs", "-Wl,-Map=" + elf[:-4] + ".map",
                                  "-Wl,--gc-sections", "-static", "--specs=nano.specs",
                                  "-Wl,--start-group", "-lc", "-lm", "-Wl,--end-group", "-o", elf]
    r = subprocess.run(lien, capture_output=True, text=True)
    sortie_lien = "\n".join(l for l in r.stderr.splitlines()
                            if "is not implemented and will always fail" not in l
                            and "has a LOAD segment with RWX permissions" not in l)
    if sortie_lien.strip():
        print(sortie_lien)
    if r.returncode != 0:
        sys.exit("ECHEC de l'édition de liens")
    subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", elf, elf[:-4] + ".bin"], check=True)
    subprocess.run(["arm-none-eabi-objcopy", "-O", "ihex", elf, elf[:-4] + ".hex"], check=True)
    taille = subprocess.run(["arm-none-eabi-size", elf], capture_output=True, text=True).stdout
    print(taille.strip())
    print("avertissements de compilation :", avertissements)
    print("ELF :", elf)


if __name__ == "__main__":
    main()
