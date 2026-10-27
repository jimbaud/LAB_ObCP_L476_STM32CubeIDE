# BLE Terminal JILAB — terminal Bluetooth Low Energy pour Android

![Android](https://img.shields.io/badge/Android-%E2%89%A5%209.0-3DDC84?logo=android&logoColor=white)
![Kotlin](https://img.shields.io/badge/Langage-Kotlin-7F52FF?logo=kotlin&logoColor=white)
![Interface](https://img.shields.io/badge/UI-Jetpack%20Compose%20%2B%20Material%203-blue)
![Version](https://img.shields.io/badge/Release-v1.0-informational)

**BLE Terminal JILAB** est une application Android de type terminal série sur Bluetooth Low Energy (BLE). Elle permet de rechercher un périphérique, de se connecter à son service GATT, d'envoyer des commandes et d'afficher les données reçues. Elle sert notamment à tester les cartes pédagogiques **ObCP**, mais peut aussi être adaptée à d'autres périphériques BLE disposant de caractéristiques d'écriture et de notification.

L'application utilise **Kotlin**, **Jetpack Compose** et **Material 3**. Son code source est inclus dans le dépôt commun des projets ObCP.

> **À qui s'adresse-t-elle ?** Aux étudiants, enseignants et développeurs qui souhaitent diagnostiquer un échange BLE ou communiquer avec un microcontrôleur sans créer une interface graphique spécifique.

## Télécharger l'application

| Ressource | Lien |
|---|---|
| **APK Android v1.0 — installation directe** | **[Télécharger Terminal_BLE_release.apk](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/releases/download/BLE_Terminal_V1.0/Terminal_BLE_release.apk)** |
| Page de la publication v1.0 | [BLE_Terminal_V1.0](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/releases/tag/BLE_Terminal_V1.0) |
| Toutes les versions publiées | [GitHub Releases](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/releases) |
| Code source | [Dossier BLE_Terminal_JILAB](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/tree/main/Android/BLE_Terminal_JILAB) |

Le fichier APK de la publication v1.0 fait environ **19,3 Mo**. Le lien direct pointe vers **cette version précise** ; pour rechercher une version ultérieure, consulter la page « Toutes les versions publiées ».

### Installer l'APK sur un téléphone

1. Utiliser un téléphone Android **9.0 (API 28) ou plus récent**, équipé du Bluetooth LE.
2. Télécharger l'APK depuis le lien ci-dessus et ouvrir le fichier depuis le téléphone.
3. Autoriser, si Android le demande, l'installation d'applications provenant de cette source (navigateur ou gestionnaire de fichiers). Ne donner cette autorisation qu'à une source de confiance.
4. Installer et lancer **BLE Terminal JILAB**.
5. Activer le Bluetooth et accepter les autorisations nécessaires à la recherche des périphériques, notamment *Appareils à proximité* et, suivant la version d'Android, *Localisation*.

**Mise à jour :** l'installation d'un nouvel APK par-dessus l'ancien nécessite le même identifiant d'application, une signature compatible et un `versionCode` supérieur. Conserver la clé de signature des versions distribuées.

## Fonctionnalités

| Fonction | Utilisation |
|---|---|
| Recherche BLE | Scan des périphériques proches, filtrage par nom ou adresse MAC |
| Connexion GATT | Connexion, découverte des services et activation des notifications |
| Terminal texte | Envoi de chaînes UTF-8 et affichage des données reçues |
| Mode hexadécimal | Envoi d'octets à partir d'une chaîne HEX et affichage des octets en HEX |
| Historique | Affichage chronologique avec heures et catégories `TX`, `RX`, `STATUS` |
| Macros | Boutons configurables (étiquette + commande), nombre réglable de 1 à 32 |
| Reconnexion | Option de tentative de reconnexion au dernier périphérique connu |
| Paramètres BLE | UUID de service, UUID de notification et UUID d'écriture modifiables |
| Journalisation | Option d'écriture des communications dans `ble_log.txt` (voir limitation Android ci-dessous) |

### Démarrer une session BLE

1. Mettre sous tension et démarrer le firmware du périphérique BLE.
2. Appuyer sur **Scanner** et sélectionner son nom (ou son adresse MAC).
3. Attendre l'état **« Prêt à communiquer »**, obtenu après activation des notifications.
4. Saisir un texte dans le terminal puis appuyer sur **Envoyer**, ou utiliser une macro.
5. Observer les messages envoyés (`TX`), reçus (`RX`) et les messages de statut (`STATUS`).
6. Cliquer sur l'état de connexion pour afficher le bouton **Déconnecter**.

**Important pour les protocoles texte :** en mode texte, la version actuelle envoie les octets UTF-8 de la chaîne saisie **sans ajouter automatiquement `CR` (`\r`), `LF` (`\n`) ou `CRLF`**. Le protocole de la carte doit donc accepter ce format, ou être adapté si un terminateur est exigé. En mode HEX, saisir par exemple `41 42 0D 0A` pour transmettre `AB\r\n`.

### Macros et paramètres

- **Appui court** sur une macro : envoi de sa commande si elle n'est pas vide.
- **Appui long** : modification de son libellé et de la commande.
- **Icône de configuration** : paramètres BLE, mode HEX, reconnexion, journalisation et nombre de macros.
- Les paramètres et les macros sont conservés dans les préférences locales de l'application.

## Profil BLE : attention aux UUID

L'application emploie par défaut les UUID suivants, **selon les rôles indiqués dans son code actuel** :

| Fonction réellement attendue par BLE Terminal JILAB | UUID | Propriété GATT attendue |
|---|---|---|
| Service de communication | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | Service GATT |
| Réception sur le téléphone (**notifications**) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | `NOTIFY` (+ CCCD `0x2902`) |
| Émission depuis le téléphone (**écriture**) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | `WRITE` ou `WRITE WITHOUT RESPONSE` |

**Différence avec le Nordic UART Service (NUS) officiel :** dans le profil NUS de Nordic Semiconductor, `6E400002` est la caractéristique **RX / WRITE** du périphérique et `6E400003` sa caractéristique **TX / NOTIFY**. Les rôles utilisés par l'application sont **inversés** par rapport à cette définition officielle. Cette inversion reflète le paramétrage du projet STM32 ObCP documenté dans le dépôt ; elle peut empêcher l'utilisation immédiate avec un périphérique NUS standard.

Pour une cible **NUS Nordic standard**, ouvrir **Configuration** et utiliser :

- **TX Characteristic UUID (Notify)** : `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` ;
- **RX Characteristic UUID (Write)** : `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`.

Les intitulés `TX`/`RX` de l'écran sont vus **du point de vue du périphérique**, et non du téléphone. Vérifier les propriétés exposées par le firmware à l'aide d'un explorateur GATT en cas de doute. Voir la [documentation officielle Nordic NUS](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/libraries/bluetooth/services/nus.html).

## Construire le projet avec Android Studio

### Prérequis

- Android Studio avec le **SDK Android** requis et un **JDK compatible avec la version de Gradle et le plugin Android** du projet (le JDK embarqué d'Android Studio est conseillé).
- Accès réseau lors de la première synchronisation pour télécharger Gradle et les dépendances.
- Pour tester en BLE : un téléphone Android physique et un périphérique BLE compatible.

**Attention :** le dépôt contient **plusieurs projets Android indépendants**. Il faut ouvrir **ce sous-dossier**, pas le dépôt entier comme s'il s'agissait d'un projet Gradle unique.

```bash
git clone https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE.git
```

Dans Android Studio : **File > Open… > `LAB_ObCP_L476_STM32CubeIDE/Android/BLE_Terminal_JILAB`**, puis **Sync Project with Gradle Files**.

La version actuellement déclarée dans `app/build.gradle.kts` est `versionName = "1.0"`, `versionCode = 1`, `minSdk = 28`, `targetSdk = 34`.

### Compiler depuis le terminal (Windows / PowerShell)

Depuis le dossier `Android/BLE_Terminal_JILAB` :

```powershell
.\gradlew.bat :app:assembleDebug
```

Le résultat est normalement `app/build/outputs/apk/debug/app-debug.apk`. Pour Linux/macOS : `./gradlew :app:assembleDebug`.

### Générer un APK Release signé

Dans Android Studio : **Build > Generate Signed App Bundle / APK… > APK > Next**. Sélectionner ou créer un **keystore** (à conserver hors du dépôt Git), choisir la variante **release** et lancer la génération. Le résultat signé est généralement sous `app/build/outputs/apk/release/`.

> `assembleRelease` ne garantit pas à lui seul que l'APK produit est signé : le bloc Gradle `release` fourni ne définit pas de `signingConfig`. Utiliser l'assistant de signature ou configurer une signature externe sécurisée.

Avant une publication, augmenter `versionCode` et adapter `versionName`, puis tester l'APK sur un vrai téléphone. Publier le binaire dans une **GitHub Release**, pas dans le code source du dépôt.

## Organisation du code

```text
BLE_Terminal_JILAB/
├── app/src/main/
│   ├── AndroidManifest.xml
│   └── java/com/example/BLE_Terminal_JILAB/
│       ├── MainActivity.kt       # Scan, connexion GATT, terminal, macros
│       ├── Settings.kt           # Paramètres et persistance locale
│       ├── SettingsScreen.kt     # Écran de configuration
│       └── ui/theme/             # Thème Jetpack Compose
├── gradle/                       # Versions des dépendances, wrapper Gradle
├── app/build.gradle.kts          # Configuration de l'application
├── settings.gradle.kts           # Projet Gradle autonome
├── gradlew / gradlew.bat          # Scripts de compilation
└── README.md
```

### Points d'attention et limites connues

- **Journaux dans `Documents/` :** le code actuel utilise un accès direct au répertoire public via `Environment.getExternalStoragePublicDirectory`. Sur les versions récentes d'Android, les restrictions de stockage (*scoped storage*) peuvent empêcher l'écriture ; ne pas considérer cette fonction comme garantie sur Android 11 et au-delà. Une évolution vers le *Storage Access Framework* ou MediaStore serait préférable.
- **Trames et MTU :** l'application n'implémente pas un protocole générique de fragmentation/réassemblage de longs messages applicatifs. Tenir compte de la taille utile des échanges BLE.
- **UUID et type d'écriture :** le périphérique doit exposer les caractéristiques dans le sens attendu, avec les propriétés correspondantes. Le code sélectionne `WRITE_TYPE_DEFAULT` lors de l'envoi ; un périphérique qui ne propose que `WRITE WITHOUT RESPONSE` peut nécessiter une adaptation du type d'écriture.
- **Reconnexion automatique :** elle utilise l'adresse du dernier périphérique enregistrée et peut ne pas fonctionner si son adresse BLE change (adresse privée aléatoire, par exemple).
- **Permissions :** si aucun périphérique n'est trouvé, vérifier le Bluetooth, les autorisations Android, la localisation et l'état de publicité du périphérique.

## Ressources associées

- [Documentation générale du projet ObCP](../../README.md)
- [Projet STM32 ObCP BLE UART](../../OBCP_BLE_UART/README.md)
- [Application Android ObCP Control](../ObCP_Control/README.md)
- [Documentation officielle Android sur le BLE](https://developer.android.com/develop/connectivity/bluetooth/ble/overview)
- [Nordic UART Service — spécification des caractéristiques](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/libraries/bluetooth/services/nus.html)

## Auteur et cadre d'utilisation

Application documentée dans le cadre des projets pédagogiques et de prototypage **JILAB / ObCP**. Dépôt administré par [Joël IMBAUD](https://github.com/jimbaud) ; projets ObCP associés à [SUPMICROTECH–ENSMM](https://www.supmicrotech.fr/).

Le README d'origine mentionne **« Tous droits réservés »** ; l'accès public au code ne vaut pas, à lui seul, attribution d'une licence de réutilisation. Consulter les conditions précisées par le responsable du dépôt avant toute redistribution ou modification sous une autre licence.
