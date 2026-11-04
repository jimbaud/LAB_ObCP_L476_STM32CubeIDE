"""Qui appelle qui : nombre d'appels des fonctions de chaque module (colonnes),
dans chaque fichier du programme (lignes). Lu dans le code source :

    python matrice.py ../.. ../images/matrice.png      (dossier du projet, image)

Une fonction d'un module est une fonction déclarée dans son .h ; un appel est
son nom suivi de « ( », hors commentaires, textes entre guillemets, et hors
de sa propre définition.
"""
import os
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

plt.rcParams["font.family"] = "Carlito"

COLONNES = ["app", "commands", "outputs", "accelerometer", "button", "timer_1s", "usb_console",
            "ble_uart", "line_builder", "fifo", "fault", "ota_switch", "obcp_ota_request"]
GENERES = [("main.c", "Core/Src/main.c"), ("stm32l4xx_it.c", "Core/Src/stm32l4xx_it.c"),
           ("usbd_cdc_if.c", "USB_DEVICE/App/usbd_cdc_if.c"), ("usbd_conf.c", "USB_DEVICE/Target/usbd_conf.c")]
APPLICATION = ["app", "commands", "outputs", "accelerometer", "button", "timer_1s", "usb_console",
               "ble_uart", "line_builder", "fifo", "fault", "hal_callbacks", "ota_switch"]


def sans_commentaires(code):
    code = re.sub(r"/\*.*?\*/", " ", code, flags=re.S)
    code = re.sub(r"//[^\n]*", " ", code)
    return re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', code)


def lire(racine, chemin):
    with open(os.path.join(racine, chemin), encoding="utf-8", errors="replace") as f:
        return sans_commentaires(f.read())


def fonctions(racine, module):
    code = lire(racine, "Core/Inc/%s.h" % module)
    noms = set(re.findall(r"^[A-Za-z_][\w \*]*?\b([a-z_][a-z0-9_]*)\s*\([^;{]*\)\s*;", code, re.M))
    noms |= set(re.findall(r"static\s+inline\s+[\w \*]+?\b([a-z_][a-z0-9_]*)\s*\(", code))
    return noms


def compter(code, noms):
    total = 0
    for nom in noms:
        for m in re.finditer(r"\b%s\s*\(" % re.escape(nom), code):
            debut_ligne = code.rfind("\n", 0, m.start()) + 1
            avant = code[debut_ligne:m.start()]
            definition = re.match(r"^\s*(static\s+)?(inline\s+)?[A-Za-z_]\w*[\s\*]+$", avant)
            if definition and not re.search(r"\b(return|else|case)\b", avant):
                continue                        # définition ou déclaration, pas un appel
            total += 1
    return total


def calculer(racine):
    noms = {m: fonctions(racine, m) for m in COLONNES}
    lignes = [(n, c, True) for n, c in GENERES] + [(m + ".c", "Core/Src/%s.c" % m, False) for m in APPLICATION]
    tableau = []
    for nom, chemin, genere in lignes:
        code = lire(racine, chemin)
        tableau.append((nom, genere, [0 if nom[:-2] == m else compter(code, noms[m]) for m in COLONNES]))
    return tableau


def couleur(v):
    if v >= 10:
        return "#3B8FD3", "white"
    if v >= 3:
        return "#92C3E9", "#1E2833"
    return "#D3E4F4", "#1E2833"


def tracer(tableau, destination):
    nl, nc = len(tableau), len(COLONNES)
    fig, ax = plt.subplots(figsize=(1.5 + 0.78 * nc, 0.42 * nl + 1.6), dpi=150)
    ax.set_xlim(-2.0, nc)
    ax.set_ylim(nl + 0.6, -1.9)
    ax.axis("off")
    for i, (nom, genere, valeurs) in enumerate(tableau):
        ax.text(-0.1, i + 0.5, nom, ha="right", va="center", fontsize=12,
                color="#5F6B7A" if genere else "#1E2833", style="italic" if genere else "normal")
        for j, v in enumerate(valeurs):
            fond, texte = couleur(v) if v else ("white", None)
            ax.add_patch(Rectangle((j, i), 1, 1, facecolor=fond, edgecolor="#C9D1DB", linewidth=0.8))
            if v:
                ax.text(j + 0.5, i + 0.5, str(v), ha="center", va="center", fontsize=12, color=texte)
    for j, m in enumerate(COLONNES):
        ax.text(j + 0.55, -0.12, m + ".h", ha="left", va="bottom", rotation=32, fontsize=12,
                color="#1E2833", rotation_mode="anchor")
    ax.text(nc / 2, nl + 0.45, "nombre d'appels des fonctions du module en colonne, par le fichier en ligne "
            "(en gris : fichiers générés par CubeMX)", ha="center", va="center", fontsize=11, color="#5F6B7A")
    fig.savefig(destination, bbox_inches="tight", facecolor="white")


if __name__ == "__main__":
    t = calculer(sys.argv[1])
    for nom, _, valeurs in t:
        print("%-16s %s" % (nom, " ".join("%2s" % (v or ".") for v in valeurs)))
    if len(sys.argv) > 2:
        tracer(t, sys.argv[2])
