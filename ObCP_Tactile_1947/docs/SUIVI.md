# Suivi du plan ObCP tactile

**Livraison** · `ObCP_Tactile_v0.4.1`

**Base** · `jimbaud/LAB_ObCP_L476_STM32CubeIDE`, `cef4cdae9e7564ef920e2c2c74603c82b863f89c`, dossier `OBCP_template_2026`.

**Versions vérifiées** · STM32CubeMX 6.11.1 pour les sources, HAL/CMSIS du gabarit STM32CubeL4 V1.18.1 et GNU Tools for STM32 13.3.1 pour les images Debug et Release.

## Bilan par lot

| Lot | Livrable principal | Vérification réalisée | À relever sur le poste ou au banc |
| --- | --- | --- | --- |
| L00 | Brochage, ressources, provenance et budget mémoire | Sources ObCP, CAO des shields et configuration CubeMX confrontées | Révision et continuité du shield reçu |
| L01 | `.ioc`, projet CubeIDE, linker et contrôle | Régénération des sources et compilation GNU STM32 Debug et Release en v0.4.0 | Compilation et lancement dans CubeIDE sous Windows |
| L02 | ILI9341, FT6206, diagnostic et cibles | Paramètres générés, transformation et cycle SW1 testés sur PC | Mires, I²C et cinq touches |
| L03 | Rendu SPI1 DMA, menu et captures | Navigation et équivalence pixel par pixel sur PC | Débit et cadence mesurés |
| L04 | Inclinaison partagée et labyrinthe | Filtre, zone morte, murs et arrivée testés sur PC | Sens des axes et partie jouée |
| L05 | Flash A/B, CRC, ECC et linker | HEX en banque 1, coupures simulées et contrôle d’alignement | Coupure contrôlée sur carte d’essai |
| L06 | Paint indexé, deux emplacements | Interpolation, bords, sauvegarde et ouverture testés sur PC | Dessins persistants et durée d’écriture |
| L07 | PWM, synthèse et schéma de l’interface | Chaîne timer et DMA compilée, fréquence numérique vérifiée | Porteuse, reconstruction, ampli et charge |
| L08 | Groovebox et format des quatre motifs | Tempo, timbres et rappel de motif testés sur PC | Écoute et relecture sur carte |
| L09 | Casse-briques | Sous-pas, impact, score et états testés sur PC | Partie jouée et cadence |
| L10 | Course auto | Butées, collision et rendu partiel testés sur PC | Trafic, direction et cadence |
| L11 | Documentation, images et binaires | Régénération sur copie propre, HEX identiques et recette PC | Recette matérielle complète |
| L12 | 1651 TSC2007 et STMPE610, détection et calibrage | Bus simulés, calibration affine, coupures de flash et lecture d’une image v0.1.1 en v0.2.0 | Une configuration résistive a fonctionné en v0.2.0 selon le retour d’essai. |
| L13 | Navigation tactile, niveaux, axes et LED | Transitions des trois jeux, huit orientations, secours tactile et migration v0.2.0 testés sur PC | Sens des axes sur la carte, difficulté ressentie, durée et polarité de la LED |
| L14 | Dix niveaux, réactivité, dessin RVB et accueil SUPMICROTECH | Parcours des 30 niveaux, migration flash 0.3.0, coupures de migration dessin et captures du moteur | Difficulté et réactivité en main sur ObCP |
| L15 | Documentation complète du code et parcours débutant | 243 fonctions, 115 déclarations d’interface, champs et constantes documentés ; Doxygen sans avertissement ; tests C11 strict ; HEX équivalents et régénération CubeMX vérifiée | Lecture et appropriation du code par les étudiants |

## Décisions de réalisation

Le firmware est autonome. L’écran est en paysage et le tactile exploite un contact. Le module BLE est maintenu au reset. Le lecteur microSD est désélectionné. La note de diagnostic et la groovebox partagent le même moteur de synthèse. Le contrôleur tactile est interrogé sans IRQ. Les deux zones qui utilisent le DMA disposent de canaux et de priorités distincts.

Les retours d’essai des v0.2.0 et v0.3.0 confirment l’affichage, le tactile résistif et le lancement des applications. Ils ont conduit à retourner les signes des deux commandes par défaut, à ajouter un assistant d’orientation persistant et à proposer plusieurs niveaux par jeu. Le menu de chaque jeu propose maintenant une sortie tactile explicite après une défaite. Les commandes du diagnostic et le recalibrage passent par l’écran. SW1 peut servir de raccourci lorsqu’il est accessible. La LED donne une impulsion au démarrage et à la fin des opérations de stockage.

La v0.4.0 porte chaque jeu à dix niveaux. Le défaut de réactivité est 6/10 sur chaque axe. La palette de seize couleurs est propre à chaque dessin. L’accueil fixe dure environ 3,2 s. Les boutons du menu ont des dimensions et une police uniformes.

La v0.4.1 développe les contrats Doxygen, les explications d’algorithmes et les commentaires des crochets CubeMX, du linker, des tests et outils. Le README présente les flux de données et le répertoire des modules. Trois guides progressifs accompagnent les captures, les planches annotées et l’API HTML hors connexion. Les termes C11 et Doxygen ont des portées distinctes, précisées dans les conventions.

Le placement des sauvegardes laisse la dernière page de la banque 2 à part. La coexistence avec un firmware OTA doit être examinée avec le plan mémoire du chargeur utilisé.

## Reprise

Programmer l’HEX Release en conservant la banque 2, puis vérifier les mires et les cinq cibles. Enregistrer l’orientation dans **Axes jeux** et vérifier sa persistance au redémarrage. La [fiche de banc](RECETTE.md#fiche-de-banc) couvre ensuite les trois jeux, la LED et le stockage.
