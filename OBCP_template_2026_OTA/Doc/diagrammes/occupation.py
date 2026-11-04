"""Occupation de la mémoire par partie du programme, d'après le fichier .map
de l'éditeur de liens (configuration Release) :

    python occupation.py ../../build/Release/OBCP_template_2026.map ../images/occupation.png

Le fichier .map liste chaque morceau (« section d'entrée ») placé en mémoire,
avec son adresse, sa taille et le fichier objet ou la bibliothèque d'où il
vient. On additionne ces tailles par famille de fichiers : flash = code,
constantes et valeurs de départ des variables (.data) ; RAM = .data et .bss
(sans le tas ni la pile, réservés à part).
"""
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams["font.family"] = "Carlito"

APPLICATION = {"app", "accelerometer", "ble_uart", "button", "commands", "fault", "fifo",
               "hal_callbacks", "line_builder", "ota_switch", "outputs", "timer_1s", "usb_console"}

FAMILLES = [  # (nom affiché, couleur)
    ("Application (Core/Src : app.c, commands.c...)", "#2E8B57"),
    ("Code généré par CubeMX (main.c, USB_DEVICE...)", "#7D8896"),
    ("HAL STM32L4 (Drivers)", "#3EB1E3"),
    ("Bibliothèque USB de ST", "#7A4FA8"),
    ("Bibliothèque BlueNRG-MS", "#2F6FB8"),
    ("Bibliothèque C (printf, %f, malloc...)", "#E8743B"),
    ("Démarrage du C, calculs (libgcc)", "#B9C0CA"),
]
FLASH_SECTIONS = (".isr_vector", ".text", ".rodata", ".ARM.extab", ".ARM", ".preinit_array",
                  ".init_array", ".fini_array")


def famille(objet):
    """Numéro de la famille d'un fichier objet (chemin lu dans le .map)."""
    chemin = objet.replace("\\", "/")
    nom = re.sub(r"\.o$", "", chemin.rsplit("/", 1)[-1])
    if "/Core/Src/" in chemin or chemin.startswith("Core/Src/"):
        return 0 if nom in APPLICATION else 1
    if "Core/Startup" in chemin or "USB_DEVICE/" in chemin or "BlueNRG-MS/Target" in chemin:
        return 1
    if "STM32L4xx_HAL_Driver" in chemin:
        return 2
    if "STM32_USB_Device_Library" in chemin:
        return 3
    if "Middlewares/ST/BlueNRG-MS" in chemin:
        return 4
    if re.search(r"lib(c|c_nano|m|nosys|g|g_nano)\.a", chemin):
        return 5
    return 6                                       # libgcc.a, crt*.o


def lire_map(chemin):
    """Rend (flash, ram) : octets par famille, d'après le fichier .map."""
    texte = open(chemin, encoding="utf-8", errors="replace").read()
    texte = texte[texte.index("Linker script and memory map"):]
    entrees = []               # (section de sortie, nom, adresse, taille, objet)
    sortie = None              # section de sortie en cours (.text, .data...)
    en_attente = None          # nom d'une section d'entrée écrit seul sur sa ligne
    for ligne in texte.splitlines():
        m = re.match(r"^(\.\S+)\s+0x[0-9a-f]+\s+0x[0-9a-f]+", ligne)
        if m or re.match(r"^\.\S+\s*$", ligne):
            sortie = (m.group(1) if m else ligne.strip())
            en_attente = None
            continue
        m = re.match(r"^ (\S+)\s*$", ligne)        # «  .text.nom_long » puis la suite en dessous
        if m:
            en_attente = m.group(1)
            continue
        m = re.match(r"^ (\S+)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)(\s+\S.*)?$", ligne)
        if not m or sortie is None or (m.group(1) or en_attente) is None:
            en_attente = None
            continue
        entrees.append((sortie, m.group(1) or en_attente, int(m.group(2), 16), int(m.group(3), 16),
                        (m.group(4) or "").strip()))
        en_attente = None

    # Taille réelle de chaque morceau : écart avec l'adresse du suivant. Les
    # chaînes de caractères identiques sont fusionnées par l'éditeur de liens
    # (sections « .str1.1 »...) : le .map donne leur taille avant fusion, et
    # le bloc fusionné à la place du premier. Ce bloc est partagé entre les
    # fichiers au prorata de leur taille avant fusion.
    reelle = []
    for i, (sortie, nom, adr, taille, objet) in enumerate(entrees):
        suivante = entrees[i + 1] if i + 1 < len(entrees) else None
        if suivante is not None and suivante[0] == sortie and suivante[2] >= adr:
            reelle.append(min(taille, suivante[2] - adr) if not re.search(r"\.str\d+\.\d+$", nom)
                          else suivante[2] - adr)
        else:
            reelle.append(taille)
    flash = [0.0] * len(FAMILLES)
    ram = [0.0] * len(FAMILLES)

    def ajouter(sortie, objet, octets):
        f = famille(objet)
        if sortie in FLASH_SECTIONS:
            flash[f] += octets
        elif sortie == ".data":
            flash[f] += octets                     # valeurs de départ, rangées en flash
            ram[f] += octets
        elif sortie == ".bss":
            ram[f] += octets

    groupes = {}               # (section de sortie, « str1.1 »...) -> [bloc, [(objet, taille)]]
    for (sortie, nom, adr, taille, objet), vraie in zip(entrees, reelle):
        if nom == "*fill*" or not objet.endswith((".o", ")")):
            continue                               # bourrage, ou ligne sans fichier
        fusion = re.search(r"\.(str\d+\.\d+)$", nom)
        if fusion:
            g = groupes.setdefault((sortie, fusion.group(1)), [0, []])
            g[0] += vraie
            g[1].append((objet, taille))
        else:
            ajouter(sortie, objet, vraie)
    for (sortie, _), (bloc, membres) in groupes.items():
        avant = sum(t for _, t in membres)
        for objet, t in membres:
            ajouter(sortie, objet, bloc * t / avant)
    return flash, ram


def tracer(flash, ram, destination):
    noms = [n for n, _ in FAMILLES][::-1]
    couleurs = [c for _, c in FAMILLES][::-1]
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(10.9, 4.9), dpi=150, sharey=True,
                                 gridspec_kw={"width_ratios": [1.0, 0.8], "wspace": 0.06})
    for ax, valeurs, titre, limite in ((a1, flash, "Mémoire flash (code et constantes)", 25),
                                       (a2, ram, "RAM (variables, hors tas et pile)", 10)):
        ko = [v / 1024 for v in valeurs][::-1]
        ax.barh(noms, ko, color=couleurs, height=0.62)
        for y, v in enumerate(ko):
            ax.text(v + limite * 0.015, y, ("%.1f ko" % v).replace(".", ","), va="center", fontsize=11)
        ax.set_xlim(0, limite)
        ax.set_title("%s\ntotal %s" % (titre, ("%.1f ko" % (sum(valeurs) / 1024)).replace(".", ",")),
                     fontsize=13, color="#1F3A5F")
        ax.set_xlabel("ko", fontsize=11)
        ax.spines["top"].set_visible(False)
        ax.spines["right"].set_visible(False)
        ax.tick_params(labelsize=11)
    fig.savefig(destination, bbox_inches="tight", facecolor="white")


if __name__ == "__main__":
    f, r = lire_map(sys.argv[1])
    for (nom, _), a, b in zip(FAMILLES, f, r):
        print("%-48s flash %8.1f   RAM %8.1f" % (nom, a, b))
    print("%-48s flash %8.1f   RAM %8.1f" % ("total", sum(f), sum(r)))
    if len(sys.argv) > 2:
        tracer(f, r, sys.argv[2])
