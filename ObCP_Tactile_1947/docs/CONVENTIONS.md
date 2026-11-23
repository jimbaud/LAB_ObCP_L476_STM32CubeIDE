# Langage C, contrats et commentaires

## Portée des conventions

Le projet utilise les éléments du langage ISO C11 employés par son code applicatif, les services HAL/CMSIS du STM32 et la chaîne GNU Arm. Les modules autonomes sont aussi compilés sur PC avec `-std=c11 -pedantic-errors -Wall -Wextra -Werror -Wshadow`. Le firmware embarqué utilise `-std=gnu11`, comme sa configuration de compilation fournie.

« ANSI C » est souvent employé pour parler du C standard. C90, C99 et C11 sont des éditions différentes. Le projet emploie notamment `stdint.h`, `stdbool.h`, les initialiseurs désignés, les littéraux composés et `_Static_assert`. Sa référence de langage est donc explicitement C11.

Doxygen décrit la documentation extraite du code. Ses balises ne constituent pas une norme de sûreté ou une certification de conformité ISO. Cette livraison applique une convention de documentation et des vérifications de compilation. Elle ne revendique pas de certification MISRA, ISO 26262 ou d’analyse exhaustive de tous les comportements du firmware.

## Trois niveaux de commentaire

| Emplacement | Ce qu’il explique | Exemple du projet |
| --- | --- | --- |
| Début de fichier | Responsabilité, dépendances et unités | `@file`, `@brief`, `@ingroup` |
| Avant une interface | Contrat d’appel, paramètres, résultats et contexte | `obcp_storage.h`, `obcp_gfx.h` |
| Dans un traitement | Choix d’algorithme, masque, propriété du tampon et cas d’erreur | `paint_segment`, `renderer_task`, `storage_save` |

Une fonction publique est documentée dans son en-tête. Son implémentation contient un rappel lisible de l’algorithme, puis les explications locales utiles. Une fonction privée est documentée dans son fichier C. La séparation évite d’entretenir deux contrats Doxygen différents pour une même interface.

Les champs de structures et les constantes d’énumérations portent une description `/**< ... */`. Les constantes privées et les tableaux générés précisent leur signification. Les valeurs numériques du logo sont décrites par leur format, plutôt que commentées octet par octet.

## Lire et écrire un contrat

Exemple représentatif, à lire comme une convention de présentation.

```c
/**
 * @brief Lire un objet dont la longueur exacte est connue.
 * @param[in,out] s Port de stockage et callbacks synchrones valides.
 * @param[in] object Identifiant inférieur à SAVE_OBJECTS.
 * @param[out] data Tampon de destination d’au moins size octets.
 * @param[in] size Nombre d’octets demandés.
 * @return STORE_OK au succès, ou un résultat décrivant l’échec.
 */
StoreResult storage_read(Storage *s, SaveObject object,
                         void *data, size_t size);
```

`in` indique une donnée lue, `out` un résultat écrit et `in,out` un état consulté puis modifié. La description donne aussi l’unité, la capacité ou la plage. La direction n’établit pas à elle seule la validité d’un pointeur.

`@pre` documente une condition que l’appelant doit garantir. `@note` signale notamment le contexte interruption ou boucle principale. `@return` décrit les codes et leurs conséquences. Une fonction `void` ne reçoit pas de faux retour documenté.

Un contrat ne promet pas un contrôle automatique absent du code. Par exemple, plusieurs primitives demandent un pointeur valide sans tester `NULL`. L’appelant doit respecter cette condition.

## Repères C pour débuter

| Écriture | Sens | Point d’attention |
| --- | --- | --- |
| `uint8_t`, `uint16_t`, `uint32_t` | Entiers non signés de largeur fixée sur cette cible | Choisir selon le format ou le registre |
| `int16_t` | Entier signé de 16 bits | Convient aux coordonnées et registres signés |
| `size_t` | Type des tailles d’objets et tableaux | Les offsets persistants utilisent leur format explicite |
| `float` | Calcul flottant simple précision | Unités g, pixels ou secondes à préciser |
| `bool` | Valeur logique `true` ou `false` | Ne remplace pas un code d’erreur détaillé |
| `&objet` | Adresse d’un objet | L’objet doit rester vivant pendant son utilisation |
| `*p` | Objet désigné par un pointeur | Vérifier validité et alignement |
| `p->champ` | Champ de la structure désignée | Équivalent à `(*p).champ` |
| `const T *p` | Accès en lecture à un T par ce pointeur | Ne signifie pas que tout autre accès est immuable |
| `static` au niveau fichier | Liaison interne et durée de vie statique | Masque le nom aux autres fichiers |
| `volatile` | Accès observables au compilateur | Ne fournit ni verrou ni exclusion mutuelle |
| `1U << bit` | Masque d’un bit | L’indice doit rester dans la largeur du type |
| `flags & MASK` | Test ou extraction de bits | Différent du ET logique `&&` |
| `flags &= ~MASK` | Effacement de bits sélectionnés | Choisir la largeur du masque avec soin |
| `(Rect){0}` | Littéral composé initialisé à zéro | Ici, un rectangle vide |
| `sizeof(table) / sizeof(table[0])` | Nombre de cases d’un tableau réel | Ne fonctionne pas sur un simple pointeur reçu |

L’addition non signée de phase DDS reboucle volontairement. Un débordement signé ne bénéficie pas de la même règle du langage. Les masques utilisent des constantes non signées et `UINT32_C` quand la largeur compte.

`memcpy` copie des octets. `memset` remplit chaque octet avec la même valeur. Remplir un tableau de `uint16_t` avec `memset(..., 512, ...)` ne créerait pas des échantillons à 512. Le transport audio utilise une boucle pour initialiser ces valeurs.

## Unités et limites retenues

Les coordonnées écran sont en pixels, X vers la droite et Y vers le bas. Les rectangles utilisent des bords droit et bas exclus. Les durées de la boucle sont en millisecondes ; les intégrations physiques convertissent en secondes. Les commandes de jeu sont bornées entre −1 et +1. Les accélérations sont en g.

Les couleurs du rendu sont des mots RGB565, mais le buffer SPI contient leurs octets fort puis faible. Les sauvegardes emploient un encodage little-endian explicite. Ces deux ordres servent des protocoles différents.

Le code applicatif utilise des objets statiques et des tableaux de taille connue. Les buffers sont bornés, les entrées sont filtrées et les codes d’erreur sont propagés vers le diagnostic. Les appels HAL sont concentrés dans les ports matériels. Les actions flash sont explicites et différées.

## Contexte d’exécution et propriété

| Donnée ou action | Écriture principale | Lecture ou consommation | Contrat |
| --- | --- | --- | --- |
| `App` et ses modèles | Boucle principale | Rendu dans la même boucle | Un propriétaire logique |
| Buffer de bande | CPU quand libre | DMA SPI1 en transfert | Réutiliser après fin confirmée |
| Anneau audio | CPU sur moitié libre | DMA timer sur l’autre moitié | Époque et compteur contrôlés avant copie |
| `epoch` audio | IRQ de moitié/fin DMA | Boucle principale | Compteur volatile court, suivi des échéances |
| État TFT | Callbacks HAL et pilote | `poll` dans la boucle | Pas de composition graphique en IRQ |
| Drapeau ECC | NMI flash | Callback de lecture flash | Échec renvoyé après lecture |
| Flash persistante | Travail explicite dans la boucle | Chargement ou demande utilisateur | Alignement, CRC, validation A/B |

Les fonctions d’interruption sont courtes. Le calcul des voix, les jeux, les opérations de menu et les écritures flash restent dans la boucle principale.

## Sources générées et bibliothèques

Les commentaires ajoutés dans les fichiers CubeMX se trouvent dans `USER CODE`. Leurs paramètres matériels se modifient dans le `.ioc`. Les fichiers personnalisés sont dans `Core/Src/obcp` et `Core/Inc/obcp_*.h`, plus l’interface `accelerometer.h`. Le linker est dans `Linker`.

Les fichiers ST conservent leurs licences et commentaires d’origine. Les tests, les outils Python, le Makefile et le linker ont leurs explications propres. Le générateur de logo lit un modèle C commenté pour préserver cette documentation à chaque conversion.

## Vérifier la documentation

Installer Doxygen et Graphviz, puis lancer à la racine du projet.

```bash
doxygen Doxyfile
python3 Tools/verif_documentation.py
```

La configuration extrait les fonctions statiques et publiques des modules applicatifs. Elle signale les membres non documentés, les incohérences de paramètres et les retours absents. Les avertissements provoquent un code de sortie en échec.

Le script lit le XML, déduplique les fonctions présentes dans le `.h` et le `.c`, compare avec les définitions des sources et contrôle descriptions, paramètres, retours, champs et constantes d’énumération. Le rapport donne une couverture mesurable. Une relecture reste nécessaire pour vérifier que le texte explique correctement le comportement.

Le HTML livré dans `docs/api` permet une lecture hors connexion. Une génération locale actualise `build/documentation/html` et `build/documentation/xml`. Après une évolution du programme, régénérer ces pages et les captures pour que l’explication suive le code.

## Petit glossaire des termes rencontrés

| Terme | Sens dans le projet |
| --- | --- |
| HAL | Hardware Abstraction Layer, bibliothèque ST qui fournit les accès aux périphériques |
| CMSIS | Common Microcontroller Software Interface Standard, définitions et services du cœur Arm |
| SPI | Serial Peripheral Interface, bus synchrone avec horloge et sélection de composant |
| I²C | Inter-Integrated Circuit, bus partagé utilisant SDA, SCL et des adresses |
| DMA | Direct Memory Access, transfert de données par un contrôleur matériel |
| IRQ | Interrupt Request, événement qui déclenche temporairement un gestionnaire du processeur |
| Callback | Fonction appelée par un autre module lorsqu’un événement ou une opération survient |
| ADC | Analog-to-Digital Converter, conversion d’une grandeur analogique en valeur numérique |
| PWM | Pulse Width Modulation, modulation de largeur d’impulsion pour faire varier une valeur moyenne |
| DDS | Direct Digital Synthesis, synthèse de signal par accumulation numérique de phase |
| LFSR | Linear Feedback Shift Register, registre à décalage à rétroaction utilisé pour un bruit déterministe |
| FIFO | First In, First Out, file qui restitue d’abord les données les plus anciennes |
| CRC | Cyclic Redundancy Check, signature calculée pour détecter des altérations de données |
| ECC | Error Correcting Code, contrôle matériel des erreurs de mémoire flash |
| RGB565 | Rouge sur 5 bits, vert sur 6, bleu sur 5 |
| Clipping | Limitation du dessin à une région autorisée |
| IIR | Infinite Impulse Response, filtre récursif utilisant son résultat précédent |
| LSB | Least Significant Bit, bit de poids faible ; sert aussi à exprimer un pas de conversion |
| Little-endian | Ordre d’octets où le poids faible précède le poids fort |
| Liaison interne | Nom limité à un fichier de traduction par `static` |

## Références

[Projet public du texte C11 N1570, WG14](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf) · [Blocs de documentation Doxygen](https://www.doxygen.nl/manual/docblocks.html) · [Commandes Doxygen](https://www.doxygen.nl/manual/commands.html) · [Configuration Doxygen](https://www.doxygen.nl/manual/config.html)
