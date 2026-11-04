# -*- coding: utf-8 -*-
"""
Tests de l'application PC (dossier ObCP_OTA_Tools), sans carte ni Bluetooth.

  1. Lecture des fichiers .elf / .hex / .bin produits par la compilation des
     projets (compilés par run_tests.sh avec build_arm.py ; à défaut, les
     programmes du dossier Binaires sont utilisés).
  2. Cohérence entre le C (ota_protocol.h, loader_config.h,
     obcp_ota_request.h, scripts .ld) et le Python (ota_protocole.py).
  3. Simulation de bout en bout : l'algorithme de transfert Python
     (ota_transfert.py) dialogue avec le VRAI code C du chargeur (ota.c,
     compilé sur le PC dans build/sim/libota_sim.so avec une flash simulée),
     y compris avec des pannes injectées.

Lancement : ./run_tests.sh   (ou python3 -m unittest -v test_outil_pc)
"""

import asyncio
import contextlib
import ctypes
import io
import json
import os
import random
import re
import shutil
import struct
import sys
import tempfile
import time
import unittest
import zlib

ICI = os.path.dirname(os.path.abspath(__file__))
RACINE = os.path.dirname(ICI)
OUTILS = os.path.join(RACINE, "ObCP_OTA_Tools")
LOADER = os.path.join(RACINE, "ObCP_BLE_Loader")
DEMO = os.path.join(RACINE, "ObCP_OTA_Demo")
BUILD = os.path.join(ICI, "build")
sys.path.insert(0, OUTILS)

import obcp_ota                                    # noqa: E402
import ota_image                                   # noqa: E402
import ota_protocole as P                          # noqa: E402
import ota_transfert                               # noqa: E402

def programme_compile(projet, nom_dans_binaires):
    """Fichier .elf d'un projet : celui compilé dans Tests/build (Debug) s'il
    existe, sinon celui livré dans le dossier Binaires (Release)."""
    chemin = os.path.join(BUILD, projet, "Debug", projet + ".elf")
    if os.path.exists(chemin):
        return chemin
    return os.path.join(RACINE, "Binaires", nom_dans_binaires)


DEMO_ELF = programme_compile("ObCP_OTA_Demo", "ObCP_OTA_Demo_v1.elf")
LOADER_ELF = programme_compile("ObCP_BLE_Loader", "ObCP_BLE_Loader.elf")


# ===========================================================================
# 1. Lecture des fichiers
# ===========================================================================
class TestImage(unittest.TestCase):

    def test_elf_hex_bin_identiques(self):
        """Les trois formats produits par la compilation donnent la même image."""
        elf = ota_image.charger(DEMO_ELF)
        hexa = ota_image.charger(DEMO_ELF[:-4] + ".hex")
        self.assertEqual(elf.adresse, 0x08080000)
        self.assertEqual(hexa.adresse, 0x08080000)
        self.assertEqual(elf.donnees, hexa.donnees)
        self.assertEqual(elf.crc32, zlib.crc32(elf.donnees))
        if os.path.exists(DEMO_ELF[:-4] + ".bin"):  # pas de .bin dans Binaires
            binaire = ota_image.charger(DEMO_ELF[:-4] + ".bin")
            self.assertEqual(elf.donnees, binaire.donnees)
        ota_image.verifier(elf)                     # ne lève rien

    def test_programme_lie_en_0x08000000_refuse(self):
        """Le chargeur lui-même (lié en 0x08000000) est refusé avec une explication."""
        image = ota_image.charger(LOADER_ELF)
        self.assertEqual(image.adresse, 0x08000000)
        with self.assertRaises(ota_image.ErreurImage) as ctx:
            ota_image.verifier(image)
        self.assertIn("STM32L476RGTX_FLASH.ld", str(ctx.exception))
        self.assertIn("0x8080000", str(ctx.exception))

    def test_trop_gros_et_trop_petit(self):
        image = ota_image.charger(DEMO_ELF)
        gros = ota_image.Image(image.donnees + b"\0" * (512 * 1024), image.adresse, "x", "ELF")
        with self.assertRaises(ota_image.ErreurImage):
            ota_image.verifier(gros)
        with self.assertRaises(ota_image.ErreurImage):
            ota_image.verifier(ota_image.Image(b"\0" * 4, 0x08080000, "x", "BIN"))

    def test_table_des_vecteurs_invalide(self):
        image = ota_image.charger(DEMO_ELF)
        abime = ota_image.Image(b"\xff" * 8 + image.donnees[8:], image.adresse, "x", "BIN")
        with self.assertRaises(ota_image.ErreurImage) as ctx:
            ota_image.verifier(abime)
        self.assertIn("table des vecteurs", str(ctx.exception))

    def test_hex_somme_de_controle_fausse(self):
        with open(DEMO_ELF[:-4] + ".hex") as f:
            lignes = f.read().splitlines()
        ligne = lignes[3]
        lignes[3] = ligne[:-2] + ("%02X" % ((int(ligne[-2:], 16) + 1) & 0xFF))
        with self.assertRaises(ota_image.ErreurImage):
            ota_image.lire_hex("\n".join(lignes))

    def test_hex_trous_remplis_par_00(self):
        """Trous du .hex (alignement entre sections) remplis par 0x00, comme
        dans le .elf et le .bin : les trois formats donnent le même CRC."""
        def enr(adresse, type_enr, donnees):        # un enregistrement avec sa somme
            octets = bytes([len(donnees), adresse >> 8, adresse & 0xFF, type_enr]) + donnees
            return ":" + (octets + bytes([(-sum(octets)) & 0xFF])).hex().upper()
        texte = "\n".join([enr(0, 4, b"\x08\x08"), enr(0x0000, 0, b"\x01\x02\x03\x04"),
                           enr(0x0010, 0, b"\xaa\xbb\xcc\xdd"), enr(0, 1, b"")])
        adresse, donnees = ota_image.assembler(ota_image.lire_hex(texte))
        self.assertEqual(adresse, 0x08080000)
        self.assertEqual(donnees, b"\x01\x02\x03\x04" + b"\x00" * 12 + b"\xaa\xbb\xcc\xdd")

    def test_projet_elf_le_plus_recent_et_fraicheur(self):
        with tempfile.TemporaryDirectory() as projet:
            os.makedirs(os.path.join(projet, "Core", "Src"))
            os.makedirs(os.path.join(projet, "Debug"))
            os.makedirs(os.path.join(projet, "Release"))
            main_c = os.path.join(projet, "Core", "Src", "main.c")
            open(main_c, "w").close()
            ancien = os.path.join(projet, "Debug", "p.elf")
            recent = os.path.join(projet, "Release", "p.elf")
            shutil.copy(DEMO_ELF, ancien)
            shutil.copy(DEMO_ELF, recent)
            t = time.time()
            os.utime(main_c, (t - 100, t - 100))
            os.utime(ancien, (t - 50, t - 50))
            os.utime(recent, (t - 10, t - 10))
            self.assertEqual(ota_image.trouver_elf(projet), recent)
            self.assertIsNone(ota_image.source_plus_recente(projet, recent))
            os.utime(main_c, (t, t))                    # main.c modifié après la compilation
            self.assertEqual(ota_image.source_plus_recente(projet, recent), main_c)


# ===========================================================================
# 2. Cohérence C <-> Python
# ===========================================================================
def lire_defines(*chemins):
    """Lit les #define NOM VALEUR (numériques ou chaînes) des fichiers C."""
    valeurs = {}
    for chemin in chemins:
        with open(chemin, encoding="utf-8") as f:
            for nom, valeur in re.findall(r"^#define\s+(\w+)\s+(.+?)\s*(?:/\*.*)?$", f.read(), re.M):
                valeurs[nom] = valeur
    return valeurs


def valeur_c(texte):
    texte = texte.strip()
    if texte.startswith('"'):
        return texte.strip('"')
    m = re.fullmatch(r"\(?\s*(\d+)UL?\s*\*\s*(\d+)UL?\s*\)?", texte)
    if m:
        return int(m.group(1)) * int(m.group(2))
    return int(re.sub(r"[UL]+$", "", texte), 0)


class TestInfo(unittest.TestCase):
    """Décodage de la caractéristique INFO (chargeurs 1.0 et 1.1)."""

    def test_info_1_0_et_1_1(self):
        base = struct.pack("<BBBBIIHBB", 1, 1, 0, 1, 0x08080000, 512 * 1024, 1024, 19, 2)
        ancienne = P.decoder_info(base)
        self.assertEqual(ancienne.version_chargeur, "1.0")
        self.assertTrue(ancienne.application_presente)
        self.assertIsNone(ancienne.cause_deconnexion)
        self.assertIsNone(ancienne.redemarrage_module)
        nouvelle = P.decoder_info(base + bytes([0x08, 8]))
        self.assertEqual((nouvelle.cause_deconnexion, nouvelle.redemarrage_module), (0x08, 8))
        self.assertEqual(nouvelle.raison_demarrage, "bouton USER maintenu pendant le démarrage")
        with self.assertRaises(ValueError):
            P.decoder_info(base[:15])

    def test_textes_du_diagnostic(self):
        self.assertIn("plus aucun échange radio", P.decrire_cause_deconnexion(0x08))
        self.assertIn("0x08", P.decrire_cause_deconnexion(0x08))
        self.assertIn("demandée par le PC", P.decrire_cause_deconnexion(0x13))
        self.assertIn("cause inconnue (code 0x99)", P.decrire_cause_deconnexion(0x99))
        self.assertIn("plantage", P.decrire_redemarrage_module(8))


class TestCoherence(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        inc = os.path.join(LOADER, "Loader", "Inc")
        cls.c = lire_defines(os.path.join(inc, "ota_protocol.h"), os.path.join(inc, "loader_config.h"))
        cls.app = lire_defines(os.path.join(DEMO, "Core", "Inc", "obcp_ota_request.h"))

    def test_uuid(self):
        with open(os.path.join(LOADER, "Loader", "Inc", "ota_protocol.h"), encoding="utf-8") as f:
            octets = re.search(r"OTA_UUID_BYTES\(n\)\s*\{(.*?)\}", f.read(), re.S).group(1)
        valeurs = [v.strip() for v in octets.replace("\\", "").split(",")]
        for n, uuid_py in ((1, P.UUID_SERVICE), (2, P.UUID_CTRL), (3, P.UUID_DATA), (4, P.UUID_INFO)):
            b = bytes(int(v, 16) if v != "(n)" else n for v in valeurs)[::-1]   # petit-boutiste
            h = b.hex()
            self.assertEqual("%s-%s-%s-%s-%s" % (h[:8], h[8:12], h[12:16], h[16:20], h[20:]), uuid_py)

    def test_codes_et_tailles(self):
        paires = {
            "OTA_PROTOCOL_VERSION": P.VERSION_PROTOCOLE,
            "OTA_CMD_START": P.CMD_START, "OTA_CMD_END": P.CMD_END, "OTA_CMD_ABORT": P.CMD_ABORT,
            "OTA_CMD_RUN": P.CMD_RUN, "OTA_CMD_IDENTIFY": P.CMD_IDENTIFY,
            "OTA_RSP_READY": P.RSP_READY, "OTA_RSP_ACK": P.RSP_ACK, "OTA_RSP_DONE": P.RSP_DONE,
            "OTA_ERR_STATE": P.ERR_STATE, "OTA_ERR_SIZE": P.ERR_SIZE, "OTA_ERR_FLASH": P.ERR_FLASH,
            "OTA_ERR_SEQUENCE": P.ERR_SEQUENCE, "OTA_ERR_OVERFLOW": P.ERR_OVERFLOW,
            "OTA_ERR_CRC": P.ERR_CRC, "OTA_ERR_INCOMPLETE": P.ERR_INCOMPLETE,
            "OTA_ERR_COMMAND": P.ERR_COMMAND, "OTA_ERR_IMAGE": P.ERR_IMAGE,
            "OTA_RSP_LEN": P.LONGUEUR_REPONSE, "OTA_INFO_LEN": P.LONGUEUR_INFO,
            "OTA_INFO_DISCONNECT_REASON": P.POSITION_CAUSE_DECONNEXION,
            "OTA_INFO_MODULE_RESTART": P.POSITION_REDEMARRAGE_MODULE,
            "OTA_DATA_PAYLOAD": P.OCTETS_PAR_PAQUET, "OTA_BLOCK_SIZE": P.TAILLE_BLOC,
            "OTA_START_LEN": len(P.commande_start(0, 0)),
            "OTA_DATA_MAX_LEN": P.OCTETS_PAR_PAQUET + 1,
            "APP_ADDRESS": P.ADRESSE_APPLICATION, "APP_MAX_SIZE": P.TAILLE_MAX_APPLICATION,
            "LOADER_FLASH_PAGE_SIZE": P.TAILLE_PAGE_FLASH, "LOADER_NAME_PREFIX": P.PREFIXE_NOM,
        }
        for nom, attendu in paires.items():
            self.assertEqual(valeur_c(self.c[nom]), attendu, nom)
        erreurs = [valeur_c(v) for n, v in self.c.items() if n.startswith("OTA_ERR_")]
        self.assertEqual(sorted(erreurs), sorted(P.MESSAGES_ERREUR))

    def test_boite_aux_lettres(self):
        self.assertEqual(valeur_c(self.app["OBCP_OTA_REQUEST_ADDRESS"]), valeur_c(self.c["OTA_REQUEST_ADDRESS"]))
        self.assertEqual(valeur_c(self.app["OBCP_OTA_REQUEST_MAGIC"]), valeur_c(self.c["OTA_REQUEST_MAGIC"]))

    def test_adresse_du_programme(self):
        """Le chargeur inverse le bit 0 du premier octet affiché de l'adresse
        de la carte (ble_ota.c) ; l'application PC fait le calcul inverse."""
        with open(os.path.join(LOADER, "Loader", "Src", "ble_ota.c"), encoding="utf-8") as f:
            self.assertIn("loader_addr[5] ^= 0x01U;", f.read())
        self.assertEqual(P.adresse_du_programme("C0:B7:00:00:BE:BB"), "C1:B7:00:00:BE:BB")
        self.assertEqual(P.adresse_du_programme("c1:22:33:44:a1:b2"), "C0:22:33:44:A1:B2")
        for invalide in (None, "", "C0:B7:00:00:BE", "3A0F2C1D-1234-5678-9ABC-DEF012345678"):
            self.assertIsNone(P.adresse_du_programme(invalide))

    def test_scripts_edition_de_liens(self):
        def zone_flash(projet):
            with open(os.path.join(projet, "STM32L476RGTX_FLASH.ld"), encoding="ascii") as f:
                texte = f.read()
            m = re.search(r"^\s*FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+),\s*LENGTH\s*=\s*(\d+)K", texte, re.M)
            return int(m.group(1), 16), int(m.group(2)) * 1024
        self.assertEqual(zone_flash(LOADER), (0x08000000, 512 * 1024))
        self.assertEqual(zone_flash(DEMO), (0x08080000, 512 * 1024))


# ===========================================================================
# 3. Simulation de bout en bout avec le vrai ota.c
# ===========================================================================
LIB = ctypes.CDLL(os.path.join(BUILD, "sim", "libota_sim.so"))
NOTIFY = ctypes.CFUNCTYPE(None, ctypes.c_uint8, ctypes.c_uint32)
LIB.ota_on_control.argtypes = [ctypes.c_char_p, ctypes.c_uint16]
LIB.ota_on_data.argtypes = [ctypes.c_char_p, ctypes.c_uint16]
LIB.ota_build_info.argtypes = [ctypes.c_char_p, ctypes.c_uint8]
LIB.sim_flash_data.restype = ctypes.POINTER(ctypes.c_uint8)
LIB.sim_flash_fail_program_at.argtypes = [ctypes.c_long]
LIB.sim_flash_write_raw.argtypes = [ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint32]
for f in ("ota_take_run_request", "ota_take_identify_request", "app_image_is_installed"):
    getattr(LIB, f).restype = ctypes.c_bool
LIB.sim_flash_erase_count.restype = ctypes.c_uint32


def contenu_flash(n):
    return ctypes.string_at(LIB.sim_flash_data(), n)


class LiaisonSimulee(ota_transfert.Liaison):
    """Liaison vers le chargeur simulé. Pannes possibles :
       perdre[k]   = numéro d'essai -> perd le k-ième paquet DATA de cet essai
       abimer[k]   = idem, mais modifie un octet du paquet (sans changer son numéro)
       muette      = la carte ne répond jamais
       coupure_sur = code de commande (CMD_RUN...) : la carte la reçoit, puis la
                     liaison est coupée avant l'accusé de réception (cas Windows)
    """

    def __init__(self, perdre=None, abimer=None, muette=False, coupure_sur=None):
        super().__init__()
        self.perdre = perdre or {}
        self.abimer = abimer or {}
        self.muette = muette
        self.coupure_sur = coupure_sur
        self.essai = 0
        self.paquet = 0
        self.reponses = []
        self._rappel = NOTIFY(self._notifier)      # garder une référence (ctypes)
        LIB.ota_init(self._rappel)

    def _notifier(self, code, valeur):
        self.reponses.append((code, valeur))
        if not self.muette:
            self.recevoir_notification(struct.pack("<BI", code, valeur))

    async def lire_info(self):
        info = ctypes.create_string_buffer(P.LONGUEUR_INFO)
        LIB.ota_build_info(info, 1)
        return info.raw

    async def ecrire_commande(self, message):
        if message[0] == P.CMD_START:
            self.essai += 1
            self.paquet = 0
        LIB.ota_on_control(message, len(message))
        if message[0] == self.coupure_sur:
            raise ota_transfert.LiaisonPerdue("connexion Bluetooth perdue (simulation)")

    async def ecrire_donnees(self, paquet):
        self.paquet += 1
        if self.perdre.get(self.paquet) in (self.essai, "toujours"):
            return                                   # paquet perdu en route
        if self.abimer.get(self.paquet) == self.essai:
            paquet = paquet[:5] + bytes([paquet[5] ^ 0x40]) + paquet[6:]
        LIB.ota_on_data(paquet, len(paquet))


def image_aleatoire(taille, graine=1):
    """Image plausible (table des vecteurs correcte) de la taille voulue."""
    r = random.Random(graine)
    corps = bytes(r.getrandbits(8) for _ in range(taille - 8))
    return ota_image.Image(struct.pack("<II", 0x20018000, 0x08080000 + 0x189) + corps,
                           0x08080000, "aleatoire", "BIN")


def executer(coroutine):
    return asyncio.run(coroutine)


class TestBoutEnBout(unittest.TestCase):

    def setUp(self):
        LIB.sim_flash_reset(0x00)
        self.messages = []

    def transferer(self, liaison, image, **options):
        async def scenario():
            return await ota_transfert.transferer(liaison, image, journal=self.messages.append, **options)
        return executer(scenario())

    def test_programme_de_demo(self):
        image = ota_image.charger(DEMO_ELF)
        liaison = LiaisonSimulee()
        self.transferer(liaison, image)
        self.assertEqual(contenu_flash(image.taille), image.donnees)
        self.assertTrue(LIB.app_image_is_installed())
        self.assertTrue(LIB.ota_take_run_request())        # commande RUN envoyée
        acks = [v for c, v in liaison.reponses if c == P.RSP_ACK]
        self.assertEqual(len(acks), (image.taille + 1023) // 1024)
        self.assertEqual(liaison.reponses[-1], (P.RSP_DONE, image.crc32))

    def test_sans_lancer(self):
        self.transferer(LiaisonSimulee(), ota_image.charger(DEMO_ELF), lancer=False)
        self.assertFalse(LIB.ota_take_run_request())

    def test_coupure_pendant_run_normale(self):
        """La carte redémarre dès qu'elle reçoit RUN, parfois avant d'avoir
        accusé réception : le transfert est quand même réussi."""
        image = ota_image.charger(DEMO_ELF)
        self.transferer(LiaisonSimulee(coupure_sur=P.CMD_RUN), image)
        self.assertTrue(LIB.ota_take_run_request())
        self.assertTrue(LIB.app_image_is_installed())

    def test_coupure_pendant_end_signalee(self):
        """Une coupure AVANT la réponse DONE est signalée par LiaisonPerdue avec
        fin_envoyee = True : c'est obcp_ota.py qui se reconnecte pour savoir si
        le programme a été validé."""
        with self.assertRaises(ota_transfert.LiaisonPerdue) as ctx:
            self.transferer(LiaisonSimulee(coupure_sur=P.CMD_END), image_aleatoire(3000))
        self.assertTrue(ctx.exception.fin_envoyee)
        self.assertFalse(LIB.ota_take_run_request())
        self.assertTrue(LIB.app_image_is_installed())      # la carte avait validé

    def test_coupure_signalee_pendant_une_attente(self):
        """Une coupure pendant l'attente d'un ACK arrête l'attente aussitôt
        (sans attendre la fin du délai) ; fin_envoyee reste faux."""
        liaison = LiaisonSimulee(muette=True)
        async def scenario():
            asyncio.get_running_loop().call_later(0.2, liaison.signaler_coupure)
            await ota_transfert.transferer(liaison, image_aleatoire(3000), journal=self.messages.append)
        depart = time.monotonic()
        with self.assertRaises(ota_transfert.LiaisonPerdue) as ctx:
            executer(scenario())
        self.assertLess(time.monotonic() - depart, 2.0)
        self.assertFalse(ctx.exception.fin_envoyee)

    def test_image_maximale_512_ko(self):
        image = image_aleatoire(512 * 1024, graine=5)
        self.transferer(LiaisonSimulee(), image)
        self.assertEqual(contenu_flash(image.taille), image.donnees)

    def test_paquet_perdu_puis_nouvel_essai(self):
        image = ota_image.charger(DEMO_ELF)
        liaison = LiaisonSimulee(perdre={100: 1})
        self.transferer(liaison, image)
        self.assertEqual(contenu_flash(image.taille), image.donnees)
        self.assertEqual(liaison.essai, 2)
        self.assertEqual(LIB.sim_flash_erase_count(), 2)
        self.assertTrue(any("paquet" in m and "nouvel essai" in m for m in self.messages), self.messages)

    def test_octet_abime_detecte_par_le_crc(self):
        image = image_aleatoire(5000, graine=2)
        liaison = LiaisonSimulee(abimer={7: 1})
        self.transferer(liaison, image)
        # Le 7e paquet porte les octets 6 x 19 à 7 x 19 de l'image ; l'octet 5
        # du paquet (après le numéro) est l'octet 6 x 19 + 4 de l'image.
        abimee = bytearray(image.donnees)
        abimee[6 * 19 + 4] ^= 0x40
        self.assertIn((P.ERR_CRC, zlib.crc32(abimee)), liaison.reponses)
        self.assertEqual(contenu_flash(image.taille), image.donnees)
        self.assertEqual(liaison.essai, 2)

    def test_panne_flash_puis_nouvel_essai(self):
        image = image_aleatoire(9000, graine=3)
        LIB.sim_flash_fail_program_at(4096)
        liaison = LiaisonSimulee()
        self.transferer(liaison, image)
        self.assertIn(P.ERR_FLASH, [c for c, _ in liaison.reponses])
        self.assertEqual(contenu_flash(image.taille), image.donnees)

    def test_echec_definitif_apres_trois_essais(self):
        image = image_aleatoire(3000, graine=4)
        liaison = LiaisonSimulee(perdre={10: "toujours"})
        with self.assertRaises(ota_transfert.ErreurTransfert) as ctx:
            self.transferer(liaison, image)
        self.assertTrue(ctx.exception.apres_effacement)
        self.assertEqual(liaison.essai, 3)
        self.assertFalse(LIB.app_image_is_installed())   # jamais de programme incomplet

    def test_carte_muette(self):
        anciens = (ota_transfert.DELAI_EFFACEMENT_BASE, ota_transfert.DELAI_PAR_PAGE)
        ota_transfert.DELAI_EFFACEMENT_BASE, ota_transfert.DELAI_PAR_PAGE = 0.2, 0.0
        try:
            with self.assertRaises(ota_transfert.ErreurTransfert) as ctx:
                self.transferer(LiaisonSimulee(muette=True), image_aleatoire(100), tentatives=1)
            self.assertIn("ne répond pas", str(ctx.exception))
        finally:
            ota_transfert.DELAI_EFFACEMENT_BASE, ota_transfert.DELAI_PAR_PAGE = anciens

    def test_image_mal_liee_refusee_par_la_carte(self):
        """Si le PC ne vérifiait pas l'image, la carte la refuserait quand
        même à la fin (ERR_IMAGE, sans nouvel essai) et ne la lancerait pas."""
        image = image_aleatoire(4000)
        mauvaise = ota_image.Image(struct.pack("<II", 0x20018000, 0x08000189) + image.donnees[8:],
                                   0x08080000, "x", "BIN")
        liaison = LiaisonSimulee()
        with self.assertRaises(ota_transfert.ErreurTransfert):
            self.transferer(liaison, mauvaise)
        self.assertEqual(liaison.essai, 1)
        self.assertIn(P.ERR_IMAGE, [c for c, _ in liaison.reponses])
        self.assertFalse(LIB.app_image_is_installed())

    def test_ancienne_application_remplacee(self):
        ancienne = image_aleatoire(20000, graine=8)
        LIB.sim_flash_write_raw(0, ancienne.donnees, ancienne.taille)
        self.assertTrue(LIB.app_image_is_installed())
        nouvelle = ota_image.charger(DEMO_ELF)
        self.transferer(LiaisonSimulee(), nouvelle)
        self.assertEqual(contenu_flash(nouvelle.taille), nouvelle.donnees)

    def test_identification(self):
        async def scenario():
            await ota_transfert.identifier(LiaisonSimulee())
        executer(scenario())
        self.assertTrue(LIB.ota_take_identify_request())


# ===========================================================================
# 4. Ligne de commande (sans Bluetooth)
# ===========================================================================
class TestLigneDeCommande(unittest.TestCase):

    def lancer(self, *arguments):
        sortie = io.StringIO()
        with contextlib.redirect_stdout(sortie):
            code = obcp_ota.main(list(arguments))
        return code, sortie.getvalue()

    def test_refuse_le_chargeur(self):
        code, texte = self.lancer(LOADER_ELF)
        self.assertEqual(code, 1)
        self.assertIn("0x08000000", texte)
        self.assertIn("ORIGIN = 0x8080000", texte)

    def test_compilation_ratee_detectee(self):
        with tempfile.TemporaryDirectory() as projet:
            os.makedirs(os.path.join(projet, "Debug"))
            elf = os.path.join(projet, "Debug", "demo.elf")
            shutil.copy(DEMO_ELF, elf)
            source = os.path.join(projet, "main.c")
            open(source, "w").close()
            t = time.time()
            os.utime(elf, (t - 60, t - 60))
            code, texte = self.lancer(projet)
            self.assertEqual(code, 1)
            self.assertIn("main.c", texte)
            self.assertIn("--forcer", texte)

    def test_noms_de_carte(self):
        for texte in ("a1b2", "A1B2", "ObCP-OTA-A1B2", "obcp-ota-a1b2"):
            self.assertEqual(obcp_ota.normaliser_nom(texte), "ObCP-OTA-A1B2")


# ===========================================================================
# 5. Programme complet avec un faux module « bleak »
#    (BleakScanner et BleakClient simulés, reliés au vrai ota.c)
# ===========================================================================
import types                                        # noqa: E402
from unittest import mock                           # noqa: E402


class FauxAppareil:
    def __init__(self, nom):
        self.name = nom
        self.address = "C0:B7:00:00:" + nom[-4:-2] + ":" + nom[-2:]


class FauxAppareilProgramme:
    """Carte qui fonctionne normalement : son programme annonce sous son
    propre nom (OBCP-CROC pour le programme modèle) et sa propre adresse."""
    def __init__(self, adresse, nom):
        self.name = nom
        self.address = adresse


def chargeur_du_programme(adresse):
    """C1:B7:00:00:BE:BB -> ObCP-OTA-BEBB (nom pris dans la fin de l'adresse)."""
    return "ObCP-OTA-" + adresse[-5:-3] + adresse[-2:]


class FausseAnnonce:
    def __init__(self, nom, rssi):
        self.local_name = nom
        self.rssi = rssi


class FauxScanner:
    cartes = []                                      # [(nom, rssi)] chargeurs visibles
    programmes = []                                  # [(adresse, nom, rssi)] cartes en fonctionnement

    def __init__(self, detection_callback=None, **_options):
        self.rappel = detection_callback

    async def __aenter__(self):
        boucle = asyncio.get_running_loop()
        for i, (nom, rssi) in enumerate(FauxScanner.cartes):
            boucle.call_later(0.05 * (i + 1), self.rappel, FauxAppareil(nom), FausseAnnonce(nom, rssi))
        for i, (adresse, nom, rssi) in enumerate(FauxScanner.programmes):
            boucle.call_later(0.05 * (i + 1), self.rappel, FauxAppareilProgramme(adresse, nom),
                              FausseAnnonce(nom, rssi))
        return self

    async def __aexit__(self, *_):
        return False


class FauxClient:
    """Faux BleakClient relié au vrai ota.c. Les attributs de classe décrivent
    les pannes à simuler (remis à zéro avant chaque test)."""
    connexions = []                  # noms des cartes connectées, dans l'ordre
    paquets = 0                      # paquets DATA reçus par la carte
    coupure_sur = None               # commande reçue par la carte, puis coupure
                                     # avant l'accusé de réception (cas Windows)
    coupure_avant = None             # commande perdue : coupure avant la carte
    coupure_apres_paquets = None     # coupure juste après ce paquet DATA
    coupures_restantes = 1           # nombre de coupures encore à simuler
    disparait = False                # après une coupure, la carte n'annonce plus
    cause_coupure = 0x08             # cause notée par la carte (octet 16 de INFO)
    cause_notee = 0                  # valeur actuelle de l'octet 16
    info_v10 = False                 # INFO de 16 octets, comme un chargeur 1.0
    echecs_connexion = 0             # prochaines tentatives de connexion ratées
    # Programme d'une carte en fonctionnement (commande « #ota ») :
    connexions_programme = []        # adresses des programmes connectés
    commandes_programme = []         # textes écrits dans leur caractéristique RX
    programme_sans_ota = False       # le programme ne connaît pas « #ota »
    programme_coupe = False          # liaison coupée avant l'accusé de réception
    programme_injoignable = False    # connexion au programme impossible

    def __init__(self, appareil, timeout=30.0, winrt=None, disconnected_callback=None, **_options):
        self.appareil = appareil
        self.winrt = winrt
        self.services = types.SimpleNamespace(
            get_service=lambda uuid: object() if uuid == P.UUID_SERVICE else None)
        self.rappel = None
        self.rappel_coupure = disconnected_callback
        self.coupe = False
        self._c = NOTIFY(self._notifier)

    def _notifier(self, code, valeur):
        if self.rappel and not self.coupe:
            self.rappel(None, bytearray(struct.pack("<BI", code, valeur)))

    def _doit_couper(self):
        return FauxClient.coupures_restantes > 0 and not self.coupe

    def _couper(self):
        """La liaison tombe : la carte le constate, puis Bleak prévient le PC."""
        self.coupe = True
        FauxClient.coupures_restantes -= 1
        FauxClient.cause_notee = FauxClient.cause_coupure
        LIB.ota_on_disconnect()
        if FauxClient.disparait:
            FauxScanner.cartes = []
        if self.rappel_coupure:
            self.rappel_coupure(self)

    def _programme(self):
        return not self.appareil.name.startswith(P.PREFIXE_NOM)

    async def connect(self):
        if self._programme():
            if FauxClient.programme_injoignable:
                raise TimeoutError("délai de connexion dépassé")
            FauxClient.connexions_programme.append(self.appareil.address)
            return
        if FauxClient.echecs_connexion > 0:
            FauxClient.echecs_connexion -= 1
            raise TimeoutError("délai de connexion dépassé")
        FauxClient.connexions.append(self.appareil.name)
        LIB.ota_init(self._c)

    async def start_notify(self, uuid, rappel):
        assert uuid == P.UUID_CTRL
        self.rappel = rappel

    async def read_gatt_char(self, uuid):
        assert uuid == P.UUID_INFO
        info = ctypes.create_string_buffer(P.LONGUEUR_INFO)
        LIB.ota_build_info(info, 2)
        octets = bytearray(info.raw)
        octets[P.POSITION_CAUSE_DECONNEXION] = FauxClient.cause_notee
        return octets[:P.LONGUEUR_INFO_MIN] if FauxClient.info_v10 else octets

    async def write_gatt_char(self, uuid, donnees, response=None):
        donnees = bytes(donnees)
        if self.coupe:
            raise OSError("Not connected")
        if self._programme():
            assert uuid == P.UUID_RX_PROGRAMME and response is True
            FauxClient.commandes_programme.append(donnees)
            if donnees == P.COMMANDE_OTA and not FauxClient.programme_sans_ota:
                # la carte redémarre dans son chargeur : le programme n'annonce
                # plus, le chargeur annonce à sa place
                adresse = self.appareil.address
                FauxScanner.programmes = [x for x in FauxScanner.programmes if x[0] != adresse]
                FauxScanner.cartes = FauxScanner.cartes + [(chargeur_du_programme(adresse), -55)]
                if FauxClient.programme_coupe:
                    raise OSError("[WinError -2147023673] L’opération a été annulée par l’utilisateur.")
            return
        if uuid == P.UUID_CTRL:
            assert response is True
            if donnees[0] == FauxClient.coupure_avant and self._doit_couper():
                self._couper()
                raise OSError("[WinError -2147023673] L’opération a été annulée par l’utilisateur.")
            LIB.ota_on_control(donnees, len(donnees))
            if donnees[0] == FauxClient.coupure_sur and self._doit_couper():
                self._couper()
                raise OSError("[WinError -2147023673] L’opération a été annulée par l’utilisateur.")
        else:
            assert uuid == P.UUID_DATA and response is False and len(donnees) <= 20
            FauxClient.paquets += 1
            LIB.ota_on_data(donnees, len(donnees))
            if FauxClient.paquets == FauxClient.coupure_apres_paquets and self._doit_couper():
                self._couper()

    async def disconnect(self):
        if not self._programme():
            LIB.ota_on_disconnect()


class TestProgrammeComplet(unittest.TestCase):

    def setUp(self):
        LIB.sim_flash_reset(0xFF)
        LIB.ota_take_run_request()
        LIB.ota_take_identify_request()
        FauxClient.connexions = []
        FauxClient.paquets = 0
        FauxClient.coupure_sur = None
        FauxClient.coupure_avant = None
        FauxClient.coupure_apres_paquets = None
        FauxClient.coupures_restantes = 1
        FauxClient.disparait = False
        FauxClient.cause_coupure = 0x08
        FauxClient.cause_notee = 0
        FauxClient.info_v10 = False
        FauxClient.echecs_connexion = 0
        FauxClient.connexions_programme = []
        FauxClient.commandes_programme = []
        FauxClient.programme_sans_ota = False
        FauxClient.programme_coupe = False
        FauxClient.programme_injoignable = False
        FauxScanner.cartes = []
        FauxScanner.programmes = []
        self.dossier = tempfile.TemporaryDirectory()
        self.memoire = os.path.join(self.dossier.name, "memoire.json")
        self.correctifs = [
            mock.patch.dict(sys.modules, {"bleak": types.SimpleNamespace(
                BleakScanner=FauxScanner, BleakClient=FauxClient)}),
            mock.patch.object(obcp_ota, "FICHIER_MEMOIRE", self.memoire),
            mock.patch.object(obcp_ota, "DELAI_RECONNEXION", 0.5),
            mock.patch.object(obcp_ota, "DELAI_CHARGEUR", 0.5),
        ]
        for c in self.correctifs:
            c.start()

    def tearDown(self):
        for c in self.correctifs:
            c.stop()
        self.dossier.cleanup()

    def lancer(self, *arguments, reponses=""):
        sortie = io.StringIO()
        with contextlib.redirect_stdout(sortie), mock.patch("sys.stdin", io.StringIO(reponses)):
            code = obcp_ota.main(list(arguments))
        return code, sortie.getvalue()

    def test_transfert_carte_designee(self):
        FauxScanner.cartes = [("ObCP-OTA-A1B2", -60), ("ObCP-OTA-C3D4", -70)]
        code, texte = self.lancer(DEMO_ELF, "--carte", "c3d4")
        self.assertEqual(code, 0, texte)
        self.assertEqual(FauxClient.connexions, ["ObCP-OTA-C3D4"])
        image = ota_image.charger(DEMO_ELF)
        self.assertEqual(contenu_flash(image.taille), image.donnees)
        self.assertTrue(LIB.ota_take_run_request())
        self.assertIn("Terminé", texte)
        self.assertIn("CRC-32 0x%08X correct" % image.crc32, texte)
        # nombre de paquets : chaque bloc de 1024 octets est découpé en paquets de 19
        paquets = sum(-(-min(1024, image.taille - d) // 19) for d in range(0, image.taille, 1024))
        self.assertEqual(FauxClient.paquets, paquets)
        self.assertEqual(obcp_ota.lire_carte_memorisee(), "ObCP-OTA-C3D4")
        self.assertIn("[1/4]", texte)
        self.assertIn("100 %", texte)              # progression affichée
        print("\n--- exemple de sortie ---\n" + texte)

    def test_windows_coupe_pendant_run(self):
        """Cas observé sur une vraie ObCP sous Windows (version 1.0) : l'écriture
        de RUN échoue avec WinError -2147023673 car la carte a déjà redémarré."""
        FauxScanner.cartes = [("ObCP-OTA-BEBB", -61)]
        FauxClient.coupure_sur = P.CMD_RUN
        code, texte = self.lancer(DEMO_ELF)
        self.assertEqual(code, 0, texte)
        self.assertNotIn("ERREUR", texte)
        self.assertIn("redémarre sur le nouveau programme", texte)
        self.assertTrue(LIB.ota_take_run_request())

    def test_coupure_apres_end_programme_valide(self):
        """La liaison tombe juste après END, mais la carte avait vérifié et
        validé le programme : le PC se reconnecte, le lit dans INFO, affiche
        la cause notée par la carte et envoie RUN, sans second transfert."""
        FauxScanner.cartes = [("ObCP-OTA-BEBB", -61)]
        FauxClient.coupure_sur = P.CMD_END
        code, texte = self.lancer(DEMO_ELF)
        self.assertEqual(code, 0, texte)
        self.assertEqual(FauxClient.connexions, ["ObCP-OTA-BEBB"] * 2)
        self.assertIn("nouvelle connexion (2/3)", texte)
        self.assertIn("plus aucun échange radio", texte)
        self.assertIn("validé le programme avant la coupure", texte)
        self.assertNotIn("ERREUR", texte)
        self.assertTrue(LIB.ota_take_run_request())
        self.assertEqual(LIB.sim_flash_erase_count(), 1)
        image = ota_image.charger(DEMO_ELF)
        self.assertEqual(contenu_flash(image.taille), image.donnees)
        print("\n--- coupure après END ---\n" + texte)

    def test_coupure_avant_que_la_carte_recoive_end(self):
        """END perdu avec la liaison : la carte n'a rien validé ; après la
        reconnexion, le PC recommence le transfert."""
        FauxScanner.cartes = [("ObCP-OTA-BEBB", -61)]
        FauxClient.coupure_avant = P.CMD_END
        code, texte = self.lancer(DEMO_ELF)
        self.assertEqual(code, 0, texte)
        self.assertEqual(len(FauxClient.connexions), 2)
        self.assertEqual(LIB.sim_flash_erase_count(), 2)
        self.assertTrue(LIB.app_image_is_installed())
        self.assertTrue(LIB.ota_take_run_request())
        self.assertNotIn("validé le programme avant la coupure", texte)

    def test_coupure_pendant_les_donnees(self):
        FauxScanner.cartes = [("ObCP-OTA-BEBB", -61)]
        FauxClient.coupure_apres_paquets = 100
        code, texte = self.lancer(DEMO_ELF)
        self.assertEqual(code, 0, texte)
        self.assertEqual(len(FauxClient.connexions), 2)
        self.assertEqual(LIB.sim_flash_erase_count(), 2)
        image = ota_image.charger(DEMO_ELF)
        self.assertEqual(contenu_flash(image.taille), image.donnees)
        self.assertTrue(LIB.ota_take_run_request())

    def test_carte_disparue_apres_coupure(self):
        """Après la coupure, la carte n'annonce plus rien (elle a peut-être
        redémarré) : message clair, sans fausse alerte sur l'effacement."""
        FauxScanner.cartes = [("ObCP-OTA-BEBB", -61)]
        FauxClient.coupure_sur = P.CMD_END
        FauxClient.disparait = True
        code, texte = self.lancer(DEMO_ELF)
        self.assertEqual(code, 1, texte)
        self.assertIn("n'est plus visible", texte)
        self.assertIn("CRC", texte)
        self.assertNotIn("relancez simplement", texte)
        self.assertEqual(len(FauxClient.connexions), 1)

    def test_coupures_a_repetition(self):
        """La liaison tombe à chaque connexion : abandon après 3 connexions."""
        FauxScanner.cartes = [("ObCP-OTA-BEBB", -61)]
        FauxClient.coupure_avant = P.CMD_START
        FauxClient.coupures_restantes = 10
        code, texte = self.lancer(DEMO_ELF)
        self.assertEqual(code, 1, texte)
        self.assertEqual(len(FauxClient.connexions), 3)
        self.assertIn("ERREUR (transfert) : connexion Bluetooth perdue", texte)

    def test_premiere_connexion_ratee(self):
        """La première connexion échoue : nouvel essai automatique ; les
        renseignements de la carte ne s'affichent qu'une fois."""
        FauxScanner.cartes = [("ObCP-OTA-BEBB", -61)]
        FauxClient.echecs_connexion = 1
        code, texte = self.lancer(DEMO_ELF)
        self.assertEqual(code, 0, texte)
        self.assertIn("connexion impossible (délai de connexion dépassé)", texte)
        self.assertEqual(texte.count("chargeur version"), 1)
        self.assertEqual(texte.count("[4/4]"), 1)
        self.assertNotIn("la carte indique", texte)
        self.assertTrue(LIB.ota_take_run_request())

    def test_chargeur_1_0(self):
        """Un chargeur 1.0 (INFO de 16 octets) reste utilisable, reconnexion
        comprise ; seul le diagnostic manque."""
        FauxScanner.cartes = [("ObCP-OTA-BEBB", -61)]
        FauxClient.info_v10 = True
        FauxClient.coupure_sur = P.CMD_END
        code, texte = self.lancer(DEMO_ELF)
        self.assertEqual(code, 0, texte)
        self.assertNotIn("la carte indique", texte)
        self.assertIn("validé le programme avant la coupure", texte)

    def test_carte_memorisee_choisie_parmi_plusieurs(self):
        obcp_ota.memoriser_carte("ObCP-OTA-C3D4")
        FauxScanner.cartes = [("ObCP-OTA-A1B2", -40), ("ObCP-OTA-C3D4", -80)]
        code, texte = self.lancer(DEMO_ELF)
        self.assertEqual(code, 0, texte)
        self.assertEqual(FauxClient.connexions, ["ObCP-OTA-C3D4"])

    def test_choix_demande_si_plusieurs_cartes(self):
        FauxScanner.cartes = [("ObCP-OTA-A1B2", -80), ("ObCP-OTA-C3D4", -40)]
        code, texte = self.lancer(DEMO_ELF, reponses="2\n")
        self.assertEqual(code, 0, texte)
        self.assertIn("1. ObCP-OTA-C3D4", texte)    # la plus proche en premier
        self.assertEqual(FauxClient.connexions, ["ObCP-OTA-A1B2"])

    def test_confirmation_si_autre_carte_que_la_memorisee(self):
        obcp_ota.memoriser_carte("ObCP-OTA-9999")
        FauxScanner.cartes = [("ObCP-OTA-A1B2", -50)]
        code, texte = self.lancer(DEMO_ELF, reponses="n\n")
        self.assertEqual(code, 1)
        self.assertEqual(FauxClient.connexions, [])
        self.assertIn("annulé", texte)

    def test_aucune_carte(self):
        FauxScanner.cartes = []
        code, texte = self.lancer(DEMO_ELF, "--attente", "3.5")
        self.assertEqual(code, 1)
        self.assertIn("maintenez le bouton USER", texte)
        self.assertNotIn("effacée", texte)          # rien n'a été effacé

    def test_liste_et_identification(self):
        FauxScanner.cartes = [("ObCP-OTA-A1B2", -50), ("ObCP-OTA-C3D4", -60)]
        code, texte = self.lancer("--liste", "--duree-liste", "0.5", reponses="2\n")
        self.assertEqual(code, 0, texte)
        self.assertTrue(LIB.ota_take_identify_request())
        self.assertEqual(obcp_ota.lire_carte_memorisee(), "ObCP-OTA-C3D4")
        self.assertEqual(obcp_ota.lire_memoire()["adresse"], "C0:B7:00:00:C3:D4")
        self.assertIn("clignote en blanc", texte)

    # ---- version 1.2 : la carte qui fonctionne reçoit « #ota » ----------------
    def test_ota_envoye_au_programme(self):
        """Carte connue (adresse de son chargeur mémorisée) qui fonctionne
        normalement : « #ota » lui est envoyé, puis le transfert a lieu. Ni
        l'autre carte en mode chargement, ni le programme d'une autre carte ne
        sont touchés."""
        obcp_ota.memoriser_carte("ObCP-OTA-BEBB", "C0:B7:00:00:BE:BB")
        FauxScanner.cartes = [("ObCP-OTA-A1B2", -40)]
        FauxScanner.programmes = [("C1:B7:00:00:C3:D4", "OBCP-CROC", -45),
                                  ("C1:B7:00:00:BE:BB", "OBCP-CROC", -55)]
        code, texte = self.lancer(DEMO_ELF)
        self.assertEqual(code, 0, texte)
        self.assertEqual(FauxClient.connexions_programme, ["C1:B7:00:00:BE:BB"])
        self.assertEqual(FauxClient.commandes_programme, [b"#ota"])
        self.assertEqual(FauxClient.connexions, ["ObCP-OTA-BEBB"])
        self.assertIn("fonctionne normalement : envoi de la commande #ota", texte)
        self.assertNotIn("ERREUR", texte)
        self.assertTrue(LIB.ota_take_run_request())
        print("\n--- #ota envoyé par l'application PC ---\n" + texte)

    def test_ota_liaison_coupee_pendant_l_envoi(self):
        """La carte ferme la liaison avant l'accusé de réception de « #ota »
        (comme pour RUN sous Windows) : on cherche son chargeur quand même."""
        obcp_ota.memoriser_carte("ObCP-OTA-BEBB", "C0:B7:00:00:BE:BB")
        FauxScanner.programmes = [("C1:B7:00:00:BE:BB", "OBCP-CROC", -55)]
        FauxClient.programme_coupe = True
        code, texte = self.lancer(DEMO_ELF)
        self.assertEqual(code, 0, texte)
        self.assertIn("a sans doute déjà reçu #ota", texte)
        self.assertEqual(FauxClient.connexions, ["ObCP-OTA-BEBB"])

    def test_programme_sans_ota(self):
        """Le programme de la carte ignore « #ota » : message clair, rien effacé."""
        obcp_ota.memoriser_carte("ObCP-OTA-BEBB", "C0:B7:00:00:BE:BB")
        FauxScanner.programmes = [("C1:B7:00:00:BE:BB", "OBCP-CROC", -55)]
        FauxClient.programme_sans_ota = True
        code, texte = self.lancer(DEMO_ELF)
        self.assertEqual(code, 1, texte)
        self.assertIn("n'est pas passée dans son chargeur", texte)
        self.assertIn("USER maintenu pendant un appui", texte)
        self.assertEqual(FauxClient.connexions, [])
        self.assertNotIn("effacée", texte)

    def test_programme_injoignable(self):
        obcp_ota.memoriser_carte("ObCP-OTA-BEBB", "C0:B7:00:00:BE:BB")
        FauxScanner.programmes = [("C1:B7:00:00:BE:BB", "OBCP-CROC", -55)]
        FauxClient.programme_injoignable = True
        code, texte = self.lancer(DEMO_ELF)
        self.assertEqual(code, 1, texte)
        self.assertIn("connexion impossible au programme de la carte ObCP-OTA-BEBB", texte)
        self.assertEqual(FauxClient.commandes_programme, [])

    def test_adresse_memorisee_au_premier_transfert(self):
        """Fichier de la version 1.1 (nom seul) : aucun programme ne reçoit
        « #ota », et l'adresse du chargeur est mémorisée pour la suite."""
        with open(self.memoire, "w", encoding="utf-8") as f:
            json.dump({"carte": "ObCP-OTA-BEBB"}, f)
        FauxScanner.cartes = [("ObCP-OTA-BEBB", -61)]
        FauxScanner.programmes = [("C1:B7:00:00:A1:B2", "OBCP-CROC", -50)]
        code, texte = self.lancer(DEMO_ELF)
        self.assertEqual(code, 0, texte)
        self.assertEqual(FauxClient.commandes_programme, [])
        self.assertEqual(obcp_ota.lire_memoire(), {"carte": "ObCP-OTA-BEBB", "adresse": "C0:B7:00:00:BE:BB"})

    def test_pas_de_ota_pour_une_autre_carte(self):
        """Seul le programme d'une AUTRE carte est visible : il ne reçoit rien."""
        obcp_ota.memoriser_carte("ObCP-OTA-BEBB", "C0:B7:00:00:BE:BB")
        FauxScanner.programmes = [("C1:B7:00:00:A1:B2", "OBCP-CROC", -50)]
        code, texte = self.lancer(DEMO_ELF, "--attente", "3.5")
        self.assertEqual(code, 1)
        self.assertEqual(FauxClient.connexions_programme, [])
        self.assertIn("maintenez le bouton USER", texte)

    def test_carte_designee_differente_de_la_memorisee(self):
        """--carte vise une autre carte que la mémorisée : on ne connaît pas
        l'adresse de son chargeur, rien n'est envoyé au programme mémorisé."""
        obcp_ota.memoriser_carte("ObCP-OTA-BEBB", "C0:B7:00:00:BE:BB")
        FauxScanner.cartes = [("ObCP-OTA-A1B2", -50)]
        FauxScanner.programmes = [("C1:B7:00:00:BE:BB", "OBCP-CROC", -55)]
        code, texte = self.lancer(DEMO_ELF, "--carte", "a1b2")
        self.assertEqual(code, 0, texte)
        self.assertEqual(FauxClient.connexions_programme, [])
        self.assertEqual(FauxClient.connexions, ["ObCP-OTA-A1B2"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
