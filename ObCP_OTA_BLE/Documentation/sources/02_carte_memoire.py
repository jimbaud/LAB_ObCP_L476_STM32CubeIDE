"""Carte mémoire de l'ObCP pour le chargement par Bluetooth (schéma, pas à l'échelle)."""
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

plt.rcParams["font.family"] = "Carlito"
fig, ax = plt.subplots(figsize=(8.6, 4.6), dpi=220)
ax.set_xlim(0, 12.3)
ax.set_ylim(0, 10)
ax.axis("off")

def bloc(x, y_haut, y_bas, couleur, bord, texte, taille=10.5, gras=False):
    ax.add_patch(FancyBboxPatch((x, y_bas), 2.6, y_haut - y_bas, boxstyle="square,pad=0",
                                facecolor=couleur, edgecolor=bord, linewidth=1.2))
    ax.text(x + 1.3, (y_haut + y_bas) / 2, texte, ha="center", va="center", fontsize=taille,
            fontweight="bold" if gras else "normal", color="#263238")

def adresse(x, y, texte):
    ax.text(x - 0.08, y, texte, ha="right", va="center", fontsize=9, family="DejaVu Sans Mono", color="#37474F")
    ax.plot([x - 0.05, x], [y, y], color="#37474F", linewidth=0.8)

def note(x, y, texte, couleur="#37474F"):
    ax.text(x, y, texte, ha="left", va="center", fontsize=9.5, color=couleur)

# ---- Flash -------------------------------------------------------------
X = 1.75
ax.text(X + 1.3, 9.65, "Flash (1 Mo)", ha="center", fontsize=12, fontweight="bold", color="#263238")
bloc(X, 9.3, 8.0, "#A5D6A7", "#2E7D32", "Chargeur BLE\n14 à 24 Ko", gras=True)
bloc(X, 8.0, 5.3, "#F5F5F5", "#9E9E9E", "libre", taille=10)
bloc(X, 5.3, 3.4, "#FFE082", "#F57F17", "Application\n(taille variable)", gras=True)
bloc(X, 3.4, 0.4, "#FFF8E1", "#F9A825", "pages effacées\nà la demande", taille=10)
for y, a in ((9.3, "0x0800 0000"), (5.3, "0x0808 0000"), (0.4, "0x0810 0000")):
    adresse(X, y, a)
ax.text(X - 1.55, 7.3, "banque 1\n512 Ko", ha="center", va="center", fontsize=9.5, color="#2E7D32", rotation=90)
ax.text(X - 1.55, 2.85, "banque 2\n512 Ko", ha="center", va="center", fontsize=9.5, color="#E65100", rotation=90)
note(X + 2.75, 8.65, "programmé une fois (USB, DFU)\njamais modifié par Bluetooth", "#1B5E20")
note(X + 2.75, 5.0, "table des vecteurs : 8 premiers\noctets écrits EN DERNIER", "#E65100")
note(X + 2.75, 3.9, "remplacée par Bluetooth", "#E65100")

# ---- RAM ------------------------------------------------------------------
X2 = 9.5
ax.text(X2 + 1.3, 9.65, "RAM", ha="center", fontsize=12, fontweight="bold", color="#263238")
bloc(X2, 9.3, 5.6, "#E3F2FD", "#1565C0", "SRAM1 (96 Ko)\nvariables et pile\ndu programme actif", taille=10)
adresse(X2, 9.3, "0x2000 0000")
bloc(X2, 4.4, 1.35, "#ECEFF1", "#607D8B", "SRAM2 (32 Ko)\nnon utilisée par\ndéfaut", taille=10)
bloc(X2, 1.35, 0.6, "#FFCDD2", "#C62828", "boîte aux lettres", taille=9.5, gras=True)
adresse(X2, 4.4, "0x1000 0000")
adresse(X2, 1.35, "0x1000 7FF0")
ax.text(X2 + 1.3, -0.05, "demande « chargeur » de l'application,\nconservée pendant un reset logiciel",
        ha="center", va="top", fontsize=9.5, color="#B71C1C")

plt.savefig(sys.argv[1], bbox_inches="tight", facecolor="white")
