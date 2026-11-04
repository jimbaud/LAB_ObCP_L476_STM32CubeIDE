# -*- coding: utf-8 -*-
"""
ota_image.py - Lecture du programme à envoyer (.elf, .hex ou .bin) et
vérifications AVANT le transfert.

Le chargeur reçoit une suite d'octets qu'il écrit en flash à partir de
l'adresse 0x08080000. Il faut donc transformer le fichier produit par
STM32CubeIDE en une « image » : les octets du programme, dans l'ordre, à
partir de cette adresse.

    .elf  fichier produit par la compilation (contient les adresses)
    .hex  format texte Intel HEX (contient aussi les adresses)
    .bin  octets bruts, sans adresse : on suppose qu'ils commencent à 0x08080000

Les vérifications sont faites ici, sur le PC, avant d'effacer quoi que ce
soit sur la carte : un programme mal placé ne doit pas détruire
l'application déjà installée.
"""

from __future__ import annotations

import os
import struct
import time
import zlib
from dataclasses import dataclass

from ota_protocole import ADRESSE_APPLICATION, TAILLE_MAX_APPLICATION

# Zones de mémoire vive admises pour le pointeur de pile initial
# (mêmes règles que app_image.c dans le chargeur).
RAM_PRINCIPALE = (0x20000000, 0x20020000)   # SRAM1 (96 Ko) + alias de la SRAM2
RAM_SRAM2 = (0x10000000, 0x10008000)        # SRAM2 (32 Ko)


class ErreurImage(Exception):
    """Le fichier ne peut pas être envoyé (le message explique pourquoi)."""


@dataclass
class Image:
    """Programme prêt à être envoyé."""
    donnees: bytes        # les octets, à écrire à partir de 'adresse'
    adresse: int          # adresse du premier octet (0x08080000 attendu)
    chemin: str           # fichier d'origine
    format: str           # "ELF", "HEX" ou "BIN"

    @property
    def taille(self) -> int:
        """Nombre d'octets du programme."""
        return len(self.donnees)

    @property
    def crc32(self) -> int:
        """CRC-32 de l'image, identique à celui que calcule l'ObCP."""
        return zlib.crc32(self.donnees) & 0xFFFFFFFF


# ---------------------------------------------------------------------------
# Lecture des différents formats
# Chaque lecteur renvoie une liste de « morceaux » (adresse, octets).
# ---------------------------------------------------------------------------

def lire_elf(contenu: bytes) -> list[tuple[int, bytes]]:
    """Extrait les segments à charger d'un fichier ELF 32 bits (ARM).

    Un fichier ELF décrit, dans ses « en-têtes de programme », les morceaux
    à copier en mémoire (type PT_LOAD). Pour chacun on utilise l'adresse
    PHYSIQUE (p_paddr) : c'est l'adresse en flash où il est rangé. Par
    exemple les variables initialisées (.data) vivent en RAM mais leurs
    valeurs de départ sont rangées en flash, juste après le code.
    """
    if contenu[:4] != b"\x7fELF":
        raise ErreurImage("ce n'est pas un fichier ELF")
    if contenu[4] != 1 or contenu[5] != 1:
        raise ErreurImage("fichier ELF qui n'est ni 32 bits ni petit-boutiste (pas pour STM32)")
    (_type, machine, _version, _entree, phoff, _shoff, _flags, _ehsize,
     phentsize, phnum, _shentsize, _shnum, _shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", contenu, 16)
    if machine != 40:                                     # 40 = EM_ARM
        raise ErreurImage("fichier ELF qui n'est pas pour un processeur ARM")

    morceaux = []
    for i in range(phnum):
        (p_type, p_offset, _p_vaddr, p_paddr, p_filesz, _p_memsz, _p_flags,
         _p_align) = struct.unpack_from("<IIIIIIII", contenu, phoff + i * phentsize)
        if p_type == 1 and p_filesz > 0:                  # 1 = PT_LOAD
            morceaux.append((p_paddr, contenu[p_offset:p_offset + p_filesz]))
    if not morceaux:
        raise ErreurImage("le fichier ELF ne contient aucun segment à charger")
    return morceaux


def lire_hex(texte: str) -> list[tuple[int, bytes]]:
    """Lit un fichier Intel HEX.

    Chaque ligne a la forme  :LLAAAATT[données]CC  en hexadécimal
      LL = nombre d'octets de données, AAAA = adresse (16 bits basse),
      TT = type (00 données, 01 fin, 04 partie haute de l'adresse),
      CC = somme de contrôle (la somme de tous les octets doit valoir 0).
    """
    morceaux = []
    base = 0                     # partie haute de l'adresse (enregistrement 04)
    for numero, ligne in enumerate(texte.splitlines(), start=1):
        ligne = ligne.strip()
        if not ligne:
            continue
        if not ligne.startswith(":"):
            raise ErreurImage("ligne %d du fichier HEX invalide" % numero)
        try:
            octets = bytes.fromhex(ligne[1:])
        except ValueError:
            raise ErreurImage("ligne %d du fichier HEX invalide" % numero) from None
        if len(octets) < 5 or len(octets) != octets[0] + 5:
            raise ErreurImage("ligne %d du fichier HEX : longueur incorrecte" % numero)
        if sum(octets) & 0xFF != 0:
            raise ErreurImage("ligne %d du fichier HEX : somme de contrôle fausse" % numero)
        longueur, adresse, type_enr = octets[0], (octets[1] << 8) | octets[2], octets[3]
        donnees = octets[4:4 + longueur]
        if type_enr == 0x00:                    # données
            morceaux.append((base + adresse, donnees))
        elif type_enr == 0x01:                  # fin de fichier
            break
        elif type_enr == 0x02:                  # adresse de segment (x 16)
            base = int.from_bytes(donnees, "big") << 4
        elif type_enr == 0x04:                  # partie haute de l'adresse
            base = int.from_bytes(donnees, "big") << 16
        # types 03 et 05 (adresse de démarrage) : sans intérêt ici
    if not morceaux:
        raise ErreurImage("le fichier HEX ne contient aucune donnée")
    return morceaux


def assembler(morceaux: list[tuple[int, bytes]]) -> tuple[int, bytes]:
    """Met les morceaux bout à bout. Renvoie (adresse de départ, octets).

    Entre deux sections, l'éditeur de liens laisse parfois quelques octets
    inutilisés (alignement). Le fichier .elf les contient (des 0), le .bin
    fabriqué par objcopy aussi, mais le .hex les saute : on remplit donc les
    trous par 0x00, pour que les trois fichiers donnent la même image (et le
    même CRC-32). Ces octets ne sont jamais lus par le programme."""
    debut = min(adresse for adresse, _ in morceaux)
    fin = max(adresse + len(octets) for adresse, octets in morceaux)
    if fin - debut > 16 * 1024 * 1024:
        raise ErreurImage(
            "les données du fichier s'étalent de 0x%08X à 0x%08X : ce n'est pas "
            "un programme pour la flash du STM32L476" % (debut, fin))
    image = bytearray(fin - debut)              # rempli de 0x00
    for adresse, octets in morceaux:
        image[adresse - debut:adresse - debut + len(octets)] = octets
    return debut, bytes(image)


def charger(chemin: str) -> Image:
    """Lit un fichier .elf, .hex ou .bin et renvoie l'image correspondante."""
    extension = os.path.splitext(chemin)[1].lower()
    try:
        with open(chemin, "rb") as f:
            contenu = f.read()
    except OSError as e:
        raise ErreurImage("impossible de lire %s : %s" % (chemin, e.strerror)) from None

    if extension == ".elf":
        adresse, donnees = assembler(lire_elf(contenu))
        return Image(donnees, adresse, chemin, "ELF")
    if extension in (".hex", ".ihex"):
        adresse, donnees = assembler(lire_hex(contenu.decode("ascii", errors="replace")))
        return Image(donnees, adresse, chemin, "HEX")
    if extension == ".bin":
        return Image(contenu, ADRESSE_APPLICATION, chemin, "BIN")
    raise ErreurImage("format de fichier inconnu (%s) : utilisez .elf, .hex ou .bin" % extension)


# ---------------------------------------------------------------------------
# Vérifications
# ---------------------------------------------------------------------------

def verifier(image: Image, taille_max: int = TAILLE_MAX_APPLICATION) -> None:
    """Lève ErreurImage si le programme ne peut pas fonctionner une fois
    chargé en banque 2. Les messages expliquent comment corriger."""
    if image.adresse == 0x08000000:
        raise ErreurImage(
            "le programme commence à l'adresse 0x08000000 : il est prévu pour être "
            "seul sur la carte (chargement par USB en mode DFU).\n"
            "Pour le chargement par Bluetooth, il doit commencer à 0x08080000 :\n"
            "  dans le fichier STM32L476RGTX_FLASH.ld du projet, remplacez la ligne\n"
            "    FLASH (rx) : ORIGIN = 0x8000000, LENGTH = 1024K\n"
            "  par\n"
            "    FLASH (rx) : ORIGIN = 0x8080000, LENGTH = 512K\n"
            "  puis recompilez (voir la documentation, chapitre 5).")
    if image.adresse != ADRESSE_APPLICATION:
        raise ErreurImage(
            "le programme commence à l'adresse 0x%08X au lieu de 0x%08X "
            "(vérifiez le script d'édition de liens .ld)" % (image.adresse, ADRESSE_APPLICATION))
    if image.taille < 8:
        raise ErreurImage("le programme est vide ou presque (%d octets)" % image.taille)
    if image.taille > taille_max:
        raise ErreurImage(
            "le programme fait %d octets, la zone application n'en contient que %d"
            % (image.taille, taille_max))

    # La table des vecteurs : pointeur de pile puis adresse de Reset_Handler.
    pile, reset = struct.unpack_from("<II", image.donnees, 0)
    pile_ok = (RAM_PRINCIPALE[0] < pile <= RAM_PRINCIPALE[1]
               or RAM_SRAM2[0] < pile <= RAM_SRAM2[1]) and pile % 4 == 0
    reset_ok = (image.adresse <= reset < image.adresse + image.taille) and (reset & 1) == 1
    if not pile_ok or not reset_ok:
        raise ErreurImage(
            "le début du fichier ne ressemble pas à une table des vecteurs de "
            "STM32 (pile 0x%08X, Reset_Handler 0x%08X).\n"
            "S'agit-il bien d'un programme compilé pour l'ObCP et lié à 0x08080000 ?"
            % (pile, reset))


# ---------------------------------------------------------------------------
# Projet STM32CubeIDE : trouver le .elf le plus récent
# ---------------------------------------------------------------------------

EXTENSIONS_SOURCES = (".c", ".s", ".S", ".ld")


def trouver_elf(dossier_projet: str) -> str:
    """Cherche le fichier .elf le plus récent dans les dossiers de compilation
    du projet (Debug, Release ou tout autre sous-dossier)."""
    candidats = []
    for nom in os.listdir(dossier_projet):
        sous_dossier = os.path.join(dossier_projet, nom)
        if os.path.isdir(sous_dossier) and not nom.startswith("."):
            for fichier in os.listdir(sous_dossier):
                if fichier.lower().endswith(".elf"):
                    candidats.append(os.path.join(sous_dossier, fichier))
    if not candidats:
        raise ErreurImage(
            "aucun fichier .elf dans le projet %s : compilez-le d'abord (marteau "
            "de STM32CubeIDE)" % os.path.basename(os.path.normpath(dossier_projet)))
    return max(candidats, key=os.path.getmtime)


def source_plus_recente(dossier_projet: str, elf: str) -> str | None:
    """Renvoie un fichier source modifié APRÈS la création du .elf (la
    dernière compilation a donc échoué ou n'a pas été lancée), ou None."""
    date_elf = os.path.getmtime(elf)
    dossiers_compilation = {os.path.dirname(os.path.abspath(elf))}
    for racine, dossiers, fichiers in os.walk(dossier_projet):
        # on ignore les dossiers cachés et les dossiers de compilation
        dossiers[:] = [d for d in dossiers if not d.startswith(".")
                       and os.path.join(racine, d) not in dossiers_compilation
                       and not any(f.lower().endswith(".elf")
                                   for f in os.listdir(os.path.join(racine, d)))]
        for fichier in fichiers:
            if fichier.endswith(EXTENSIONS_SOURCES):
                chemin = os.path.join(racine, fichier)
                if os.path.getmtime(chemin) > date_elf + 1.0:   # 1 s de marge
                    return chemin
    return None


def decrire_date(chemin: str) -> str:
    """Date de modification d'un fichier, lisible (pour les messages)."""
    return time.strftime("%d/%m/%Y %H:%M:%S", time.localtime(os.path.getmtime(chemin)))
