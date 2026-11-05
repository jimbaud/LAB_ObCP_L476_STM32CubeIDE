# -*- coding: utf-8 -*-
"""
detourer.py - Détoure les rendus 3D de l'ObCP (fond blanc -> transparent)
pour les illustrations du README (thèmes clair et sombre).

    python detourer.py

Entrées : Images/ObCP_3Diso.png et Images/ObCP_3DTOP.png, à la racine du
dépôt LAB_ObCP_L476_STM32CubeIDE. Sorties : ressources/carte_iso.png et
ressources/carte_dessus.png (PNG avec canal alpha).

Principe : on « remplit » le fond depuis les bords de l'image, de proche en
proche, tant que les pixels sont gris (peu saturés) et varient doucement
(le fond blanc, puis l'ombre portée, en dégradé). Le bord d'un composant
(changement brusque de couleur) arrête le remplissage, même pour un
composant blanc posé sur le fond blanc (connecteur de batterie).
Le fond devient transparent ; l'ombre devient un noir semi-transparent,
invisible sur fond sombre et identique à l'original sur fond blanc.

Dépendances : Pillow, NumPy, SciPy.
"""
import os
from collections import deque

import numpy as np
from PIL import Image
from scipy import ndimage

ICI = os.path.dirname(os.path.abspath(__file__))
DEPOT = os.path.normpath(os.path.join(ICI, "..", "..", "..", ".."))   # racine du dépôt
SORTIE = os.path.join(ICI, "ressources")


def detourer(entree, sortie, largeur, recadrer=True, pas_max=3.0, saturation_max=12):
    image = Image.open(entree).convert("RGB")
    if image.width > largeur:
        hauteur = round(image.height * largeur / image.width)
        image = image.resize((largeur, hauteur), Image.LANCZOS)
    rvb = np.asarray(image).astype(np.int16)
    lum = rvb.mean(axis=2)
    gris = (rvb.max(axis=2) - rvb.min(axis=2)) <= saturation_max
    h, w = lum.shape

    # Remplissage du fond depuis les bords (parcours en largeur)
    fond = np.zeros((h, w), dtype=bool)
    file = deque()
    for x in range(w):
        for y in (0, h - 1):
            if gris[y, x] and lum[y, x] > 200 and not fond[y, x]:
                fond[y, x] = True
                file.append((y, x))
    for y in range(h):
        for x in (0, w - 1):
            if gris[y, x] and lum[y, x] > 200 and not fond[y, x]:
                fond[y, x] = True
                file.append((y, x))
    while file:
        y, x = file.popleft()
        l0 = lum[y, x]
        for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
            if 0 <= ny < h and 0 <= nx < w and not fond[ny, nx] and gris[ny, nx] \
                    and abs(lum[ny, nx] - l0) <= pas_max:
                fond[ny, nx] = True
                file.append((ny, nx))

    # Petits trous isolés dans la carte (reflets) : ils restent opaques.
    # Seul le plus grand morceau est gardé (pas le repère d'axes du logiciel 3D).
    carte = ndimage.binary_fill_holes(~fond)
    etiquettes, nombre = ndimage.label(carte)
    if nombre > 1:
        tailles = ndimage.sum(carte, etiquettes, range(1, nombre + 1))
        carte = etiquettes == 1 + int(np.argmax(tailles))
    retire = ~carte & ~fond                     # morceaux écartés : transparents
    fond = ~carte

    # Alpha : carte opaque, ombre en noir translucide
    alpha = np.zeros((h, w), dtype=np.float32)
    ombre = np.clip((252.0 - lum) / 252.0, 0.0, 1.0) * 0.92
    # L'ombre du rendu d'origine est coupée net par le bord de l'image :
    # on l'estompe sur les 90 derniers pixels.
    yy, xx = np.mgrid[0:h, 0:w]
    marge = np.minimum(np.minimum(yy, h - 1 - yy), np.minimum(xx, w - 1 - xx)).astype(np.float32)
    ombre *= np.clip(marge / 90.0, 0.0, 1.0) ** 1.5
    alpha[fond] = ombre[fond]
    alpha[retire] = 0.0
    alpha[carte] = 1.0
    couleur = rvb.astype(np.float32)

    # Bord de la carte : un pixel du bord mélange la couleur du composant (F)
    # et celle du fond (B). On retrouve sa part de composant (alpha) et on lui
    # rend la couleur F : pas de liseré clair sur un fond sombre.
    profondeur = ndimage.distance_transform_edt(carte)        # distance au fond
    bord = carte & (profondeur <= 2.0)          # le lissage déborde sur 2 pixels
    interieur = carte & ~bord
    dist_i, (iy, ix) = ndimage.distance_transform_edt(~interieur, return_indices=True)
    _, (fy, fx) = ndimage.distance_transform_edt(~fond, return_indices=True)
    ys, xs = np.nonzero(bord)
    F = couleur[iy[ys, xs], ix[ys, xs]]
    B = couleur[fy[ys, xs], fx[ys, xs]]
    P = couleur[ys, xs]
    ecart = ((F - B) ** 2).sum(axis=1)
    fiable = (ecart > 900.0) & (dist_i[ys, xs] <= 3.0)       # pas sur les pièces fines
    part = np.where(fiable, ((P - B) * (F - B)).sum(axis=1) / np.maximum(ecart, 1.0), 1.0)
    F = np.where(fiable[:, None], F, P)
    alpha[ys, xs] = np.clip(part, 0.0, 1.0)
    couleur[ys, xs] = F
    couleur[fond] = 0.0
    rgba = np.dstack([couleur, alpha * 255.0]).round().clip(0, 255).astype(np.uint8)

    # Recadrage au plus juste (ombre comprise), ou image entière (pour garder
    # les coordonnées des repères de carte-reperes.html)
    if recadrer:
        visibles = np.argwhere(rgba[..., 3] > 6)
        (y0, x0), (y1, x1) = visibles.min(axis=0), visibles.max(axis=0) + 1
    else:
        y0, x0, y1, x1 = 0, 0, h, w
    Image.fromarray(rgba[y0:y1, x0:x1], "RGBA").save(sortie, optimize=True)
    print("%s : %d x %d" % (os.path.basename(sortie), x1 - x0, y1 - y0))


if __name__ == "__main__":
    os.makedirs(SORTIE, exist_ok=True)
    detourer(os.path.join(DEPOT, "Images", "ObCP_3Diso.png"),
             os.path.join(SORTIE, "carte_iso.png"), largeur=1600)
    detourer(os.path.join(DEPOT, "Images", "ObCP_3DTOP.png"),
             os.path.join(SORTIE, "carte_dessus.png"), largeur=678, recadrer=False)
