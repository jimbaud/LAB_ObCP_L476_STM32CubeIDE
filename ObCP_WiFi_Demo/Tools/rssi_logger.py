#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Enregistreur de niveau de signal (démo 1) pour le démonstrateur Wi-Fi 6
X-NUCLEO-67W61M1 (carte ObCP ou NUCLEO-L476RG).

Le script ouvre le port COM de la carte (USB de l'ObCP ou ST-LINK de la
NUCLEO), lance la commande « track [ssid] csv », enregistre les mesures dans
un fichier CSV puis trace la courbe RSSI(t) (si matplotlib est installé).

Prérequis :  pip install pyserial matplotlib
Exemples :
    python rssi_logger.py --port COM5 --duree 120
    python rssi_logger.py --port /dev/ttyACM0 --ssid "Ma Box" --sortie couloir.csv
Arrêt anticipé : Ctrl+C (la mesure en cours sur la carte est arrêtée proprement).

Attention : fermer le terminal série (PuTTY, Tera Term...) avant de lancer le
script, un port COM ne pouvant être ouvert que par un seul programme.
"""
import argparse
import csv
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("Module pyserial manquant : pip install pyserial")

LIGNE_CSV = re.compile(r"^(\d+);(-?\d*);(\d+)$")
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def lire_ligne(port):
    """Lit une ligne (sans séquences ANSI) ; chaîne vide si délai dépassé."""
    brut = port.readline()
    return ANSI.sub("", brut.decode("utf-8", errors="replace")).strip()


def main():
    ap = argparse.ArgumentParser(description="Enregistre le RSSI mesuré par la carte (commande track ... csv).")
    ap.add_argument("--port", required=True, help="port série (COM5, /dev/ttyACM0...)")
    ap.add_argument("--ssid", default="", help="réseau à suivre (vide : réseau connecté)")
    ap.add_argument("--duree", type=float, default=60.0, help="durée d'enregistrement en secondes (60)")
    ap.add_argument("--sortie", default="rssi.csv", help="fichier CSV produit (rssi.csv)")
    ap.add_argument("--pas-de-graphe", action="store_true", help="ne pas tracer la courbe")
    args = ap.parse_args()

    with serial.Serial(args.port, 115200, timeout=0.5) as port:
        port.write(b"\x03")                      # Ctrl-C : efface une saisie éventuelle
        time.sleep(0.3)
        port.reset_input_buffer()
        cmd = "track " + (('"%s" ' % args.ssid) if args.ssid else "") + "csv\r"
        port.write(cmd.encode("utf-8"))
        print("Commande envoyée :", cmd.strip())

        mesures = []
        t_fin = time.time() + args.duree
        try:
            while time.time() < t_fin:
                ligne = lire_ligne(port)
                if not ligne:
                    continue
                m = LIGNE_CSV.match(ligne)
                if m:
                    t_ms, rssi, canal = int(m.group(1)), m.group(2), int(m.group(3))
                    valeur = int(rssi) if rssi else None
                    mesures.append((t_ms, valeur, canal))
                    print("%8.1f s  %s dBm  (canal %d)" % (t_ms / 1000.0, rssi if rssi else "---", canal))
                elif "introuvable" in ligne or "préciser" in ligne or "Commande inconnue" in ligne:
                    print("Carte :", ligne)
                    return 1
        except KeyboardInterrupt:
            print("\nArrêt demandé.")
        finally:
            port.write(b"x")                     # une touche arrête « track » sur la carte
            time.sleep(0.5)
            port.reset_input_buffer()

    if not mesures:
        print("Aucune mesure reçue (carte connectée ? bon port ? terminal série fermé ?)")
        return 1

    # Format CSV « à la française » (séparateur ; et virgule décimale) : le
    # fichier s'ouvre directement dans un tableur réglé en français.
    with open(args.sortie, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(["temps_s", "rssi_dbm", "canal"])
        for t_ms, rssi, canal in mesures:
            w.writerow([("%.3f" % (t_ms / 1000.0)).replace(".", ","), "" if rssi is None else rssi, canal])
    valeurs = [r for _, r, _ in mesures if r is not None]
    print("%d mesure(s) enregistrée(s) dans %s" % (len(mesures), args.sortie))
    if valeurs:
        moy = sum(valeurs) / len(valeurs)
        print("RSSI min %d dBm, moyenne %.1f dBm, max %d dBm" % (min(valeurs), moy, max(valeurs)))

    if not args.pas_de_graphe and valeurs:
        try:
            import matplotlib.pyplot as plt
        except ImportError:
            print("matplotlib absent : pas de graphe (pip install matplotlib)")
            return 0
        t = [m[0] / 1000.0 for m in mesures if m[1] is not None]
        plt.figure(figsize=(9, 4))
        plt.plot(t, valeurs, marker=".", linewidth=1)
        plt.xlabel("temps (s)")
        plt.ylabel("RSSI (dBm)")
        plt.title("Niveau du signal reçu - %s" % (args.ssid or "réseau connecté"))
        plt.grid(True, alpha=0.3)
        image = args.sortie.rsplit(".", 1)[0] + ".png"
        plt.tight_layout()
        plt.savefig(image, dpi=120)
        print("Graphe enregistré dans", image)
        plt.show()
    return 0


if __name__ == "__main__":
    sys.exit(main())
