# Images du README : sources

Les images de `Documentation/images/` sont fabriquées à partir des fichiers de ce dossier. Rien de tout cela n'est nécessaire pour utiliser le projet : ce dossier ne sert qu'à modifier une image.

| Image | Source | Outil |
|---|---|---|
| `banniere-*.png` | `banniere.html` | `generer_images.py` |
| `principe-*.png` | `principe.html` | `generer_images.py` |
| `carte-reperes-*.png` | `carte-reperes.html` | `generer_images.py` |
| `ota-auto-*.png` | `ota-auto.html` | `generer_images.py` |
| `voyants-*.png` | `voyants.html` | `generer_images.py` |
| `session-obcp-ota.gif` | `session.html` + `session.json` | `animer_session.py` |

Chaque figure existe en deux versions, `-clair` et `-sombre`, pour les deux thèmes de GitHub ; le README choisit la bonne avec la balise `<picture>`.

## Modifier une figure

1. Ouvrir la page HTML dans un navigateur pour la voir (ajouter `?theme=sombre` à l'adresse pour le thème sombre), modifier le texte dans la page.
2. Relancer : `python generer_images.py` (toutes les figures) ou `python generer_images.py voyants` (une seule).

Il faut Python 3 et :

```
python -m pip install playwright pillow
python -m playwright install chromium
python -m pip install imagequant pyoxipng      (facultatif : images 5 fois plus légères)
```

ainsi que les polices **Inter** et **DejaVu Sans Mono** installées sur le PC (sinon le navigateur en prend d'autres, et la mise en page peut changer).

## Les autres fichiers

- `theme.css` : couleurs des thèmes clair et sombre (palette de GitHub), éléments communs.
- `outils.js` : tracé des flèches entre les éléments d'une figure.
- `ressources/carte_iso.png`, `ressources/carte_dessus.png` : rendus 3D de l'ObCP (dossier `Images/` du dépôt) détourés par `detourer.py` (Pillow, NumPy, SciPy).
- `capturer_session.py` : enregistre `session.json`, une vraie session de `obcp_ota.py` face à la carte simulée des tests (Linux, après `Tests/run_tests.sh`). Les textes sont ceux de l'application PC ; seules les durées sont réglées sur celles d'une vraie carte.
- `animer_session.py` : transforme `session.json` en animation GIF (le terminal et la LED de la carte).
