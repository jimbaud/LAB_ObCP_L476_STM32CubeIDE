#!/usr/bin/env python3
"""Convertir les PPM du banc PC en PNG et produire trois planches pédagogiques.

Exécuter make -C Tests captures, puis python Tools/generer_captures.py. Pillow est
nécessaire sur le PC, jamais sur la carte. Chaque écran vient du véritable app_render.
Les numéros et légendes sont ajoutés autour du rendu pour relier l’interface au code.
"""
import os
import textwrap
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "docs/images"


def font(size):
    """Choisir une police lisible parmi les emplacements Linux et Windows usuels."""
    paths = [
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
        Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/arial.ttf",
    ]
    for path in paths:
        if path.exists():
            return ImageFont.truetype(str(path), size)
    return ImageFont.load_default(size=size)


def plate(name, title, subtitle, notes):
    """Composer une capture agrandie et des repères numérotés associés aux fonctions.

    notes contient les coordonnées en pixels du firmware, un intitulé et une
    explication. L’agrandissement au plus proche conserve la grille du TFT 320 × 240.
    """
    image = Image.new("RGB", (1240, 730), "#f2f5f8")
    draw = ImageDraw.Draw(image)
    draw.text((36, 26), title, font=font(30), fill="#102c43")
    draw.text((36, 78), subtitle, font=font(17), fill="#465b6a")
    capture = Image.open(DEST / (name + ".png")).convert("RGB")
    image.paste(capture.resize((640, 480), Image.Resampling.NEAREST), (36, 150))
    draw.rectangle((35, 149, 676, 630), outline="#c3ced6", width=1)
    for number, (x, y, heading, explanation) in enumerate(notes, 1):
        cx, cy = 36 + x * 2, 150 + y * 2
        draw.ellipse((cx - 15, cy - 15, cx + 15, cy + 15), fill="#ffd24b", outline="#102c43", width=2)
        draw.text((cx, cy), str(number), font=font(18), fill="#102c43", anchor="mm")
        top = 151 + (number - 1) * 123
        draw.ellipse((722, top, 752, top + 30), fill="#ffd24b", outline="#102c43", width=1)
        draw.text((737, top + 15), str(number), font=font(18), fill="#102c43", anchor="mm")
        draw.text((768, top + 1), heading, font=font(20), fill="#102c43")
        lines = textwrap.fill(explanation, width=43)
        draw.multiline_text((768, top + 34), lines, font=font(17), fill="#465b6a", spacing=5)
    draw.text((36, 666), "Capture logicielle issue du C du projet • agrandissement ×2 • repères ajoutés pour le guide",
              font=font(16), fill="#465b6a")
    image.save(DEST / ("guide-" + name + ".png"))


def main():
    """Convertir les scènes produites par Tests/test_obcp.c, puis les annoter."""
    sources = sorted((ROOT / "build/host/captures").glob("*.ppm"))
    if not sources:
        raise SystemExit("Aucune image PPM. Exécuter d’abord make -C Tests captures.")
    for path in sources:
        Image.open(path).save(DEST / (path.stem + ".png"))
    plate("menu", "Du toucher au changement d’application", "Le menu est une scène dessinée ; les boutons sont des rectangles testés dans app_touch.", [
        (18, 17, "1. Identifier le bouton", "card() dessine la carte Paint. Le tableau de rectangles fixe sa zone tactile."),
        (174, 17, "2. Distribuer l’événement", "touch_update() produit DOWN. app_touch() choisit la page selon les coordonnées."),
        (18, 156, "3. Préparer la page", "app_open() initialise le modèle, puis damage_full() demande sa recomposition."),
        (174, 156, "4. Afficher et régler", "app_render() compose chaque bande. Paramètres donne accès aux pilotes et aux axes."),
    ])
    plate("paint", "Un trait tactile devient des pixels indexés", "Le canevas garde des indices de palette ; le transfert écran utilise des couleurs RGB565.", [
        (302, 32, "1. Stocker le dessin", "paint_segment() relie deux contacts. Deux pixels partagent un octet dans PaintImage."),
        (110, 213, "2. Régler le diamètre", "Taille ouvre les six diamètres. dab() dépose des disques le long du segment."),
        (62, 213, "3. Choisir une couleur", "Coul. puis RVB modifient une case. paint_draw() traduit l’indice par la palette."),
        (197, 213, "4. Demander la sauvegarde", "Sauve prépare un travail différé. storage_save() écrit et valide la copie A/B."),
    ])
    plate("diagnostic", "Observer les pilotes et la boucle principale", "Les valeurs affichées proviennent de Diagnostics ; obcp_live les expose au débogueur.", [
        (17, 78, "1. Reconnaître les composants", "lcd_init() lit 0x9341. touch_probe() choisit FT62xx, STMPE610 ou TSC2007."),
        (17, 133, "2. Transformer le contact", "BRUT indique les données du contrôleur. LCD montre les pixels après conversion."),
        (17, 167, "3. Mesurer le service", "update_rates() calcule lots/s et pixels/s. Les compteurs DMA signalent les incidents."),
        (140, 210, "4. Vérifier l’orientation", "Axes jeux ouvre l’assistant. tilt_project() applique permutation, signes et gain."),
    ])
    print(f"{len(sources)} captures PNG et 3 planches pédagogiques générées.")


if __name__ == "__main__":
    main()
