# -*- coding: utf-8 -*-
"""
ota_transfert.py - Algorithme de transfert d'un programme vers le chargeur.

Ce module ne dépend pas du Bluetooth : il parle à une « liaison » (classe
Liaison ci-dessous) qui sait lire INFO, écrire dans CTRL et DATA, et
recevoir les notifications. Si la liaison est coupée, l'exception
LiaisonPerdue indique à quel moment (avant ou après END) ; la reprise
(reconnexion) est faite par obcp_ota.py. Deux liaisons existent :
  - LiaisonBLE (dans obcp_ota.py) : la vraie, avec la bibliothèque Bleak ;
  - LiaisonSimulee (dans Tests/) : appelle directement le code C du
    chargeur compilé sur le PC, pour tester sans carte.

Python « asynchrone » (async / await)
------------------------------------
Le Bluetooth répond avec un délai. Plutôt que de bloquer le programme, on
écrit les fonctions avec « async def » et on attend une réponse avec
« await » : pendant l'attente, Python peut traiter autre chose (par exemple
les notifications qui arrivent). C'est le fonctionnement de la bibliothèque
Bleak.
"""

from __future__ import annotations

import asyncio
import time
from typing import Callable, Optional

from ota_image import Image
from ota_protocole import (
    CMD_ABORT, CMD_END, CMD_IDENTIFY, CMD_RUN, ERR_CRC, ERR_FLASH, ERR_INCOMPLETE,
    ERR_OVERFLOW, ERR_SEQUENCE, MESSAGES_ERREUR, OCTETS_PAR_PAQUET, RSP_ACK,
    RSP_DONE, RSP_READY, TAILLE_BLOC, TAILLE_PAGE_FLASH, VERSION_PROTOCOLE,
    InfoChargeur, commande_simple, commande_start, decoder_info, decoder_reponse,
    nom_reponse, paquet_donnees,
)

# Délais d'attente des réponses (secondes)
DELAI_ACK = 5.0             # un bloc de 1024 octets prend normalement 0,2 à 0,5 s
DELAI_DONE = 10.0           # calcul du CRC sur tout le programme
DELAI_EFFACEMENT_BASE = 3.0
DELAI_PAR_PAGE = 0.03       # effacement d'une page : 22 à 25 ms d'après la notice

# Erreurs pour lesquelles un nouvel essai a des chances de réussir
ERREURS_PASSAGERES = (ERR_SEQUENCE, ERR_CRC, ERR_FLASH, ERR_OVERFLOW, ERR_INCOMPLETE)


class ErreurTransfert(Exception):
    """Échec du transfert. 'reessayable' indique si un nouvel essai peut réussir."""

    def __init__(self, message: str, reessayable: bool = False):
        super().__init__(message)
        self.reessayable = reessayable
        self.apres_effacement = False    # vrai si la carte a déjà effacé l'ancienne application


class LiaisonPerdue(ErreurTransfert):
    """La liaison Bluetooth a été coupée.

    'fin_envoyee' est vrai si la coupure a eu lieu après l'envoi de END : la
    carte a peut-être vérifié et validé le programme juste avant. Le PC le
    saura en se reconnectant et en relisant INFO (obcp_ota.py).
    """

    def __init__(self, message: str = "connexion Bluetooth perdue"):
        super().__init__(message, reessayable=False)
        self.fin_envoyee = False


class Liaison:
    """Interface commune aux liaisons (Bluetooth réel ou simulation).

    Une classe dérivée doit fournir lire_info, ecrire_commande et
    ecrire_donnees, et appeler recevoir_notification() pour chaque
    notification de la caractéristique CTRL.
    """

    def __init__(self) -> None:
        self._reponses: asyncio.Queue = asyncio.Queue()
        self.coupee = False                 # la liaison a été coupée

    async def lire_info(self) -> bytes:
        """Lit la caractéristique INFO (16 octets avec un chargeur 1.0, 18 avec un 1.1)."""
        raise NotImplementedError

    async def ecrire_commande(self, message: bytes) -> None:
        """Écrit une commande dans CTRL (écriture avec accusé de réception)."""
        raise NotImplementedError

    async def ecrire_donnees(self, paquet: bytes) -> None:
        """Écrit un paquet dans DATA (écriture sans réponse, plus rapide)."""
        raise NotImplementedError

    # --- réception des réponses -------------------------------------------
    def signaler_coupure(self) -> None:
        """À appeler dès que la liaison est coupée : une attente de réponse en
        cours s'arrête aussitôt (au lieu d'attendre la fin de son délai)."""
        self.coupee = True
        self._reponses.put_nowait(None)     # None réveille attendre_reponse()

    def recevoir_notification(self, octets: bytes) -> None:
        """À appeler à chaque notification reçue sur CTRL."""
        try:
            self._reponses.put_nowait(decoder_reponse(octets))
        except ValueError:
            pass                       # notification de taille inattendue : ignorée

    def vider_reponses(self) -> None:
        """Oublie les réponses arrivées et non lues."""
        while not self._reponses.empty():
            self._reponses.get_nowait()

    async def attendre_reponse(self, delai: float) -> tuple[int, int]:
        """Attend la prochaine réponse de la carte : (code, valeur)."""
        if self.coupee:
            raise LiaisonPerdue()
        try:
            reponse = await asyncio.wait_for(self._reponses.get(), delai)
        except asyncio.TimeoutError:
            raise ErreurTransfert("la carte ne répond pas (attente de %.0f s dépassée)" % delai,
                                  reessayable=True) from None
        if reponse is None:                 # signaler_coupure() a été appelée
            raise LiaisonPerdue()
        return reponse


def erreur_de_la_carte(code: int, valeur: int, attendu: str) -> ErreurTransfert:
    """Transforme une réponse inattendue de la carte en message clair."""
    if code in MESSAGES_ERREUR:
        message = "la carte signale : %s (code 0x%02X, valeur %d)" % (MESSAGES_ERREUR[code], code, valeur)
        return ErreurTransfert(message, reessayable=code in ERREURS_PASSAGERES)
    return ErreurTransfert("réponse %s alors que %s était attendu" % (nom_reponse(code), attendu),
                           reessayable=True)


async def lire_et_verifier_info(liaison: Liaison, image: Image) -> InfoChargeur:
    """Lit INFO et vérifie que la carte peut recevoir ce programme."""
    info = decoder_info(await liaison.lire_info())
    if info.version_protocole != VERSION_PROTOCOLE:
        raise ErreurTransfert(
            "version de protocole %d sur la carte, %d dans ce programme : mettez à jour "
            "le chargeur ou l'application PC" % (info.version_protocole, VERSION_PROTOCOLE))
    if image.adresse != info.adresse_application:
        raise ErreurTransfert("la carte attend un programme à 0x%08X" % info.adresse_application)
    if image.taille > info.taille_max:
        raise ErreurTransfert("programme trop gros pour la carte (%d octets au maximum)" % info.taille_max)
    if info.taille_bloc != TAILLE_BLOC or info.octets_par_paquet != OCTETS_PAR_PAQUET:
        raise ErreurTransfert("réglages du chargeur inattendus (bloc %d, paquet %d)"
                              % (info.taille_bloc, info.octets_par_paquet))
    return info


async def _un_essai(liaison: Liaison, image: Image,
                    progression: Optional[Callable[[int, int], None]],
                    journal: Callable[[str], None]) -> None:
    """Un transfert complet : START, données, END. Lève ErreurTransfert."""
    donnees = image.donnees
    taille = len(donnees)
    crc = image.crc32

    # 1) START : la carte efface les pages nécessaires puis répond READY.
    pages = (taille + TAILLE_PAGE_FLASH - 1) // TAILLE_PAGE_FLASH
    journal("effacement de %d page%s de flash..." % (pages, "s" if pages > 1 else ""))
    liaison.vider_reponses()
    await liaison.ecrire_commande(commande_start(taille, crc))
    code, valeur = await liaison.attendre_reponse(DELAI_EFFACEMENT_BASE + DELAI_PAR_PAGE * pages)
    if code != RSP_READY:
        raise erreur_de_la_carte(code, valeur, "READY")

    # 2) Les données, bloc par bloc. Dans un bloc, les paquets partent sans
    #    attendre ; à la fin du bloc on attend l'accusé (ACK) de la carte.
    numero = 0
    if progression:
        progression(0, taille)
    for debut in range(0, taille, TAILLE_BLOC):
        fin = min(debut + TAILLE_BLOC, taille)
        for position in range(debut, fin, OCTETS_PAR_PAQUET):
            morceau = donnees[position:min(position + OCTETS_PAR_PAQUET, fin)]
            await liaison.ecrire_donnees(paquet_donnees(numero, morceau))
            numero = (numero + 1) & 0xFF          # 255 + 1 -> 0
        code, valeur = await liaison.attendre_reponse(DELAI_ACK)
        if code != RSP_ACK:
            raise erreur_de_la_carte(code, valeur, "ACK")
        if valeur != fin:
            raise ErreurTransfert("la carte a reçu %d octets au lieu de %d" % (valeur, fin),
                                  reessayable=True)
        if progression:
            progression(fin, taille)

    # 3) END : la carte recalcule le CRC sur ce qu'elle a écrit et valide
    #    le programme s'il est identique. Une coupure à partir d'ici est
    #    signalée avec fin_envoyee = True : le programme a peut-être déjà
    #    été validé par la carte.
    try:
        await liaison.ecrire_commande(commande_simple(CMD_END))
        code, valeur = await liaison.attendre_reponse(DELAI_DONE)
    except LiaisonPerdue as perte:
        perte.fin_envoyee = True
        raise
    if code != RSP_DONE:
        raise erreur_de_la_carte(code, valeur, "DONE")
    if valeur != crc:
        raise ErreurTransfert("CRC calculé par la carte (0x%08X) différent de 0x%08X" % (valeur, crc),
                              reessayable=True)


async def transferer(liaison: Liaison, image: Image, *, lancer: bool = True, tentatives: int = 3,
                     progression: Optional[Callable[[int, int], None]] = None,
                     journal: Callable[[str], None] = print,
                     info: Optional[InfoChargeur] = None) -> float:
    """Envoie le programme, avec de nouveaux essais en cas d'erreur passagère.

    Renvoie la durée du transfert réussi (en secondes). Si 'lancer' est
    vrai, la carte redémarre ensuite sur le nouveau programme. 'info' est
    le contenu de INFO s'il a déjà été lu et vérifié.
    """
    if info is None:
        await lire_et_verifier_info(liaison, image)

    for essai in range(1, tentatives + 1):
        depart = time.monotonic()
        try:
            await _un_essai(liaison, image, progression, journal)
            duree = time.monotonic() - depart
            break
        except ErreurTransfert as erreur:
            erreur.apres_effacement = True      # START a été envoyé
            if not erreur.reessayable or essai == tentatives:
                raise
            journal("%s ; nouvel essai (%d/%d)" % (erreur, essai + 1, tentatives))
            await liaison.ecrire_commande(commande_simple(CMD_ABORT))
            await asyncio.sleep(0.5)             # laisse arriver les réponses en retard
            liaison.vider_reponses()

    if lancer:
        await envoyer_run(liaison)
    return duree


async def envoyer_run(liaison: Liaison) -> None:
    """Demande à la carte de redémarrer sur le nouveau programme (RUN).

    À ce stade le programme est déjà validé par la carte (réponse DONE avec
    le bon CRC). Le chargeur 1.0 redémarre dès qu'il reçoit RUN : il coupe
    la liaison AVANT que l'accusé de réception de l'écriture ne parvienne
    au PC, et Windows signale alors une « opération annulée »
    (WinError -2147023673). Le chargeur 1.1 attend que le PC se déconnecte,
    mais une coupure à ce moment-là reste sans conséquence : on l'ignore.
    """
    try:
        await liaison.ecrire_commande(commande_simple(CMD_RUN))
    except ErreurTransfert:
        pass                    # la carte a redémarré avant d'accuser réception


async def identifier(liaison: Liaison) -> None:
    """Demande à la carte de faire clignoter sa LED en blanc (3 s)."""
    await liaison.ecrire_commande(commande_simple(CMD_IDENTIFY))
