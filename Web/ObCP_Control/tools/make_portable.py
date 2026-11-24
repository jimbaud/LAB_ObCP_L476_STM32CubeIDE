"""Regénère une page HTML autonome à partir de la version PWA, sans dépendance."""
import argparse
import base64
from pathlib import Path
import re

WEB = Path(__file__).resolve().parents[1] / "dist"


def inline_asset(filename):
    mime = {"webp": "image/webp", "png": "image/png"}[filename.suffix.lstrip(".")]
    data = base64.b64encode(filename.read_bytes()).decode("ascii")
    return f"data:{mime};base64,{data}"


def build(output):
    page = (WEB / "index.html").read_text(encoding="utf-8")
    page = page.replace('<html lang="fr">', '<html lang="fr" data-portable>')
    page = re.sub(r'^.*<link rel="(?:manifest|apple-touch-icon)"[^>]*>\n', "", page, flags=re.M)
    page = page.replace('<link rel="stylesheet" href="styles.css">',
                        "<style>\n" + (WEB / "styles.css").read_text(encoding="utf-8") + "\n</style>")
    for asset in ["assets/icon-192.png", "assets/obcp.webp"]:
        page = page.replace('="' + asset + '"', '="' + inline_asset(WEB / asset) + '"')
    for name in ["protocol.js", "charts.js", "app.js"]:
        source = (WEB / name).read_text(encoding="utf-8")
        if "</script" in source.lower():
            raise ValueError("Une balise de fin doit être échappée dans " + name)
        page = page.replace(f'<script src="{name}"></script>', "<script>\n" + source + "\n</script>")
    page = re.sub(r"Pour installer la version hébergée,.*?première mise en cache\.",
                  "Cette édition tient dans un seul fichier HTML. Vous pouvez l’ouvrir hors ligne en démonstration. Pour la liaison Bluetooth, ouvrez sa version HTTPS ou servez le fichier sur localhost.", page)
    output.write_text(page, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fichier", type=Path, help="Chemin du fichier HTML à produire")
    build(parser.parse_args().fichier)
