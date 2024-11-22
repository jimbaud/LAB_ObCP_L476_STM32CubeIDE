---

# Description du programme OBCP_USB_BLE

### **À propos**

Le programme **OBCP_USB_BLE** est conçu pour faciliter la communication bidirectionnelle entre un microcontrôleur STM32 et des périphériques via USB et Bluetooth Low Energy (BLE). Ce programme permet de transférer des données de manière fluide entre ces deux interfaces, offrant ainsi une solution pratique pour le développement des applications IoT.

---

### **Fonctionnalités principales**

1. **Transfert de données USB vers BLE :**
   - Copie des données reçues via USB et envoi vers un périphérique BLE.
   - Gestion efficace des paquets pour assurer une transmission fiable.

2. **Transfert de données BLE vers USB :**
   - Réception de données depuis un périphérique BLE et envoi vers un port USB virtuel.
   - Support de différents formats de données pour une flexibilité maximale.

3. **Gestion des connexions :**
   - Suivi des états de connexion USB et BLE pour garantir une communication continue.
   - Notifications en cas de déconnexion ou de perte de connexion.

4. **Interface utilisateur simple :**
   - Utilisation d'un bouton poussoir pour initier des actions, comme le démarrage ou l'arrêt des transferts de données.
   - Indications visuelles (par exemple, LED) pour signaler l'état des connexions.

5. **Optimisation des performances :**
   - Utilisation de buffers pour minimiser les latences lors des transferts de données.
   - Implémentation de mécanismes de contrôle d'erreur pour assurer l'intégrité des données.

---

### **Fonctionnement**

#### **Initialisation :**
- Configuration des interfaces USB et BLE avec les paramètres appropriés.
- Mise en place des buffers pour le stockage temporaire des données.

#### **Traitement des transferts :**
- Surveillance continue des données entrantes et sortantes sur les interfaces USB et BLE.
- Exécution des transferts de données en fonction des événements détectés.

#### **Gestion des événements :**
- Notification des utilisateurs lors de la connexion ou de la déconnexion des périphériques.
- Traitement des erreurs et des exceptions pour maintenir la stabilité du programme.

---

### **Cas d'utilisation**

1. **Applications IoT :**
   - Intégration de capteurs et d'autres dispositifs connectés pour collecter et transmettre des données.

2. **Prototypage rapide :**
   - Développement de solutions de communication pour des projets de recherche ou d'enseignement.

3. **Contrôle à distance :**
   - Utilisation d'appareils mobiles pour interagir avec des systèmes embarqués via BLE.

---

### **Documentation et ressources**

- **Documentation officielle du STM32 :**
  STM32 USB pour des informations sur l'implémentation USB.
- **Ressources BLE :**
  - nRF Connect / BLE terminal pour tester les services BLE.

---

**Remarque :** Ce programme est destiné à des fins d'apprentissage et de prototypage. Veuillez l'adapter pour une utilisation en production selon vos besoins.

---
