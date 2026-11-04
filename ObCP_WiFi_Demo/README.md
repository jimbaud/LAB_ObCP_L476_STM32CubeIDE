# Démonstrateur Wi-Fi 6 — ObCP + X-NUCLEO-67W61M1

Programme de démonstration « bare-metal » (sans RTOS) pour l'**ObCP** (Objet Connecté
Personnel, SUPMICROTECH-ENSMM, STM32L476RG) équipé de la carte d'extension
**X-NUCLEO-67W61M1** (module Wi-Fi 6 / BLE **ST67W611M1**). Il reprend les quatre démos de
la version NUCLEO-L476RG et les adapte à l'ObCP :

- la console passe par le **port USB de l'ObCP** (port COM virtuel, classe CDC : aucun
  pilote à installer, débit indifférent) ;
- le programme se charge par le **chargeur USB DFU** de ST (interrupteur Prog/Run + Reset,
  ou commande `dfu`), avec STM32CubeProgrammer ;
- la **LED RVB**, la **sortie PWM** du bornier et l'**accéléromètre** de l'ObCP sont
  utilisés par les commandes `led`, `pwm`, `sensors`, la page web et MQTT ;
- le firmware du module se met à jour par le terminal (`module ota`) et, pour une première
  programmation, l'ObCP relaie son port USB vers l'UART du module (`module boot` +
  QConn_Flash), sans adaptateur ni autre carte ;
- **version 1.1** : le projet est décrit par un fichier STM32CubeMX
  (**`ObCP_WiFi_Demo.ioc`**) ; l'initialisation du matériel est générée par CubeMX et peut
  être modifiée puis **régénérée sans casser la démo** (voir plus bas) ;
- **version 1.2** : **démarrage automatique** pour une utilisation sur batterie, sans
  terminal (connexion à la box enregistrée, point d'accès et serveur web, télémétrie MQTT :
  commande `auto`, ou carte « Démarrage automatique » de la page web) ; code commenté pour
  un lecteur débutant et **documentation du programme**.

Le guide complet (montage, programmation par DFU, mise à jour du module, démos pas à pas,
utilisation sur batterie, dépannage) est dans **`Doc/Guide_Demonstrateur_WiFi6_ObCP.docx`** ;
l'organisation du code, son déroulement et la référence de toutes ses fonctions sont dans
**`Doc/Documentation_Programme_ObCP.docx`** (voir « Lire le code »).

| Démo | Contenu | Commandes |
|------|---------|-----------|
| 1. Scanner Wi-Fi | réseaux visibles (RSSI, canal, sécurité, Wi-Fi 6), occupation des canaux, suivi du RSSI en direct (couleur de la LED RVB, CSV) | `scan`, `channels`, `track` |
| 2. Connexion et services | DHCP ou IP fixe, DNS, ping, RTC mise à l'heure par SNTP (fuseau, heure d'été), reconnexion automatique | `connect`, `status`, `ip`, `dns`, `ping`, `ntp`, `time` |
| 3. Serveur web (SoftAP) | point d'accès avec QR code, tableau de bord (accéléromètre et bille, couleur de la LED, sortie PWM, réseaux voisins, configuration Wi-Fi), API JSON | `web start`, `web status`, `web stop` |
| 4. Télémétrie MQTT | état JSON périodique (accélérations, inclinaisons, LED, PWM…), commandes à distance (LED, couleur, PWM), évènements boutons, online/offline (LWT) | `mqtt start`, `mqtt pub`, `mqtt sub` |

## Contenu

```
ObCP_WiFi_Demo.ioc        configuration STM32CubeMX : broches (étiquettes), horloges, SPI1, SPI3, ADC1,
                          TIM1, TIM3, RTC, USB (CDC), NVIC — CubeMX 6.11.1, STM32Cube FW_L4 V1.18.1
Core/Src, Core/Inc        fichiers générés par CubeMX (main.c, gpio.c, spi.c, adc.c, tim.c, rtc.c,
                          stm32l4xx_it.c, stm32l4xx_hal_msp.c, main.h...) : la démo n'y occupe que
                          des sections USER CODE ; et fichiers de la démo, jamais générés :
  board.c                   chargeur DFU, quartz 32 kHz, suite de l'initialisation, diagnostic, RTC
  cdc_port.c                port COM virtuel (tampons, contrôle de flux, DTR)
  fault_handlers.c          HardFault & co. : rapport conservé pendant le redémarrage
  uart_bridge.c             passerelle USB <-> UART du module (module boot)
  ncp_port_obcp.c           adaptation du pilote du module à la carte
Core/Src/app/             console, commandes, configuration en flash, LED RVB, accéléromètre,
                          démos 1 à 4, page web, QR code, mise à jour du module
Core/Src/st67w6x/         pilote du ST67W611M1 : trames SPI, moteur AT, Wi-Fi, réseau, MQTT (portable)
Core/Src/qrcodegen/       QR codes (Project Nayuki, MIT)
Core/Inc/                 tous les en-têtes (générés et de la démo)
USB_DEVICE/               généré par CubeMX : USB device, classe CDC (rappels -> cdc_port.c)
Drivers/, Middlewares/    bibliothèques copiées par CubeMX (HAL STM32L4, CMSIS, USB device de ST)
Tests/                    tests sur PC : pilote, modules, port COM virtuel, simulation des 4 démos
Tools/verif_cubemx.py     vérification du projet après une régénération par CubeMX
Tools/rssi_logger.py      enregistrement du RSSI en CSV + courbe (démo 1)
Tools/maj_module_ota.py   mise à jour du firmware du module depuis le PC (YMODEM, commande module ota)
Binaries/                 programme précompilé (.hex, .bin, .elf) à charger par USB DFU
Doc/                      guide d'utilisation et documentation du programme (Word)
.project, .cproject       projet STM32CubeIDE (Debug -Og, Release -Os), .mxproject (CubeMX)
Makefile                  compilation en ligne de commande
```

Tout le code de la démo est sous `Core/` : il est compilé avec les seuls chemins et
dossiers que CubeMX déclare lui-même, sans réglage ajouté au projet CubeIDE.

## Démarrage rapide

1. **Matériel** : cavaliers JP1 et JP2 de la X-NUCLEO en place ; interrupteur **SW4** de
   l'ObCP sur **Run** ; X-NUCLEO enfichée sur les connecteurs Arduino de l'ObCP ; ObCP
   reliée au PC par un câble USB **de données** ; interrupteur **SW2** sur **ON** (LED
   PWR ON allumée).
2. **Programmation de l'ObCP** (USB DFU, STM32CubeProgrammer) :
   - activer le chargeur DFU : SW4 sur **Prog** puis appui sur **RST**, ou, si le
     démonstrateur tourne déjà, commande `dfu` puis touche `o` ;
   - STM32CubeProgrammer : interface **USB**, port USB1, *Connect*, *Erasing & programming*,
     fichier `Binaries/ObCP_WiFi_Demo.hex`, *Verify* + *Run after programming*, *Start
     Programming* ; ou en ligne de commande :
     `STM32_Programmer_CLI -c port=usb1 -w Binaries/ObCP_WiFi_Demo.hex -v -s` ;
   - remettre SW4 sur **Run** (et appuyer sur RST) si SW4 a été utilisé.
3. **Terminal** : « Périphérique série USB (COMx) » sous Windows, `/dev/ttyACM0` sous Linux ;
   débit indifférent, 8N1, UTF-8, Entrée = CR, au moins 100 colonnes. La bannière de
   démarrage est conservée jusqu'à l'ouverture du terminal ; le port disparaît pendant un
   reset (rouvrir la connexion). Dans la console de STM32CubeIDE (sans couleurs ANSI) :
   `set term.color 0` puis `save`.
4. **Firmware du module** : il faut le firmware *mission T01* v2.0.x (2.0.106 conseillé).
   La bannière et la commande `module` affichent la version présente. Si elle est plus
   ancienne : `module ota` (voir ci-dessous). Un module d'usine trop ancien (en-tête SPI
   `0x5501`, signalé au démarrage) ou en T02 demande une première programmation :
   `module boot` + QConn_Flash (guide, 3.4), ou une NUCLEO-H563ZI (guide, 3.5).
5. **Premières commandes** :

```
help demo                         guide rapide
led rouge 30 · led off · pwm 50   LED RVB et sortie PWM de l'ObCP
sensors suivi                     accéléromètre et inclinaisons en direct (touche = arrêt)
scan                              démo 1
connect "Ma Box" motdepasse       démo 2, puis « save » pour s'y reconnecter au démarrage
ping 8.8.8.8 · ntp · time
web start                         démo 3 : rejoindre ObCP-WiFi-<id> (obcpdemo), ouvrir http://10.19.96.1/
mqtt start                        démo 4 : broker.emqx.io, topics obcp/<id>/...
auto web on · auto mqtt on · save services lancés seuls à la mise sous tension (batterie)
```

Côté PC pour la démo 4 :

```
mosquitto_sub -h broker.emqx.io -t "obcp/<id>/#" -v
mosquitto_pub -h broker.emqx.io -t "obcp/<id>/cmd" -m "color=vert"
mosquitto_pub -h broker.emqx.io -t "obcp/<id>/cmd" -m "pwm=50"
```

Maintenir le bouton **User1** pendant un reset démarre avec les valeurs d'usine.

## Utilisation sur batterie (démarrage automatique)

Sur batterie, sans PC ni terminal, la carte lance seule à la mise sous tension les services
choisis, avec la connexion enregistrée en flash :

| Paramètre | Commande | Défaut | Démarre seul |
|-----------|----------|--------|--------------|
| `wifi.auto` | `auto wifi on` | 1 | connexion à la box enregistrée (`connect` puis `save`), reconnexion après une coupure |
| `web.auto` | `auto web on` | 0 | point d'accès ObCP-WiFi-<id> et serveur web, une fois la tentative de connexion à la box terminée (35 s d'attente au plus ; aussitôt sans box enregistrée) |
| `mqtt.auto` | `auto mqtt on` | 0 | télémétrie MQTT, dès que la carte a obtenu son adresse sur la box |

```
connect "Ma Box" motdepasse       une fois, avec le terminal
auto web on                       et/ou  auto mqtt on   (off pour retirer)
save                              puis alimenter la carte par sa batterie (J1, SW2 sur ON)
auto                              résumé de ce qui démarre seul
```

La bannière l'indique (ligne « Démarrage ») et `info` aussi. Le même réglage se fait depuis
la page web (carte « Démarrage automatique », enregistré aussitôt en flash) ou par
`GET /api/auto?web=1&mqtt=1`. En démarrage automatique, le QR code n'est pas affiché
(`web start` le montre à la demande). Pour reprendre la main : `auto web off`,
`auto mqtt off`, `save`, ou User1 maintenu pendant un reset (valeurs d'usine). Changer le
mot de passe du point d'accès (`set ap.pass ...`, `save`) avant de laisser une carte ainsi
réglée sans surveillance. Détails : guide, 5.6.

## Lire le code

Le code de la démo est commenté pour un lecteur débutant : chaque fichier commence par un
résumé de son rôle, chaque fonction publique est décrite dans son en-tête (`Core/Inc/*.h`),
et les passages délicats (interruptions, tampons circulaires, machines à états, trames
SPI) sont expliqués sur place. Ordre de lecture conseillé :

1. `Core/Src/main.c` : initialisation générée par CubeMX, puis `app_init()` et `app_loop()` ;
2. `Core/Src/app/app.c` : démarrage (bannière, module, services automatiques), boucle
   principale et tâches de fond (`app_idle()`) ;
3. `Core/Src/app/cli.c` : table des commandes `s_cmds[]` et commandes générales ;
4. une démo : `demo_scan.c`, `demo_net.c`, `demo_web.c` ou `demo_mqtt.c` ;
5. le pilote du module : `Core/Src/st67w6x/ncp_at.c` (commandes AT), puis `ncp_spi.c`
   (trames SPI).

La **documentation du programme** (`Doc/Documentation_Programme_ObCP.docx`) en donne la vue
d'ensemble : carte des fichiers et de leurs dépendances, schémas de déroulement
(démarrage, boucle principale, trajet d'une commande, d'une requête web, d'un message
MQTT), dialogue avec le module, occupation de la mémoire, brochage, référence des 470
fonctions, paramètres, codes d'erreur, et recettes pour ajouter une commande, un
paramètre, une adresse au serveur web, une mesure ou un périphérique.

## Compilation

- STM32CubeIDE : *File › Import › General › Existing Projects into Workspace*, dossier
  `ObCP_WiFi_Demo`, puis compiler (Debug ou Release ; les fichiers `.bin` et `.hex` sont
  produits aussi). Le fichier `.ioc` s'ouvre dans l'éditeur de configuration de CubeIDE 1.x
  ou dans STM32CubeMX (CubeIDE 2.x). Programmation depuis l'IDE par un outil externe (*Run › External
  Tools*) : `STM32_Programmer_CLI.exe` avec les arguments `-c port=usb1 -d ${project_name}.elf -v -s`
  et le dossier `${workspace_loc}${project_path}\Debug`, chargeur DFU actif.
- En ligne de commande : `make` (build/ObCP_WiFi_Demo.elf, .hex, .bin), `make flash`
  (USB DFU, chargeur actif), `make test` (tests sur PC).
- L'ObCP n'a pas de ST-LINK : le débogage pas à pas demande une sonde SWD externe sur le
  connecteur Debug J17 (TCK = SWCLK, TMS = SWDIO, RST = NRST, GND ; référence de tension
  sur la broche 3V3 de J6), par exemple le ST-LINK d'une Nucleo-64 (cavaliers CN2 retirés,
  connecteur CN4).

## Modifier la configuration avec STM32CubeMX

Le projet se régénère comme n'importe quel projet CubeMX : ouvrir `ObCP_WiFi_Demo.ioc`
(double-clic dans STM32CubeIDE 1.x, ou STM32CubeMX à partir de CubeIDE 2.0), modifier
(broches, nouveaux périphériques, horloges…), puis **Generate Code** (CubeIDE : enregistrer
le `.ioc` et accepter la génération ; CubeMX : *GENERATE CODE*, puis *F5* dans CubeIDE).
À la première ouverture, CubeMX peut proposer de migrer vers sa version ou un paquet
STM32Cube FW_L4 plus récent : accepter.

Le code de la démo survit à la régénération parce qu'il est :

- dans des fichiers que CubeMX ne génère pas (`Core/Src/board.c`, `cdc_port.c`,
  `fault_handlers.c`, `uart_bridge.c`, `ncp_port_obcp.c`, `Core/Src/app/`,
  `Core/Src/st67w6x/`, `Core/Src/qrcodegen/`, en-têtes dans `Core/Inc/`) ;
- relié au code généré par quelques lignes placées dans des sections **USER CODE**, que
  CubeMX conserve :

| Fichier généré | Section USER CODE | Contenu |
|----------------|-------------------|---------|
| `Core/Src/main.c` | `1` | `board_early_init();` (saut éventuel dans le chargeur DFU) |
| | `Init` | `board_lse_start();` (quartz 32 kHz, amplification « medium high ») |
| | `2` | `board_post_init();` puis `app_init();` |
| | `WHILE` | `app_loop();` |
| | `Error_Handler_Debug` | `board_error_blink();` (LED rouge : 2 éclats = quartz 8 MHz, 3 = quartz 32 kHz) |
| `Core/Src/stm32l4xx_it.c` | `NonMaskableInt_IRQn 0` | `fault_nmi();` |
| `USB_DEVICE/App/usbd_cdc_if.c` | `3`, `4`, `5`, `6`, `7`, `13` | appels de `cdc_port.c` (port COM virtuel) |
| `USB_DEVICE/Target/usbd_conf.c` | `2`, `3` | `cdc_port_on_suspend(...)` (veille / reprise de l'USB) |

À respecter pour que la démo continue de fonctionner :

- n'écrire dans les fichiers générés **qu'entre** `USER CODE BEGIN` et `USER CODE END` ;
  mettre le nouveau code dans des fichiers à soi, sous `Core/Src` et `Core/Inc` (compilés
  sans autre réglage) ;
- garder les **étiquettes** des broches (`NCP_CS`, `NCP_RDY`, `Led_Red`, `User1`…) : le code
  de la démo les utilise (`Core/Inc/board.h`) ; déplacer une broche en gardant son
  étiquette suffit ;
- laisser décochée la génération des gestionnaires *Hard fault*, *Memory management
  fault*, *Prefetch fault* et *Undefined instruction* (NVIC › Code generation) : ils sont
  dans `fault_handlers.c` ;
- ne pas désactiver SPI1, SPI3, ADC1, TIM1, TIM3, RTC ni USB_DEVICE, et ne pas activer
  l'USART2 (réservé à la passerelle `module boot`, pilotée par registres).

Après chaque régénération : `python3 Tools/verif_cubemx.py` (ou `make -C Tests check-cubemx`)
vérifie ces points (accroches présentes, étiquettes, périphériques, gestionnaires de faute)
avant la compilation. Différence avec la version 1.0 : l'initialisation générée ne prévoit
pas de repli automatique sur les oscillateurs internes ; si l'un des quartz ne démarre pas,
la carte s'arrête dans `Error_Handler()` avec le code de clignotement ci-dessus (3 éclats
au bout d'une dizaine de secondes pour le quartz 32 kHz). Dépannage d'une carte dont le
quartz 32 kHz est défectueux : dans CubeMX, Clock Configuration › RTC Clock Mux sur LSI et
Timers › RTC › Synchronous Predivider value à 249 (32 kHz / 128 / 250 = 1 Hz), puis
régénérer (démarrage retardé de 5 s, heure moins précise entre deux mises à l'heure NTP).

## Mise à jour du module

Inutile de télécharger tout X-CUBE-ST67W61 (plus de 300 Mo) : un seul fichier suffit,
dans le dossier [NCP_Binaries](https://github.com/STMicroelectronics/x-cube-st67w61/tree/main/Projects/ST67W6X_Scripts/Binaries/NCP_Binaries)
du dépôt GitHub de ST :

- [`st67w611m_mission_t01_v2.0.106.xz.ota`](https://github.com/STMicroelectronics/x-cube-st67w61/raw/main/Projects/ST67W6X_Scripts/Binaries/NCP_Binaries/st67w611m_mission_t01_v2.0.106.xz.ota) (compressé, 860 Ko, conseillé) ;
- [`st67w611m_mission_t01_v2.0.106.bin.ota`](https://github.com/STMicroelectronics/x-cube-st67w61/raw/main/Projects/ST67W6X_Scripts/Binaries/NCP_Binaries/st67w611m_mission_t01_v2.0.106.bin.ota) (non compressé, 1,5 Mo).

Le fichier part du PC en YMODEM sur le port COM virtuel de l'ObCP ; le STM32 le transmet au
module par le lien SPI (AT+OTASTART / AT+OTASEND / AT+OTAFIN) ; le module vérifie
l'empreinte SHA-256 de l'image puis redémarre dessus. La LED RVB (bleue) indique la
progression.

```
Tera Term :   module ota   puis Fichier › Transfert › YMODEM › Envoyer (File › Transfer › YMODEM › Send)
Script PC :   pip install pyserial
              python Tools/maj_module_ota.py --port COM7 st67w611m_mission_t01_v2.0.106.xz.ota
Linux/macOS : picocom --send-cmd "sz -vv --ymodem -k" /dev/ttyACM0
              module ota   puis Ctrl-A Ctrl-S et le nom du fichier (paquet lrzsz)
```

En cas d'interruption ou de fichier refusé (image T02 ou `mfg`, en-tête ou taille
incorrects, empreinte fausse), la mise à jour est abandonnée et le module garde son
firmware. `module ota` suppose un module qui exécute déjà un firmware mission T01 2.0.x.

### Première programmation par l'ObCP (`module boot`)

Sur l'ObCP, l'UART du module (D0/D1) est relié au STM32 (PA3/PA2, USART2). `module boot`
redémarre le module en mode téléchargement puis relaie le port COM virtuel vers cet UART
(DMA, débit suivant celui du PC : 500 kbit/s puis 2 Mbit/s) ; la LED RVB est magenta.

1. Récupérer le dossier `Projects/ST67W6X_Scripts` de X-CUBE-ST67W61 (liste des fichiers
   utiles dans le guide, 3.4) ; dans `Binaries/NCP_Binaries`, copier
   `mission_t01_flash_prog_cfg.ini` en `cfg_obcp.ini` et, section `[FW]`, remplacer
   `st67w611m_mission_t01_vxxx.bin` par `st67w611m_mission_t01_v2.0.106.bin`.
2. Terminal : `module boot`, puis **fermer le terminal**.
3. Dans le dossier `Binaries` :

```
QConn_Flash\QConn_Flash_Cmd.exe --port COM7 --config NCP_Binaries\cfg_obcp.ini --efuse=NCP_Binaries\efusedata.bin
```

4. QConn_Flash terminé : **rouvrir le terminal sans rien taper** ; après 2 s de silence, la
   passerelle s'arrête, affiche un bilan et le module redémarre sur le nouveau firmware.
   Arrêt immédiat de la passerelle : bouton User1 (ObCP) ou SW1 (X-NUCLEO) ; arrêt
   automatique après 2 min sans échange.

La fin de session ne se fie pas au seul signal DTR : QConn_Flash le manipule (option
`do_reset`) et peut le laisser à 0 pendant la programmation (voir `Core/Src/app/bridge_ctl.c`,
testé sur PC).

## Brochage

| Fonction | Arduino | STM32L476 | Étiquette CubeMX |
|----------|---------|-----------|------------------|
| SPI SCK / MISO / MOSI (SPI1, 10 MHz, mode 0) | D13 / D12 / D11 | PA5 / PA6 / PA7 | `NCP_SCK` / `NCP_MISO` / `NCP_MOSI` |
| SPI CS (actif haut) | D10 | PB6 | `NCP_CS` |
| SPI RDY | D3 | PB3 | `NCP_RDY` |
| CHIP_EN / BOOT | D5 / D6 | PB4 / PB10 | `NCP_CHIP_EN` / `NCP_BOOT` |
| Bouton SW1 (X-NUCLEO) | D8 | PA9 | `NCP_SW1` |
| UART du module (seulement pendant `module boot`) | D0 / D1 | PA3 / PA2 (USART2) | `NCP_UART_RX` / `NCP_UART_TX` |
| Console (USB, port COM virtuel) | – | PA11 / PA12 (USB OTG FS) | – |
| LED RVB D5 (via 2N2222) | – | PC6 / PC8 / PC9 (TIM3_CH1 / CH3 / CH4, 1 kHz) | `Led_Red` / `Led_Green` / `Led_Blue` |
| Sortie PWM, bornier J13 (IRLML2803, charge entre +5 V et PWM, 2,5 W max.) | – | PB1 (TIM1_CH3N, 1 kHz) | `PWM_Motor` |
| Accéléromètre U4 (LIS3DH ou LIS3DSH, reconnu au démarrage) | – | SPI3 PC10 / PC11 / PC12, CS PC5 | `ACC_SCK` / `ACC_MISO` / `ACC_MOSI`, `ACC_CS` |
| Bouton User1 (SW1 de l'ObCP) | – | PC13 | `User1` |
| Module BLE (non utilisé, maintenu en reset) | – | PC2 (reset) | `BLE_Reset` |

Remarques : le brochage Arduino de l'ObCP est celui de la NUCLEO-L476RG (pilote du module
inchangé) ; PB3 = SWO (pas de trace SWV, SWD possible par J17) ; le module fonctionne en
2,4 GHz uniquement. La nomenclature de fabrication de l'ObCP cite un LIS3DSH, le programme
modèle de l'ObCP pilote un LIS3DH : les deux sont reconnus (WHO_AM_I 0x33 / 0x3F).

## Tests sur PC

```
make -C Tests      (ou make test)
```

Cinq programmes compilés avec gcc, AddressSanitizer et UBSan :
`test_driver` (pilote face à un module simulé, dont la mise à jour par AT+OTASEND,
193 vérifications), `test_app` (modules indépendants du matériel : réception YMODEM avec
erreurs injectées, couleurs de la LED, accéléromètres LIS3DH et LIS3DSH simulés, fin de
session de la passerelle `module boot`, relecture d'une configuration enregistrée par la
version 1.1, 325 vérifications), `test_usb` (port COM virtuel :
`cdc_port.c` et les rappels générés de `usbd_cdc_if.c` face à une classe CDC simulée,
115 vérifications) et `test_sim` (application complète,
quatre démos, LED, PWM, capteurs, `dfu`, `module ota`, `module boot`, 284 vérifications ;
journal de session dans `Tests/build/sim_transcript.txt`) et `test_boot` (mises sous
tension avec une configuration déjà enregistrée en flash, comme sur batterie : box, serveur
web et MQTT ; serveur web sans box ; box qui refuse la connexion ; 87 vérifications). Les réponses JSON et le
JavaScript de la page web sont aussi validés, puis `Tools/verif_cubemx.py` contrôle les
accroches du code de la démo dans les fichiers générés.

Si Python 3 et pyserial sont présents, un essai de bout en bout fait tourner
l'application en temps réel sur un pseudo-terminal et lui envoie des images `.ota` avec
`Tools/maj_module_ota.py`, puis avec `sz --ymodem` (lrzsz) s'il est installé.

## État de validation

- Version 1.0 (initialisation écrite à la main) : essayée sur ObCP, fonctionne.
- Version 1.1 (initialisation générée par CubeMX 6.11.1, mêmes réglages : 80 MHz, USB
  48 MHz, SPI1 mode 0 à 10 Mbit/s, SPI3 mode 3 à 5 Mbit/s, PWM 1 kHz, RTC sur le quartz
  32 kHz, priorités d'interruption) : essayée sur une ObCP réelle, avant et après une
  régénération effective par STM32CubeMX suivie d'une recompilation : fonctionne.
- Version 1.2 : démarrage automatique, commande `auto`, carte de la page web, commentaires ;
  l'initialisation du matériel et les fichiers générés sont ceux de la 1.1.
  - Compilation sans erreur ni avertissement avec le Makefile (GCC Arm 15, `-Wall -Wextra`)
    et avec les réglages Debug (-Og, et -O0) et Release de STM32CubeIDE, y compris avec
    les seuls chemins déclarés par CubeMX. Release : environ 137 Ko de flash et 49 Ko de RAM.
  - Tests sur PC réussis (ci-dessus) ; page web affichée et manipulée dans Chromium ;
    `Tools/verif_cubemx.py` : 20 accroches sur 20.
  - **Non essayée sur une ObCP réelle.** Premier essai conseillé : programmer par DFU,
    vérifier la bannière (version 1.2.0, ligne « Démarrage »), `connect`, puis
    `auto web on`, `auto mqtt on`, `save` ; alimenter ensuite la carte par sa batterie,
    sans câble USB : le point d'accès doit apparaître, la page web s'ouvrir et la
    télémétrie arriver sur le broker.

## Versions

| Version | Contenu |
|---------|---------|
| 1.0.0 | Version ObCP, issue du démonstrateur NUCLEO-L476RG 1.1.0 : console USB (CDC), programmation par DFU (`dfu`), LED RVB, sortie PWM, accéléromètre, passerelle `module boot` pour la première programmation du module |
| 1.1.0 | Projet STM32CubeMX (`ObCP_WiFi_Demo.ioc`) régénérable sans perte : initialisation générée, code de la démo regroupé sous `Core/` et relié par des sections USER CODE, `Tools/verif_cubemx.py` ; diagnostic des quartz par la LED rouge ; mesures de l'ADC moyennées par logiciel |
| 1.2.0 | Démarrage automatique pour une utilisation sur batterie : paramètre `web.auto` (point d'accès et serveur web), commande `auto` (avec `wifi.auto` et `mqtt.auto`), carte « Démarrage automatique » de la page web et `GET /api/auto`, ligne « Démarrage » de la bannière et de `info` ; configuration enregistrée par la 1.1 relue sans perte ; commentaires pour débutants dans tout le code de la démo ; documentation du programme (`Doc/Documentation_Programme_ObCP.docx`) ; tests de mise sous tension (`test_boot`) |

La version du programme chargé s'affiche dans la bannière de démarrage et avec `info`.

## Licences

| Composant | Licence |
|-----------|---------|
| Code écrit pour ce paquet (`Core/Src/app/`, `board.c`, `cdc_port.c`, `fault_handlers.c`, `uart_bridge.c`, `ncp_port_obcp.c`, `Tests/`, `Tools/`, sections USER CODE) | aucune licence imposée : à choisir en cas de diffusion |
| Pilote `Core/Src/st67w6x` (protocole repris du pilote ST67W6X_Network_Driver) | notice BSD-3-Clause de ST conservée, voir `Core/Src/st67w6x/LICENSE.md` |
| Fichiers générés par STM32CubeMX dans `Core/` (main.c, gpio.c… démarrage, `syscalls.c`, script d'édition de liens), HAL STM32L4 | BSD-3-Clause (STMicroelectronics), voir `Core/LICENSE_ST.md` |
| STM32 USB Device Library (`Middlewares/ST/STM32_USB_Device_Library`) et fichiers générés de `USB_DEVICE/` | SLA0044 « Ultimate Liberty » (STMicroelectronics), voir `LICENSE.txt` du dossier de la bibliothèque |
| CMSIS | Apache 2.0 |
| QR-Code-generator (Project Nayuki) | MIT |

Les binaires du module ST67W611M1 et les outils de programmation (QConn_Flash) ne sont pas
inclus : ils sont fournis par STMicroelectronics dans X-CUBE-ST67W61
(<https://github.com/STMicroelectronics/x-cube-st67w61>, licence dans
`Projects/ST67W6X_Scripts/Binaries/LICENSE_BINARIES`). Documents de fabrication et
programme modèle de l'ObCP : <https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE>.
