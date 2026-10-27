# Applications Android — ObCP et BLE Terminal JILAB

Ce répertoire rassemble les applications **Android** associées à la plateforme pédagogique **ObCP (Objet Connecté Personnel)** et aux travaux de communication **Bluetooth Low Energy (BLE)**. Les projets sont développés en **Kotlin**, avec **Jetpack Compose / Material 3**, et peuvent être ouverts indépendamment dans Android Studio.

> **Vous souhaitez simplement utiliser une application ?** Téléchargez l'APK correspondant ci-dessous. **Vous souhaitez consulter ou modifier le code ?** Ouvrez le dossier du projet et son README détaillé.

## 📲 Télécharger les applications (APK Release)

| Application | Utilisation | Version publiée | APK Android | Détails de la Release |
|---|---|---|---|---|
| **ObCP Control** | Pilotage et télémesure de la carte ObCP | **v1.0** | **[⬇️ Télécharger l'APK (48,0 Mo)](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/releases/download/ObCP_Control_V1.0/ObCP_Control_release.apk)** | [ObCP_Control_V1.0](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/releases/tag/ObCP_Control_V1.0) |
| **BLE Terminal JILAB** | Terminal BLE UART générique | **v1.0** | **[⬇️ Télécharger l'APK (19,3 Mo)](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/releases/download/BLE_Terminal_V1.0/Terminal_BLE_release.apk)** | [BLE_Terminal_V1.0](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/releases/tag/BLE_Terminal_V1.0) |

➡️ **[Consulter toutes les Releases du dépôt](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/releases)** pour retrouver l'historique et les futures versions.

*Ces liens directs correspondent aux fichiers publiés le 25 octobre 2026. Les liens vers les pages de Release permettent également de lire les notes de version.*

## 🧭 Quelle application choisir ?

### 🎛️ ObCP Control — interface dédiée à la carte ObCP

**[Code source](./ObCP_Control/)** · **[Documentation complète](./ObCP_Control/README.md)** · **[Release v1.0](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/releases/tag/ObCP_Control_V1.0)**

Application conçue pour commander et superviser la carte **ObCP / STM32L476** par BLE :

- Recherche et connexion au périphérique BLE ;
- commande des **LED RGB**, de la **sortie PWM** et du **timer** ;
- commande de réinitialisation de la carte ;
- visualisation en temps réel de l'**accélération X/Y/Z** et de la **température** ;
- interface adaptable aux orientations portrait et paysage.

**Public visé :** utilisateurs de la carte ObCP et étudiants réalisant les travaux pratiques associés.

### 💬 BLE Terminal JILAB — terminal BLE polyvalent

**[Code source](./BLE_Terminal_JILAB/)** · **[Documentation complète](./BLE_Terminal_JILAB/README.md)** · **[Release v1.0](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/releases/tag/BLE_Terminal_V1.0)**

Terminal de communication BLE UART utilisable avec la carte ObCP ou avec d'autres systèmes embarqués compatibles :

- Scan BLE, filtrage par nom ou adresse MAC et connexion GATT ;
- affichage et émission en modes **texte UTF-8** ou **hexadécimal** ;
- **macros de commandes** configurables (jusqu'à 32) ;
- **UUID de service et de caractéristiques personnalisables** ;
- horodatage des échanges et possibilité d'**exporter les journaux**.

**Public visé :** développement, mise au point et diagnostic de liaisons BLE sur STM32 et autres microcontrôleurs.

## 📱 Installation sur téléphone ou tablette

1. Utiliser un appareil **Android 9.0 (API 28) ou supérieur** équipé du Bluetooth Low Energy.
2. Télécharger le fichier `.apk` depuis l'un des liens ci-dessus.
3. Ouvrir le fichier sur l'appareil. Si Android le demande, autoriser temporairement l'installation depuis cette application source (navigateur ou gestionnaire de fichiers).
4. Installer et lancer l'application.
5. Autoriser les permissions Bluetooth, et les permissions de localisation lorsqu'elles sont demandées pour la recherche BLE.
6. Mettre sous tension la carte ObCP ou le périphérique BLE cible, puis lancer la recherche et se connecter.

> **Mises à jour :** une nouvelle version peut normalement s'installer au-dessus de l'ancienne si l'identifiant de l'application et la clé de signature restent inchangés et si le `versionCode` progresse. Les APK sont distribués en dehors du Google Play Store.

## 📡 Compatibilité BLE

Les deux applications documentent une communication de type **Nordic UART Service (NUS)**, ou équivalent lorsque les UUID sont configurables.

| Élément | UUID |
|---|---|
| Service UART | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| Écriture depuis Android vers le périphérique | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |
| Notifications du périphérique vers Android | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |

**Important :** la présence de ce service BLE ne garantit pas la compatibilité des **commandes applicatives**. *ObCP Control* attend les commandes et les trames décrites dans [son README](./ObCP_Control/README.md), alors que *BLE Terminal JILAB* permet des échanges configurables.

## 🛠️ Ouvrir et compiler les projets dans Android Studio

Cloner le dépôt une seule fois :

```bash
git clone https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE.git
```

Dans **Android Studio**, choisir **File > Open** et ouvrir **l'un des répertoires suivants**, qui constituent chacun un projet Gradle autonome :

- `LAB_ObCP_L476_STM32CubeIDE/Android/ObCP_Control`
- `LAB_ObCP_L476_STM32CubeIDE/Android/BLE_Terminal_JILAB`

Attendre la synchronisation Gradle et configurer le SDK Android demandé par le projet. Pour construire un APK de développement, exécuter dans le terminal du projet :

**Windows (PowerShell) :**

```powershell
.\gradlew.bat :app:assembleDebug
```

**Linux/macOS :**

```bash
./gradlew :app:assembleDebug
```

L'APK Debug est généré dans `app/build/outputs/apk/debug/`. Pour produire une version distribuable, utiliser **Build > Generate Signed App Bundle / APK > APK > release** et conserver la clé de signature **hors du dépôt**.

### Versions Android définies par les projets

| Projet | Version Android minimale | SDK cible déclaré |
|---|---|---|
| ObCP Control | Android 9.0 (API 28) | API 36 |
| BLE Terminal JILAB | Android 9.0 (API 28) | API 34 |

## 📁 Structure du répertoire

```text
Android/
├── README.md                   # Cette page d'accueil
├── .gitignore                  # Exclusion des fichiers locaux et générés
├── ObCP_Control/              # Projet Android Studio autonome
│   ├── README.md              # Documentation détaillée
│   ├── app/                   # Sources, ressources et manifeste
│   └── settings.gradle.kts    # Configuration du projet Gradle
└── BLE_Terminal_JILAB/        # Projet Android Studio autonome
    ├── README.md
    ├── app/
    └── settings.gradle.kts
```

Le dépôt héberge les **sources** des applications ; les fichiers **APK Release** destinés à l'installation sont publiés dans la rubrique [Releases](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/releases). Les fichiers générés, configurations locales et clés de signature ne doivent pas être versionnés.

## 🔗 Documentation et crédits

- [Accueil du dépôt ObCP et projets STM32CubeIDE](../README.md)
- [Projets firmware STM32 dans le dépôt](../ObCP_SampleApp_BLE/)
- [Documentation ObCP](../Documentation%20ObCP/)
- [SUPMICROTECH — ENSMM](https://www.supmicrotech.fr/)
- [Compte GitHub du projet : jimbaud](https://github.com/jimbaud)

**Projet pédagogique ObCP — SUPMICROTECH-ENSMM.** Les conditions de réutilisation sont à vérifier dans les README et les éventuels fichiers de licence propres à chaque application.
