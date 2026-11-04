"""Carte mémoire du programme modèle 2.1.0 (python memoire.py ../images/memoire.png).

Schéma, pas à l'échelle. Les adresses viennent du programme compilé en
configuration Release (arm-none-eabi-objdump -h et arm-none-eabi-nm sur
OBCP_template_2026.elf : sections .isr_vector, .text, .rodata, symboles
_sidata, _sdata, _ebss, _end, _estack). À mettre à jour si le programme change.
"""
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, Rectangle

plt.rcParams["font.family"] = "Carlito"

# ---- Valeurs relevées dans le programme compilé (Release, version 2.1.0) ----
VECTEURS, TEXTE, RODATA, SIDATA, FIN_FLASH = 0x08080000, 0x080801C0, 0x0808D238, 0x0808D9FC, 0x0808DD00
TAILLE_VECTEURS = 0x188                    # .isr_vector : 98 adresses de 4 octets
TAILLE_CHARGEUR = 14184                     # ObCP_BLE_Loader 1.1, octets
SDATA, SBSS, END, ESTACK = 0x20000000, 0x20000304, 0x20003168, 0x20018000
TAS, PILE = 0x800, 0x1000

NAVY, GRIS, TRAIT = "#1F2D4D", "#5F6B7A", "#5F6B7A"


def ko(n):
    """Taille en ko (1 ko = 1024 octets), avec une décimale."""
    return ("%.1f ko" % (n / 1024)).replace(".", ",")


fig, ax = plt.subplots(figsize=(9.4, 7.1), dpi=150)
ax.set_xlim(0, 14)
ax.set_ylim(-0.9, 11.0)
ax.axis("off")


def bloc(x, bas, haut, couleur, texte, taille=10.5):
    ax.add_patch(Rectangle((x, bas), 3.0, haut - bas, facecolor=couleur, edgecolor=TRAIT, linewidth=1.1))
    ax.text(x + 1.5, (bas + haut) / 2, texte, ha="center", va="center", fontsize=taille, color="#1E2833",
            linespacing=1.25)


def adresse(x, y, texte):
    ax.text(x - 0.12, y, texte, ha="right", va="center", fontsize=9.5, family="DejaVu Sans Mono", color=GRIS)


def titre(x, texte, sous_titre):
    ax.text(x + 1.5, 10.75, texte, ha="center", va="center", fontsize=15, fontweight="bold", color=NAVY)
    ax.text(x + 1.5, 10.3, sous_titre, ha="center", va="center", fontsize=10.5, color=GRIS)


# ---- Flash : chargeur en banque 1, programme en banque 2 ----------------------
XF = 2.0
titre(XF, "Mémoire flash", "1024 ko : chargeur Bluetooth (banque 1), programme (banque 2)")
niveaux = [0.0, 1.8, 2.5, 4.7, 5.5, 6.3, 9.6]
bloc(XF, niveaux[0], niveaux[1], "#E7EEF8", "chargeur Bluetooth\nObCP-OTA (banque 1)\n%s sur 512 ko" % ko(TAILLE_CHARGEUR))
bloc(XF, niveaux[1], niveaux[2], "#CFE0F2", "vecteurs d'interruption\n(%d octets)" % TAILLE_VECTEURS)
bloc(XF, niveaux[2], niveaux[3], "#9DC6E9", "code (.text)\n%s" % ko(RODATA - TEXTE))
bloc(XF, niveaux[3], niveaux[4], "#BFDBF3", "constantes (.rodata)\n%s" % ko(SIDATA - RODATA))
bloc(XF, niveaux[4], niveaux[5], "#E5D7F0", "valeurs de départ des\nvariables (.data) %d o" % (FIN_FLASH - SIDATA))
bloc(XF, niveaux[5], niveaux[6], "#ECEFF2", "libre (fin de la banque 2)\n%s" % ko(0x08100000 - FIN_FLASH))
for y, a in zip(niveaux, (0x08000000, VECTEURS, TEXTE, RODATA, SIDATA, FIN_FLASH)):
    adresse(XF, y, "0x%08X" % a)
adresse(XF, niveaux[6], "0x080FFFFF")

# ---- RAM (SRAM1) : inchangée ----------------------------------------------------
XR = 9.9
titre(XR, "RAM (SRAM1)", "96 ko, dont %s de variables" % ko(END - SDATA))
r = [0.0, 0.9, 2.6, 4.0, 8.2, 9.6]
bloc(XR, r[0], r[1], "#E5D7F0", "variables initialisées\n(.data) %d o" % (SBSS - SDATA))
bloc(XR, r[1], r[2], "#F6D7B5", "variables à zéro (.bss)\n%s : tampons USB,\nBLE, bibliothèques" % ko(END - SBSS))
bloc(XR, r[2], r[3], "#F7E7A1", "tas (malloc, printf %%f)\n%d ko réservés, monte\nvers le haut si besoin" % (TAS // 1024))
bloc(XR, r[3], r[4], "#ECEFF2", "libre")
bloc(XR, r[4], r[5], "#F0B7B5", "pile (variables locales,\nappels) : %d ko réservés,\ndescend depuis le haut" % (PILE // 1024))
for y, a in zip(r, (SDATA, SBSS, END, None, ESTACK - PILE, None)):
    if a is not None:
        adresse(XR, y, "0x%08X" % a)
adresse(XR, r[5], "0x%08X" % (ESTACK - 1))
ax.annotate("", xy=(XR + 1.5, r[4] - 0.2), xytext=(XR + 1.5, r[4] - 0.8),
            arrowprops=dict(arrowstyle="<|-", color=GRIS, lw=1.6))
ax.annotate("", xy=(XR + 1.5, r[3] + 0.8), xytext=(XR + 1.5, r[3] + 0.2),
            arrowprops=dict(arrowstyle="-|>", color=GRIS, lw=1.6))

# ---- Copie des valeurs de départ au démarrage ----------------------------------
ax.add_patch(FancyArrowPatch((XF + 3.0, (niveaux[4] + niveaux[5]) / 2), (XR, (r[0] + r[1]) / 2),
                             connectionstyle="arc3,rad=0.25", arrowstyle="-|>", mutation_scale=14,
                             color="#8064B0", linestyle=(0, (5, 4)), linewidth=1.6))
ax.text(7.05, 3.2, "copiées au\ndémarrage\n(Reset_Handler)", ha="center", va="center", fontsize=10,
        color="#6A4FA0", bbox=dict(facecolor="white", edgecolor="none", pad=1.5))

ax.text(7.0, -0.55, "Hauteurs non proportionnelles. Adresses lues dans le programme compilé (Release). "
        "SRAM2 (0x10000000, 32 ko) : seuls ses 8 derniers octets\n(0x10007FF0) servent, de boîte aux lettres "
        "entre le programme et le chargeur (commande #ota).", ha="center", va="center",
        fontsize=9, style="italic", color=GRIS)

plt.savefig(sys.argv[1] if len(sys.argv) > 1 else "memoire.png", bbox_inches="tight", facecolor="white")
