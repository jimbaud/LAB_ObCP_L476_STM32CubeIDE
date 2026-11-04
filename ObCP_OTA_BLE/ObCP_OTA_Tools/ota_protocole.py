# -*- coding: utf-8 -*-
"""
ota_protocole.py - Le « contrat » entre l'application PC et le chargeur BLE.

Ce fichier est le jumeau Python de ota_protocol.h (côté ObCP) : mêmes UUID,
mêmes codes de commande, mêmes tailles. Si l'on modifie l'un, il faut
modifier l'autre ; le test Tests/test_outil_pc.py vérifie qu'ils restent
d'accord.

Rappel du déroulement d'un transfert (détails dans ota_protocol.h) :

    PC                                   ObCP (chargeur)
    lit INFO                   ---->     version, taille maximale...
    CTRL : START(taille, crc)  ---->     efface la flash
                               <----     READY
    DATA : [n°][19 octets] x N ---->     écrit en flash
                               <----     ACK tous les 1024 octets
    CTRL : END                 ---->     vérifie le CRC, valide le programme
                               <----     DONE(crc)
    CTRL : RUN                 ---->     redémarre sur le nouveau programme
"""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass
from typing import Optional

# ---------------------------------------------------------------------------
# Identifiants Bluetooth (UUID de 128 bits)
# ---------------------------------------------------------------------------
UUID_SERVICE = "9e090001-33d3-4337-976a-6f024ca6b676"
UUID_CTRL = "9e090002-33d3-4337-976a-6f024ca6b676"   # commandes et réponses
UUID_DATA = "9e090003-33d3-4337-976a-6f024ca6b676"   # morceaux du programme
UUID_INFO = "9e090004-33d3-4337-976a-6f024ca6b676"   # renseignements (lecture)

# Nom annoncé par le chargeur : "ObCP-OTA-" suivi de 4 chiffres hexadécimaux.
PREFIXE_NOM = "ObCP-OTA-"

# ---------------------------------------------------------------------------
# Le programme de la carte, quand elle fonctionne normalement
# ---------------------------------------------------------------------------
# Le programme modèle de l'ObCP (OBCP_template_2026, version 2.1 et suivantes)
# publie un service « UART » : la commande « #ota », écrite dans sa
# caractéristique RX comme on la taperait dans un terminal, le fait redémarrer
# dans le chargeur. L'application PC s'en sert pour une carte qu'elle connaît.
UUID_RX_PROGRAMME = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
COMMANDE_OTA = b"#ota"

VERSION_PROTOCOLE = 1

# ---------------------------------------------------------------------------
# Commandes (écrites par le PC dans CTRL)
# ---------------------------------------------------------------------------
CMD_START = 0x01      # + taille sur 4 octets + CRC-32 sur 4 octets
CMD_END = 0x02        # toutes les données sont envoyées
CMD_ABORT = 0x03      # abandon du transfert
CMD_RUN = 0x04        # redémarrer sur l'application
CMD_IDENTIFY = 0x05   # faire clignoter la LED en blanc

# ---------------------------------------------------------------------------
# Réponses (notifications du chargeur sur CTRL) : [code][valeur sur 4 octets]
# ---------------------------------------------------------------------------
RSP_READY = 0x10      # valeur = taille acceptée
RSP_ACK = 0x11        # valeur = nombre d'octets reçus
RSP_DONE = 0x12       # valeur = CRC-32 calculé par l'ObCP

ERR_STATE = 0x20
ERR_SIZE = 0x21
ERR_FLASH = 0x22
ERR_SEQUENCE = 0x23
ERR_OVERFLOW = 0x24
ERR_CRC = 0x25
ERR_INCOMPLETE = 0x26
ERR_COMMAND = 0x27
ERR_IMAGE = 0x28

# Explication de chaque erreur, en français, pour l'utilisateur.
MESSAGES_ERREUR = {
    ERR_STATE: "commande inattendue à ce moment du transfert",
    ERR_SIZE: "taille de programme refusée par la carte",
    ERR_FLASH: "échec d'effacement ou d'écriture de la mémoire flash",
    ERR_SEQUENCE: "un paquet de données a été perdu en route",
    ERR_OVERFLOW: "la carte a reçu plus de données que prévu",
    ERR_CRC: "le programme reçu par la carte est différent de celui envoyé (CRC)",
    ERR_INCOMPLETE: "fin de transfert annoncée alors qu'il manque des données",
    ERR_COMMAND: "message mal formé",
    ERR_IMAGE: "le programme n'est pas prévu pour l'adresse 0x08080000",
}

# ---------------------------------------------------------------------------
# Tailles
# ---------------------------------------------------------------------------
LONGUEUR_REPONSE = 5          # octets d'une notification CTRL
LONGUEUR_INFO = 18            # octets de la caractéristique INFO (chargeur 1.1)
LONGUEUR_INFO_MIN = 16        # un chargeur 1.0 n'envoie que les 16 premiers
POSITION_CAUSE_DECONNEXION = 16     # octets de diagnostic dans INFO
POSITION_REDEMARRAGE_MODULE = 17
OCTETS_PAR_PAQUET = 19        # octets de programme dans une écriture DATA
TAILLE_BLOC = 1024            # un accusé de réception tous les 1024 octets
TAILLE_PAGE_FLASH = 2048      # plus petite zone effaçable du STM32L476

# Zone réservée à l'application (banque 2 de la flash)
ADRESSE_APPLICATION = 0x08080000
TAILLE_MAX_APPLICATION = 512 * 1024

# Raisons du démarrage dans le chargeur (octet 15 de INFO)
RAISONS_DEMARRAGE = {
    0: "inconnue",
    1: "aucune application valide installée",
    2: "bouton USER maintenu pendant le démarrage",
    3: "demande de l'application (commande #ota...)",
}

# Diagnostic (chargeur 1.1). Octet 16 de INFO : cause de la dernière
# déconnexion, sous forme de code d'erreur de la norme Bluetooth.
CAUSES_DECONNEXION = {
    0x08: "plus aucun échange radio : carte trop loin, obstacle, perturbation, "
          "ou carte qui a redémarré",
    0x13: "déconnexion demandée par le PC",
    0x16: "déconnexion demandée par la carte",
    0x22: "le PC n'a pas répondu à une procédure de la liaison radio",
    0x28: "changement des paramètres de connexion manqué",
    0x3B: "paramètres de connexion refusés",
    0x3D: "paquet radio abîmé (contrôle d'intégrité)",
    0x3E: "connexion jamais complètement établie",
}

# Octet 17 de INFO : cause du dernier redémarrage imprévu du module BLE
# (code de l'événement « HAL initialized », manuel ST PM0237).
CAUSES_REDEMARRAGE_MODULE = {
    1: "broche RESET du module",
    2: "mode de mise à jour du module",
    3: "mode de mise à jour du module",
    4: "mode de mise à jour du module",
    5: "chien de garde du module",
    6: "blocage du processeur du module",
    7: "baisse de la tension d'alimentation",
    8: "plantage du module",
    9: "erreur dans la mémoire du module",
}


# ---------------------------------------------------------------------------
# Fabrication et lecture des messages
# ---------------------------------------------------------------------------
# struct.pack transforme des nombres en octets selon un « format » :
#   "<"  petit-boutiste (octet de poids faible en premier, comme le Cortex-M4)
#   "B"  un octet non signé, "H" deux octets, "I" quatre octets.

def commande_start(taille: int, crc: int) -> bytes:
    """Message START : code, taille du programme, CRC-32 attendu."""
    return struct.pack("<BII", CMD_START, taille, crc)


def commande_simple(code: int) -> bytes:
    """Message d'un seul octet (END, ABORT, RUN, IDENTIFY)."""
    return bytes([code])


def paquet_donnees(numero: int, morceau: bytes) -> bytes:
    """Paquet DATA : numéro de séquence (0 à 255) suivi de 1 à 19 octets."""
    return bytes([numero & 0xFF]) + morceau


def decoder_reponse(octets: bytes) -> tuple[int, int]:
    """Notification CTRL -> (code, valeur)."""
    if len(octets) != LONGUEUR_REPONSE:
        raise ValueError("réponse de %d octets au lieu de %d" % (len(octets), LONGUEUR_REPONSE))
    code, valeur = struct.unpack("<BI", bytes(octets))
    return code, valeur


def nom_reponse(code: int) -> str:
    """Nom lisible d'un code de réponse, pour les messages."""
    noms = {RSP_READY: "READY", RSP_ACK: "ACK", RSP_DONE: "DONE"}
    if code in noms:
        return noms[code]
    if code in MESSAGES_ERREUR:
        return "erreur 0x%02X (%s)" % (code, MESSAGES_ERREUR[code])
    return "code inconnu 0x%02X" % code


@dataclass
class InfoChargeur:
    """Contenu de la caractéristique INFO, décodé. Les deux derniers champs
    valent None avec un chargeur 1.0, qui ne fournit pas le diagnostic."""
    version_protocole: int
    version_chargeur: str
    application_presente: bool
    adresse_application: int
    taille_max: int
    taille_bloc: int
    octets_par_paquet: int
    raison_demarrage: str
    cause_deconnexion: Optional[int] = None
    redemarrage_module: Optional[int] = None


def decoder_info(octets: bytes) -> InfoChargeur:
    """16 ou 18 octets de INFO -> InfoChargeur (voir ota_protocol.h)."""
    if len(octets) < LONGUEUR_INFO_MIN:
        raise ValueError("INFO trop courte (%d octets)" % len(octets))
    (protocole, majeure, mineure, presente, adresse, taille_max,
     bloc, par_paquet, raison) = struct.unpack("<BBBBIIHBB", bytes(octets[:LONGUEUR_INFO_MIN]))
    diagnostic = len(octets) >= LONGUEUR_INFO
    return InfoChargeur(
        version_protocole=protocole,
        version_chargeur="%d.%d" % (majeure, mineure),
        application_presente=bool(presente),
        adresse_application=adresse,
        taille_max=taille_max,
        taille_bloc=bloc,
        octets_par_paquet=par_paquet,
        raison_demarrage=RAISONS_DEMARRAGE.get(raison, "code %d" % raison),
        cause_deconnexion=octets[POSITION_CAUSE_DECONNEXION] if diagnostic else None,
        redemarrage_module=octets[POSITION_REDEMARRAGE_MODULE] if diagnostic else None,
    )


def decrire_cause_deconnexion(code: int) -> str:
    """Texte lisible pour l'octet 16 de INFO."""
    return "%s (code 0x%02X)" % (CAUSES_DECONNEXION.get(code, "cause inconnue"), code)


def decrire_redemarrage_module(code: int) -> str:
    """Texte lisible pour l'octet 17 de INFO."""
    return "%s (code %d)" % (CAUSES_REDEMARRAGE_MODULE.get(code, "cause inconnue"), code)


def adresse_du_programme(adresse_chargeur: Optional[str]) -> Optional[str]:
    """Adresse Bluetooth du programme d'une carte, d'après celle de son chargeur.

    Le chargeur prend l'adresse de la carte avec le bit 0 du premier octet
    affiché inversé (ble_ota.c : loader_addr[5] ^= 0x01U) : C0:22:33:44:A1:B2
    pour le chargeur d'une carte dont le programme annonce C1:22:33:44:A1:B2.
    Rend None si l'adresse n'a pas cette forme (sous macOS, Bleak ne donne pas
    les adresses Bluetooth, seulement des identifiants propres au Mac)."""
    if not adresse_chargeur or not re.fullmatch(r"([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", adresse_chargeur):
        return None
    return "%02X%s" % (int(adresse_chargeur[:2], 16) ^ 0x01, adresse_chargeur[2:].upper())
