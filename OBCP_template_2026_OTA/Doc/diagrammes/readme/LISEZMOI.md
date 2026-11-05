# Illustrations du README : sources

Les images `Doc/images/*-clair.png` et `*-sombre.png` (thèmes clair et sombre de GitHub, choisis par la balise `<picture>` du README) sont fabriquées à partir des pages HTML de ce dossier. Rien de tout cela n'est nécessaire pour utiliser le programme : ce dossier ne sert qu'à modifier une image. Les autres schémas de `Doc/images/` ont leurs sources dans `Doc/diagrammes/` (voir `LISEZMOI.txt`).

| Image | Source |
|---|---|
| `banniere-*.png` | `banniere.html` |
| `interfaces-*.png` | `interfaces.html` |
| `carte-reperes-*.png` | `carte-reperes.html` |
| `ota-auto-*.png` | `ota-auto.html` |
| `codes-led-*.png` | `codes-led.html` (durées lues dans `app.c` et `fault.c`) |

## Modifier une figure

1. Ouvrir la page HTML dans un navigateur (ajouter `?theme=sombre` à l'adresse pour le thème sombre), modifier le texte.
2. Relancer `python generer_images.py` (toutes les figures) ou `python generer_images.py codes-led` (une seule).

Il faut Python 3 et :

```
python -m pip install playwright pillow
python -m playwright install chromium
python -m pip install imagequant pyoxipng      (facultatif : images 5 fois plus légères)
```

ainsi que les polices **Inter** et **DejaVu Sans Mono** installées sur le PC.

## Les autres fichiers

- `theme.css` : couleurs des deux thèmes (palette de GitHub) et éléments communs ; `outils.js` : tracé des flèches.
- `ressources/carte_iso.png`, `ressources/carte_dessus.png` : rendus 3D de l'ObCP (dossier `Images/` du dépôt) détourés par `detourer.py` (Pillow, NumPy, SciPy).

Les mêmes outils servent au README du projet `ObCP_OTA_BLE` (`Documentation/sources/readme/`).
