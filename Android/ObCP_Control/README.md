# ObCP Control — commande et télémesure de la carte ObCP sous Android

![Android](https://img.shields.io/badge/Android-%E2%89%A5%209.0-3DDC84?logo=android&logoColor=white)
![Kotlin](https://img.shields.io/badge/Langage-Kotlin-7F52FF?logo=kotlin&logoColor=white)
![Interface](https://img.shields.io/badge/UI-Jetpack%20Compose%20%2B%20Material%203-blue)
![Connexion](https://img.shields.io/badge/Communication-Bluetooth%20LE-blueviolet)
![Version](https://img.shields.io/badge/Release-v1.0-informational)

**ObCP Control** est une application Android conçue pour **piloter la carte pédagogique ObCP** (*Objet Connecté Personnel*, plateforme pédagogique) basée sur un microcontrôleur **STM32L476** et un module BLE. Développée pour les enseignements et projets de [SUPMICROTECH–ENSMM](https://www.supmicrotech.fr/), elle permet de commander les sorties de la carte et de visualiser les données de capteurs par Bluetooth Low Energy (BLE).

L'application fournit une interface plus spécialisée que le [terminal générique BLE Terminal JILAB](../BLE_Terminal_JILAB/README.md) : elle traduit les gestes de l'utilisateur (curseurs, boutons) en commandes du firmware, et affiche des graphiques de mesures.

## Télécharger l'application

| Ressource | Lien |
|---|---|
| **APK Android v1.0 — installation directe** | **[Télécharger ObCP_Control_release.apk](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/releases/download/ObCP_Control_V1.0/ObCP_Control_release.apk)** |
| Page de la publication v1.0 | [ObCP_Control_V1.0](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/releases/tag/ObCP_Control_V1.0) |
| Toutes les versions publiées | [GitHub Releases](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/releases) |
| Code source | [Dossier Android/ObCP_Control](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/tree/main/Android/ObCP_Control) |

Le fichier APK de la publication v1.0 fait environ **48,0 Mo**. Le lien direct reste associé à **cette version précise** ; consulter « Toutes les versions publiées » pour les mises à jour.

### Installation sur un téléphone

1. Vérifier que le téléphone dispose d'**Android 9.0 (API 28) ou plus récent** et du Bluetooth LE.
2. Télécharger l'APK et l'ouvrir depuis le téléphone.
3. Autoriser temporairement l'installation depuis la source utilisée si Android le demande, puis installer l'application.
4. Mettre la carte ObCP sous tension, avec un firmware qui expose les services/caractéristiques BLE attendus.
5. Lancer **ObCP Control**, activer le Bluetooth et accepter les autorisations de recherche des périphériques (*Appareils à proximité* et éventuellement *Localisation*).
6. Rechercher la carte, sélectionner son nom et attendre l'établissement de la connexion.

Pour les mises à jour installées par-dessus la version existante, conserver le même identifiant d'application et la même clé de signature, avec un `versionCode` supérieur.

## Fonctionnalités principales

| Partie de l'interface | Fonction | Effet sur la carte |
|---|---|---|
| Scan BLE | Recherche et filtre par nom/MAC | Détection des cartes proches |
| Connexion | Sélection d'un périphérique BLE | Découverte du service et activation des notifications |
| LED rouge, verte, bleue | Trois curseurs 0–100 % | Commande individuelle des composantes RGB |
| PWM | Curseur 0–100 % | Réglage d'une sortie PWM externe |
| Timer | Bouton marche/arrêt | Demande d'envoi périodique des mesures |
| Reset | Bouton de réinitialisation | Envoi de `#rst` et remise à zéro des commandes dans l'interface |
| Accéléromètre | Graphiques X, Y, Z | Visualisation des accélérations reçues |
| Température | Graphique dédié | Affichage de la température reçue |
| Graphiques | Effacement, zoom, défilement | Exploration des historiques en mémoire |

### Correction appliquée aux curseurs RGB

Les curseurs RGB sont affichés en pourcentage de 0 à 100, puis convertis en valeur entière sur 16 bits par la loi mise en œuvre dans l'application :

$$
N_{\mathrm{RGB}}=\left\lfloor 65535\left(\frac{p}{100}\right)^3\right\rfloor,\qquad 0\le p\le 100.
$$

Cette loi en puissance trois donne une progression perceptive plus progressive aux faibles niveaux (ce n'est **pas** un logarithme au sens mathématique strict). Pour la sortie PWM, la conversion est **linéaire** :

$$
N_{\mathrm{PWM}}=\left\lfloor 65535\frac{p}{100}\right\rfloor.
$$

Les crochets de partie entière inférieure (plancher) correspondent ici à la conversion `toInt()` de Kotlin : les valeurs calculées sont non négatives, donc tronquées à l'entier inférieur.

L'envoi est déclenché à la fin du déplacement du curseur, afin d'éviter d'émettre une commande à chaque changement de valeur affichée.

## Protocole de communication avec le STM32

### Service BLE et caractéristiques GATT

**Configuration réellement codée dans `BleManager.kt` :**

| Utilisation | UUID | Propriété attendue |
|---|---|---|
| Service de communication | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | Service GATT |
| Données **reçues** par le téléphone | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | `NOTIFY` (+ CCCD `0x2902`) |
| Commandes **envoyées** par le téléphone | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | `WRITE WITHOUT RESPONSE` dans cette implémentation |

> **Compatibilité Nordic NUS :** ces caractéristiques sont **inversées** par rapport à la définition Nordic NUS officielle (`002 = RX / WRITE`, `003 = TX / NOTIFY`). La configuration ci-dessus correspond aux rôles utilisés par l'application et décrits dans le projet ObCP BLE UART du dépôt. Pour communiquer avec un périphérique NUS standard, il faut modifier les UUID dans `BleManager.kt` (l'application ObCP Control n'a pas d'écran de configuration des UUID). Voir la [documentation Nordic NUS](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/libraries/bluetooth/services/nus.html).

### Commandes envoyées (Android → STM32)

| Action | Format envoyé | Exemple | Réponse utilisée par l'application |
|---|---|---|---|
| LED rouge | `r<valeur>` | `r65535` | `Red = 65535` |
| LED verte | `v<valeur>` | `v32768` | `Green = 32768` |
| LED bleue | `b<valeur>` | `b0` | `Blue = 0` |
| Sortie PWM | `m<valeur>` | `m65535` | `PWM = 65535` |
| Démarrer l'envoi périodique | `start timer` | — | Aucune réponse vérifiée |
| Arrêter l'envoi périodique | `stop timer` | — | Aucune réponse vérifiée |
| Réinitialiser la carte | `#rst` | — | Aucune réponse vérifiée |

Les valeurs RGB et PWM sont des entiers compris entre **0 et 65535**. Dans le code actuel, les chaînes sont transmises telles quelles en octets : **aucun caractère de fin de ligne n'est ajouté automatiquement** (`CR`, `LF`, `CRLF`). Si le firmware exige un terminateur, il faut adapter l'un des deux côtés.

Pour les curseurs, le ViewModel applique une temporisation de 100 ms après la fin du geste et peut tenter jusqu'à **5 envois**, en attendant à chaque essai une réponse **textuelle exacte** pendant 100 ms. Il s'agit d'une stratégie actuellement implémentée, **pas d'une garantie de livraison des commandes**.

### Trames de données attendues (STM32 → Android)

| Grandeur | Trame attendue | Exemple | Exploitation |
|---|---|---|---|
| Accélération X/Y/Z | `X<valX>Y<valY>Z<valZ>` | `X0.02Y-0.98Z0.15` | Trois courbes de valeurs successives |
| Température | `T = <valeur>` | `T = 24.5` | Courbe de température |

Le code convertit les valeurs en nombres flottants. L'abscisse actuelle des courbes est **l'indice d'échantillon**, et non un temps horodaté. Les mesures sont conservées en mémoire tant que l'application les garde, avec un bouton permettant de vider les courbes.

**Important :** le parseur actuel traite les chaînes reçues telles que délivrées par la notification BLE. Il ne reconstitue pas de manière générale plusieurs trames concaténées ni une trame découpée sur plusieurs notifications. Prévoir une évolution du protocole ou du parseur pour une exploitation plus robuste.

## Construire ou modifier l'application

### Prérequis

- Android Studio, le SDK Android requis et un JDK compatible avec Gradle/Android Gradle Plugin (préférer celui fourni avec Android Studio).
- Connexion Internet lors du premier téléchargement des dépendances.
- Un smartphone Android physique avec Bluetooth LE pour valider la liaison matérielle.

Le dépôt comprend plusieurs projets Android **indépendants** ; ouvrir le sous-dossier `Android/ObCP_Control` directement dans Android Studio.

```bash
git clone https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE.git
```

**Android Studio :** `File > Open… > LAB_ObCP_L476_STM32CubeIDE/Android/ObCP_Control`, puis **File > Sync Project with Gradle Files**.

Paramètres actuellement déclarés dans `app/build.gradle.kts` :

| Paramètre | Valeur |
|---|---|
| `applicationId` | `com.example.obcp_control` |
| `minSdk` | 28 — Android 9.0 |
| `targetSdk` | 36 — **Android 16** |
| `versionCode` | `1` |
| `versionName` | `1.0` |
| Interface | Kotlin, Jetpack Compose, Material 3 |
| Graphiques | MPAndroidChart (dépendance JitPack) |

### Compiler en Debug

Dans le terminal PowerShell d'Android Studio, depuis `Android/ObCP_Control` :

```powershell
.\gradlew.bat :app:assembleDebug
```

L'APK Debug se trouve normalement dans `app/build/outputs/apk/debug/app-debug.apk`. Sur Linux/macOS : `./gradlew :app:assembleDebug`.

### Générer un APK Release signé

Choisir **Build > Generate Signed App Bundle / APK… > APK**, fournir le **keystore**, sélectionner la variante **release**, puis compiler. Le fichier signé est généralement disponible sous `app/build/outputs/apk/release/`.

> Le `build.gradle.kts` actuel n'inclut pas de `signingConfig` dans la variante Release : la seule commande `assembleRelease` ne garantit **pas** que l'APK soit signé. Le keystore et ses mots de passe doivent rester en dehors de GitHub.

Avant toute nouvelle diffusion, incrémenter `versionCode`, mettre à jour `versionName`, tester l'installation puis joindre le nouvel APK à une **GitHub Release**, sous un tag distinct (par exemple `ObCP_Control_V1.1`).

## Structure du projet et rôle des fichiers

```text
ObCP_Control/
├── app/src/main/
│   ├── AndroidManifest.xml
│   └── java/com/example/obcp_control/
│       ├── BleManager.kt       # Scan BLE, GATT, caractéristiques, écriture et notifications
│       ├── BleViewModel.kt     # État, commandes, acquittements et décodage des mesures
│       ├── MainActivity.kt     # Écrans Compose, commandes et graphiques
│       └── ui/theme/           # Thème de l'application
├── app/build.gradle.kts        # Paramètres Android de l'application
├── gradle/                     # Dépendances et wrapper
├── settings.gradle.kts         # Projet Gradle autonome
├── gradlew / gradlew.bat
└── README.md
```

Cette séparation est utile pour les travaux pédagogiques :

- `BleManager.kt` : interface matérielle et événements Bluetooth GATT (implémentation à valider notamment sur Android 13+ pour les callbacks et les accès aux caractéristiques) ;
- `BleViewModel.kt` : interprétation des échanges et état de l'interface ;
- `MainActivity.kt` : composants visuels et interaction utilisateur.

## Diagnostic rapide

| Symptôme | Vérifications prioritaires |
|---|---|
| Carte invisible au scan | Carte alimentée, firmware démarré, publicité BLE active, Bluetooth et autorisations Android |
| Connexion établie mais aucune mesure | UUID et propriétés GATT, notifications/CCCD, timer démarré, format de trame |
| LED/PWM non commandé | Caractéristique d'écriture correcte, valeur 0–65535, syntaxe des commandes, firmware correspondant |
| Commande envoyée plusieurs fois | Réponse texte absente, différente ou hors du délai d'attente de 100 ms |
| Graphiques vides | Vérifier la correspondance des trames `X...Y...Z...` et `T = ...` avec les données du firmware |
| Erreur de compilation Gradle | Ouvrir le sous-projet correct, synchroniser Gradle, SDK/JDK, accès à Maven Central et JitPack |

## Ressources utiles

- [Dépôt principal, firmware STM32 et documentation ObCP](../../README.md)
- [Projet STM32 BLE UART](../../OBCP_BLE_UART/README.md)
- [Projet STM32 USB et BLE](../../OBCP_USB_BLE/README.md)
- [BLE Terminal JILAB : terminal de diagnostic générique](../BLE_Terminal_JILAB/README.md)
- [Bluetooth Low Energy sur Android](https://developer.android.com/develop/connectivity/bluetooth/ble/overview)
- [Nordic UART Service : définition des UUID](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/libraries/bluetooth/services/nus.html)
- [MPAndroidChart](https://github.com/PhilJay/MPAndroidChart)

## Auteur et cadre du projet

Application pédagogique **ObCP**, conçue par **Joël IMBAUD** pour [SUPMICROTECH–ENSMM](https://www.supmicrotech.fr/). Voir le [profil GitHub du responsable du dépôt](https://github.com/jimbaud).

Aucune licence de réutilisation ne doit être présumée du seul fait que le dépôt soit accessible publiquement. Se référer à la licence effectivement publiée par le projet, le cas échéant.
