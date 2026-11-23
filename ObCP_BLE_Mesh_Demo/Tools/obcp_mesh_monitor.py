#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
obcp_mesh_monitor.py - Supervision du réseau maillé des cartes ObCP
===================================================================

Une ObCP branchée en USB sert de « passerelle » : elle entend le réseau
comme les autres, et transmet au PC chaque évènement sous forme d'une ligne
JSON (commande « pc on », envoyée par ce programme). Le programme dessine :

  - la CARTE DU RÉSEAU : un rond par nœud, coloré selon le nombre de relais
    qu'il faut pour atteindre la passerelle (mêmes couleurs que la vague :
    rouge = voisin direct, orange, jaune, vert...), et les liaisons radio
    annoncées dans les trames HELLO (vert : forte, orange : moyenne, rouge :
    faible). Un nœud qui vient d'émettre est entouré d'un cercle ;
  - le TABLEAU des nœuds : relais, RSSI, température, dernière trame ;
  - le JOURNAL : messages reçus, réponses aux pings et aux traces...
  - une ZONE DE COMMANDE : les commandes du terminal (« ping 5 »,
    « vague », « regle tous ttl 3 »...) sont envoyées à la passerelle.

Utilisation (depuis le dossier du projet) :
    python Tools/obcp_mesh_monitor.py                 cherche l'ObCP sur les ports USB
    python Tools/obcp_mesh_monitor.py COM7            port donné (Windows)
    python Tools/obcp_mesh_monitor.py /dev/ttyACM0    port donné (Linux)
    python Tools/obcp_mesh_monitor.py --demo          sans carte : réseau simulé
    python Tools/obcp_mesh_monitor.py --texte         sans fenêtre : évènements en texte
    python Tools/obcp_mesh_monitor.py --enregistre journal.jsonl
                                                      garde chaque ligne JSON reçue
                                                      (analyse après la séance)

Il faut Python 3.8 ou plus récent, et :
    pip install pyserial matplotlib
(le mode --demo --texte n'a besoin de rien).

Format des lignes JSON : documentation du programme
(Doc/Documentation_Programme_BLE_Mesh_ObCP.pdf), « Lignes JSON pour l'outil PC ».
"""
import argparse
import json
import math
import queue
import random
import sys
import textwrap
import threading
import time
from collections import deque

VERSION = "1.2.1"

# Couleurs de la vague (led_fx.c), selon le nombre de relais traversés.
HOPS_COLORS = ["#ff0000", "#ff5000", "#ffc800", "#00c000", "#00c8ff", "#0040ff", "#a000ff", "#ff2870"]
GATEWAY_COLOR = "#ffffff"
UNKNOWN_COLOR = "#c8c8c8"

NODE_TIMEOUT_S = 120.0       # nœud oublié (grisé) au-delà
LINK_TIMEOUT_S = 90.0        # liaison oubliée au-delà
FLASH_S = 0.5                # durée du cercle « vient d'émettre »


# =============================================================================
# Modèle du réseau
# =============================================================================
class Node:
    """Ce que l'on sait d'un nœud."""

    def __init__(self, node_id):
        self.id = node_id
        self.tag = None
        self.relay = None
        self.fixed = None
        self.hops = None           # relais traversés jusqu'à la passerelle
        self.rssi = None           # RSSI à la passerelle (si voisin direct)
        self.temp = None
        self.accel = None          # (x, y, z) en mg
        self.last_seen = 0.0
        self.frames = 0


class NetworkModel:
    """Tient à jour l'état du réseau à partir des lignes reçues."""

    def __init__(self):
        self.gateway = None        # numéro de la passerelle
        self.net = None
        self.info = {}
        self.nodes = {}            # numéro -> Node
        self.links = {}            # (a, b), a < b -> (rssi, instant)
        self.flash = {}            # numéro -> instant de la dernière émission vue
        self.log = deque(maxlen=200)
        self.counts = {"rx": 0, "dup": 0, "relay": 0, "tx": 0}

    # ---- outils ----------------------------------------------------------
    def node(self, node_id):
        if node_id not in self.nodes:
            self.nodes[node_id] = Node(node_id)
        return self.nodes[node_id]

    def add_log(self, text, now):
        self.log.append((now, text))

    def set_link(self, a, b, rssi, now):
        if a == b or a is None or b is None:
            return
        key = (min(a, b), max(a, b))
        old = self.links.get(key)
        if old is not None and (now - old[1]) < 30.0:
            rssi = round((old[0] + rssi) / 2)          # moyenne des deux sens
        self.links[key] = (rssi, now)

    def fresh_nodes(self, now):
        return {k: n for k, n in self.nodes.items() if now - n.last_seen < NODE_TIMEOUT_S}

    def fresh_links(self, now):
        return {k: v for k, v in self.links.items() if now - v[1] < LINK_TIMEOUT_S}

    # ---- lignes reçues ------------------------------------------------------
    def handle_line(self, line, now):
        """Ligne reçue de la passerelle : JSON ou texte."""
        line = line.strip()
        if not line:
            return
        if line.startswith("{"):
            try:
                event = json.loads(line)
            except ValueError:
                self.add_log("(ligne illisible) " + line[:60], now)
                return
            self.handle_event(event, now)
        else:
            self.add_log(line, now)

    def handle_hello(self, ev, now):
        """Liste des voisins d'un HELLO (reçu, ou émis par la passerelle)."""
        src = ev.get("src")
        n = self.node(src)
        n.tag = ev.get("tag", n.tag)
        n.relay = ev.get("relay", n.relay)
        n.fixed = ev.get("fixed", n.fixed)
        for pair in ev.get("nb", []):
            if isinstance(pair, list) and len(pair) == 2:
                self.set_link(src, pair[0], pair[1], now)
                self.node(pair[0])                    # au moins connu de nom

    def handle_event(self, ev, now):
        kind = ev.get("ev")
        if kind == "info":
            self.info = ev
            self.gateway = ev.get("id")
            self.net = ev.get("net")
            g = self.node(self.gateway)
            g.last_seen = now
            g.hops = -1
            module = ""
            if ev.get("fw"):                          # pile du module (programme 1.1.0 et plus)
                module = ", pile %s%s, écoute %s pendant l'émission" % (
                    ev["fw"], ", mode 4" if ev.get("stack_mode4") else "",
                    "maintenue" if ev.get("mode") == "simultane" else "arrêtée")
            self.add_log("Passerelle : nœud %s, réseau %s (%s %s%s)" % (
                ev.get("id"), ev.get("net"), ev.get("name", "?"), ev.get("version", "?"), module), now)
            return
        if kind == "id_changed":
            old, new = ev.get("old"), ev.get("new")
            if old == self.gateway:
                self.gateway = new
            self.add_log("Numéro de la passerelle changé : %s -> %s" % (old, new), now)
            return
        if kind in ("rx", "dup", "echo"):
            self.counts[kind if kind != "echo" else "dup"] += 1
            last = ev.get("last")
            self.flash[last] = now
            if last is not None and self.gateway is not None:
                self.set_link(self.gateway, last, ev.get("rssi", -80), now)
                ln = self.node(last)
                ln.last_seen = now
                ln.hops = 0
                ln.rssi = ev.get("rssi")
        if kind == "rx":
            src = ev.get("src")
            n = self.node(src)
            n.last_seen = now
            n.frames += 1
            n.hops = ev.get("hops", n.hops)
            if ev.get("hops") == 0:
                n.rssi = ev.get("rssi")
            t = ev.get("type")
            if t == "HELLO":
                self.handle_hello(ev, now)
            elif t == "MESURE":
                if ev.get("temp_ok"):
                    n.temp = ev.get("temp")
                if ev.get("acc_ok"):
                    n.accel = (ev.get("ax"), ev.get("ay"), ev.get("az"))
            elif t == "TEXTE":
                self.add_log("Message de %s (%s relais) : %s" % (src, ev.get("hops"), ev.get("text", "")), now)
            elif t == "VAGUE":
                self.add_log("Vague lancée par %s" % src, now)
            elif t == "REGLAGE":
                self.add_log("Réglage de %s vers %s : %s = %s" % (
                    src, self.dest(ev.get("dst")), ev.get("key"), ev.get("value")), now)
        elif kind in ("tx", "relay"):
            self.counts[kind] += 1
            if self.gateway is not None:
                self.flash[self.gateway] = now
                self.node(self.gateway).last_seen = now
            if kind == "tx" and ev.get("type") == "HELLO":
                self.handle_hello(ev, now)
        elif kind == "pong":
            self.add_log("Réponse de %s au ping n°%s : %s ms (aller %s relais, retour %s)" % (
                ev.get("from"), ev.get("ping"), ev.get("rtt"), ev.get("hops_out"), ev.get("hops_back")), now)
        elif kind == "ping_end":
            if ev.get("replies", 0) == 0:
                self.add_log("Pas de réponse au ping n°%s vers %s" % (ev.get("ping"), self.dest(ev.get("to"))), now)
            elif ev.get("to", 0) > 239:
                self.add_log("Ping n°%s vers %s : %s réponse(s)" % (
                    ev.get("ping"), self.dest(ev.get("to")), ev.get("replies")), now)
        elif kind == "trace":
            me = self.gateway
            fwd = " > ".join(str(x) for x in [me] + ev.get("fwd", []) + [ev.get("to")])
            back = " > ".join(str(x) for x in [ev.get("to")] + ev.get("back", []) + [me])
            self.add_log("Route vers %s : aller %s ; retour %s (%s ms)" % (ev.get("to"), fwd, back, ev.get("rtt")), now)
        elif kind == "trace_timeout":
            self.add_log("Pas de réponse à la trace vers %s" % ev.get("to"), now)
        elif kind == "conflict":
            self.add_log("Numéro %s en double !" % ev.get("src"), now)
        elif kind == "setting":
            self.add_log("Réglage de la passerelle changé par %s : %s = %s" % (
                ev.get("by"), ev.get("key"), ev.get("value")), now)
        elif kind == "radio":                         # retour automatique au mode alterné (1.2.1)
            self.add_log("Le module de la passerelle refuse d'émettre pendant l'écoute (code 0x%02X) : "
                         "écoute arrêtée pendant chaque émission" % ev.get("code", 0), now)
        elif kind == "tx_fail":
            self.add_log("Trame %s refusée par la radio de la passerelle (code 0x%02X)" % (
                ev.get("type", "?"), ev.get("code", 0)), now)

    @staticmethod
    def dest(addr):
        if addr is None:
            return "?"
        if addr == 255:
            return "tous"
        if 240 <= addr <= 254:
            return "g%d" % (addr - 239)
        return str(addr)


# =============================================================================
# Sources de lignes : port série, ou réseau simulé (--demo)
# =============================================================================
def find_obcp_port():
    """Cherche le port COM virtuel d'une ObCP (identifiants USB de ST)."""
    from serial.tools import list_ports
    ports = list(list_ports.comports())
    for p in ports:
        if p.vid == 0x0483 and p.pid == 0x5740:
            return p.device
    for p in ports:
        text = "%s %s" % (p.description or "", p.manufacturer or "")
        if "CROC" in text or "SupMicrotech" in text or "STM" in text:
            return p.device
    return None


class SerialSource:
    """Lit les lignes de la passerelle dans un fil d'exécution séparé ;
    rouvre le port si la carte est débranchée ou redémarre."""

    def __init__(self, port):
        import serial
        self.serial_module = serial
        self.port = port
        self.lines = queue.Queue()
        self.ser = None
        self.running = True
        self.lock = threading.Lock()
        threading.Thread(target=self.run, daemon=True).start()

    def run(self):
        while self.running:
            try:
                with self.lock:
                    self.ser = self.serial_module.Serial(self.port, 115200, timeout=0.2)
                self.lines.put("(connecté à %s)" % self.port)
                self.write("pc on")
                buf = b""
                while self.running:
                    data = self.ser.read(256)
                    if not data:
                        continue
                    buf += data
                    while b"\n" in buf:
                        raw, buf = buf.split(b"\n", 1)
                        self.lines.put(raw.decode("utf-8", errors="replace").strip("\r"))
            except (OSError, self.serial_module.SerialException) as exc:
                self.lines.put("(port %s indisponible : %s ; nouvel essai dans 2 s)" % (self.port, exc))
                with self.lock:
                    if self.ser is not None:
                        try:
                            self.ser.close()
                        except OSError:
                            pass
                    self.ser = None
                time.sleep(2.0)

    def write(self, command):
        with self.lock:
            if self.ser is not None:
                try:
                    self.ser.write((command + "\r\n").encode("utf-8"))
                except (OSError, self.serial_module.SerialException):
                    pass

    def close(self):
        self.running = False
        with self.lock:
            if self.ser is not None:
                try:
                    self.ser.write(b"pc off\r\n")
                    self.ser.close()
                except (OSError, self.serial_module.SerialException):
                    pass


class DemoSource:
    """Réseau simulé, pour essayer le programme sans carte : quelques nœuds
    posés au hasard, des HELLO, des mesures, des messages, et des réponses
    aux commandes ping, trace, mesure, vague et msg."""

    def __init__(self, count=12, seed=7, speed=1.0):
        self.rng = random.Random(seed)
        self.lines = queue.Queue()
        self.speed = speed
        self.t0 = time.time()
        self.gateway = 17
        ids = [self.gateway] + self.rng.sample([i for i in range(1, 60) if i != self.gateway], count - 1)
        self.pos = {i: (self.rng.uniform(0, 10), self.rng.uniform(0, 7)) for i in ids}
        self.pos[self.gateway] = (5.0, 3.5)
        self.ids = ids
        self.links = {}
        for a in ids:
            for b in ids:
                d = math.dist(self.pos[a], self.pos[b])
                if a < b and d < 3.8:
                    self.links[(a, b)] = int(-42 - 30 * math.log10(max(d, 0.3) * 1.6))
        self.hops = self.bfs()
        self.seq = {i: self.rng.randrange(256) for i in ids}
        self.pending = []                  # (instant, ligne)
        self.next_hello = {i: self.rng.uniform(0.2, 3.0) for i in ids}
        self.next_measure = 2.0
        self.lines.put(json.dumps(self.info(), ensure_ascii=False))

    def bfs(self):
        hops = {self.gateway: -1}
        frontier = [self.gateway]
        while frontier:
            nxt = []
            for a in frontier:
                for b in self.neighbors(a):
                    if b not in hops:
                        hops[b] = hops[a] + 1
                        nxt.append(b)
            frontier = nxt
        return hops

    def neighbors(self, a):
        for (x, y), _ in self.links.items():
            if x == a:
                yield y
            elif y == a:
                yield x

    def rssi(self, a, b):
        return self.links.get((min(a, b), max(a, b)), -95) + self.rng.randint(-3, 3)

    def info(self):
        return {"ev": "info", "t": 0, "name": "ObCP BLE Mesh Demo (démo PC)", "version": VERSION, "ota": 0,
                "id": self.gateway, "fixed": 1, "tag": "D3E0D3E0", "net": 0, "addr": "C1:00:00:00:00:11",
                "radio": 1, "mode": "alterne", "fw": "7.2c", "stack_mode4": 1,
                "ttl": 4, "relais": 1, "suppr": 3, "rssimin": -127,
                "puissance": -2, "hello": 20, "auto": 0, "flash": 1, "groups": []}

    def now(self):
        return (time.time() - self.t0) * self.speed

    def frame_event(self, src, typ, extra, dst=255):
        """Évènement « rx » d'une trame de src arrivée à la passerelle."""
        hops = max(self.hops.get(src, 0), 0)
        if hops == 0:
            last = src
        else:
            last = min((b for b in self.neighbors(self.gateway) if self.hops.get(b, 99) <= hops),
                       key=lambda b: self.hops.get(b, 99), default=src)
        self.seq[src] = (self.seq[src] + 1) % 256
        ev = {"ev": "rx", "t": int(self.now() * 1000), "src": src, "dst": dst, "seq": self.seq[src],
              "ttl": max(4 - hops, 0), "hops": hops, "last": last, "type": typ, "rssi": self.rssi(self.gateway, last)}
        ev.update(extra)
        return json.dumps(ev, ensure_ascii=False)

    def schedule(self, delay, line):
        self.pending.append((self.now() + delay, line))

    def step(self):
        """Fait avancer la simulation jusqu'à l'instant présent."""
        now = self.now()
        for i in self.ids:
            if i == self.gateway or i not in self.hops:
                continue
            if now >= self.next_hello[i]:
                self.next_hello[i] = now + self.rng.uniform(16, 24)
                nb = sorted(((b, self.rssi(i, b)) for b in self.neighbors(i)), key=lambda p: -p[1])[:4]
                self.schedule(0.05 * (self.hops[i] + 1), self.frame_event(
                    i, "HELLO", {"tag": "%08X" % (0x1000 + i), "relay": 1, "fixed": 0, "nb": [list(p) for p in nb]}))
                for b in self.neighbors(self.gateway):
                    if b != i and self.rng.random() < 0.3:
                        dup = json.loads(self.frame_event(i, "HELLO", {}))
                        dup.update({"ev": "dup", "last": b, "hops": self.hops[b] + 1})
                        self.schedule(0.3, json.dumps(dup))
        if now >= self.next_hello.get(self.gateway, 0):
            self.next_hello[self.gateway] = now + 20
            nb = sorted(((b, self.rssi(self.gateway, b)) for b in self.neighbors(self.gateway)),
                        key=lambda p: -p[1])[:4]
            self.lines.put(json.dumps({"ev": "tx", "t": int(now * 1000), "src": self.gateway, "dst": 255,
                                       "seq": 1, "ttl": 4, "hops": 0, "last": self.gateway, "type": "HELLO",
                                       "tag": "D3E0D3E0", "relay": 1, "fixed": 1, "nb": [list(p) for p in nb]}))
        if now >= self.next_measure:
            self.next_measure = now + 3.0
            i = self.rng.choice([x for x in self.ids if x != self.gateway and x in self.hops])
            self.schedule(0.1, self.frame_event(i, "MESURE", {
                "acc_ok": 1, "temp_ok": 1, "temp": self.rng.randint(22, 29),
                "ax": self.rng.randint(-40, 40), "ay": self.rng.randint(-40, 40), "az": self.rng.randint(960, 1040)}))
        due = [p for p in self.pending if p[0] <= now]
        self.pending = [p for p in self.pending if p[0] > now]
        for _, line in sorted(due):
            self.lines.put(line)

    def write(self, command):
        words = command.split()
        if not words:
            return
        reachable = [x for x in self.ids if x != self.gateway and x in self.hops]
        if words[0] == "ping" and len(words) > 1:
            targets = reachable if words[1] == "tous" else [int(words[1])] if words[1].isdigit() else []
            self.lines.put("Ping n°1 envoyé à %s (ttl 4)" % words[1])
            for t in targets:
                if t in self.hops:
                    h = self.hops[t]
                    rtt = int((h + 1) * 2 * 170 + self.rng.randint(0, 200))
                    self.schedule(rtt / 1000.0, json.dumps({"ev": "pong", "t": 0, "from": t, "ping": 1, "rtt": rtt,
                                                            "hops_out": h, "rssi_out": -60, "hops_back": h,
                                                            "rssi_back": -62}))
            if not any(t in self.hops for t in targets):
                self.schedule(5.0, json.dumps({"ev": "ping_end", "t": 0, "to": 255 if words[1] == "tous" else
                                               int(words[1]) if words[1].isdigit() else 0, "ping": 1, "replies": 0}))
        elif words[0] == "trace" and len(words) > 1 and words[1].isdigit() and int(words[1]) in self.hops:
            t = int(words[1])
            path = [t]
            while self.hops.get(path[-1], -1) > 0:
                path.append(min(self.neighbors(path[-1]), key=lambda b: self.hops.get(b, 99)))
            relays = list(reversed(path[1:]))
            self.lines.put("Trace n°1 envoyée à %d (ttl 4)" % t)
            self.schedule(0.8, json.dumps({"ev": "trace", "t": 0, "to": t, "trace": 1, "rtt": 800,
                                           "fwd": relays, "back": list(reversed(relays))}))
        elif words[0] == "mesure" and len(words) > 1:
            for k, t in enumerate(reachable if words[1] == "tous" else []):
                self.schedule(0.2 + 0.15 * k, self.frame_event(t, "MESURE", {
                    "acc_ok": 1, "temp_ok": 1, "temp": self.rng.randint(22, 29), "ax": 3, "ay": -8, "az": 1001},
                    dst=self.gateway))
            self.lines.put("Demande de mesures envoyée à %s (ttl 4)" % words[1])
        elif words[0] == "vague":
            self.lines.put("Vague lancée (ttl 4) : rouge en direct, puis orange, jaune, vert, cyan...")
        elif words[0] == "pc":
            self.lines.put(json.dumps(self.info()))
        else:
            self.lines.put("(démo) commande transmise : %s" % command)

    def close(self):
        pass


# =============================================================================
# Placement des nœuds : algorithme de Fruchterman et Reingold
# =============================================================================
class Layout:
    """Positions des nœuds : ressorts sur les liaisons, répulsion entre
    nœuds. Les positions sont gardées d'une image à l'autre : la carte
    bouge doucement quand un nœud apparaît."""

    def __init__(self, seed=3):
        self.pos = {}
        self.rng = random.Random(seed)

    def update(self, ids, edges, iterations=15):
        for i in ids:
            if i not in self.pos:
                near = [b for (a, b) in edges if a == i and b in self.pos] + \
                       [a for (a, b) in edges if b == i and a in self.pos]
                if near:
                    x, y = self.pos[near[0]]
                    self.pos[i] = (x + self.rng.uniform(-0.1, 0.1), y + self.rng.uniform(-0.1, 0.1))
                else:
                    self.pos[i] = (self.rng.uniform(0.1, 0.9), self.rng.uniform(0.1, 0.9))
        ids = [i for i in ids if i in self.pos]
        if len(ids) < 2:
            return
        k = 0.8 * math.sqrt(1.0 / len(ids))
        temp = 0.05
        for _ in range(iterations):
            disp = {i: [0.0, 0.0] for i in ids}
            for a_i, a in enumerate(ids):
                ax, ay = self.pos[a]
                for b in ids[a_i + 1:]:
                    dx, dy = ax - self.pos[b][0], ay - self.pos[b][1]
                    d = max(math.hypot(dx, dy), 0.01)
                    f = k * k / d
                    disp[a][0] += dx / d * f
                    disp[a][1] += dy / d * f
                    disp[b][0] -= dx / d * f
                    disp[b][1] -= dy / d * f
            for (a, b) in edges:
                if a in disp and b in disp:
                    dx, dy = self.pos[a][0] - self.pos[b][0], self.pos[a][1] - self.pos[b][1]
                    d = max(math.hypot(dx, dy), 0.01)
                    f = d * d / k
                    disp[a][0] -= dx / d * f
                    disp[a][1] -= dy / d * f
                    disp[b][0] += dx / d * f
                    disp[b][1] += dy / d * f
            for i in ids:
                dx, dy = disp[i]
                d = max(math.hypot(dx, dy), 0.01)
                x = self.pos[i][0] + dx / d * min(d, temp)
                y = self.pos[i][1] + dy / d * min(d, temp)
                self.pos[i] = (min(max(x, 0.03), 0.97), min(max(y, 0.03), 0.97))


# =============================================================================
# Affichage en texte (--texte)
# =============================================================================
def run_text(source, model, record, duration):
    start = time.time()
    last_table = 0.0
    shown_log = 0
    try:
        while duration is None or time.time() - start < duration:
            if hasattr(source, "step"):
                source.step()
            got = False
            while True:
                try:
                    line = source.lines.get(timeout=0.05)
                except queue.Empty:
                    break
                got = True
                if record is not None and line.startswith("{"):
                    record.write(line + "\n")
                model.handle_line(line, time.time())
            new = list(model.log)[shown_log:] if len(model.log) >= shown_log else list(model.log)
            for t, text in new:
                print("[%s] %s" % (time.strftime("%H:%M:%S", time.localtime(t)), text))
            shown_log = len(model.log)
            if time.time() - last_table > 10.0:
                last_table = time.time()
                print_table(model)
            if not got:
                time.sleep(0.05)
    except KeyboardInterrupt:
        pass


def print_table(model):
    now = time.time()
    nodes = model.fresh_nodes(now)
    print("--- %d nœud(s) ; passerelle %s ; liaisons %d ---" % (
        len(nodes), model.gateway, len(model.fresh_links(now))))
    for k in sorted(nodes):
        n = nodes[k]
        print("  %3d  relais %-3s RSSI %-5s T %-4s vu il y a %.0f s" % (
            k, "-" if n.hops is None or n.hops < 0 else n.hops, "-" if n.rssi is None else n.rssi,
            "-" if n.temp is None else n.temp, now - n.last_seen))


# =============================================================================
# Affichage graphique (matplotlib)
# =============================================================================
class GraphView:
    def __init__(self, source, model, record, show=True):
        import matplotlib
        if not show:
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        from matplotlib.widgets import Button, TextBox
        self.plt = plt
        self.source = source
        self.model = model
        self.record = record
        self.layout = Layout()
        self.fig = plt.figure(figsize=(13, 7.5))
        try:
            self.fig.canvas.manager.set_window_title("ObCP - réseau maillé")
        except AttributeError:
            pass
        self.ax_map = self.fig.add_axes([0.02, 0.23, 0.56, 0.72])
        self.ax_table = self.fig.add_axes([0.60, 0.50, 0.39, 0.45])
        self.ax_log = self.fig.add_axes([0.60, 0.17, 0.39, 0.31])
        for ax in (self.ax_table, self.ax_log):
            ax.set_axis_off()
        ax_box = self.fig.add_axes([0.10, 0.05, 0.36, 0.05])
        self.box = TextBox(ax_box, "Commande ", initial="")
        self.box.on_submit(self.send_command)
        self.buttons = []
        for k, (label, cmd) in enumerate([("Vague", "vague"), ("Ping tous", "ping tous"),
                                          ("Mesures", "mesure tous"), ("Voisins", "voisins")]):
            ax_b = self.fig.add_axes([0.50 + 0.12 * k, 0.05, 0.11, 0.05])
            b = Button(ax_b, label)
            b.on_clicked(lambda _event, c=cmd: self.send_command(c))
            self.buttons.append(b)

    def send_command(self, text):
        text = text.strip()
        if text:
            self.model.add_log("> " + text, time.time())
            self.source.write(text)
        self.box.set_val("")

    def poll(self):
        if hasattr(self.source, "step"):
            self.source.step()
        while True:
            try:
                line = self.source.lines.get_nowait()
            except queue.Empty:
                break
            if self.record is not None and line.startswith("{"):
                self.record.write(line + "\n")
            self.model.handle_line(line, time.time())

    def draw(self):
        now = time.time()
        m = self.model
        nodes = m.fresh_nodes(now)
        links = m.fresh_links(now)
        edges = [k for k in links if k[0] in nodes and k[1] in nodes]
        self.layout.update(sorted(nodes), edges)
        pos = self.layout.pos

        ax = self.ax_map
        ax.clear()
        ax.set_xlim(-0.02, 1.02)
        ax.set_ylim(-0.02, 1.02)
        ax.set_xticks([])
        ax.set_yticks([])
        title = "Réseau %s - passerelle : nœud %s - %d nœud(s), %d liaison(s)" % (
            "?" if m.net is None else m.net, "?" if m.gateway is None else m.gateway, len(nodes), len(edges))
        ax.set_title(title, fontsize=11)
        for (a, b) in edges:
            rssi = links[(a, b)][0]
            color = "#2ca02c" if rssi >= -60 else ("#ff9f1c" if rssi >= -75 else "#d62728")
            ax.plot([pos[a][0], pos[b][0]], [pos[a][1], pos[b][1]], color=color,
                    linewidth=1.0 + max(0, (rssi + 90)) / 12.0, alpha=0.7, zorder=1)
        for i, n in nodes.items():
            x, y = pos[i]
            if i == m.gateway:
                face = GATEWAY_COLOR
            elif n.hops is None:
                face = UNKNOWN_COLOR
            else:
                face = HOPS_COLORS[min(max(n.hops, 0), 7)]
            ax.scatter([x], [y], s=520, c=face, edgecolors="#202020", linewidths=1.5, zorder=3)
            ax.text(x, y, str(i), ha="center", va="center", fontsize=9, fontweight="bold", zorder=4)
            if now - m.flash.get(i, -10) < FLASH_S:
                ax.scatter([x], [y], s=1100, facecolors="none", edgecolors="#1f77b4", linewidths=2.5, zorder=2)
        legend = "Couleur = relais jusqu'à la passerelle : rouge 0 (direct), orange 1, jaune 2, vert 3, cyan 4...\n" \
                 "Liaisons : verte >= -60 dBm, orange >= -75 dBm, rouge plus faible. Cercle bleu : vient d'émettre."
        ax.text(0.0, -0.03, legend, transform=ax.transAxes, fontsize=8, va="top")

        # tableau
        self.ax_table.clear()
        self.ax_table.set_axis_off()
        rows = ["nœud  relais  RSSI    T°C   vu il y a"]
        for k in sorted(nodes)[:22]:
            n = nodes[k]
            rows.append("%4d  %6s  %5s  %4s  %6.0f s%s" % (
                k, "-" if n.hops is None or n.hops < 0 else n.hops, "-" if n.rssi is None else n.rssi,
                "-" if n.temp is None else n.temp, now - n.last_seen, "  (passerelle)" if k == m.gateway else ""))
        self.ax_table.text(0, 1, "\n".join(rows), family="monospace", fontsize=8.5, va="top")
        self.ax_table.text(0, 0.0, "trames reçues %d, doublons %d, émises %d, relayées %d" % (
            m.counts["rx"], m.counts["dup"], m.counts["tx"], m.counts["relay"]), fontsize=8, va="bottom")

        # journal
        self.ax_log.clear()
        self.ax_log.set_axis_off()
        lines = []
        for t, text in list(m.log)[-13:]:                 # lignes longues : coupées en plusieurs
            parts = textwrap.wrap(text, width=66) or [""]
            lines.append("%s  %s" % (time.strftime("%H:%M:%S", time.localtime(t)), parts[0]))
            lines.extend(" " * 10 + part for part in parts[1:])
        self.ax_log.text(0, 1, "\n".join(lines[-14:]), family="monospace", fontsize=7.5, va="top")

    def tick(self):
        self.poll()
        self.draw()
        self.fig.canvas.draw_idle()

    def run(self):
        timer = self.fig.canvas.new_timer(interval=300)
        timer.add_callback(self.tick)
        timer.start()
        self.tick()
        self.plt.show()

    def capture(self, path, duration):
        """Mode sans fenêtre : simule duration secondes, puis enregistre l'image."""
        end = time.time() + duration
        while time.time() < end:
            self.poll()
            self.layout.update(sorted(self.model.fresh_nodes(time.time())),
                               [k for k in self.model.fresh_links(time.time())], iterations=5)
            time.sleep(0.05)
        self.poll()
        self.draw()
        self.fig.savefig(path, dpi=110)


# =============================================================================
def main():
    parser = argparse.ArgumentParser(description="Supervision du réseau maillé des cartes ObCP.")
    parser.add_argument("port", nargs="?", help="port série de la passerelle (COM7, /dev/ttyACM0...)")
    parser.add_argument("--demo", action="store_true", help="réseau simulé, sans carte")
    parser.add_argument("--texte", action="store_true", help="sans fenêtre : évènements en texte")
    parser.add_argument("--enregistre", metavar="FICHIER", help="garde chaque ligne JSON reçue dans FICHIER")
    parser.add_argument("--duree", type=float, default=None, help="arrêt au bout de N secondes")
    parser.add_argument("--capture", metavar="IMAGE", help="(démo) enregistre une image de la fenêtre et s'arrête")
    parser.add_argument("--version", action="version", version=VERSION)
    args = parser.parse_args()

    if args.demo:
        source = DemoSource(speed=4.0 if args.capture else 1.0)
    else:
        port = args.port or find_obcp_port()
        if port is None:
            print("Aucune ObCP trouvée : branchez-la, ou donnez le port (exemple : python %s COM7)." % sys.argv[0])
            print("Pour essayer sans carte : python %s --demo" % sys.argv[0])
            return 1
        source = SerialSource(port)
    model = NetworkModel()
    record = open(args.enregistre, "a", encoding="utf-8") if args.enregistre else None
    try:
        if args.texte:
            run_text(source, model, record, args.duree)
        elif args.capture:
            GraphView(source, model, record, show=False).capture(args.capture, args.duree or 8.0)
        else:
            GraphView(source, model, record).run()
    finally:
        source.close()
        if record is not None:
            record.close()
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    sys.exit(main())
