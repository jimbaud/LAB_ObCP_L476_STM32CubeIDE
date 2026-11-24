/* ObCP Control — protocole partagé entre la page, le simulateur et les tests.
   Les octets sont ceux de l'application Android et des templates STM32 2024/2026. */
(function (root, make) {
  const api = make();
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.ObCP = api;
})(globalThis, function () {
  "use strict";
  const suffix = "-b5a3-f393-e0a9-e50e24dcca9e";
  const UUID = Object.freeze({
    service: "6e400001" + suffix,
    notify: "6e400002" + suffix,
    write: "6e400003" + suffix
  });
  const CHANNELS = Object.freeze({
    r: {label: "Rouge", ack: "Red"}, v: {label: "Vert", ack: "Green"},
    b: {label: "Bleu", ack: "Blue"}, m: {label: "PWM", ack: "PWM"}
  });

  function setpoint(channel, percent) {
    if (!Object.hasOwn(CHANNELS, channel) || !Number.isFinite(percent) || percent < 0 || percent > 100) {
      throw new RangeError("Consigne hors limites.");
    }
    const fraction = percent / 100;
    const value = Math.floor(65535 * (channel === "m" ? fraction : fraction ** 3));
    return {value, command: channel + value, ack: CHANNELS[channel].ack + " = " + value};
  }

  const number = "([+-]?(?:\\d+(?:\\.\\d+)?|\\.\\d+)(?:[eE][+-]?\\d+)?)";
  const xyz = new RegExp("^X" + number + "Y" + number + "Z" + number + "$");
  const temperature = new RegExp("^T\\s*=\\s*" + number + "$");
  function parseLine(text) {
    const line = text.trim();
    let m = xyz.exec(line);
    if (m) {
      const values = m.slice(1).map(Number);
      if (values.every(Number.isFinite)) return {kind: "acc", values};
    }
    m = temperature.exec(line);
    if (m && Number.isFinite(Number(m[1]))) return {kind: "temp", values: [Number(m[1])]};
    m = /^(Red|Green|Blue|PWM)\s*=\s*(\d{1,5})$/.exec(line);
    if (m && Number(m[2]) <= 65535) return {kind: "ack", text: m[1] + " = " + Number(m[2])};
    return {kind: "message", text: line};
  }

  /* Les firmwares du dépôt terminent leurs messages par LF CR. Les notifications
     BLE peuvent couper un nombre au milieu, ou regrouper plusieurs messages. */
  class FrameReader {
    constructor(onLine, legacyDelay = 140) {
      this.onLine = onLine;
      this.delay = legacyDelay;
      this.reset();
    }
    reset() {
      clearTimeout(this.timeout);
      this.pending = "";
      this.discard = false;
      this.delimited = false;
    }
    feed(text) {
      clearTimeout(this.timeout);
      // Compatibilité avec une ancienne carte qui envoie une trame entière sans EOL.
      // Un nouveau préfixe explicite peut terminer la trame précédente.
      if (!this.delimited && this.pending && /^(?:X[+.\d-]|T\s*=|(?:Red|Green|Blue|PWM)\s*=)/.test(text) &&
          parseLine(this.pending).kind !== "message") this.flush();
      for (const c of text) {
        if (c === "\r" || c === "\n" || c === "\0") {
          this.delimited = true;
          if (!this.discard) this.flush();
          this.discard = false;
        } else if (!this.discard) {
          this.pending += c;
          if (this.pending.length > 512) {
            this.pending = "";
            this.discard = true;
          }
        }
      }
      if (!this.delimited && text.length < 20 && this.pending && parseLine(this.pending).kind !== "message") {
        this.timeout = setTimeout(() => this.flush(), this.delay);
      }
    }
    flush() {
      const text = this.pending.trim();
      this.pending = "";
      if (text) this.onLine(text);
    }
  }

  /* Une seule opération GATT à la fois. L'attente d'ACK est armée AVANT
     l'écriture pour accepter une notification reçue avant la fin de la promesse. */
  class CommandQueue {
    constructor(write, trace, {timeout = 500, attempts = 5} = {}) {
      this.write = write;
      this.trace = trace;
      this.timeout = timeout;
      this.attempts = attempts;
      this.tail = Promise.resolve();
      this.active = true;
      this.waiter = null;
    }
    close() {
      this.active = false;
      if (this.waiter) this.waiter.finish(false);
    }
    receive(message) {
      if (message.kind === "ack" && this.waiter && message.text === this.waiter.expected) {
        this.waiter.finish(true);
      }
    }
    enqueue(command, expected = null, current = () => true) {
      const run = async () => {
        const limit = expected ? this.attempts : 1;
        for (let attempt = 1; attempt <= limit; attempt++) {
          if (!this.active || !current()) return "cancelled";
          let timer;
          let finish;
          let ack;
          if (expected) {
            ack = new Promise(resolve => { finish = resolve; });
            this.waiter = {expected, finish};
          }
          try {
            this.trace(command, attempt);
            await this.write(command);
            if (!this.active || !current()) return "cancelled";
            if (!expected) return "sent";
            timer = setTimeout(() => finish(false), this.timeout);
            if (await ack) return this.active && current() ? "ack" : "cancelled";
          } finally {
            clearTimeout(timer);
            this.waiter = null;
          }
        }
        return this.active && current() ? "timeout" : "cancelled";
      };
      const result = this.tail.then(run);
      this.tail = result.catch(() => {});
      return result;
    }
  }

  function chooseDevice(bluetooth, prefix = "") {
    const options = {optionalServices: [UUID.service]};
    if (prefix) options.filters = [{namePrefix: prefix}];
    else options.acceptAllDevices = true; // Certaines cartes annoncent uniquement leur nom.
    return bluetooth.requestDevice(options);
  }

  async function openGatt(device, bytesReceived, disconnected) {
    const server = device.gatt;
    if (!server) throw new Error("Ce périphérique ne fournit pas de serveur GATT.");
    let rx, closed = false;
    const notification = event => {
      if (!closed) bytesReceived(event.target.value);
    };
    function release() {
      if (closed) return;
      closed = true;
      device.removeEventListener("gattserverdisconnected", lost);
      if (rx) rx.removeEventListener("characteristicvaluechanged", notification);
      if (server.connected) server.disconnect();
    }
    function lost() { release(); disconnected(); }
    device.addEventListener("gattserverdisconnected", lost);
    try {
      await server.connect();
      const uart = await server.getPrimaryService(UUID.service);
      rx = await uart.getCharacteristic(UUID.notify);
      const tx = await uart.getCharacteristic(UUID.write);
      const p = tx.properties;
      if (!(rx.properties.notify || rx.properties.indicate) || !(p.writeWithoutResponse || p.write)) {
        throw new Error("Profil GATT incompatible. ObCP exige les notifications sur …0002 et les commandes sur …0003.");
      }
      rx.addEventListener("characteristicvaluechanged", notification);
      await rx.startNotifications();
      if (closed || !server.connected) throw new Error("La liaison a été interrompue.");
      const encode = new TextEncoder();
      return {
        name: device.name || "Carte ObCP", close: release,
        async write(command) {
          if (closed || !server.connected) throw new Error("La carte est déconnectée.");
          const bytes = encode.encode(command); // Ni CR, ni LF, ni octet nul.
          if (p.writeWithoutResponse && typeof tx.writeValueWithoutResponse === "function") {
            await tx.writeValueWithoutResponse(bytes);
          } else if (p.write && typeof tx.writeValueWithResponse === "function") {
            await tx.writeValueWithResponse(bytes);
          } else if (typeof tx.writeValue === "function") {
            await tx.writeValue(bytes); // Navigateurs BLE plus anciens.
          } else {
            throw new Error("Le navigateur ne propose pas le type d’écriture de cette carte.");
          }
        }
      };
    } catch (error) {
      release();
      throw error;
    }
  }

  // Le scénario de démonstration utilise les mêmes trames que le matériel.
  function demoFrame(n) {
    const roll = 0.77 * Math.sin(n * 0.17) + 0.18 * Math.sin(n * 0.62);
    const pitch = 0.70 * Math.sin(n * 0.15 + 0.8) + 0.13 * Math.cos(n * 0.39);
    const axes = [Math.sin(roll), Math.cos(roll) * Math.sin(pitch), Math.cos(roll) * Math.cos(pitch)];
    const temp = 24.6 + 0.55 * Math.sin(n * 0.095 - 1.1) + 0.09 * Math.sin(n * 0.48);
    return "X" + axes[0].toFixed(2) + "Y" + axes[1].toFixed(2) + "Z" + axes[2].toFixed(2)
      + "\n\rT = " + temp.toFixed(1) + "\n\r";
  }

  class DemoDevice {
    constructor(receive) {
      this.receive = receive;
      this.outputs = Object.fromEntries(Object.entries({r: 65, v: 35, b: 80, m: 50})
        .map(([key, percent]) => [key, setpoint(key, percent).value]));
      this.n = 0;
      this.closed = false;
    }
    sample() { if (!this.closed) this.receive(demoFrame(this.n++)); }
    start() { clearInterval(this.timer); this.timer = setInterval(() => this.sample(), 1000); }
    async write(command) {
      if (this.closed) throw new Error("Simulation terminée.");
      const slider = /^([rvbm])(\d+)$/.exec(command);
      if (slider) {
        this.outputs[slider[1]] = Number(slider[2]);
        this.receive(CHANNELS[slider[1]].ack + " = " + slider[2] + "\n\r");
      } else if (command === "start timer") this.start();
      else if (command === "stop timer" || command === "#rst") {
        clearInterval(this.timer);
        if (command === "#rst") this.outputs = {r: 0, v: 0, b: 0, m: 0};
      }
    }
    close() { this.closed = true; clearInterval(this.timer); }
  }
  return {UUID, CHANNELS, setpoint, parseLine, FrameReader, CommandQueue, chooseDevice, openGatt, DemoDevice, demoFrame};
});
