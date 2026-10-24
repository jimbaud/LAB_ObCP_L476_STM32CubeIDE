# 📱 BLE Terminal JILAB

[![Android](https://img.shields.io/badge/Platform-Android-green.svg)](https://developer.android.com/)
[![Kotlin](https://img.shields.io/badge/Language-Kotlin-purple.svg)](https://kotlinlang.org/)
[![Jetpack Compose](https://img.shields.io/badge/UI-Jetpack%20Compose-blue.svg)](https://developer.android.com/jetpack/compose)
[![Material 3](https://img.shields.io/badge/Design-Material%203-orange.svg)](https://m3.material.io/)
[![Min SDK](https://img.shields.io/badge/Min%20SDK-28%20(Android%209.0)-brightgreen.svg)](https://developer.android.com/about/versions/pie)
[![Target SDK](https://img.shields.io/badge/Target%20SDK-34%20(Android%2014)-blue.svg)](https://developer.android.com/about/versions/14)

Une application Android moderne, fluide et complète servant de **terminal Bluetooth Low Energy (BLE)** pour la communication série UART. Développée spécialement pour dialoguer avec des systèmes embarqués et microcontrôleurs (STM32, ESP32, Arduino, nRF52, HM-10, etc.) utilisant le profil **Nordic UART Service (NUS)** ou un service BLE UART personnalisé.

---

## 📋 Sommaire

- [✨ Fonctionnalités Principales](#-fonctionnalités-principales)
- [🛠️ Spécifications Techniques](#️-spécifications-techniques)
- [📐 Architecture du Projet](#-architecture-du-projet)
- [📡 Protocoles & UUIDs](#-protocoles--uuids)
- [🚀 Installation & Déploiement](#-installation--déploiement)
- [📖 Guide d'Utilisation](#-guide-dutilisation)
- [⚙️ Configuration Avancée](#️-configuration-avancée)

---

## ✨ Fonctionnalités Principales

### 🔍 1. Scanner BLE & Connexion
- **Détection rapide** : Recherche en temps réel des appareils BLE à proximité en mode *Low Latency*.
- **Filtrage dynamique** : Recherche instantanée par nom d'appareil ou par adresse MAC.
- **Indicateur de statut visuel** :
  - 🟢 **Vert** : Connecté et prêt à communiquer.
  - 🟡 **Jaune** : Connexion en cours / Découverte des services.
  - 🔴 **Rouge** : Déconnecté.
- **Reconnexion automatique** : Reconnexion automatique au dernier appareil utilisé au lancement de l'application (option déconnectable).

### 💬 2. Terminal de Communication Interactive
- **Formats Texte (UTF-8) & Hexadécimal (HEX)** : Basculez dynamiquement entre l'affichage/envoi en caractères UTF-8 ou en valeurs hexadécimales.
- **Horodatage & Typage** : Chaque flux est distinctement catégorisé avec son horodatage (`HH:mm:ss`) :
  - `[TX]` : Données envoyées par l'application.
  - `[RX]` : Données reçues du périphérique BLE.
  - `[STATUS]` : Messages système et informations de connexion.
- **Défilement automatique** : La console défile automatiquement pour afficher les messages les plus récents.
- **Nettoyage rapide** : Un bouton permet d'effacer la console d'un simple clic.

### 🎛️ 3. Grille de Macros Personnalisables
- **Accès rapide aux commandes** : Grille de boutons de raccourcis pour envoyer des commandes prédéfinies d'une simple pression.
- **Édition à la volée** : **Appui long** sur n'importe quel bouton pour ouvrir un dialogue de modification (étiquette et commande).
- **Nombre configurable** : Choisissez entre 1 et 32 boutons de macros selon vos besoins.
- **Mise en page adaptative** : Ajustement automatique de la taille du texte et du placement en grille.
- **Sauvegarde permanente** : Les raccourcis sont conservés en mémoire (`SharedPreferences` + `Gson`).

### 📑 4. Journalisation / Logs sur Fichier
- **Enregistrement sur disque** : Possibilité d'enregistrer tous les échanges dans un fichier texte (`ble_log.txt`).
- **Horodatage détaillé** : Horodatage précis à la milliseconde (`yyyy-MM-dd HH:mm:ss.SSS`).
- **Chemin personnalisable** : Définissez le dossier cible (par défaut : `Documents/BLE_Terminal_AppLogs`).

### 🛡️ 5. Prise en charge des Permissions Modernes
- Gestion complète des permissions requises sous Android 12+ (`BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT`, `ACCESS_FINE_LOCATION`).
- Demandes dynamiques au runtime avec explications si nécessaires.

---

## 🛠️ Spécifications Techniques

| Composant | Technologie / Version |
| :--- | :--- |
| **Langage** | Kotlin 1.9+ |
| **Interface Utilisateur** | Jetpack Compose (Material Design 3) |
| **Architecture UI** | Declarative UI + Navigation Compose |
| **Persistance** | SharedPreferences + Gson |
| **Min SDK** | API 28 (Android 9.0 Pie) |
| **Target SDK** | API 34 (Android 14) |
| **Build System** | Gradle (Kotlin DSL - `build.gradle.kts`) |

---

## 📐 Architecture du Projet

```
app/src/main/java/com/example/BLE_Terminal_JILAB/
│
├── MainActivity.kt         # Activité principale : Scan BLE, gestion GATT, terminal & macros UI
├── Settings.kt             # Modèle BleSettings & persistance SharedPreferences des paramètres
├── SettingsScreen.kt       # Écran de configuration (UUIDs, logs, mode HEX, nombre de macros)
│
└── ui/theme/               # Fichiers de thématisation Compose
    ├── Color.kt            # Palette de couleurs Material 3
    ├── Theme.kt            # Application du thème clair/sombre
    └── Type.kt             # Typographies
```

---

## 📡 Protocoles & UUIDs

Par défaut, l'application utilise les identifiants du standard **Nordic UART Service (NUS)** :

| Service / Caractéristique | UUID par Défaut | Propriété GATT | Rôle |
| :--- | :--- | :--- | :--- |
| **UART Service** | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | Primary Service | Service conteneur |
| **TX Characteristic** | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | NOTIFY | Réception des données sur Android |
| **RX Characteristic** | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | WRITE / WRITE NO RESPONSE | Émission des données depuis Android |

> 💡 **Remarque** : Tous ces UUIDs sont modifiables directement dans l'écran de **Configuration** pour s'adapter à n'importe quel autre profil BLE.

---

## 🚀 Installation & Déploiement

### Prérequis
- **Android Studio** (version Ladybug ou ultérieure recommandée).
- **JDK 17** configuré dans Android Studio.
- Un appareil Android physique sous **Android 9.0 ou supérieur** avec Bluetooth et Localisation activés.
- Un équipement cible BLE (ex: STM32, ESP32, Arduino, Nordic nRF).

### Étapes de compilation

1. Cloner le dépôt :
   ```bash
   git clone https://github.com/votre-compte/BLE_Terminal_JILAB.git
   ```
2. Ouvrir le projet dans **Android Studio**.
3. Lancer une synchronisation Gradle : `File` > `Sync Project with Gradle Files`.
4. Connecter votre smartphone en mode Débogage USB.
5. Cliquer sur **Run 'app'** (`Shift + F10`).

---

## 📖 Guide d'Utilisation

1. **Scanner et Se Connecter** :
   - Ouvrez l'application.
   - Appuyez sur **Scanner** pour rechercher les équipements BLE environnants.
   - Utilisez le champ **Filtrer les appareils...** pour restreindre la liste par nom ou adresse MAC.
   - Cliquez sur l'appareil cible pour initier la connexion GATT.

2. **Échanger des données** :
   - Saisissez votre texte dans la zone de saisie au bas du terminal.
   - Appuyez sur **Envoyer** (ou utilisez les boutons de macros).
   - Les données reçues du microcontrôleur apparaissent instantanément dans la console.

3. **Configurer les Macros** :
   - Effectuez un **appui long** sur un bouton de macro pour personnaliser son nom et la commande associée.
   - Un **clic court** transmet immédiatement la commande au périphérique.

4. **Modifier les Paramètres** :
   - Appuyez sur l'icône **Configuration ⚙️** dans la barre supérieure.
   - Ajustez les UUIDs, activez le mode **HEX**, configurez la **reconnexion automatique** ou le **fichier de logs**.
   - Cliquez sur **Enregistrer** pour valider vos modifications.

---

## ⚙️ Configuration Avancée

Dans l'écran **Configuration**, vous pouvez ajuster :
- **UUIDs personnalisés** : Modifier le service et les caractéristiques TX/RX.
- **Afficher en Hexadécimal (HEX)** : Convertit automatiquement le texte saisi et les octets reçus en paires hexadécimales (ex: `48 45 4C 4C 4F`).
- **Enregistrer les logs** : Active/désactive l'écriture automatique dans `ble_log.txt`.
- **Nombre de boutons macros** : Boutons `+` et `-` pour régler le nombre de macros affichées (de 1 à 32).
- **Reset par défaut** : Restaure les paramètres et les macros d'origine.

---

## 📄 Licence

Ce projet est développé dans le cadre des travaux de laboratoire **JILAB**.Tous droits réservés.
