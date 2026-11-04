# -*- coding: utf-8 -*-
"""
capturer_session.py - Enregistre une vraie session de obcp_ota.py 1.2 face à
la carte SIMULÉE des tests, pour l'animation session-obcp-ota.gif du README.

    python capturer_session.py          (Linux, après ../../../Tests/run_tests.sh)

Rien n'est inventé dans les textes : c'est obcp_ota.py qui écrit, et c'est le
vrai code du chargeur (ota.c, compilé pour le PC dans Tests/build/sim) qui
répond. Seuls changent : le chemin du fichier (affiché comme sous Windows) et
la durée des opérations Bluetooth, réglée sur celles d'une vraie ObCP
(connexion en 1 s environ, effacement de la flash, 6,8 Ko/s mesurés lors du
premier essai sur carte). Scénario : la carte fait tourner le programme modèle
2026 (2.1.0), le PC la connaît déjà ; l'application PC lui envoie #ota, puis
le nouveau programme.

Sortie : session.json, que lit animer_session.py.
Le programme envoyé est OBCP_template_2026_OTA/BIN/OBCP_template_2026.elf du
dépôt (s'il manque : Binaires/ObCP_OTA_Demo_v2.elf).
"""
import asyncio
import io
import json
import os
import shutil
import struct
import sys
import tempfile
import time
import types
from unittest import mock

ICI = os.path.dirname(os.path.abspath(__file__))
PROJET = os.path.normpath(os.path.join(ICI, "..", "..", ".."))          # dossier ObCP_OTA_BLE
DEPOT = os.path.dirname(PROJET)
sys.path.insert(0, os.path.join(PROJET, "Tests"))
import test_outil_pc as T                                                # noqa: E402

obcp_ota, P, LIB = T.obcp_ota, T.P, T.LIB

PROGRAMME = os.path.join(DEPOT, "OBCP_template_2026_OTA", "BIN", "OBCP_template_2026.elf")
if not os.path.exists(PROGRAMME):
    PROGRAMME = os.path.join(PROJET, "Binaires", "ObCP_OTA_Demo_v2.elf")
CHEMIN_WINDOWS = r"C:\ObCP\OBCP_template_2026_OTA\Debug\OBCP_template_2026.elf"
COMMANDE = r"python ObCP_OTA_BLE\ObCP_OTA_Tools\obcp_ota.py OBCP_template_2026_OTA"
CARTE, ADRESSE_CHARGEUR, ADRESSE_PROGRAMME = "ObCP-OTA-A1B2", "C0:B7:00:00:A1:B2", "C1:B7:00:00:A1:B2"

# Durées d'une vraie carte (secondes)
ANNONCE_PROGRAMME = 0.9      # le programme qui fonctionne est vu par le PC
ANNONCE_CHARGEUR = 1.6       # après #ota : redémarrage, puis annonces du chargeur
CONNEXION = 1.05
LECTURE = 0.12
ECRITURE_OTA = 0.25
EFFACEMENT_PAR_PAGE = 0.023  # STM32L4 : environ 22 ms par page de 2 Ko
DEBIT = 6.8 * 1024           # octets par seconde (premier essai sur carte, Windows)

DEPART = None
evenements = []              # [t, "texte", chaîne] ou [t, "etat", nom]


def noter(genre, valeur):
    evenements.append([round(time.monotonic() - DEPART, 3), genre, valeur])


class Sortie(io.TextIOBase):
    """Remplace sys.stdout : note chaque écriture avec son heure. isatty()
    vaut True pour que la barre de progression se réécrive sur place."""
    def write(self, texte):
        if texte:
            noter("texte", texte)
        return len(texte)

    def isatty(self):
        return True


class Scanner(T.FauxScanner):
    async def __aenter__(self):
        boucle = asyncio.get_running_loop()
        for nom, rssi in Scanner.cartes:
            boucle.call_later(ANNONCE_CHARGEUR, self.rappel, T.FauxAppareil(nom), T.FausseAnnonce(nom, rssi))
        for adresse, nom, rssi in Scanner.programmes:
            boucle.call_later(ANNONCE_PROGRAMME, self.rappel, T.FauxAppareilProgramme(adresse, nom),
                              T.FausseAnnonce(nom, rssi))
        return self


class Client(T.FauxClient):
    bloc = 0

    async def connect(self):
        await asyncio.sleep(CONNEXION)
        await super().connect()
        if not self._programme():
            noter("etat", "connecte")

    async def read_gatt_char(self, uuid):
        await asyncio.sleep(LECTURE)
        info = __import__("ctypes").create_string_buffer(P.LONGUEUR_INFO)
        LIB.ota_build_info(info, 3)          # raison 3 : demande de l'application (#ota)
        return bytearray(info.raw)

    async def write_gatt_char(self, uuid, donnees, response=None):
        donnees = bytes(donnees)
        if self._programme():
            await asyncio.sleep(ECRITURE_OTA)
            await super().write_gatt_char(uuid, donnees, response)
            noter("etat", "redemarrage")
            # le programme s'arrête ; le chargeur démarre et clignote en bleu
            asyncio.get_running_loop().call_later(0.35, noter, "etat", "attente")
            Scanner.cartes, Scanner.programmes = [(CARTE, -55)], []
            return
        if uuid == P.UUID_CTRL and donnees[0] == P.CMD_START:
            taille = struct.unpack_from("<I", donnees, 1)[0]
            await asyncio.sleep(-(-taille // 2048) * EFFACEMENT_PAR_PAGE)
        if uuid == P.UUID_DATA and T.FauxClient.paquets == 0:
            noter("etat", "transfert")
        await super().write_gatt_char(uuid, donnees, response)
        if uuid == P.UUID_DATA and T.FauxClient.paquets % 54 == 0:
            # fin d'un bloc de 1 Ko : durée de son envoi par Bluetooth
            await asyncio.sleep(1024 / DEBIT)
            noter("etat", "ko")              # la LED verte change à chaque Ko reçu
        if uuid == P.UUID_CTRL and donnees[0] == P.CMD_END:
            noter("etat", "valide")
        if uuid == P.UUID_CTRL and donnees[0] == P.CMD_RUN:
            asyncio.get_running_loop().call_later(0.45, noter, "etat", "nouveau")


def preparer_projet(dossier):
    """Projet STM32CubeIDE minimal : Debug/OBCP_template_2026.elf compilé
    après le dernier fichier source modifié."""
    debug = os.path.join(dossier, "Debug")
    src = os.path.join(dossier, "Core", "Src")
    os.makedirs(debug)
    os.makedirs(src)
    elf = os.path.join(debug, "OBCP_template_2026.elf")
    shutil.copy(PROGRAMME, elf)
    with open(os.path.join(src, "main.c"), "w") as f:
        f.write("/* main.c */\n")
    compile_le = time.mktime((2026, 11, 4, 16, 42, 7, 0, 0, -1))
    os.utime(os.path.join(src, "main.c"), (compile_le - 60, compile_le - 60))
    os.utime(elf, (compile_le, compile_le))
    return elf


def main():
    global DEPART
    travail = tempfile.mkdtemp()
    projet = os.path.join(travail, "OBCP_template_2026_OTA")
    elf = preparer_projet(projet)
    memoire = os.path.join(travail, "memoire.json")
    correctifs = [
        mock.patch.dict(sys.modules, {"bleak": types.SimpleNamespace(BleakScanner=Scanner, BleakClient=Client)}),
        mock.patch.object(obcp_ota, "FICHIER_MEMOIRE", memoire),
    ]
    for c in correctifs:
        c.start()
    try:
        # 1. La carte a déjà reçu une version du programme : envoi silencieux,
        #    simulation instantanée (sans les durées réelles).
        LIB.sim_flash_reset(0xFF)
        Scanner.cartes, Scanner.programmes = [(CARTE, -55)], []
        T.FauxClient.paquets = 0
        with mock.patch.object(Client, "write_gatt_char", T.FauxClient.write_gatt_char), \
                mock.patch.object(Client, "connect", T.FauxClient.connect), \
                mock.patch("sys.stdout", io.StringIO()):
            assert obcp_ota.main([elf]) == 0
        LIB.ota_take_run_request()
        obcp_ota.memoriser_carte(CARTE, ADRESSE_CHARGEUR)

        # 2. Session enregistrée : la carte fonctionne (programme modèle)
        Scanner.cartes = []
        Scanner.programmes = [(ADRESSE_PROGRAMME, "OBCP-CROC", -52)]
        T.FauxClient.paquets = 0
        T.FauxClient.connexions, T.FauxClient.connexions_programme, T.FauxClient.commandes_programme = [], [], []
        DEPART = time.monotonic()
        with mock.patch("sys.stdout", Sortie()):
            code = obcp_ota.main([projet])
        noter("fin", code)
    finally:
        for c in correctifs:
            c.stop()
        shutil.rmtree(travail, ignore_errors=True)

    # chemin affiché comme sous Windows
    for e in evenements:
        if e[1] == "texte":
            e[2] = e[2].replace(elf, CHEMIN_WINDOWS)
    if code != 0:
        sys.exit("la session a échoué :\n" + "".join(e[2] for e in evenements if e[1] == "texte"))
    with open(os.path.join(ICI, "session.json"), "w", encoding="utf-8") as f:
        json.dump({"commande": COMMANDE, "invite": "C:\\ObCP>", "evenements": evenements}, f,
                  ensure_ascii=False, indent=0)
    print("".join(e[2] for e in evenements if e[1] == "texte").replace("\r", "\n"))
    print("%d événements, %.1f s" % (len(evenements), evenements[-1][0]))


if __name__ == "__main__":
    main()
