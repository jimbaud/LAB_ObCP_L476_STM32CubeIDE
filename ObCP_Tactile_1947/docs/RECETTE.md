# Recette de la version 0.4.1

**Cible** · ObCP STM32L476RG, shields Adafruit 1947 et 1651

**Date** · 7 novembre 2026

**Méthode** · C compilé et testé sur PC, compilation ARM, régénération des sources ST et relevés de banc à compléter sur l’exemplaire réel.

## Résultats exécutés

| Vérification | Résultat |
| --- | --- |
| Documentation Doxygen 1.18.0 | Génération HTML et XML réussie avec zéro avertissement. Les avertissements sont configurés pour provoquer un échec. |
| Couverture documentaire applicative | 243 fonctions définies, 463 paramètres, 251 champs de structures et 198 constantes d’énumération contrôlés dans le XML. Les 115 déclarations d’interface sont documentées. Le comptage déduplique déclaration et définition. |
| Référence de langage | Les modèles et le banc PC compilent avec C11 strict, pedantic-errors, Wall, Wextra, Werror et Wshadow, puis sont exécutés sous ASan/UBSan. Le firmware utilise GNU C11 et la HAL/CMSIS. |
| Équivalence de la livraison documentaire | Debug et Release comparés à la v0.4.0 recompilée avec le seul libellé de version remplacé par 0.4.1. Les HEX de chaque profil sont identiques octet pour octet. |
| Illustrations pédagogiques | Dix-neuf captures du moteur graphique régénérées et trois planches numérotées ajoutées. Menu, Paint et Diagnostic relient les actions visibles aux fonctions. |
| Test PC GCC avec AddressSanitizer et UndefinedBehaviorSanitizer | Réussi. Entrées, rendu, navigation, jeux, séquenceur, stockage et rappels. |
| Détection sur bus simulés | FT62xx, TSC2007 et STMPE610 en modes SPI 0 et 1. Les tests couvrent aussi l’absence de shield, une réponse I²C de format incompatible, une erreur de lecture et l’attente pendant un DMA TFT. |
| Calibrage résistif | Échange et inversion des coordonnées, rejet d’un alignement et d’une quatrième cible incorrecte. Deux profils conservés avec neuf coupures injectées. Seuil de l’appui de secours, entrée tactile dans le calibrage et annulation testés sur PC. |
| Diagnostic | Cycle des quatre couleurs RGB565, retour au menu et accès au réglage des axes testés par événements tactiles. Les raccourcis de SW1 ont aussi leurs tests d’appui court, long, de rebond et de débordement du tick. |
| Axes des jeux | Huit configurations vérifiées, avec diagonale et répétition d’axe refusées. Relecture en flash simulée et neutralisation sans modifier la permutation. |
| Niveaux | Dix parcours de labyrinthe vérifiés par recherche en largeur, avec longueurs croissantes et temps décroissants. Commande rapide de raquette, briques à deux impacts, familles de véhicules et durée de collision vérifiées. Pour les trois jeux, les transitions testent victoire, progression, défaite, rejeu, pause et retour au menu même avec une mesure d’inclinaison indisponible. |
| Stockage PC | Dessins avec palette et diamètre relus. Huit coupures pendant le passage du format 32 000 octets au format 32 036 octets. Quinze interruptions injectées sur l’effacement, les données ou le marqueur d’un objet de test. La dernière copie valide est conservée. Erreur de lecture sans effacement et bascule vers la copie précédente en cas de CRC invalide. |
| Compatibilité flash 0.3.0, essai historique v0.4.0 | Deux dessins, quatre motifs, deux profils tactiles et les axes produits avec les sources v0.3.0 ont été relus avec celles de la v0.4.0. Après écriture de la réactivité, les 140 Kio historiques étaient identiques octet pour octet. Un dessin ancien a aussi été réenregistré et relu avec sa palette RVB. L’équivalence des HEX documentée ci-dessus relie ce résultat à la présente version. |
| Accueil et paramètres | Accueil de 3,2 s et destination de calibrage vérifiés. Commandes X et Y distinctes, effet immédiat, persistance et rejet de préférences hors plage vérifiés. |
| Audio numérique | 440 passages montants en une seconde pour la note d’essai. Huit pas après une seconde à 120 BPM. |
| Rendu PC | Une image complète divisée en 15 bandes. Équivalence pixel par pixel pour l’accueil, les paramètres, le RVB et toute l’animation BOUM. Course simulée sur 25 lots, moyenne de 2 932 pixels par lot. |
| CubeMX | Génération des sources avec STM32CubeMX 6.11.1 sur la v0.4.1. Les fichiers générés, leurs commentaires USER CODE et les modules utilisateur sont identiques après normalisation des fins de ligne. Le .ioc, le projet et le linker sont conservés sur la copie. |
| Recompilation GNU Tools for STM32 13.3 | Debug et Release réussis sur la copie de livraison et la copie régénérée. Les HEX de chaque profil sont identiques octet pour octet. Le linker signale un segment ELF `LOAD` en `RWX`. |
| Métadonnées CubeIDE | Les fichiers `.project` et `.cproject` et l’arborescence des sources reprennent la v0.2.0, dont les makefiles CubeIDE avaient été générés et compilés sous Linux. La v0.4.1 a été reconstruite avec le Makefile fourni. |
| Import, compilation et lancement CubeIDE sur Windows | À relever sur le poste utilisateur avec STM32CubeIDE 2.0. |
| Comportement sur ObCP | Le retour utilisateur indique que le programme livré fonctionne. Les essais de cette livraison documentaire sont exécutés sur PC et par compilation ARM. Le passage de la v0.4.1 dans CubeIDE sous Windows et son essai matériel restent à relever sur le poste et la carte. |

| Configuration livrée, GNU ST 13.3.1 | Flash de l’image HEX | RAM liée, réserves incluses | Adresses programmées |
| --- | --- | --- | --- |
| Release | 76 960 octets | 51 592 octets | `0x08000000` à `0x08012CA7` |
| Debug | 109 000 octets | 51 600 octets | `0x08000000` à `0x0801A9CF` |

La RAM liée est la somme de `.data` et `.bss` indiquée par `arm-none-eabi-size`. Elle inclut la réservation de pile de 4 Kio et de tas de 1 Kio dans le script de liaison. Le maximum de pile en fonctionnement demande un essai sur la cible.

[Journaux de compilation Release](../validation/Compilation_Release.txt) · [Debug](../validation/Compilation_Debug.txt) · [Construction CubeIDE historique v0.2.0](../validation/Construction_CubeIDE_v0.2.0.txt) · [Tests PC](../validation/Tests_PC.txt) · [Régénération](../validation/Regeneration.txt)

[Couverture Doxygen](../validation/Documentation.txt) · [Équivalence des images](../validation/Equivalence_v0.4.1.txt)

Le contrôle documentaire mesure la présence des contrats et de leurs champs. La relecture des explications reste distincte de ce comptage. Le code d’origine ST et Arm conserve ses licences et sa documentation ; le périmètre contrôlé ici est celui des modules ObCP ajoutés et de leurs interfaces.

## Reproduire la régénération des sources

Le chemin testé a utilisé la commande documentée **generate code** sur une copie contenant `Core/Inc` et `Core/Src`. La commande **project generate** concerne la génération du projet complet et peut demander l’authentification ST avec cette version. Le flux de vérification des sources garde les métadonnées CubeIDE livrées et leurs deux chemins de liaison. Avec CubeMX 6.11.1, la commande attend un fichier texte de cette forme, avec les chemins adaptés.

```text
config load C:/Projets/ObCP_Tactile_1947/ObCP_Tactile_1947.ioc
generate code C:/Projets/ObCP_Tactile_1947/Core
exit
```

Depuis le dossier d’installation de CubeMX sous Windows, exécuter le script avec `jre\bin\java.exe -jar STM32CubeMX.exe -q C:\Projets\regeneration.txt`. Après génération, lancer `python Tools\verif_cubemx.py` puis reconstruire les deux configurations. Si CubeMX sélectionne un nouveau linker, utiliser le contrôle fourni pour rétablir `Linker/ObCP_Flash.ld`. [Flux graphique dans le README](../README.md) · [Guide STM32CubeMX UM1718](https://www.st.com/resource/en/user_manual/um1718-stm32cubemx-for-stm32-configuration-and-initialization-c-code-generation-stmicroelectronics.pdf)

## Fiche de banc

Renseigner d’abord la révision de l’ObCP, le produit et la révision du shield, le SHA256 du fichier HEX et la version des outils. Répéter la séquence pour chaque shield, carte hors tension lors des échanges. Les cibles ci-dessous sont des critères d’essai.

| Essai | Action et critère de fin | Mesure ou résultat |
| --- | --- | --- |
| R3 et alimentation | Shield hors tension, vérifier la continuité de D11–D13, les ponts A4/A5 et l’absence de court-circuit. Sous tension, relever 5 V et IOREF 3,3 V. | À relever |
| Diagnostic tactile | Ouvrir **Paramètres → Diagnostic** au doigt. Toucher cinq fois **Aplats** pour défiler du rouge aux cibles, puis revenir avec **< Ret.**. Fréquence SCK proche de 8 MHz. | À relever |
| 1947 FT62xx | Adresse I²C `0x38` et identifiant affiché. Toucher les cinq cibles. Relever les coordonnées brutes et écran. Vérifier `obcp_live.touch_hits` à `0x1F`. | À relever |
| 1651 TSC2007 | Adresse I²C `0x48` et reconnaissance par conversion. Relever Z1 et Z2 au repos et avec un appui stable. Faire le calibrage et valider les cinq cibles. | À relever |
| 1651 STMPE610 | Identifiant `0x0811`. Vérifier l’alternance de SCK entre 1 MHz pour D8 et 8 MHz pour D10. Vérifier l’absence de chevauchement des deux sélections. Calibrer et valider les cinq cibles. | À relever |
| Persistance du calibrage | Avec un 1651, ouvrir **Paramètres → Calibrer**, essayer **Annuler**, calibrer puis redémarrer. Vérifier les cinq cibles. Refaire un calibrage par maintien du doigt dès le reset. | À relever |
| Mise à jour 0.3.0 ou 0.4.0 | Préparer un dessin, un motif et le calibrage avec la version précédente. Programmer la v0.4.1 avec un effacement limité aux pages programme. Rouvrir les objets et vérifier les cibles. | À relever |
| Accéléromètre | Poser l’ObCP à plat, vérifier les directions par défaut, puis suivre **Paramètres → Axes jeux**. Vérifier que le point suit les bords abaissés, enregistrer et redémarrer. Faire **Neutre** et vérifier que le sens est conservé. | À relever |
| Labyrinthe | Atteindre la cible, vérifier le niveau suivant, mettre en pause et vérifier le temps figé. Laisser expirer le chronomètre puis toucher **Menu principal**. | À relever |
| Casse-briques | Observer le rebond et le retrait des briques. Après une balle perdue, rejouer le même niveau puis utiliser **Menu principal**. Après une victoire, vérifier la nouvelle raquette et le nombre de briques. | À relever |
| Course auto | Vérifier le premier objectif à 50, l’éclat de collision et les différents véhicules, puis **Rejouer** et **Menu principal** après collision. Mesurer la cadence sur une scène avec trafic. | À relever |
| Réactivité | Comparer X à 1, 6 et 10 au casse-briques. Tester Y au labyrinthe. Enregistrer puis redémarrer pour vérifier la reprise. | À relever |
| Accueil et menu | Vérifier le logo, la durée de 3,2 s, les six boutons uniformes, la signature et les retours des sous-pages de paramètres. | À relever |
| LED | Mesurer le bref vert à l’initialisation et après une sauvegarde réussie. Ouvrir un emplacement vide pour observer le rouge. Vérifier l’extinction entre les événements. | À relever |
| Paint | Tester les six diamètres et la gomme. Créer deux dessins avec des palettes RVB distinctes, redémarrer et rouvrir les deux. Relever `obcp_live.last_flash_ms`. | À relever |
| Coupure flash | Sur une carte d’essai, enregistrer un dessin A, modifier puis sauvegarder B. Couper l’alimentation pendant une écriture volontaire. Après redémarrage, vérifier A si B est incomplète. Refaire une sauvegarde normale. | À relever |
| PWM et HP | Mesurer 62,5 kHz à TP1, puis 440 Hz à ±1 % à TP2. Relever le niveau à TP3 et écouter après amplification. | À relever |
| Audio sous charge | Sur Diagnostic, maintenir la note une minute avec rafraîchissements et gestes tactiles. Relever la variation de `obcp_live.audio_missed`. Cible zéro échéance manquée. | À relever |
| Groovebox | Enregistrer un motif, couper la carte, rouvrir et comparer tempo, timbres et pas. Tester le clavier tactile. | À relever |
| Cadence TFT | Pendant une partie de course, relever `frames_per_second` et `pixels_per_second`. Cible 25 lots/s dans une scène animée. | À relever |

Les variables `obcp_live` sont accessibles en **Live Expressions** avec ST-LINK. Le compteur de lots tient compte uniquement des bandes achevées et ne correspond à une cadence d’animation que sur une scène qui change régulièrement. Les photos, relevés SPI et signaux audio de la carte complètent les captures PC.
