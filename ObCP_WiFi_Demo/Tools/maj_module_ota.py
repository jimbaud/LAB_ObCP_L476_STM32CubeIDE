#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Mise à jour du firmware du module ST67W611M1 depuis le PC, pour le démonstrateur
Wi-Fi 6 avec la X-NUCLEO-67W61M1 (carte ObCP ou NUCLEO-L476RG).

Le script lance la commande « module ota » sur la carte, puis envoie le fichier
.ota par le protocole YMODEM sur le port COM de la carte (port USB de l'ObCP ou
port COM virtuel du ST-LINK de la NUCLEO). La carte
transmet l'image au module par le lien SPI ; le module la vérifie et redémarre
sur le nouveau firmware. Aucun câblage supplémentaire n'est nécessaire, mais le
module doit déjà exécuter un firmware « mission T01 » 2.0.x.

Fichier à envoyer (X-CUBE-ST67W61, dossier NCP_Binaries) :
  https://github.com/STMicroelectronics/x-cube-st67w61/tree/main/Projects/ST67W6X_Scripts/Binaries/NCP_Binaries
  par exemple st67w611m_mission_t01_v2.0.106.xz.ota (image compressée, 860 Ko)

Prérequis : pip install pyserial
Exemples :
    python maj_module_ota.py --port COM5 st67w611m_mission_t01_v2.0.106.xz.ota
    python3 maj_module_ota.py --port /dev/ttyACM0 st67w611m_mission_t01_v2.0.106.xz.ota

Fermer d'abord le terminal série (PuTTY, Tera Term...) : un port COM ne peut
être ouvert que par un seul programme.
"""
import argparse
import os
import re
import struct
import sys
import time
import binascii

try:
    import serial
except ImportError:
    sys.exit("Module pyserial manquant : pip install pyserial")

SOH, STX, EOT, ACK, NAK, CAN = 0x01, 0x02, 0x04, 0x06, 0x15, 0x18
C = 0x43
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
MARQUEUR = "En attente du fichier (YMODEM)"
INVITES = ("obcp>", "st67>")          # invites de l'ObCP et de la NUCLEO
MAX_ESSAIS = 10


class Echec(Exception):
    """Erreur expliquée à l'utilisateur."""


def verifier_fichier(chemin):
    """Mêmes contrôles que la carte, faits avant d'ouvrir le port."""
    nom = os.path.basename(chemin)
    bas = nom.lower()
    if not bas.endswith(".ota"):
        raise Echec("fichier .ota attendu (.xz.ota ou .bin.ota) : « %s »" % nom)
    if "t02" in bas or "mfg" in bas:
        raise Echec("« %s » : image T02 ou de test (mfg) ; seule une image « mission T01 » convient" % nom)
    if "t01" not in bas:
        raise Echec("« %s » : nom inattendu, utiliser st67w611m_mission_t01_vX.Y.Z.xz.ota" % nom)
    taille = os.path.getsize(chemin)
    with open(chemin, "rb") as f:
        entete = f.read(512)
    if len(entete) < 512 or entete[:16] != b"QCC74X_OTAVer1.0":
        raise Echec("« %s » n'est pas une image .ota du ST67W611M1 (signature absente)" % nom)
    longueur = struct.unpack("<I", entete[20:24])[0]
    if longueur + 512 != taille:
        raise Echec("fichier tronqué : %d octets, %d attendus d'après l'en-tête" % (taille, longueur + 512))
    version = entete[48:64].split(b"\0")[0].decode("ascii", "replace")
    nature = entete[16:20].decode("ascii", "replace").strip()
    return nom, taille, version, nature


class Liaison:
    """Port série avec lecture du texte de la carte (sans séquences ANSI)."""

    def __init__(self, port):
        self.port = serial.Serial(port, 115200, timeout=0.05)
        self.texte = ""

    def fermer(self):
        self.port.close()

    def afficher_texte(self, octets):
        brut = ANSI.sub("", octets.decode("utf-8", errors="replace")).replace("\r", "")
        self.texte += brut
        return brut

    def octet(self, delai):
        """Octet de protocole suivant (int) ou None après delai secondes."""
        fin = time.monotonic() + delai
        while time.monotonic() < fin:
            b = self.port.read(1)
            if b:
                return b[0]
        return None

    def attendre_texte(self, motif, delai, echo=True):
        """Affiche le texte reçu jusqu'à la ligne contenant motif (incluse)."""
        fin = time.monotonic() + delai
        affiche = len(self.texte)
        while time.monotonic() < fin:
            b = self.port.read(256)
            if not b:
                continue
            self.afficher_texte(b)
            trouve = motif in self.texte
            limite = len(self.texte)
            if trouve:
                fin_ligne = self.texte.find("\n", self.texte.index(motif))
                limite = fin_ligne + 1 if fin_ligne >= 0 else limite
            if echo and limite > affiche:
                sys.stdout.write(self.texte[affiche:limite])
                sys.stdout.flush()
                affiche = limite
            if trouve:
                return True
            if "\u2717 module ota" in self.texte:
                return False
        return False

    def attendre_invite(self, delai):
        """Affiche le texte reçu jusqu'à l'invite de commande de la carte."""
        fin = time.monotonic() + delai
        while time.monotonic() < fin:
            b = self.port.read(256)
            if b:
                sys.stdout.write(self.afficher_texte(b))
                sys.stdout.flush()
            if self.texte.rstrip().endswith(INVITES):
                return True
        return False


def bloc(numero, donnees, taille_bloc):
    corps = donnees.ljust(taille_bloc, b"\x1a" if numero else b"\x00")
    crc = binascii.crc_hqx(corps, 0)
    tete = bytes([STX if taille_bloc == 1024 else SOH, numero & 0xFF, 0xFF - (numero & 0xFF)])
    return tete + corps + struct.pack(">H", crc)


def envoyer_bloc(liaison, paquet, delai):
    """Envoie un bloc jusqu'à son acquittement (renvoi sur NAK ou silence)."""
    for _ in range(MAX_ESSAIS):
        liaison.port.write(paquet)
        fin = time.monotonic() + delai
        while time.monotonic() < fin:
            r = liaison.octet(fin - time.monotonic())
            if r == ACK:
                return
            if r == NAK:
                break                          # bloc à renvoyer
            if r == CAN and liaison.octet(1.0) == CAN:
                raise Echec("transfert annulé par la carte")
            # autres octets (« C » encore en attente...) : ignorés
    raise Echec("la carte n'acquitte plus les blocs (trop d'essais)")


def transferer(liaison, chemin, nom, taille):
    # 1) bloc 0 : nom, taille, date de modification (octal)
    meta = ("%s\0%d %o 0" % (nom, taille, int(os.path.getmtime(chemin)))).encode("utf-8")
    liaison.port.reset_input_buffer()        # 'C' en attente : le bloc 0 part tout de suite
    envoyer_bloc(liaison, bloc(0, meta, 128), 12.0)
    if liaison.octet(10.0) != C:
        raise Echec("la carte n'a pas demandé les données")

    # 2) données par blocs de 1 Ko
    t0 = time.monotonic()
    envoye = 0
    numero = 1
    dernier_affichage = 0.0
    with open(chemin, "rb") as f:
        while True:
            morceau = f.read(1024)
            if not morceau:
                break
            envoyer_bloc(liaison, bloc(numero, morceau, 1024), 20.0)   # écriture en flash du module
            envoye += len(morceau)
            numero += 1
            maintenant = time.monotonic()
            if maintenant - dernier_affichage > 0.3 or envoye == taille:
                dernier_affichage = maintenant
                duree = max(maintenant - t0, 0.001)
                debit = envoye / duree
                reste = (taille - envoye) / debit if debit > 0 else 0
                sys.stdout.write("\r  Envoi : %3d %%  %6d / %d Ko  %5.1f Ko/s  reste %3d s " %
                                 (envoye * 100 // taille, envoye // 1024, taille // 1024, debit / 1024, reste))
                sys.stdout.flush()
    print()

    # 3) fin de fichier : EOT (NAK attendu), EOT (ACK), puis bloc 0 vide
    for _ in range(MAX_ESSAIS):
        liaison.port.write(bytes([EOT]))
        r = liaison.octet(10.0)
        if r == ACK:
            break
        if r == CAN:
            raise Echec("transfert annulé par la carte")
    if liaison.octet(5.0) == C:
        envoyer_bloc(liaison, bloc(0, b"", 128), 5.0)
    return time.monotonic() - t0


def main():
    try:
        sys.stdout.reconfigure(errors="replace")
    except AttributeError:
        pass
    ap = argparse.ArgumentParser(description="Met à jour le firmware du module ST67W611M1 (commande module ota).")
    ap.add_argument("--port", required=True, help="port série de la carte (COM5, /dev/ttyACM0...)")
    ap.add_argument("fichier", help="image st67w611m_mission_t01_vX.Y.Z.xz.ota (ou .bin.ota)")
    args = ap.parse_args()

    try:
        nom, taille, version, nature = verifier_fichier(args.fichier)
    except (Echec, OSError) as e:
        print("Fichier refusé :", e)
        return 1
    print("Image %s, version %s (%s), %d octets" % (nom, version, "compressée XZ" if nature == "XZ" else nature, taille))

    try:
        liaison = Liaison(args.port)
    except serial.SerialException as e:
        print("Port %s inaccessible : %s" % (args.port, e))
        print("(terminal série encore ouvert ? mauvais port ?)")
        return 1

    try:
        liaison.port.write(b"\x03")            # Ctrl-C : efface une saisie en cours
        time.sleep(0.3)
        liaison.port.reset_input_buffer()
        liaison.port.write(b"module ota\r")
        if not liaison.attendre_texte(MARQUEUR, 15.0):
            if "✗ module ota" in liaison.texte:
                liaison.attendre_invite(3.0)
            else:
                print("\nPas de réponse de la carte (programme du démonstrateur chargé ? bon port ?)")
            return 1
        print()
        if liaison.octet(10.0) != C:
            print("La carte n'a pas lancé la réception YMODEM.")
            return 1
        duree = transferer(liaison, args.fichier, nom, taille)
        print("Fichier transmis en %.0f s ; installation par le module (jusqu'à 40 s)..." % duree)
        fin = time.monotonic() + 120.0
        resultat = None
        while time.monotonic() < fin:
            b = liaison.port.read(256)
            if b:
                sys.stdout.write(liaison.afficher_texte(b))
                sys.stdout.flush()
            if "Firmware du module mis à jour" in liaison.texte:
                resultat = True
            elif "✗ " in liaison.texte or "Version signalée par le module" in liaison.texte:
                resultat = False if resultat is None else resultat
            if resultat is not None and liaison.texte.rstrip().endswith(INVITES):
                break
        print()
        if resultat:
            print("Mise à jour terminée.")
            return 0
        print("La mise à jour n'a pas abouti (voir les messages de la carte ci-dessus).")
        return 1
    except Echec as e:
        print("\nÉchec :", e)
        liaison.attendre_invite(3.0)
        return 1
    except KeyboardInterrupt:
        liaison.port.write(bytes([CAN] * 5))
        print("\nTransfert interrompu : le module conserve son firmware actuel.")
        return 1
    finally:
        liaison.fermer()


if __name__ == "__main__":
    sys.exit(main())
