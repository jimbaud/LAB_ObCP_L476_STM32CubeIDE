# -*- coding: utf-8 -*-
"""
generer_images.py - Fabrique les illustrations du README (thèmes clair et
sombre) à partir des pages HTML de ce dossier.

    python generer_images.py                 # toutes les figures
    python generer_images.py codes-led       # une seule

Chaque page (banniere.html, interfaces.html...) est affichée par Chromium,
deux fois (thème clair, thème sombre), puis capturée en PNG à deux fois la
résolution de l'écran. Résultat : Doc/images/<nom>-clair.png et
<nom>-sombre.png. Pour modifier un texte, il suffit de modifier la page HTML
(on peut l'ouvrir directement dans un navigateur : ajouter ?theme=sombre à
l'adresse pour voir le thème sombre) puis de relancer ce script.

Dépendances : Playwright pour Python (pip install playwright, puis
python -m playwright install chromium), Pillow ; polices Inter et
DejaVu Sans Mono installées sur le PC. Facultatif, pour des fichiers
5 fois plus légers : pip install imagequant pyoxipng.
"""
import os
import sys

from PIL import Image
from playwright.sync_api import sync_playwright

ICI = os.path.dirname(os.path.abspath(__file__))
IMAGES = os.path.normpath(os.path.join(ICI, "..", "..", "images"))
FIGURES = ["banniere", "interfaces", "carte-reperes", "chargement-dfu", "architecture", "boucle",
           "usb-reception", "ble-reception", "regeneration", "codes-led"]
THEMES = ["clair", "sombre"]
# Figures sans photo : sans imagequant, Pillow suffit pour les réduire à
# 256 couleurs (aplats nets). Les autres restent alors en couleurs vraies.
SANS_PHOTO = set(FIGURES) - {"banniere", "carte-reperes"}


def reduire(chemin, nom):
    """PNG en 256 couleurs avec transparence : invisible à l'œil, et 5 fois
    plus léger à télécharger quand on ouvre la page du dépôt."""
    image = Image.open(chemin).convert("RGBA")
    try:
        import imagequant                     # libimagequant (celle de pngquant)
        image = imagequant.quantize_pil_image(image, dithering_level=1.0, max_colors=256,
                                              min_quality=0, max_quality=95)
    except ImportError:
        if nom not in SANS_PHOTO:
            return
        image = image.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    image.save(chemin, optimize=True)
    try:
        import oxipng                         # compression sans perte, plus poussée
        oxipng.optimize(chemin, level=4)
    except ImportError:
        pass


def main(noms):
    os.makedirs(IMAGES, exist_ok=True)
    with sync_playwright() as p:
        navigateur = p.chromium.launch()
        page = navigateur.new_page(device_scale_factor=2, viewport={"width": 1400, "height": 1000})
        for nom in noms:
            for theme in THEMES:
                url = "file://" + os.path.join(ICI, nom + ".html").replace("\\", "/") + "?theme=" + theme
                if not url.startswith("file:///"):
                    url = url.replace("file://", "file:///")          # chemins Windows
                page.goto(url)
                page.wait_for_function("window.figure_prete === true", timeout=15000)
                sortie = os.path.join(IMAGES, "%s-%s.png" % (nom, theme))
                page.locator("#figure").screenshot(path=sortie, omit_background=True)
                reduire(sortie, nom)
                largeur, hauteur = Image.open(sortie).size
                print("%-28s %4d x %4d  %4d Ko" % (os.path.basename(sortie), largeur, hauteur,
                                                    os.path.getsize(sortie) // 1024))
        navigateur.close()


if __name__ == "__main__":
    demandees = sys.argv[1:] or FIGURES
    inconnues = [n for n in demandees if n not in FIGURES]
    if inconnues:
        sys.exit("figure inconnue : %s (choix : %s)" % (", ".join(inconnues), ", ".join(FIGURES)))
    main(demandees)
