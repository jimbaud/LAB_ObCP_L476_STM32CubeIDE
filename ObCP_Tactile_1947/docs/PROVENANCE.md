# Provenance et licences

| Élément | Origine et adaptation |
| --- | --- |
| Projet de départ et pilote accéléromètre | Dépôt ObCP, `OBCP_template_2026` au commit `cef4cdae9e7564ef920e2c2c74603c82b863f89c`. Le pilote garde la détection, la lecture XYZ et les conversions. L’ODR a été réglée à 100 Hz et le délai SPI raccourci. |
| HAL, CMSIS, startup, syscalls et script de liaison | Gabarit ObCP référencé sur STM32CubeL4 V1.18.1. Les notices de ST et Arm restent dans `Drivers/` et dans les en-têtes. Le script `Linker/ObCP_Flash.ld` adapte le placement à deux banques distinctes. |
| GPIO, DMA, SPI, I²C, TIM1, interruptions et main | Sources produites par STM32CubeMX 6.11.1 à partir de `ObCP_Tactile_1947.ioc`. Les crochets ObCP sont isolés dans les blocs `USER CODE`. |
| Dalle ILI9341 | Valeurs d’initialisation adaptées du pilote Adafruit ILI9341. Les commandes HAL et le transport DMA de ce projet sont séparés du code Arduino. Les mentions de l’éditeur figurent dans `docs/licenses/Adafruit_ILI9341.txt`. |
| Tactiles résistifs | Pilotes HAL du projet fondés sur les commandes et registres des fiches TI et ST. Les exemples Adafruit servent de référence pour les séquences SPI du shield et les mesures X, Y, Z1 et Z2. |
| Rendu, jeux, stockage A/B, synthèse, illustrations | Implémentation de ce projet. Le service flash reprend la géométrie et le principe de traitement ECC du démonstrateur Wi-Fi ObCP. Les captures sont des sorties du banc PC. |

L’usage du dépôt d’origine et de ses fichiers est rattaché au projet ObCP. Les notices des composants réutilisés accompagnent leurs fichiers. Pour une redistribution du code original, utiliser les conditions retenues par le responsable du dépôt.

- [Base ObCP](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/tree/cef4cdae9e7564ef920e2c2c74603c82b863f89c/OBCP_template_2026)
- [Sauvegarde du démonstrateur Wi-Fi](https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE/blob/cef4cdae9e7564ef920e2c2c74603c82b863f89c/ObCP_WiFi_Demo/Core/Src/app/config.c)
- [Adafruit ILI9341, référence de la séquence d’initialisation](https://github.com/adafruit/Adafruit_ILI9341/blob/1871d9597f0d67cd15e3a331b8952c5dd3d3c418/Adafruit_ILI9341.cpp)
- [Adafruit FT6206, registres et exemples tactiles](https://github.com/adafruit/Adafruit_FT6206_Library)
- [TI TSC2007, commandes et format de conversion](https://www.ti.com/lit/ds/symlink/tsc2007.pdf)
- [ST STMPE610, registres, FIFO et interface SPI](https://www.st.com/resource/en/datasheet/stmpe610.pdf)
- [Adafruit STMPE610, référence pour le shield ancien](https://github.com/adafruit/Adafruit_STMPE610)
- [Table de DMA STM32L4](https://www.st.com/resource/en/product_training/stm32l4_system_dma.pdf)

## Logo et conversion graphique

Le logo SUPMICROTECH a été fourni par J. Imbaud pour l’accueil de ce projet. Le fichier CMJN d’origine est `docs/images/logo_source.jpg`. Son profil ICC est utilisé par `Tools/import_logo.py` pour la conversion vers sRGB. La ressource `logo.c` est ensuite quantifiée en RGB565 et compressée par lignes. Le logo reste un élément de l’identité de SUPMICROTECH.

Le modèle `Tools/templates/logo.c.in` conserve les commentaires lors d’une nouvelle conversion. Les captures 320 × 240 sont produites par `Tests/test_obcp.c`. Les trois planches annotées sont composées par `Tools/generer_captures.py`, sans modifier les images embarquées.

## Langage et génération documentaire

La documentation v0.4.1 s’appuie sur le [texte public C11 N1570 du WG14](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf), notamment ses règles de commentaires et types entiers, et sur le [manuel officiel Doxygen](https://www.doxygen.nl/manual/docblocks.html). La référence C11 définit le langage ; les balises Doxygen organisent la documentation.

La génération a été exécutée avec [Doxygen 1.18.0](https://www.doxygen.nl/download.html) et Graphviz. Les pages HTML et les graphes sont issus des commentaires de ce projet. Le répertoire `docs/api` contient cette sortie pour une consultation locale. Le Doxyfile et le contrôle Python permettent de la reconstruire.
