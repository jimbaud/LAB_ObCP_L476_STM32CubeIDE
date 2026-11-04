#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Essai de bout en bout de Tools/maj_module_ota.py : le script envoie une image
.ota (en-tête au format ST, SHA-256) à l'application qui tourne sur le PC face
au module simulé (build/pty_sim), par un pseudo-terminal.
  1) image valide : mise à jour 2.0.97 -> 2.0.106 attendue ;
  2) image corrompue : refusée par le module, firmware conservé ;
  3) si lrzsz est installé : envoi par « sz --ymodem -k » (autre émetteur YMODEM).
"""
import hashlib
import os
import random
import shutil
import struct
import subprocess
import sys
import time

ICI = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(ICI, "build")
SCRIPT = os.path.join(ICI, "..", "Tools", "maj_module_ota.py")


def image(chemin, version, taille, corrompre=False):
    donnees = bytearray(random.Random(taille).getrandbits(8) for _ in range(taille))
    entete = bytearray(b"\xff" * 512)
    entete[0:16] = b"QCC74X_OTAVer1.0"
    entete[16:20] = b"XZ  "
    entete[20:24] = struct.pack("<I", taille)
    entete[24:32] = bytes(range(1, 9))
    entete[32:48] = b"QCC_Module_v1.1".ljust(16, b"\0")
    entete[48:64] = version.encode().ljust(16, b"\0")
    entete[64:96] = hashlib.sha256(donnees).digest()
    if corrompre:
        donnees[taille // 2] ^= 0x01
    with open(chemin, "wb") as f:
        f.write(bytes(entete) + bytes(donnees))


def envoyer(port, chemin):
    r = subprocess.run([sys.executable, SCRIPT, "--port", port, chemin], capture_output=True,
                       text=True, encoding="utf-8", errors="replace", timeout=240)
    return r.returncode, r.stdout + r.stderr


def envoyer_sz(port, chemin):
    """Tape « module ota », puis laisse sz (lrzsz) envoyer le fichier sur le port."""
    import serial
    s = serial.Serial(port, 115200, timeout=0.1)
    s.reset_input_buffer()
    s.write(b"module ota\r")
    texte = b""
    fin = time.monotonic() + 10
    while b"En attente du fichier" not in texte and time.monotonic() < fin:
        texte += s.read(256)
    s.close()
    with open(port, "r+b", buffering=0) as tty:
        r = subprocess.run(["sz", "--ymodem", "-k", chemin], stdin=tty, stdout=tty,
                           stderr=subprocess.DEVNULL, timeout=120)
    s = serial.Serial(port, 115200, timeout=0.2)
    sortie = b""
    fin = time.monotonic() + 30
    while time.monotonic() < fin:
        sortie += s.read(512)
        if b"mis \xc3\xa0 jour" in sortie and sortie.rstrip().endswith((b"obcp>", b"st67>")):
            break
    s.close()
    return r.returncode, sortie.decode("utf-8", errors="replace")


def main():
    try:
        import serial  # noqa: F401
    except ImportError:
        print("pyserial absent : essai de bout en bout du script ignoré (pip install pyserial)")
        return 0
    bon = os.path.join(BUILD, "st67w611m_mission_t01_v2.0.106.xz.ota")
    mauvais = os.path.join(BUILD, "st67w611m_mission_t01_v2.0.107.xz.ota")
    image(bon, "2.0.106", 150000)
    image(mauvais, "2.0.107", 40000, corrompre=True)

    sim = subprocess.Popen([os.path.join(BUILD, "pty_sim"), os.path.join(BUILD, "pty_transcript.txt"), "200"],
                           stdout=subprocess.PIPE, text=True)
    try:
        port = sim.stdout.readline().strip()
        time.sleep(1.5)                                     # démarrage de l'application
        code1, sortie1 = envoyer(port, bon)
        ok1 = (code1 == 0) and ("mis à jour : 2.0.97 -> 2.0.106" in sortie1)
        code2, sortie2 = envoyer(port, mauvais)
        ok2 = (code2 != 0) and ("image refusée par le module" in sortie2) and ("conserve son firmware" in sortie2)
        ok3, sortie3, avec_sz = True, "", shutil.which("sz") is not None
        if avec_sz:
            autre = os.path.join(BUILD, "st67w611m_mission_t01_v2.0.120.xz.ota")
            image(autre, "2.0.120", 90000)
            code3, sortie3 = envoyer_sz(port, autre)
            ok3 = (code3 == 0) and ("mis à jour : 2.0.106 -> 2.0.120" in sortie3)
        with open(os.path.join(BUILD, "pty_script_sortie.txt"), "w", encoding="utf-8") as f:
            f.write(sortie1 + "\n----\n" + sortie2 + "\n----\n" + sortie3)
        ok = ok1 and ok2 and ok3
        if not ok:
            print(sortie1)
            print("----")
            print(sortie2)
            print("----")
            print(sortie3)
        print("Essai de bout en bout du script maj_module_ota.py (YMODEM, module simulé) : %s"
              % (("réussi (mise à jour, image corrompue refusée%s)" % (", envoi par sz --ymodem" if avec_sz else ""))
                 if ok else "ÉCHEC"))
        return 0 if ok else 1
    finally:
        sim.terminate()
        sim.wait(5)


if __name__ == "__main__":
    sys.exit(main())
