#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
captures_doc.py - Recrée les captures d'écran de la documentation
=================================================================

    python Tools/captures_doc.py            (depuis le dossier du projet)

Toutes les images sont produites par le VRAI rendu C du jeu (oa_render.c),
compilé sur PC : aucune image n'est dessinée par Python et aucune n'est une
photographie de l'écran de la carte.

    - scènes de jeu : simulateur sans fenêtre (Simulator/build/obcp_adventure_sim
      --headless --screenshot) ;
    - menus, réglages, pause, étalonnage : scènes préparées par
      Tests/test_ui.c et Tests/test_app.c (make -C Tests captures) ;
    - trafic SPI estimé par niveau : Tests/build/benchmark_render (make -C Tests
      bench), tracé avec matplotlib s'il est installé.

Les images 320 x 240 sont agrandies trois fois sans interpolation et rangées
dans Doc/images/. Il faut gcc, make, SDL2 (simulateur) et Pillow
(python -m pip install pillow ; matplotlib en plus pour le graphique).
SPDX-License-Identifier: MIT
"""
import csv
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
IMAGES = ROOT / "Doc" / "images"
SIM = ROOT / "Simulator" / "build" / "obcp_adventure_sim"
SCENES = [  # (image, options du simulateur)
    ("jeu_menu", ["--menu", "--frames", "2"]),
    ("jeu_niveau01", ["--autoplay", "--frames", "240", "--level", "1"]),
    ("jeu_niveau04", ["--autoplay", "--frames", "300", "--level", "4"]),
    ("jeu_niveau07", ["--autoplay", "--frames", "420", "--level", "7"]),
    ("jeu_boss10", ["--spawn-boss", "--frames", "45", "--level", "10"]),
]


def agrandir(ppm, nom):
    with Image.open(ppm) as image:
        assert image.size == (320, 240), ppm
        image.resize((960, 720), Image.Resampling.NEAREST).save(IMAGES / (nom + ".png"), optimize=True)
    print("  Doc/images/%s.png" % nom)


def main():
    IMAGES.mkdir(parents=True, exist_ok=True)
    subprocess.run(["make", "-C", str(ROOT / "Simulator")], check=True)
    subprocess.run(["make", "-C", str(ROOT / "Tests"), "captures", "bench"], check=True)
    print("Scènes de jeu (simulateur, sans fenêtre) :")
    with tempfile.TemporaryDirectory() as temp:
        for nom, options in SCENES:
            ppm = Path(temp) / (nom + ".ppm")
            sauvegarde = Path(temp) / (nom + ".sav")
            subprocess.run([str(SIM), "--headless", *options, "--save", str(sauvegarde),
                            "--screenshot", str(ppm)], check=True, stdout=subprocess.DEVNULL)
            agrandir(ppm, nom)
    print("Interface (scènes de Tests/test_ui.c et Tests/test_app.c) :")
    for ppm in sorted((ROOT / "Tests" / "build" / "captures").glob("*.ppm")):
        agrandir(ppm, ppm.stem)
    tracer_trafic(ROOT / "Tests" / "build" / "render_pc.csv")


def tracer_trafic(chemin_csv):
    """Temps minimal sur le fil SPI à 10 MHz pour les pixels modifiés de chaque image."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib absent : graphique du trafic SPI non recréé")
        return
    lignes = list(csv.DictReader(chemin_csv.open(encoding="utf-8")))
    niveaux = list(range(1, 11))
    mediane, p95 = [], []
    for n in niveaux:
        v = sorted(float(r["spi10_min_us"]) / 1000 for r in lignes
                   if int(r["level"]) == n and int(r["frame"]) > 0)
        mediane.append(statistics.median(v))
        p95.append(v[int((len(v) - 1) * 0.95)])
    plt.rcParams.update({"font.size": 10, "font.family": "DejaVu Sans"})
    fig, ax = plt.subplots(figsize=(9, 4.2), layout="constrained")
    ax.bar(niveaux, mediane, color="#0969da", label="médiane")
    ax.plot(niveaux, p95, "o-", color="#bc4c00", label="95e centile")
    ax.axhline(33.333, color="#cf222e", linestyle="--", linewidth=1, label="une image tous les 1/30 s")
    ax.set(xlabel="niveau", ylabel="temps sur le fil à 10 MHz (ms)", xticks=niveaux,
           title="Trafic SPI estimé par image (rendu C sur PC, défilement imposé)")
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    ax.grid(axis="y", alpha=0.25)
    ax.legend(fontsize=8, frameon=False)
    fig.savefig(IMAGES / "trafic_spi.png", dpi=150)
    print("  Doc/images/trafic_spi.png")


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    main()
