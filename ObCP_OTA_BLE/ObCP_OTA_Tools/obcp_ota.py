# -*- coding: utf-8 -*-
"""
obcp_ota.py - Charge un programme dans l'ObCP par Bluetooth (OTA).

Exemples (dans un terminal, depuis ce dossier) :

    python obcp_ota.py ..\\ObCP_OTA_Demo                 projet CubeIDE : .elf le plus récent
    python obcp_ota.py ObCP_OTA_Demo.elf                 fichier .elf, .hex ou .bin
    python obcp_ota.py ObCP_OTA_Demo.elf --carte A1B2    vise une carte précise
    python obcp_ota.py --liste                           cherche les cartes, choisit la sienne

Depuis STM32CubeIDE, le menu Run > External Tools > « ObCP OTA - compiler et
transférer » compile le projet sélectionné puis lance ce programme
(voir la documentation, chapitre 4).

Avant le transfert, la carte doit être en « mode chargement » : maintenir le
bouton USER, appuyer brièvement sur RESET, relâcher USER. La LED bleue
clignote alors une fois par seconde. Depuis la version 1.2, c'est inutile
pour une carte qui a déjà reçu un programme depuis ce PC, si son programme
connaît la commande « #ota » (programme modèle 2.1 et suivants) : on la
reconnaît pendant qu'elle fonctionne, et on lui envoie « #ota ».

Nécessite Python 3.10 ou plus récent et la bibliothèque Bleak :
    python -m pip install bleak
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import sys
import time

from ota_image import (ErreurImage, charger, decrire_date, source_plus_recente,
                       trouver_elf, verifier)
from ota_protocole import (COMMANDE_OTA, PREFIXE_NOM, UUID_CTRL, UUID_DATA, UUID_INFO,
                           UUID_RX_PROGRAMME, UUID_SERVICE, adresse_du_programme,
                           decrire_cause_deconnexion, decrire_redemarrage_module)
from ota_transfert import (ErreurTransfert, Liaison, LiaisonPerdue, envoyer_run, identifier,
                           lire_et_verifier_info, transferer)

VERSION = "1.2"

# Fichier où l'on mémorise la carte choisie et l'adresse Bluetooth de son
# chargeur (dans le dossier personnel).
FICHIER_MEMOIRE = os.path.join(os.path.expanduser("~"), ".obcp_ota.json")

# Après une coupure de la liaison Bluetooth : nombre total de connexions
# essayées, et temps laissé pour retrouver la carte à chaque fois.
NB_CONNEXIONS = 3
DELAI_RECONNEXION = 10.0

# Après l'envoi de « #ota » : temps laissé à la carte pour redémarrer dans
# son chargeur et se remettre à annoncer.
DELAI_CHARGEUR = 15.0

# Clé du résultat de chercher_cartes() : le programme de la carte visée, vu
# pendant qu'elle fonctionne normalement.
PROGRAMME = "programme"

AIDE_MODE_CHARGEMENT = (
    "Carte introuvable pour l'instant. Mettez-la en mode chargement :\n"
    "  maintenez le bouton USER enfoncé, appuyez brièvement sur RESET, puis relâchez USER\n"
    "  (ou envoyez-lui la commande #ota par le terminal, si son programme la connaît).\n"
    "  La LED bleue (ou violette) clignote alors une fois par seconde.")


# ===========================================================================
# Affichage
# ===========================================================================

def journal(texte: str = "", retrait: int = 6) -> None:
    """Affiche une ligne (avec un retrait), immédiatement."""
    for ligne in texte.splitlines() or [""]:
        print(" " * retrait + ligne, flush=True)


def etape(numero: int, texte: str) -> None:
    """Affiche le titre d'une des 4 étapes, par exemple « [2/4] Recherche... »."""
    print("[%d/4] %s" % (numero, texte), flush=True)


def nombre(n: int) -> str:
    """12345 -> '12 345' (écriture française)."""
    return "{:,}".format(n).replace(",", " ")


def decimal(x: float, chiffres: int = 1) -> str:
    """3.14 -> '3,1' (virgule décimale)."""
    return ("%.*f" % (chiffres, x)).replace(".", ",")


class Progression:
    """Barre de progression. Dans un vrai terminal, la ligne est réécrite
    sur place (caractère \\r) ; dans la console de STM32CubeIDE, qui ne gère
    pas toujours \\r, on écrit une ligne tous les 10 %."""

    def __init__(self) -> None:
        self.terminal = sys.stdout.isatty()
        self.depart = time.monotonic()
        self.dernier_dixieme = -1

    def __call__(self, envoyes: int, total: int) -> None:
        if envoyes == 0:                        # début (ou nouvel essai)
            self.depart = time.monotonic()
            self.dernier_dixieme = -1
        pourcent = 100 * envoyes // total
        duree = max(time.monotonic() - self.depart, 1e-3)
        debit = envoyes / 1024 / duree
        pleins = pourcent * 30 // 100
        ligne = "[%s%s] %3d %%  %s/%s Ko  %s Ko/s" % (
            "#" * pleins, "-" * (30 - pleins), pourcent,
            decimal(envoyes / 1024), decimal(total / 1024), decimal(debit))
        if self.terminal:
            print("\r      " + ligne, end="\n" if envoyes == total else "", flush=True)
        elif pourcent // 10 != self.dernier_dixieme:
            self.dernier_dixieme = pourcent // 10
            journal(ligne)


def demander(question: str) -> str | None:
    """Pose une question ; renvoie None si personne ne peut répondre."""
    try:
        return input("      " + question).strip()
    except EOFError:
        return None


# ===========================================================================
# Mémoire de la carte choisie
# ===========================================================================

def lire_memoire() -> dict:
    """Contenu du fichier ~/.obcp_ota.json : {"carte": nom de la carte,
    "adresse": adresse Bluetooth de son chargeur}, ou {} s'il n'existe pas.
    (La version 1.1 n'y écrivait que le nom.)"""
    try:
        with open(FICHIER_MEMOIRE, encoding="utf-8") as f:
            memoire = json.load(f)
        return memoire if isinstance(memoire, dict) else {}
    except (OSError, ValueError):
        return {}


def lire_carte_memorisee() -> str | None:
    """Nom de la carte choisie la dernière fois, ou None."""
    return lire_memoire().get("carte")


def memoriser_carte(nom: str | None, adresse: str | None = None) -> None:
    """Enregistre la carte choisie et l'adresse Bluetooth de son chargeur
    (ou oublie tout si nom vaut None)."""
    try:
        if nom is None:
            if os.path.exists(FICHIER_MEMOIRE):
                os.remove(FICHIER_MEMOIRE)
        else:
            memoire = {"carte": nom}
            if adresse:
                memoire["adresse"] = adresse
            with open(FICHIER_MEMOIRE, "w", encoding="utf-8") as f:
                json.dump(memoire, f)
    except OSError:
        pass                                   # pas grave : on demandera la prochaine fois


def normaliser_nom(texte: str) -> str:
    """'a1b2', 'A1B2' ou 'ObCP-OTA-A1B2' -> 'ObCP-OTA-A1B2'."""
    texte = texte.strip()
    if texte.upper().startswith(PREFIXE_NOM.upper()):
        texte = texte[len(PREFIXE_NOM):]
    return PREFIXE_NOM + texte.upper()


# ===========================================================================
# Bluetooth (bibliothèque Bleak)
# ===========================================================================

def importer_bleak():
    """Importe Bleak, ou explique comment l'installer."""
    try:
        import bleak
        return bleak
    except ImportError:
        raise SystemExit(
            "ERREUR : la bibliothèque Python « bleak » n'est pas installée.\n"
            "Installez-la avec la commande :\n"
            "    %s -m pip install bleak" % (os.path.basename(sys.executable) or "python")) from None


async def chercher_cartes(nom_voulu: str | None, attente: float,
                          minimum: float = 0.0, aide: bool = True,
                          programme: str | None = None) -> dict:
    """Écoute les annonces Bluetooth et renvoie {nom: (appareil, signal_dBm)}.

    - nom_voulu : s'arrête dès que cette carte est vue ;
    - sinon : s'arrête 1,5 s après la première carte vue (pour laisser
      apparaître les autres), ou après 'minimum' secondes si c'est plus long ;
    - au bout de 3 s sans rien, affiche comment passer en mode chargement ;
    - programme : adresse Bluetooth du programme de la carte visée. S'il
      annonce (la carte fonctionne normalement), la recherche s'arrête aussi,
      et il est rendu sous la clé PROGRAMME.
    """
    bleak = importer_bleak()
    cartes: dict = {}
    vue = asyncio.Event()

    def detection(appareil, annonce):
        nom = annonce.local_name or appareil.name or ""
        if nom.startswith(PREFIXE_NOM):
            cartes[nom] = (appareil, annonce.rssi)
            if nom_voulu is None or nom == nom_voulu:
                vue.set()
        elif programme and (appareil.address or "").upper() == programme:
            cartes[PROGRAMME] = (appareil, annonce.rssi)
            vue.set()

    depart = time.monotonic()
    async with bleak.BleakScanner(detection_callback=detection):
        try:
            await asyncio.wait_for(vue.wait(), min(3.0, attente))
        except asyncio.TimeoutError:
            if aide:
                journal(AIDE_MODE_CHARGEMENT)
            try:
                await asyncio.wait_for(vue.wait(), max(attente - 3.0, 0.1))
            except asyncio.TimeoutError:
                pass
        if nom_voulu is None:
            reste = max(1.5, minimum - (time.monotonic() - depart))
            if vue.is_set() or minimum > 0:
                await asyncio.sleep(reste)
    return cartes


def lister(cartes: dict, memorisee: str | None) -> list:
    """Affiche les cartes, de la plus proche (signal le plus fort) à la plus loin."""
    noms = sorted(cartes, key=lambda n: -(cartes[n][1] or -999))
    for i, nom in enumerate(noms, start=1):
        _, signal = cartes[nom]
        journal("%d. %s   signal %s dBm%s" % (i, nom, signal,
                                              "   (carte mémorisée)" if nom == memorisee else ""))
    return noms


def choisir(cartes: dict, nom_voulu: str | None, memorisee: str | None) -> str:
    """Décide quelle carte utiliser ; pose une question si c'est ambigu."""
    if nom_voulu:
        if nom_voulu not in cartes:
            raise ErreurTransfert("carte %s introuvable" % nom_voulu)
        return nom_voulu
    if memorisee in cartes:
        return memorisee
    if len(cartes) == 1:
        nom = next(iter(cartes))
        if memorisee:
            journal("La seule carte trouvée, %s, n'est pas celle utilisée la dernière fois (%s)."
                    % (nom, memorisee))
            reponse = demander("L'utiliser quand même ? [o/N] ")
            if (reponse or "").lower() not in ("o", "oui", "y", "yes"):
                raise ErreurTransfert("transfert annulé (option --carte pour choisir la carte)")
        return nom
    journal("Plusieurs cartes sont en mode chargement :")
    noms = lister(cartes, memorisee)
    reponse = demander("Numéro de votre carte : ")
    if reponse is None or not reponse.isdigit() or not 1 <= int(reponse) <= len(noms):
        raise ErreurTransfert("aucune carte choisie (option --carte pour la désigner)")
    return noms[int(reponse) - 1]


class LiaisonBLE(Liaison):
    """Liaison réelle : caractéristiques du service OTA par Bleak."""

    def __init__(self, client) -> None:
        super().__init__()
        self.client = client

    async def demarrer(self) -> None:
        """Vérifie la présence du service OTA et active les notifications de CTRL."""
        if self.client.services.get_service(UUID_SERVICE) is None:
            raise ErreurTransfert("cet appareil n'a pas le service OTA de l'ObCP")
        try:
            await self.client.start_notify(UUID_CTRL, self._notification)
        except Exception as e:
            raise LiaisonPerdue("connexion Bluetooth perdue (%s)" % e) from None

    def _notification(self, _caracteristique, donnees: bytearray) -> None:
        """Appelée par Bleak à chaque notification de CTRL."""
        self.recevoir_notification(bytes(donnees))

    async def lire_info(self) -> bytes:
        """Lit la caractéristique INFO."""
        try:
            return bytes(await self.client.read_gatt_char(UUID_INFO))
        except Exception as e:
            raise LiaisonPerdue("connexion Bluetooth perdue (%s)" % e) from None

    async def ecrire_commande(self, message: bytes) -> None:
        """Écrit une commande dans CTRL, avec accusé de réception."""
        try:
            await self.client.write_gatt_char(UUID_CTRL, message, response=True)
        except Exception as e:                  # connexion perdue, adaptateur coupé...
            raise LiaisonPerdue("connexion Bluetooth perdue (%s)" % e) from None

    async def ecrire_donnees(self, paquet: bytes) -> None:
        """Écrit un paquet dans DATA, sans réponse."""
        try:
            await self.client.write_gatt_char(UUID_DATA, paquet, response=False)
        except Exception as e:
            raise LiaisonPerdue("connexion Bluetooth perdue (%s)" % e) from None


async def connecter(appareil):
    """Connexion à la carte. Sous Windows, on demande de relire les services
    sur la carte plutôt que dans le cache du système (il pourrait contenir
    ceux d'une autre application).

    Bleak appelle la fonction coupure() dès que la liaison tombe : une
    attente de réponse en cours s'arrête alors immédiatement."""
    bleak = importer_bleak()
    liaison = None

    def coupure(_client) -> None:
        if liaison is not None:
            liaison.signaler_coupure()

    client = bleak.BleakClient(appareil, disconnected_callback=coupure, timeout=20.0,
                               winrt={"use_cached_services": False})
    try:
        await client.connect()
    except Exception as e:
        raise LiaisonPerdue("connexion impossible (%s)" % (e or type(e).__name__)) from None
    liaison = LiaisonBLE(client)
    try:
        await liaison.demarrer()
    except Exception:
        await deconnecter(client)
        raise
    return client, liaison


async def deconnecter(client) -> None:
    """Ferme la connexion Bluetooth (sans erreur si la carte est déjà partie)."""
    try:
        await asyncio.wait_for(client.disconnect(), 5.0)
    except Exception:
        pass                                    # la carte a peut-être déjà redémarré


# ===========================================================================
# Commandes
# ===========================================================================

def preparer_image(source: str, forcer: bool):
    """Étape 1 : trouve et vérifie le programme à envoyer."""
    if os.path.isdir(source):
        elf = trouver_elf(source)
        recente = source_plus_recente(source, elf)
        if recente and not forcer:
            raise ErreurImage(
                "%s a été modifié après la dernière compilation réussie (%s).\n"
                "La compilation a sans doute échoué : corrigez les erreurs (onglet Problems)\n"
                "puis relancez. Option --forcer pour envoyer quand même l'ancien programme."
                % (os.path.relpath(recente, source), decrire_date(elf)))
        chemin = elf
    else:
        chemin = source
    image = charger(chemin)
    verifier(image)
    etape(1, "Programme : %s" % os.path.abspath(chemin))
    journal("format %s, créé le %s" % (image.format, decrire_date(chemin)))
    journal("%s octets à partir de 0x%08X, CRC-32 0x%08X"
            % (nombre(image.taille), image.adresse, image.crc32))
    return image


async def passer_en_chargement(nom: str, appareil) -> None:
    """La carte visée fonctionne normalement (son programme annonce) : on lui
    envoie la commande « #ota », comme on la taperait dans un terminal. Son
    programme répond, ferme la connexion et redémarre dans le chargeur."""
    journal("%s fonctionne normalement : envoi de la commande #ota..." % nom)
    bleak = importer_bleak()
    client = bleak.BleakClient(appareil, timeout=20.0, winrt={"use_cached_services": False})
    try:
        await client.connect()
    except Exception as e:
        raise ErreurTransfert(
            "connexion impossible au programme de la carte %s (%s).\n"
            "Mettez-la en mode chargement : USER maintenu pendant un appui sur RESET."
            % (nom, e or type(e).__name__)) from None
    try:
        await client.write_gatt_char(UUID_RX_PROGRAMME, COMMANDE_OTA, response=True)
    except Exception:
        # Comme pour RUN sous Windows : la carte a pu fermer la liaison avant
        # que l'accusé de réception arrive. On cherche le chargeur quand même.
        journal("liaison coupée pendant l'envoi (la carte a sans doute déjà reçu #ota)")
    finally:
        await deconnecter(client)
    journal("la carte redémarre dans son chargeur...")


async def retrouver_carte(nom: str, fin_envoyee: bool):
    """Après une coupure, attend que la carte réapparaisse : le chargeur se
    remet à annoncer dès qu'il constate la déconnexion."""
    cartes = await chercher_cartes(nom, DELAI_RECONNEXION, aide=False)
    if nom in cartes:
        return cartes[nom][0]
    if fin_envoyee:
        raise ErreurTransfert(
            "la carte %s n'est plus visible après la coupure.\n"
            "Elle a peut-être déjà redémarré sur le nouveau programme : s'il fonctionne, c'est\n"
            "qu'il a été vérifié et validé (le chargeur ne lance jamais un programme dont le\n"
            "CRC est faux). Sinon, remettez la carte en mode chargement et relancez." % nom)
    raise ErreurTransfert("la carte %s n'est plus visible après la coupure : remettez-la en mode "
                          "chargement (USER + RESET) et relancez le transfert." % nom)


def afficher_diagnostic(info) -> None:
    """Après une reconnexion : ce que la carte a noté sur la coupure
    (chargeur 1.1 ; un chargeur 1.0 ne fournit pas ces renseignements)."""
    if info.cause_deconnexion:
        journal("la carte indique : %s" % decrire_cause_deconnexion(info.cause_deconnexion))
    if info.redemarrage_module:
        journal("la carte indique que son module Bluetooth a redémarré : %s"
                % decrire_redemarrage_module(info.redemarrage_module))


async def commande_transfert(args, image) -> int:
    """Étapes 2 à 4 : trouver la carte, s'y connecter, transférer le programme.

    Si la liaison Bluetooth est coupée en cours de route, on se reconnecte
    (NB_CONNEXIONS connexions au plus) et on relit INFO :
      - coupure après END et programme présent : la carte l'avait vérifié et
        validé juste avant la coupure, le transfert est réussi ;
      - sinon, le transfert recommence au début (START).
    """
    nom_voulu = normaliser_nom(args.carte) if args.carte else None
    memoire = lire_memoire()
    memorisee = memoire.get("carte")

    # Étape 2 : trouver la carte. Si l'on connaît l'adresse de son chargeur,
    # on guette aussi son programme : la carte fonctionne peut-être
    # normalement, et la commande « #ota » la fera passer dans le chargeur.
    cible = nom_voulu or memorisee
    programme = adresse_du_programme(memoire.get("adresse")) if cible and cible == memorisee else None
    etape(2, "Recherche de la carte%s..." % (" %s" % cible if cible else ""))
    cartes = await chercher_cartes(nom_voulu, args.attente, programme=programme)
    vu = cartes.pop(PROGRAMME, None)
    if vu is not None and cible not in cartes:
        await passer_en_chargement(cible, vu[0])
        cartes = await chercher_cartes(cible, DELAI_CHARGEUR, aide=False)
        if cible not in cartes:
            raise ErreurTransfert(
                "la carte %s n'est pas passée dans son chargeur : son programme ne connaît peut-être\n"
                "pas la commande #ota. Mettez-la en mode chargement (USER maintenu pendant un appui\n"
                "sur RESET), puis relancez le transfert." % cible)
    if not cartes:
        raise ErreurTransfert("aucune carte en mode chargement trouvée en %d s" % args.attente)
    if nom_voulu is None and memorisee and memorisee not in cartes and len(cartes) == 1:
        # La carte mémorisée met peut-être un peu plus de temps à apparaître.
        encore = await chercher_cartes(memorisee, 3.0, aide=False)
        cartes.update(encore)
    nom = choisir(cartes, nom_voulu, memorisee)
    appareil, signal = cartes[nom]
    memoriser_carte(nom, appareil.address)
    journal("%s trouvée (signal %s dBm)" % (nom, signal))

    # Étapes 3 et 4 : connexion, lecture de INFO, transfert
    etape(3, "Connexion...")
    depart = time.monotonic()
    duree = 0.0
    perte = None              # dernière coupure (exception LiaisonPerdue)
    fin_envoyee = False       # END envoyé depuis le dernier START
    infos_affichees = False   # renseignements de la carte déjà affichés
    for connexion in range(1, NB_CONNEXIONS + 1):
        if perte is not None:
            journal("%s ; nouvelle connexion (%d/%d)..." % (perte, connexion, NB_CONNEXIONS))
            appareil = await retrouver_carte(nom, fin_envoyee)
        client = None
        try:
            client, liaison = await connecter(appareil)
            info = await lire_et_verifier_info(liaison, image)
            if not infos_affichees:
                journal("chargeur version %s, application déjà installée : %s"
                        % (info.version_chargeur, "oui" if info.application_presente else "non"))
                journal("mode chargement activé par : %s" % info.raison_demarrage)
                etape(4, "Transfert")
                infos_affichees = True
            else:
                afficher_diagnostic(info)

            if fin_envoyee and info.application_presente:
                # La carte n'installe un programme qu'après avoir vérifié son CRC.
                journal("la carte avait vérifié (CRC) et validé le programme avant la coupure")
                if not args.sans_lancer:
                    await envoyer_run(liaison)
                duree = time.monotonic() - depart
            else:
                try:
                    duree = await transferer(liaison, image, lancer=not args.sans_lancer,
                                             progression=Progression(), journal=journal, info=info)
                except LiaisonPerdue as e:
                    fin_envoyee = e.fin_envoyee     # ce transfert a renvoyé START
                    raise
                journal("vérification par l'ObCP : CRC-32 0x%08X correct" % image.crc32)
            perte = None
        except LiaisonPerdue as e:
            perte = e
        finally:
            if client is not None:
                await deconnecter(client)
        if perte is None:
            break
    if perte is not None:
        raise perte

    print("Terminé : %s octets en %s s (%s Ko/s)." % (
        nombre(image.taille), decimal(duree), decimal(image.taille / 1024 / max(duree, 1e-3))), flush=True)
    if args.sans_lancer:
        print("Le nouveau programme démarrera au prochain appui court sur USER ou sur RESET.", flush=True)
    else:
        print("L'ObCP redémarre sur le nouveau programme.", flush=True)
    return 0


async def commande_liste(args) -> int:
    """Option --liste : affiche les cartes en mode chargement et mémorise la sienne."""
    memorisee = lire_carte_memorisee()
    print("Recherche des cartes en mode chargement (%d s)..." % args.duree_liste, flush=True)
    cartes = await chercher_cartes(None, args.duree_liste, minimum=args.duree_liste, aide=False)
    if not cartes:
        journal("Aucune carte trouvée.")
        journal(AIDE_MODE_CHARGEMENT)
        return 1
    noms = lister(cartes, memorisee)
    reponse = demander("Numéro de VOTRE carte (Entrée = ne rien changer) : ")
    if not reponse:
        return 0
    if not reponse.isdigit() or not 1 <= int(reponse) <= len(noms):
        journal("Numéro invalide.")
        return 1
    nom = noms[int(reponse) - 1]
    client, liaison = await connecter(cartes[nom][0])
    try:
        await identifier(liaison)
    finally:
        await deconnecter(client)
    memoriser_carte(nom, cartes[nom][0].address)
    journal("%s est mémorisée : les prochains transferts la viseront." % nom)
    journal("Sa LED clignote en blanc pendant 3 s : vérifiez que c'est bien la vôtre !")
    return 0


# ===========================================================================
# Programme principal
# ===========================================================================

def analyser_arguments(argv):
    """Lit les options de la ligne de commande (module argparse)."""
    parser = argparse.ArgumentParser(
        prog="obcp_ota.py", add_help=False,
        description="Charge un programme dans l'ObCP par Bluetooth (chargeur ObCP-OTA).")
    parser.add_argument("-h", "--help", action="help", help="affiche cette aide")
    parser.add_argument("source", nargs="?",
                        help="fichier .elf, .hex ou .bin, ou dossier d'un projet STM32CubeIDE")
    parser.add_argument("--carte", metavar="NOM",
                        help="carte à utiliser, par exemple A1B2 ou ObCP-OTA-A1B2")
    parser.add_argument("--liste", action="store_true",
                        help="cherche les cartes en mode chargement et mémorise la vôtre")
    parser.add_argument("--oublier", action="store_true",
                        help="oublie la carte mémorisée")
    parser.add_argument("--sans-lancer", action="store_true",
                        help="ne redémarre pas la carte après le transfert")
    parser.add_argument("--attente", type=float, default=30.0, metavar="S",
                        help="temps laissé pour trouver la carte (défaut : 30 s)")
    parser.add_argument("--forcer", action="store_true",
                        help="envoie le .elf même s'il est plus ancien que les sources")
    parser.add_argument("--duree-liste", type=float, default=5.0, help=argparse.SUPPRESS)
    parser.add_argument("--version", action="version", version="obcp_ota " + VERSION,
                        help="affiche la version")
    args = parser.parse_args(argv)
    if not (args.source or args.liste or args.oublier):
        parser.error("indiquez un fichier ou un projet à envoyer, ou --liste")
    return args


def main(argv=None) -> int:
    """Programme principal ; renvoie 0 si tout s'est bien passé, 1 sinon."""
    # Les accents doivent s'afficher correctement, même si la console ne sait
    # pas les représenter (ils sont alors remplacés par « ? »).
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    args = analyser_arguments(argv)
    print("ObCP OTA %s - chargement d'un programme par Bluetooth" % VERSION, flush=True)

    if args.oublier:
        memoriser_carte(None)
        print("Carte mémorisée oubliée.", flush=True)
        if not (args.source or args.liste):
            return 0
    try:
        if args.liste:
            return asyncio.run(commande_liste(args))
        image = preparer_image(args.source, args.forcer)
        return asyncio.run(commande_transfert(args, image))
    except ErreurImage as e:
        print("ERREUR (programme) : %s" % e, flush=True)
    except ErreurTransfert as e:
        print("ERREUR (transfert) : %s" % e, flush=True)
        if e.apres_effacement:
            print("L'ancienne application a été effacée : la carte reste en mode "
                  "chargement (LED violette), relancez simplement le transfert.", flush=True)
    except KeyboardInterrupt:
        print("Interrompu.", flush=True)
    except Exception as e:                       # Bluetooth absent ou désactivé, etc.
        print("ERREUR (Bluetooth) : %s" % (e or type(e).__name__), flush=True)
        print("Le Bluetooth du PC est-il activé ? La bibliothèque bleak est-elle à jour "
              "(python -m pip install -U bleak) ?", flush=True)
    return 1


if __name__ == "__main__":
    sys.exit(main())
