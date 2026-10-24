# 📱 ObCP Control - Application Android BLE

[![Android SDK Target](https://img.shields.io/badge/Target%20SDK-36-brightgreen.svg)](https://developer.android.com/)
[![Kotlin](https://img.shields.io/badge/Language-Kotlin-blue.svg)](https://kotlinlang.org/)
[![Jetpack Compose](https://img.shields.io/badge/UI-Jetpack%20Compose-4285F4.svg)](https://developer.android.com/jetpack/compose)
[![Material 3](https://img.shields.io/badge/Design-Material%203-7B1FA2.svg)](https://m3.material.io/)
[![BLE](https://img.shields.io/badge/Connectivity-Bluetooth%20LE-0082FC.svg)](https://developer.android.com/guide/topics/connectivity/bluetooth/ble)

**ObCP Control** est une application mobile Android native conçue pour piloter et télémesurer en temps réel la carte électronique pédagogique **ObCP** (*Objet Connecté Pédagogique*), basée sur un microcontrôleur **STM32L476**, développée à **[SUPMICROTECH](https://www.supmicrotech.fr)** par **Joël IMBAUD**.

---

## 📸 Aperçu & Fonctionnalités

L'application permet d'établir une liaison sans fil **Bluetooth Low Energy (BLE)** via le profil UART pour commander les actionneurs de la carte et visualiser les grandeurs physiques mesurées par ses capteurs.

### 🔌 1. Gestion de la Connexion BLE
* **Scanner BLE dynamique** : Recherche automatique des périphériques BLE environnants.
* **Filtre de recherche rapide** : Filtrage par nom d'appareil ou par adresse MAC en temps réel.
* **Gestion automatisée des permissions** : Demande dynamique des permissions Android 12+ (`BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT`, `ACCESS_FINE_LOCATION`).
* **Indicateurs d'état** : Visualisation claire de l'état de connexion (`DISCONNECTED`, `SCANNING`, `CONNECTING`, `CONNECTED`).

### 🎛️ 2. Contrôle des Actionneurs (Commandes BLE)
* **LED RGB interactive** :
  * Curseurs de réglage indépendants pour le **Rouge**, **Vert** et **Bleu** (0 à 100 %).
  * **Correction logarithmique** de la luminosité pour un rendu visuel fluide : $PWM = 65535 \times \left(\frac{\%}{100}\right)^3$.
* **Sortie PWM** : Réglage du rapport cyclique d'un signal PWM (0 à 100 %, codé sur 16 bits).
* **Contrôle du Timer** : Bouton Start/Stop pour démarrer ou interrompre l'envoi périodique des mesures de la carte STM32.
* **Réinitialisation rapide** : Bouton Reset (`#rst`) pour réinitialiser la carte et remettre tous les curseurs à zéro.
* **Communication robuste avec Retry** : Système anti-rebond (debounce) et jusqu'à 5 tentatives d'envoi automatiques avec contrôle d'acquittement.

### 📈 3. Télémesure & Visualisation Graphique
* **Accéléromètre 3 Axes ($g$)** : Graphique en temps réel affichant les composantes $X$, $Y$ et $Z$.
* **Capteur de Température (°C)** : Graphique dédié à l'évolution temporelle de la température.
* **Graphiques interactifs** : Intégration de la bibliothèque **MPAndroidChart** permettant le zoom à deux doigts (pinch zoom), le défilement et la mise à jour dynamique.
* **Effacement des données** : Action de nettoyage des historiques graphiques à tout moment.

### 📱 4. Ergonomie & Mode Adaptatif
* Interface moderne entièrement développée en **Jetpack Compose** et **Material 3**.
* **Layout Adaptatif** : Prise en charge fluide des orientations **Portrait** et **Paysage**.
* Dialog d'information d'à-propos intégrant l'image du matériel ObCP et les liens vers le projet GitHub et SUPMICROTECH.

---

## 🛠️ Architecture Technique

L'application suit le pattern d'architecture recommandé par Google : **MVVM (Model-View-ViewModel)**.

```
com.example.obcp_control
├── BleManager.kt       # Gestionnaire de bas niveau pour le BLE (Scan, GATT, Services, Characteristics)
├── BleViewModel.kt     # ViewModel : Gestion de l'état UI, parsing des données et retries
├── MainActivity.kt     # Activité principale, permissions, Composables UI & Graphiques
└── ui/theme/           # Thème Material 3 (Colors, Type, Theme)
```

### Stack Technique

| Composant | Technologie / Bibliothèque |
| :--- | :--- |
| **Langage** | Kotlin |
| **UI Framework** | Jetpack Compose + Material 3 |
| **Icônes** | Material Icons Extended |
| **Liaison Asynchrone** | Kotlin Coroutines & `StateFlow` |
| **Graphiques** | [MPAndroidChart](https://github.com/PhilJay/MPAndroidChart) (via `AndroidView`) |
| **SDK Minimum** | API 28 (Android 9.0) |
| **SDK Cible** | API 36 (Android 15) |

---

## 📡 Protocole de Communication BLE

L'application communique avec la carte STM32 via le profil **Nordic UART Service (NUS)**.

### UUIDs BLE Utilisés

| Élément | UUID |
| :--- | :--- |
| **Service UART** | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| **Caractéristique RX** *(Écriture : Application ➔ STM32)* | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |
| **Caractéristique TX** *(Notification : STM32 ➔ Application)* | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |

### Commandes Envoyées (Application ➔ STM32)

| Action | Format de la commande | Acquittement attendu |
| :--- | :--- | :--- |
| **LED Rouge** | `r<valeur>` (ex: `r65535`) | `Red = <valeur>` |
| **LED Verte** | `v<valeur>` (ex: `v32768`) | `Green = <valeur>` |
| **LED Bleue** | `b<valeur>` (ex: `b0`) | `Blue = <valeur>` |
| **Sortie PWM** | `m<valeur>` (ex: `m65535`) | `PWM = <valeur>` |
| **Démarrer Timer** | `start timer` | — |
| **Arrêter Timer** | `stop timer` | — |
| **Réinitialiser** | `#rst` | — |

### Données Reçues (STM32 ➔ Application)

| Donnée | Format de la trame | Exemple de trame |
| :--- | :--- | :--- |
| **Accéléromètre** | `X<val_X>Y<val_Y>Z<val_Z>` | `X0.02Y-0.98Z0.15` |
| **Température** | `T = <valeur>` | `T = 24.5` |

---

## 🚀 Installation & Compilation

### Prérequis
1. **Android Studio** (version Ladybug / 2024.2+ recommandée).
2. **JDK 11** ou ultérieur.
3. Un appareil Android physique sous Android 9.0 (API 28) ou supérieur, doté du Bluetooth Low Energy.

### Étapes de compilation

1. Cloner le dépôt GitHub :
   ```bash
   git clone https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE.git
   ```
2. Naviguer vers le projet Android :
   ```bash
   cd LAB_ObCP_L476_STM32CubeIDE/Android/ObCP_Control
   ```
3. Ouvrir le projet dans **Android Studio**.
4. Lancer la compilation via Gradle ou exécuter sur votre appareil :
   ```bash
   ./gradlew assembleDebug
   ```

---

## 👨‍💻 Auteur & Crédits

* **Auteur** : Joël IMBAUD ([@jimbaud](https://github.com/jimbaud))
* **Établissement** : [SUPMICROTECH](https://www.supmicrotech.fr) (ENSMM)
* **Dépôt du firmware STM32 & matériel** : [LAB_ObCP_L476_STM32CubeIDE](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE)
