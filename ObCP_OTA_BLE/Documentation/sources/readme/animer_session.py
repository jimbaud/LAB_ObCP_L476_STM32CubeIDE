# -*- coding: utf-8 -*-
"""
animer_session.py - Fabrique l'animation Documentation/images/session-obcp-ota.gif
à partir de session.json (enregistré par capturer_session.py).

    python animer_session.py

Chaque image de l'animation est la page session.html (terminal et carte),
affichée par Chromium dans l'état du moment : texte écrit par obcp_ota.py
jusque-là, LED de la carte d'après les événements du chargeur simulé (bleu
qui clignote, bleu fixe, vert qui change à chaque Ko...). Les durées sont
celles de l'enregistrement.

Dépendances : Playwright pour Python, Pillow ; facultatif : imagequant
(meilleures couleurs de la photo de la carte).
"""
import json
import os

from PIL import Image
from playwright.sync_api import sync_playwright

ICI = os.path.dirname(os.path.abspath(__file__))
SORTIE = os.path.normpath(os.path.join(ICI, "..", "..", "images", "session-obcp-ota.gif"))
ECHELLE = 1.5             # résolution : 1,5 fois celle de l'écran (texte net, fichier léger)
PAUSE_FINALE = 4.5        # secondes, avant de recommencer

LEGENDES = {
    "programme":   (None,     "Votre programme"),
    "redemarrage": (None,     "Redémarrage…"),
    "attente":     ("bleu",   "Chargeur : en attente"),
    "connecte":    ("bleu",   "Chargeur : PC connecté"),
    "transfert":   ("vert",   "Chargeur : réception"),
    "valide":      ("vert",   "Programme validé"),
    "nouveau":     (None,     "Nouveau programme"),
}


class Terminal:
    """Terminal minimal : lignes de texte, retour chariot (\\r) et saut de ligne."""
    def __init__(self):
        self.lignes = [""]
        self.colonne = 0

    def ecrire(self, texte):
        for c in texte:
            if c == "\n":
                self.lignes.append("")
                self.colonne = 0
            elif c == "\r":
                self.colonne = 0
            else:
                ligne = self.lignes[-1]
                self.lignes[-1] = ligne[:self.colonne] + c + ligne[self.colonne + 1:]
                self.colonne += 1

    def instantane(self):
        return list(self.lignes), [len(self.lignes) - 1, self.colonne]


def chronologie(session):
    """Liste [(t, état de la page)] : une entrée à chaque changement visible."""
    invite, commande = session["invite"], session["commande"]
    terminal = Terminal()
    terminal.ecrire(invite + " ")
    textes = []                                   # (t, lignes, curseur)
    t = 0.0
    textes.append((t, *terminal.instantane()))
    t = 0.7
    for i in range(0, len(commande), 2):          # frappe : 2 caractères toutes les 45 ms
        terminal.ecrire(commande[i:i + 2])
        textes.append((t, *terminal.instantane()))
        t += 0.045
    t += 0.35
    terminal.ecrire("\n")
    debut = t
    etats = [(-1.0, "programme")]
    bascules = []                                 # changements du vert pendant le transfert
    fin = None
    for instant, genre, valeur in session["evenements"]:
        moment = debut + instant
        if genre == "texte":
            terminal.ecrire(valeur)
            textes.append((moment, *terminal.instantane()))
        elif genre == "etat":
            if valeur == "ko":
                bascules.append(moment)
            else:
                etats.append((moment, valeur))
        elif genre == "fin":
            fin = moment
    t = fin + 0.25
    terminal.ecrire(invite + " ")
    textes.append((t, *terminal.instantane()))
    total = t + PAUSE_FINALE

    def etat_carte(instant):
        nom, depuis = "programme", -1.0
        for moment, n in etats:
            if moment <= instant:
                nom, depuis = n, moment
        couleur, legende = LEGENDES[nom]
        led = None
        if nom == "attente":
            led = "bleu" if (instant - depuis) % 1.0 < 0.1 else None
        elif nom in ("connecte", "valide"):
            led = couleur
        elif nom == "transfert":
            led = "vert" if sum(1 for b in bascules if b <= instant) % 2 == 0 else None
        elif nom == "nouveau":
            # le programme modèle démarre : un éclat vert
            led = "vert" if 0.15 <= instant - depuis < 0.45 else None
            couleur = "vert" if led else None
        return {"led": led, "legende": legende, "pastille": couleur}

    # instants où quelque chose change
    instants = {round(x[0], 3) for x in textes}
    for moment, nom in etats:
        if moment >= 0:
            instants.add(round(moment, 3))
            if nom == "attente":
                fin_attente = min([m for m, _ in etats if m > moment] + [total])
                k = moment
                while k < fin_attente:
                    instants.update({round(k, 3), round(k + 0.1, 3)})
                    k += 1.0
            if nom == "nouveau":
                instants.update({round(moment + 0.15, 3), round(moment + 0.45, 3)})
    instants.update(round(b, 3) for b in bascules)
    k = t                                          # curseur qui clignote à la fin
    while k < total:
        instants.update({round(k, 3), round(k + 0.5, 3)})
        k += 1.0
    instants = sorted(i for i in instants if 0 <= i < total)

    images = []
    for instant in instants:
        lignes, curseur = textes[0][1], textes[0][2]
        for moment, l, c in textes:
            if moment <= instant:
                lignes, curseur = l, c
        if instant >= t and (instant - t) % 1.0 >= 0.5:
            curseur = None                         # phase éteinte du clignotement
        etat = {"lignes": lignes, "curseur": curseur, "invite": invite}
        etat.update(etat_carte(instant))
        images.append((instant, etat))
    # une image affichée moins de 20 ms est invisible (et les navigateurs
    # ralentissent les GIF dont les images durent moins de 20 ms) : on l'ôte
    gardees = []
    for i, (instant, etat) in enumerate(images):
        suivant = images[i + 1][0] if i + 1 < len(images) else total
        if suivant - instant >= 0.02 or i == len(images) - 1:
            gardees.append((instant, etat))
    return gardees, total


def palette_commune(images):
    """Palette de 255 couleurs commune à toutes les images, sans tramage : les
    zones qui ne changent pas restent identiques d'une image à l'autre. La
    couleur n° 255 est réservée à la transparence (voir main)."""
    choix = images[:: max(1, len(images) // 12)] + [images[-1]]
    planche = Image.new("RGB", (images[0].width, images[0].height * len(choix)))
    for k, im in enumerate(choix):
        planche.paste(im, (0, k * im.height))
    try:
        import imagequant
        reduite = imagequant.quantize_pil_image(planche.convert("RGBA"), dithering_level=0.0,
                                                max_colors=255, min_quality=0, max_quality=100)
        couleurs = reduite.convert("RGB").getcolors(256)
    except ImportError:
        couleurs = planche.quantize(colors=255, method=Image.Quantize.MEDIANCUT).convert("RGB").getcolors(256)
    valeurs = [v for _, rvb in couleurs for v in rvb][:255 * 3]
    valeurs += [0, 0, 0] * (255 - len(valeurs) // 3) + [255, 0, 255]     # n° 255 : transparent
    palette = Image.new("P", (1, 1))
    palette.putpalette(valeurs)
    return palette


def main():
    with open(os.path.join(ICI, "session.json"), encoding="utf-8") as f:
        session = json.load(f)
    images_etat, total = chronologie(session)
    print("%d images, %.1f s" % (len(images_etat), total))
    rendus = []
    with sync_playwright() as p:
        navigateur = p.chromium.launch()
        page = navigateur.new_page(device_scale_factor=ECHELLE, viewport={"width": 960, "height": 500})
        url = "file://" + os.path.join(ICI, "session.html").replace("\\", "/")
        if not url.startswith("file:///"):
            url = url.replace("file://", "file:///")
        page.goto(url)
        page.wait_for_function("window.figure_prete === true", timeout=15000)
        zone = page.locator("#figure")
        for k, (instant, etat) in enumerate(images_etat):
            page.evaluate("etat => afficher(etat)", etat)
            chemin = os.path.join(ICI, "_image.png")
            zone.screenshot(path=chemin)
            rendus.append(Image.open(chemin).convert("RGB"))
        os.remove(chemin)
        navigateur.close()
    palette = palette_commune(rendus)
    # Chaque image ne garde que les pixels qui ont changé ; les autres prennent
    # la couleur transparente n° 255 et laissent voir l'image précédente
    # (fichier bien plus léger : le GIF compresse très bien ces zones).
    import numpy as np
    indices = [np.asarray(im.quantize(palette=palette, dither=Image.Dither.NONE)) for im in rendus]
    gif = [Image.fromarray(indices[0], "P")]
    for avant, apres in zip(indices, indices[1:]):
        delta = apres.copy()
        delta[apres == avant] = 255
        gif.append(Image.fromarray(delta, "P"))
    for im in gif:
        im.putpalette(palette.getpalette())
    durees = []
    for k, (instant, _) in enumerate(images_etat):
        suivant = images_etat[k + 1][0] if k + 1 < len(images_etat) else total
        durees.append(max(20, int(round((suivant - instant) * 100)) * 10))
    os.makedirs(os.path.dirname(SORTIE), exist_ok=True)
    gif[0].save(SORTIE, save_all=True, append_images=gif[1:], duration=durees, loop=0,
                optimize=False, disposal=1, transparency=255)
    print("%s : %d x %d, %d Ko, %.1f s" % (os.path.basename(SORTIE), gif[0].width, gif[0].height,
                                          os.path.getsize(SORTIE) // 1024, sum(durees) / 1000))


if __name__ == "__main__":
    main()
