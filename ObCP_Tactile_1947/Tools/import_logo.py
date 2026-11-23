#!/usr/bin/env python3
"""Convertir le logo CMJN fourni en ressource RGB565 à 16 couleurs, par lignes.

Usage depuis le dossier du projet avec Pillow installé
    python Tools/import_logo.py
Le profil ICC du JPEG pilote la conversion CMJN vers sRGB.
"""
import io
from pathlib import Path

from PIL import Image, ImageChops, ImageCms

ROOT = Path(__file__).resolve().parents[1]
# Convertir le profil CMJN vers sRGB avant de calculer des couleurs d’écran.
source = Image.open(ROOT / "docs/images/logo_source.jpg")
if source.info.get("icc_profile"):
    source = ImageCms.profileToProfile(
        source,
        ImageCms.ImageCmsProfile(io.BytesIO(source.info["icc_profile"])),
        ImageCms.createProfile("sRGB"),
        outputMode="RGB",
    )
else:
    source = source.convert("RGB")

# Retirer les marges blanches et garder les proportions lors de la réduction.
difference = ImageChops.difference(source, Image.new("RGB", source.size, "white"))
mask = difference.convert("L").point(lambda value: 255 if value > 40 else 0)
image = source.crop(mask.getbbox())
image = image.resize((268, round(image.height * 268 / image.width)), Image.Resampling.LANCZOS)
# Limiter la palette à seize cases. Les composantes 8 bits deviennent un mot 5/6/5.
indexed = image.quantize(colors=16, dither=Image.Dither.NONE)
palette = indexed.getpalette()
colors = []
for i in range(16):
    r, green, b = palette[3 * i:3 * i + 3]
    colors.append(((r & 248) << 8) | ((green & 252) << 3) | (b >> 3))

# RLE : chaque paire code longueur puis indice. rows délimite les données de chaque ligne.
runs = []
rows = [0]
for y in range(indexed.height):
    x = 0
    while x < indexed.width:
        color = indexed.getpixel((x, y))
        count = 1
        while count < 255 and x + count < indexed.width and indexed.getpixel((x + count, y)) == color:
            count += 1
        runs.extend((count, color))
        x += count
    rows.append(len(runs))
# Les offsets de lignes sont stockés dans des uint16_t sur la cible.
assert len(runs) < 65536


def array(values, width, formatter):
    """Former des lignes C de width valeurs, selon la notation fournie par formatter.

    La virgule finale est admise dans une initialisation C11. La fonction renvoie
    seulement le contenu du tableau, pour le substituer dans le modèle commenté.
    """
    return "\n".join(
        "    " + ", ".join(formatter(value) for value in values[i:i + width]) + ","
        for i in range(0, len(values), width)
    )


# Le modèle versionné garde les commentaires Doxygen lors d’une nouvelle conversion.
output = (ROOT / "Tools/templates/logo.c.in").read_text(encoding="utf-8")
replacements = {
    "WIDTH": str(indexed.width), "HEIGHT": str(indexed.height),
    "PALETTE": array(colors, 8, lambda value: f"0x{value:04X}"),
    "ROWS": array(rows, 12, str), "RUNS": array(runs, 24, str),
}
for token, value in replacements.items():
    output = output.replace("@" + token + "@", value)
(ROOT / "Core/Src/obcp/logo.c").write_text(output, encoding="utf-8")
print(f"Logo {indexed.width} × {indexed.height}, {len(runs) + 2 * len(rows) + 32} octets de ressource")
