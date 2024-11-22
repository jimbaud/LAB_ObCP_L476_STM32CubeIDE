## Description du programme OBCP_BLE_UART

### **À propos**

Ce programme implémente une solution de communication Bluetooth Low Energy (BLE) pour un microcontrôleur STM32 équipé du module BLE BlueNRG-M0. Il offre une base pour développer des applications IoT simples, avec des fonctionnalités de communication bidirectionnelle et d'interaction avec des périphériques matériels.

---

### **Fonctionnalités principales**

1. **Communication BLE :**
   - Configuration d'un service BLE personnalisé compatible avec des applications comme nRF Connect ou BLE Terminal.
   - Transmission et réception de données via des caractéristiques TX (Transmission) et RX (Réception).

2. **Bouton poussoir utilisateur :**
   - Gestion des interruptions matérielles pour détecter les pressions sur le bouton.
   - Envoi d'une notification au périphérique BLE lorsqu'un bouton est pressé.

3. **Contrôle LED à distance :**
   - Réception de commandes depuis un appareil BLE pour allumer ou éteindre une LED verte embarquée.
   - Commandes supportées : `1` (allumer) et `0` (éteindre).

4. **Service BLE basé sur le standard Nordic UART (NUS) :**
   - UUID personnalisé compatible avec divers outils BLE pour simplifier les tests et la validation.

5. **Gestion des événements BLE :**
   - Suivi des connexions et déconnexions BLE.
   - Activation des notifications et gestion des attributs GATT.

---

### **Fonctionnement**

#### **Initialisation :**
- Mise en place du service BLE avec les caractéristiques TX et RX.
- Configuration des paramètres BLE comme le nom du périphérique et la puissance de transmission.

#### **Traitement des commandes :**
- Réception et interprétation des commandes BLE.
- Exécution des actions correspondantes (par ex., contrôle de la LED ou envoi de messages).

#### **Gestion des événements :**
- Notification des événements de connexion et de déconnexion.
- Traitement des modifications des caractéristiques BLE (par ex., réception de nouvelles données via RX).

---

### **Cas d'utilisation**

1. **Prototypage IoT :**
   - Créer des objets connectés simples pour les projets IoT personnels ou professionnels.

2. **Applications BLE éducatives :**
   - Enseigner les concepts de communication BLE avec une approche pratique.

3. **Contrôle distant :**
   - Utiliser un smartphone ou un terminal BLE pour piloter des LEDs ou recevoir des notifications.

---

### **UUIDs utilisés**

- **Service UUID :** `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- **TX UUID :** `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` (envoi de données vers le périphérique distant)
- **RX UUID :** `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` (réception de commandes depuis un périphérique distant)

---

### **Documentation et ressources**

- **Documentation officielle du STM32 :**
  [STM32 BlueNRG-MS](https://www.st.com/en/wireless-connectivity/bluenrg-ms.html)
- **Ressources BLE :**
  - [nRF Connect](https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp&hl=fr) pour tester les services BLE.

---

**Remarque :** Ce programme est destiné à des fins d'apprentissage et de prototypage. Veuillez l'adapter pour une utilisation en production selon vos besoins.
