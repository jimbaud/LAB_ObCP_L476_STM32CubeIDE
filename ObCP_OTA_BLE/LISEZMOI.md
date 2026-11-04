# ObCP OTA BLE — charger un programme dans l'ObCP par Bluetooth

Version 1.2 — novembre 2026 (chargeur 1.1, application PC 1.2). Documentation complète :
`Documentation/ObCP_OTA_BLE_Guide.docx`.

L'ObCP n'a pas de sonde ST-LINK : jusqu'ici, on la programmait par l'USB en mode
DFU (interrupteur SW4 sur « Prog », RESET, STM32CubeProgrammer, SW4 sur « Run »,
RESET). Ce projet ajoute le chargement **par Bluetooth** (OTA, *over the air*) :
un clic dans STM32CubeIDE compile le programme et l'envoie à la carte, sans câble
de données ni interrupteur.

## Principe

```
 Flash du STM32L476RG (1 Mo)
 0x0800 0000 +----------------------------+  banque 1
             | ObCP_BLE_Loader (chargeur) |  programmé une seule fois (DFU)
 0x0808 0000 +----------------------------+  banque 2
             | votre application          |  remplacée par Bluetooth
 0x0810 0000 +----------------------------+
```

À chaque démarrage, le chargeur lance l'application… sauf si le bouton **USER**
est maintenu pendant un appui sur **RESET** (ou si l'application le demande) :
il attend alors un programme par Bluetooth (LED bleue qui clignote).

## Nouveautés de la version 1.2 (application PC)

- **Plus besoin de toucher la carte** si son programme connaît la commande `#ota`
  (programme modèle `OBCP_template_2026`, version 2.1.0 et suivantes) : quand la
  carte mémorisée fonctionne normalement, `obcp_ota.py` la reconnaît, lui envoie
  `#ota`, attend qu'elle se présente en mode chargement, puis transfère.
  L'adresse Bluetooth du chargeur est mémorisée dans `~/.obcp_ota.json` à chaque
  transfert (ou par `--liste`) ; celle du programme s'en déduit (premier octet
  modifié : `C0:…` pour le chargeur, `C1:…` pour le programme). Windows et Linux
  seulement (macOS ne donne pas les adresses Bluetooth).
- Les fichiers `.elf`, `.hex` et `.bin` d'un même programme donnent maintenant
  exactement la même image (les octets d'alignement entre sections valent 0
  dans les trois), donc le même CRC-32.
- Le chargeur ne change pas (1.1) : rien à reprogrammer sur la carte.

## Nouveautés de la version 1.1

- **Fin de transfert** : le chargeur 1.0 coupait la liaison dès la commande RUN,
  avant que l'accusé de réception parvienne au PC ; Windows affichait alors
  « opération annulée » alors que le programme était installé. Le chargeur 1.1
  laisse le PC se déconnecter, et l'application PC tolère cette coupure.
- **Coupures de liaison** : l'application PC se reconnecte toute seule. Si la
  carte avait déjà validé le programme (coupure juste après END), le transfert
  est terminé ; sinon il recommence. La carte indique la cause de la coupure.
- **Chargeur plus robuste** : envois SPI protégés contre l'interruption du
  module BLE, réinitialisation automatique si le module redémarre seul.
- Pour en profiter côté carte, reprogrammer le chargeur en DFU (étape 2
  ci-dessous) ; l'application PC 1.1 fonctionne aussi avec un chargeur 1.0.

## Contenu

| Dossier | Rôle |
|---|---|
| `ObCP_BLE_Loader/` | Projet STM32CubeIDE/CubeMX du **chargeur** (code dans `Loader/`). |
| `ObCP_OTA_Demo/` | Projet d'**application d'exemple** (LED clignotante, appui long sur USER = retour au chargeur). |
| `ObCP_OTA_Tools/` | **Application PC** `obcp_ota.py` (Python + Bleak), vérificateur `verif_cubemx.py`, et commandes prêtes pour STM32CubeIDE (`*.launch`). |
| `Binaires/` | Programmes déjà compilés (configuration Release). |
| `Tests/` | Tests sur PC (aucune carte nécessaire) : `./run_tests.sh`. |
| `Documentation/` | Guide d'utilisation et description du programme (Word). |

## Démarrage rapide

1. **PC** : installer Python 3.10 ou plus récent, puis `python -m pip install bleak`.
2. **Chargeur** (une seule fois, en DFU comme d'habitude) : SW4 sur *Prog*, RESET,
   puis `STM32_Programmer_CLI -c port=usb1 -e all -w Binaires/ObCP_Chargeur_et_Demo.hex -v -s`,
   SW4 sur *Run*, RESET. La LED clignote en vert : la démonstration tourne.
3. **STM32CubeIDE** : *File › Import › General › Existing Projects into Workspace*,
   importer `ObCP_OTA_Tools` et `ObCP_OTA_Demo`.
4. **Mode chargement** : maintenir **USER**, appui bref sur **RESET**, relâcher USER
   (LED bleue clignotante). Avec la démo : appui long (2 s) sur USER suffit ; avec
   le programme modèle 2026 (2.1.0) : commande `#ota`, que `obcp_ota.py` 1.2 envoie
   lui-même à une carte qu'il connaît.
5. Dans `ObCP_OTA_Demo/Core/Src/demo.c`, remplacer `DEMO_VERSION 1` par `2`,
   sélectionner le projet puis **Run › External Tools › ObCP OTA - compiler et
   transferer** : la LED clignote maintenant en bleu.

En ligne de commande : `python ObCP_OTA_Tools/obcp_ota.py Binaires/ObCP_OTA_Demo_v2.elf`.

## Rendre une application compatible

Une seule modification, dans le fichier `STM32L476RGTX_FLASH.ld` du projet :

```
FLASH (rx) : ORIGIN = 0x8000000, LENGTH = 1024K     <- avant
FLASH (rx) : ORIGIN = 0x8080000, LENGTH = 512K      <- après
```

Recommandé : copier `ObCP_OTA_Demo/Core/Inc/obcp_ota_request.h` dans `Core/Inc`
et appeler `obcp_ota_init_vectors();` dans `USER CODE BEGIN 1` de `main.c`.
`obcp_ota_enter_loader()` permet en plus de revenir au chargeur par logiciel
(c'est ce que fait la commande `#ota` du programme modèle 2026, version 2.1.0).

## État des essais

Les deux projets compilent sans avertissement (Debug et Release). Tests sur PC :
1 679 vérifications en C du cœur du transfert et 66 de la partie Bluetooth du
chargeur (faux module BlueNRG), sous AddressSanitizer, et 52 tests Python dont
des transferts complets simulés avec pannes, coupures et reconnexions, et l'envoi
automatique de `#ota` à une carte qui fonctionne.

Premier essai sur une vraie ObCP (version 1.0, Windows) : connexion, effacement
et envoi d'un programme de 4,6 Ko à 6,8 Ko/s réussis ; erreur à la toute fin,
très probablement pendant la commande RUN (corrigée en 1.1). **Les versions 1.1
et 1.2 n'ont pas encore été essayées sur carte** : voir le chapitre 9 du guide.
