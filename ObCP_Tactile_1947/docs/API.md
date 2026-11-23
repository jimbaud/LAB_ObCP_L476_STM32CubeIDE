# Lire l’API du projet

Les commentaires placés dans les en-têtes décrivent les contrats d’appel. Les
commentaires du fichier source expliquent ensuite l’algorithme. Les fonctions
privées possèdent aussi leurs contrats et apparaissent dans les pages de fichier.

Commencer par @ref obcp_board, puis suivre les liens vers les pilotes et les
modèles. Les pages de fonction proposent leur source et les appels associés.

Un paramètre marqué `in` est lu. `out` reçoit un résultat. `in,out` conserve un
état entre deux appels et peut être modifié. Les unités et capacités de tampon
sont précisées près de chaque paramètre.

Les structures relient les données à leur rôle. @ref App possède les modèles,
@ref Renderer possède le tampon de bande et @ref Storage fournit les callbacks
du port physique. Les fonctions @ref app_touch et @ref app_render séparent la
commande de l’interface de son dessin.

## Générer les pages

Avec Doxygen et Graphviz installés, lancer depuis le dossier du projet

    doxygen Doxyfile

Ouvrir ensuite `build/documentation/html/index.html`. Les fichiers XML permettent
de contrôler que les descriptions et paramètres sont présents. Le journal des
avertissements est `build/documentation_warnings.txt`.

Le README GitHub et les guides du dossier `docs` présentent les mêmes parcours
avec des explications progressives et des captures du moteur graphique.

## Chemins conseillés dans la référence

| Question | Points d’entrée |
| --- | --- |
| Que se passe-t-il à chaque tour ? | @ref obcp_task puis les fonctions privées de board.c |
| Comment un toucher ouvre-t-il une page ? | @ref touch_read, @ref touch_update, @ref app_touch et @ref app_open |
| Qui dessine un pixel ? | @ref app_render, @ref renderer_task et @ref gfx_pixel |
| Comment conserver le dessin ? | @ref paint_save, @ref storage_save et @ref obcp_flash_storage |
| Comment piloter les jeux ? | @ref accel_read, @ref tilt_sample et @ref tilt_project |
| Comment produire le son ? | @ref audio_fill puis @ref pwm_audio_task |

Le graphe d’appels représente des relations directes visibles par Doxygen.
Les appels indirects par pointeur de fonction, comme les callbacks de Storage
et DisplayPort, demandent aussi de suivre les contrats de ces structures.

Les pages de fichier affichent les fonctions privées et leurs données.
Les pages de structure détaillent les champs et unités. Le lien vers le code
source permet de lire les commentaires internes autour du calcul concerné.
