#!/usr/bin/env python3
"""Illustrations du README GitHub d'OBCP_template_2026.
    python3 figs_github.py <dossier_images_depot> <sortie>
"""
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle
from PIL import Image

REPO_IMG = Path(sys.argv[1])
OUT = Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)
plt.rcParams.update({"font.family": "Carlito", "font.size": 10})
NAVY, BLUE, INK, GRAY, PALE = "#03234b", "#3cb4e6", "#1d2733", "#5f6b7a", "#e8f4fb"
GREEN, ORANGE, RED, PURPLE = "#2e8b57", "#e8743b", "#c0392b", "#7b4fa8"


def save(fig, name):
    fig.savefig(OUT / f"{name}.png", dpi=200, facecolor=fig.get_facecolor(), bbox_inches="tight", pad_inches=0.05)
    plt.close(fig)
    print("écrit :", name)


def rbox(ax, x, y, w, h, fc, ec, lw=1.2, r=0.6, z=1):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle=f"round,pad=0,rounding_size={r}", fc=fc, ec=ec, lw=lw,
                                zorder=z))


# ---------------------------------------------------------------------------
def banniere():
    fig = plt.figure(figsize=(12, 3.6), facecolor=NAVY)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 120)
    ax.set_ylim(0, 36)
    ax.axis("off")
    for i in range(14):   # trame discrète
        ax.plot([0, 120], [i * 2.8, i * 2.8 - 8], color="#0b3463", lw=0.6, zorder=0)
    ax.text(4, 25.5, "ObCP", fontsize=46, fontweight="bold", color="white", va="center")
    ax.text(4.3, 16.5, "Programme modèle OBCP_template_2026", fontsize=21, color="white", va="center")
    ax.text(4.3, 11.2, "STM32L476RG  ·  Bluetooth Low Energy  ·  port COM USB  ·  STM32CubeIDE / CubeMX",
            fontsize=12.5, color="#b9d9ef", va="center")
    chips = [("v2.0.0", BLUE), ("commandes 2024", GREEN), ("34 défauts corrigés", ORANGE),
             ("régénérable CubeMX", "#8e6bbf")]
    x = 4.3
    for t, c in chips:
        w = len(t) * 0.78 + 2.4
        rbox(ax, x, 3.3, w, 4.2, c, "none", r=2.0)
        ax.text(x + w / 2, 5.4, t, ha="center", va="center", fontsize=11.5, color="white", fontweight="bold")
        x += w + 1.4
    rbox(ax, 74, 2.5, 44.5, 31, "white", "none", r=2.5)
    im = Image.open(REPO_IMG / "ObCP_3Diso.png")
    ax_img = fig.add_axes([0.625, 0.10, 0.36, 0.80])
    ax_img.imshow(im)
    ax_img.axis("off")
    save(fig, "banniere")


# ---------------------------------------------------------------------------
def fonctions():
    fig, ax = plt.subplots(figsize=(11, 5.6))
    ax.set_xlim(0, 110)
    ax.set_ylim(0, 56)
    ax.axis("off")
    # carte
    rbox(ax, 30, 4, 50, 48, "#f3f6fa", "#9fb3c8", lw=1.2, r=2.0)
    ax.text(55, 49, "Carte ObCP V2.3", ha="center", fontsize=13, fontweight="bold", color=NAVY)
    rbox(ax, 44, 20, 22, 16, "#2b3440", "#11161c", r=1.0, z=2)
    ax.text(55, 30, "STM32L476RG", ha="center", va="center", color="white", fontsize=12, fontweight="bold", zorder=3)
    ax.text(55, 25.5, "Cortex-M4, 64 MHz\nsuper boucle", ha="center", va="center", color="#c9d1db", fontsize=9,
            zorder=3)
    blocks = [  # x, y, titre, sous-titre, couleur, ancre sur la puce
        (32.5, 40, "LED tricolore", "TIM3 : r, v, b", ORANGE, (49, 36)),
        (57.5, 40, "Sortie PWM J13", "TIM1 : m (moteur 5 V)", "#b3541e", (61, 36)),
        (32.5, 8, "Accéléromètre", "LIS3DH, SPI3 : a, t", GREEN, (49, 20)),
        (57.5, 8, "Bouton SW1", "PC13, anti-rebond", RED, (61, 20)),
    ]
    for x, y, t, s, c, anchor in blocks:
        rbox(ax, x, y, 20, 7.5, "white", c, lw=1.6, r=1.0, z=2)
        ax.text(x + 10, y + 5.0, t, ha="center", va="center", fontsize=10.5, fontweight="bold", color=INK, zorder=3)
        ax.text(x + 10, y + 2.2, s, ha="center", va="center", fontsize=8.8, color=GRAY, zorder=3)
        ax.plot([x + 10, anchor[0]], [y + (0 if y > 20 else 7.5), anchor[1]], color=c, lw=1.4, zorder=1)
    # USB / BLE sur les côtés
    rbox(ax, 31.5, 24, 9, 8, "white", PURPLE, lw=1.6, r=1.0, z=2)
    ax.text(36, 29.3, "USB", ha="center", fontsize=10.5, fontweight="bold", color=INK, zorder=3)
    ax.text(36, 26.3, "CDC", ha="center", fontsize=8.8, color=GRAY, zorder=3)
    ax.plot([40.5, 44], [28, 28], color=PURPLE, lw=1.4)
    rbox(ax, 69.5, 24, 9, 8, "white", "#2f6fb5", lw=1.6, r=1.0, z=2)
    ax.text(74, 29.3, "BLE", ha="center", fontsize=10.5, fontweight="bold", color=INK, zorder=3)
    ax.text(74, 26.3, "BlueNRG-M0", ha="center", fontsize=8.3, color=GRAY, zorder=3)
    ax.plot([66, 69.5], [28, 28], color="#2f6fb5", lw=1.4)
    # PC
    rbox(ax, 1, 18, 24, 20, "white", PURPLE, lw=1.6, r=1.5)
    ax.text(13, 34, "PC", ha="center", fontsize=13, fontweight="bold", color=PURPLE)
    ax.text(13, 26, "terminal (PuTTY…)\nscripts Python\nVI LabVIEW", ha="center", va="center", fontsize=9.5,
            color=INK, linespacing=1.4)
    ax.add_patch(FancyArrowPatch((25, 28), (31.5, 28), arrowstyle="<|-|>", mutation_scale=14, color=PURPLE, lw=1.6))
    ax.text(28.2, 23.0, "port COM\nvirtuel", ha="center", va="top", fontsize=8, color=PURPLE)
    # téléphone
    rbox(ax, 85, 18, 24, 20, "white", "#2f6fb5", lw=1.6, r=1.5)
    ax.text(97, 34, "Téléphone", ha="center", fontsize=13, fontweight="bold", color="#2f6fb5")
    ax.text(97, 26, "ObCP Control\nBLE Terminal JILAB\nnRF Connect", ha="center", va="center", fontsize=9.5,
            color=INK, linespacing=1.4)
    ax.add_patch(FancyArrowPatch((78.5, 28), (85, 28), arrowstyle="<|-|>", mutation_scale=14, color="#2f6fb5",
                                 lw=1.6))
    ax.text(81.8, 23.0, "OBCP-\nCROC", ha="center", va="top", fontsize=8, color="#2f6fb5")
    ax.text(55, 0.6, "Mêmes commandes par l'USB et par le Bluetooth ; chaque réponse est recopiée sur l'autre liaison.",
            ha="center", fontsize=9.5, color=GRAY, style="italic")
    save(fig, "fonctions")


# ---------------------------------------------------------------------------
def commandes():
    lines = [
        ("> ", "?", ""),
        ("", "Type command :", "out"),
        ("", "- R or r led and integer from 0 to 65535 like r50 for red led", "out"),
        ("", "  ...", "dim"),
        ("> ", "r20000", ""),
        ("", "Red = 20000", "out"),
        ("> ", "v 500", ""),
        ("", "Green = 500", "out"),
        ("> ", "m65535", ""),
        ("", "PWM = 65535", "out"),
        ("> ", "a", ""),
        ("", "Acceleration : X=0.01 Y=-0.02 Z=1.00", "out"),
        ("> ", "t", ""),
        ("", "T = 27", "out"),
        ("> ", "r70000", ""),
        ("", "False value (0-65535)", "err"),
        ("", "Push button pressed", "evt"),
        ("> ", "#rst", ""),
    ]
    fig = plt.figure(figsize=(7.6, 5.9), facecolor="white")
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 76)
    ax.set_ylim(0, 59)
    ax.axis("off")
    rbox(ax, 0.5, 0.5, 75, 58, "#1e1f24", "#3a3d46", lw=1, r=1.6)
    ax.add_patch(Rectangle((0.5, 53.5), 75, 4.2, fc="#2c2e35", ec="none"))
    for i, c in enumerate(["#ff5f57", "#febc2e", "#28c840"]):
        ax.add_patch(plt.Circle((4 + i * 2.6, 55.6), 0.8, color=c))
        _ = c
    ax.text(38, 55.6, "COM5 — ObCP (port COM virtuel USB)", ha="center", va="center", color="#c9ccd4", fontsize=9.5)
    y = 50.5
    for prompt, text, kind in lines:
        color = {"": "#ffffff", "out": "#9fe0a8", "err": "#ff8a7a", "evt": "#f6c97b", "dim": "#7d8290"}[kind]
        if prompt:
            ax.text(3, y, prompt, color="#3cb4e6", family="DejaVu Sans Mono", fontsize=9.6, va="center")
            ax.text(5.8, y, text, color=color, family="DejaVu Sans Mono", fontsize=9.6, va="center",
                    fontweight="bold")
        else:
            ax.text(5.8, y, text, color=color, family="DejaVu Sans Mono", fontsize=9.6, va="center")
        y -= 2.75
    ax.text(73, 4, "appui sur SW1", color="#f6c97b", fontsize=8.5, ha="right", style="italic")
    save(fig, "terminal")


# ---------------------------------------------------------------------------
def comparaison():
    fig, axs = plt.subplots(1, 2, figsize=(11, 3.9), gridspec_kw={"width_ratios": [1, 1.35]})
    ax = axs[0]
    ax.axis("off")
    ax.set_xlim(0, 50)
    ax.set_ylim(0, 40)
    ax.text(25, 37, "Même scénario joué par les deux versions", ha="center", fontsize=12, fontweight="bold",
            color=NAVY)
    rbox(ax, 2, 15, 21, 17, "#e9f5ec", GREEN, lw=1.6, r=1.5)
    ax.text(12.5, 26, "61 / 61", ha="center", fontsize=26, fontweight="bold", color=GREEN)
    ax.text(12.5, 19, "étapes d'usage normal\nidentiques", ha="center", fontsize=9.5, color=INK)
    rbox(ax, 27, 15, 21, 17, "#fff4e5", ORANGE, lw=1.6, r=1.5)
    ax.text(37.5, 26, "14", ha="center", fontsize=26, fontweight="bold", color=ORANGE)
    ax.text(37.5, 19, "cas d'erreur corrigés :\nréponses différentes", ha="center", fontsize=9.5, color=INK)
    ax.text(25, 8.5, "make compare  (Tests/compare)\n+ 305 vérifications sur PC, sans carte", ha="center",
            fontsize=9.5, color=GRAY, family="DejaVu Sans Mono", linespacing=1.5)
    ax = axs[1]
    dom = [("Bluetooth", 11), ("USB", 5), ("Commandes", 3), ("Accéléromètre", 3), ("Minuterie", 3),
           ("Système et code", 4), ("Broches, sorties", 4), ("Bouton", 1)]
    names = [d[0] for d in dom][::-1]
    vals = [d[1] for d in dom][::-1]
    bars = ax.barh(names, vals, color=[BLUE] * len(vals), height=0.62)
    for b, v in zip(bars, vals):
        ax.text(v + 0.2, b.get_y() + b.get_height() / 2, str(v), va="center", fontsize=10, color=INK)
    ax.set_title("Défauts de la version 2024 corrigés, par domaine (34)", fontsize=12, fontweight="bold",
                 color=NAVY, loc="left")
    for sp in ("top", "right"):
        ax.spines[sp].set_visible(False)
    ax.set_xlim(0, 13)
    ax.tick_params(labelsize=10)
    fig.tight_layout()
    save(fig, "comparaison_2024")


# ---------------------------------------------------------------------------
def chargement():
    fig, ax = plt.subplots(figsize=(11, 3.3))
    ax.set_xlim(0, 110)
    ax.set_ylim(0, 33)
    ax.axis("off")
    steps = [
        ("1", "Mode « Prog »", "SW4 sur « Prog »\nSW2 sur « On »\nappui sur SW3 (reset)", "Inter_Prog.png"),
        ("2", "Se connecter", "STM32CubeProgrammer :\n« USB » puis Connect\n(mode DFU, sans sonde)", None),
        ("3", "Charger", "Erasing & programming\nBIN/OBCP_template_2026.bin\nadresse 0x08000000", None),
        ("4", "Lancer", "SW4 sur « Run »\nappui sur SW3\n→ 1 éclat vert", "Inter_Run.png"),
    ]
    w = 25
    for i, (n, t, s, img) in enumerate(steps):
        x = 1 + i * 27.3
        rbox(ax, x, 2, w, 28, "white", "#9fb3c8", lw=1.2, r=1.5)
        ax.add_patch(plt.Circle((x + 3.6, 26.3), 2.2, color=NAVY))
        ax.text(x + 3.6, 26.3, n, ha="center", va="center", color="white", fontsize=12, fontweight="bold")
        ax.text(x + 7, 26.3, t, va="center", fontsize=11, fontweight="bold", color=NAVY)
        ax.text(x + w / 2, 19.5, s, ha="center", va="top", fontsize=9.6, color=INK, linespacing=1.4)
        if img:
            im = Image.open(REPO_IMG / img)
            ax.imshow(im, extent=(x + 6.5, x + 18.5, 2.6, 12.4), zorder=3)
        if i < 3:
            ax.add_patch(FancyArrowPatch((x + w + 0.2, 16), (x + 27.1, 16), arrowstyle="-|>", mutation_scale=14,
                                         color=BLUE, lw=1.6))
    ax.set_aspect("auto")
    save(fig, "chargement")


if __name__ == "__main__":
    for f in (banniere, fonctions, commandes, comparaison, chargement):
        f()
