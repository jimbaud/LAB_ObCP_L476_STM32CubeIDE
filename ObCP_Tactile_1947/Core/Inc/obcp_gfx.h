/**
 * @file obcp_gfx.h
 * @brief Géométrie, primitives RGB565 et rendu par bandes DMA.
 * @ingroup obcp_graphics
 *
 * Le repère écran a X vers la droite et Y vers le bas. Les bornes droites et basses des
 * rectangles sont exclues. Le contexte Gfx conserve le pas exact de sa bande. Renderer
 * transfère la propriété du tampon au DMA entre send et poll.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
#ifndef OBCP_GFX_H
#define OBCP_GFX_H

/** @var LCD_RECT
 * @brief Rectangle de tout l'écran paysage, de 320 × 240 pixels.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Dimensions, bornes et constantes du module.
 */
enum {
    LCD_WIDTH = 320, /**< Largeur paysage de 320 pixels. */
    LCD_HEIGHT = 240, /**< Hauteur paysage de 240 pixels. */
    BAND_ROWS = 16, /**< Au plus seize lignes RGB565 dans le tampon de rendu. */
    DAMAGE_CAPACITY = 28, /**< Au plus 28 rectangles avant repli sur l'écran complet. */
};
/* Les composantes 8 bits sont tronquées à 5, 6 et 5 bits. Aucun flottant ni allocation. */
/**
 * @brief Encoder trois composantes 8 bits dans un mot RGB565.
 * @param[in] r Composante rouge comprise entre 0 et 255.
 * @param[in] g Composante verte comprise entre 0 et 255.
 * @param[in] b Composante bleue comprise entre 0 et 255.
 * @return Mot uint16_t contenant rouge sur 5 bits, vert sur 6 et bleu sur 5.
 * @pre Fournir des expressions sans effet de bord et dans la plage indiquée.
 */
#define RGB565(r, g, b) ((uint16_t)((((r) & 248U) << 8) | (((g) & 252U) << 3) | ((b) >> 3)))

/* Origine en haut à gauche. Un rectangle contient [x, x+w[ et [y, y+h[. */
/**
 * @brief Rectangle entier du repère écran.
 *
 * Le rectangle contient les abscisses x à x+w exclu et les ordonnées y à y+h exclu. Une
 * dimension non positive décrit un rectangle vide.
 */
typedef struct {
    int16_t x; /**< Abscisse du bord gauche, en pixels. */
    int16_t y; /**< Ordonnée du bord supérieur, en pixels. */
    int16_t w; /**< Largeur en pixels. */
    int16_t h; /**< Hauteur en pixels. */
} Rect;
/**
 * @brief Vue de dessin sur un tampon de bande RGB565.
 *
 * Le pas mémoire est clip.w. Changer seulement clip modifierait aussi ce pas. Les
 * primitives réalisent donc leur propre intersection tout en conservant cette vue.
 */
typedef struct {
    Rect clip; /**< Position et dimensions écran de la bande stockée dans pixels. */
    uint8_t *pixels; /**< Tampon d'au moins deux fois clip.w fois clip.h octets, ordre RGB565 fort puis faible. */
} Gfx;

extern const Rect LCD_RECT;
/**
 * @brief Trouver la surface commune à deux rectangles.
 *
 * Le plus grand bord gauche et le plus petit bord droit donnent l'intervalle horizontal.
 * Le même calcul est appliqué à la hauteur.
 * @param[in] a Rectangle à comparer ou à combiner.
 * @param[in] b Rectangle à comparer ou à combiner.
 * @return Rectangle commun, éventuellement vide.
 */
Rect rect_intersection(Rect a, Rect b);
/**
 * @brief Construire une enveloppe rectangulaire commune.
 *
 * L'enveloppe couvre les deux rectangles. Elle sert notamment à fusionner une ancienne et
 * une nouvelle emprise de sprite.
 * @param[in] a Rectangle à comparer ou à combiner.
 * @param[in] b Rectangle à comparer ou à combiner.
 * @return Plus petit rectangle englobant les deux arguments.
 */
Rect rect_union(Rect a, Rect b);
/**
 * @brief Tester l'appartenance d'un point à un rectangle.
 *
 * Les limites droite et basse sont exclues. Deux boutons adjacents ne peuvent donc pas
 * consommer le même pixel de frontière.
 * @param[in] r Rectangle écran, avec bord droit et bas exclus.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 * @return true si le point appartient au rectangle.
 */
bool rect_contains(Rect r, int x, int y);
/**
 * @brief Vérifier qu'un rectangle possède une surface.
 *
 * Un rectangle vide a une largeur ou une hauteur non positive et ne demande aucun dessin.
 * @param[in] r Rectangle écran, avec bord droit et bas exclus.
 * @return true si largeur et hauteur sont strictement positives.
 */
bool rect_valid(Rect r);
/**
 * @brief Écrire un pixel dans le tampon de bande.
 *
 * Le clipping précède le calcul d'offset. Le mot RGB565 est rangé octet fort puis octet
 * faible, ordre attendu par le SPI du TFT.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 * @param[in] color Couleur encodée au format RGB565.
 */
void gfx_pixel(Gfx *g, int x, int y, uint16_t color);
/**
 * @brief Remplir la partie visible d'un rectangle.
 *
 * Intersecte d'abord l'emprise avec la bande, puis parcourt ses lignes. Toute écriture
 * conserve le pas de la bande d'origine.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 * @param[in] r Rectangle écran, avec bord droit et bas exclus.
 * @param[in] color Couleur encodée au format RGB565.
 */
void gfx_fill(Gfx *g, Rect r, uint16_t color);
/**
 * @brief Dessiner les quatre côtés d'un rectangle.
 *
 * Quatre rectangles minces sont remplis. Les primitives sous-jacentes effectuent leur
 * clipping même si un bord est hors de l'écran.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 * @param[in] r Rectangle écran, avec bord droit et bas exclus.
 * @param[in] color Couleur encodée au format RGB565.
 */
void gfx_outline(Gfx *g, Rect r, uint16_t color);
/**
 * @brief Dessiner un disque plein autour d'un centre.
 *
 * Intersecte le carré englobant avec la bande, puis conserve les pixels dont dx² + dy² ne
 * dépasse pas le rayon au carré. Le calcul évite une racine carrée et protège les bords.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 * @param[in] cx Abscisse du centre ou du troisième sommet, en pixels.
 * @param[in] cy Ordonnée du centre ou du troisième sommet, en pixels.
 * @param[in] radius Rayon entier positif ou nul, en pixels.
 * @param[in] color Couleur encodée au format RGB565.
 */
void gfx_disc(Gfx *g, int cx, int cy, int radius, uint16_t color);
/**
 * @brief Remplir un triangle par tests de demi-plans.
 *
 * Les produits croisés indiquent de quel côté de chaque arête se trouve un pixel. Accepter
 * les deux orientations évite d'imposer un ordre des sommets.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 * @param[in] ax Abscisse du premier sommet, en pixels.
 * @param[in] ay Ordonnée du premier sommet, en pixels.
 * @param[in] bx Abscisse du deuxième sommet, en pixels.
 * @param[in] by Ordonnée du deuxième sommet, en pixels.
 * @param[in] cx Abscisse du centre ou du troisième sommet, en pixels.
 * @param[in] cy Ordonnée du centre ou du troisième sommet, en pixels.
 * @param[in] color Couleur encodée au format RGB565.
 */
void gfx_triangle(Gfx *g, int ax, int ay, int bx, int by, int cx, int cy, uint16_t color);
/**
 * @brief Dessiner une chaîne avec une police de cinq par sept pixels.
 *
 * Chaque bit allumé produit un petit rectangle agrandi. L'avance de six pixels réserve un
 * espace entre deux caractères. Le facteur d'échelle doit être positif.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 * @param[in] text Chaîne valide terminée par zéro, conservée pendant l’appel.
 * @param[in] scale Facteur entier positif d’agrandissement des glyphes.
 * @param[in] color Couleur encodée au format RGB565.
 */
void gfx_text(Gfx *g, int x, int y, const char *text, unsigned scale, uint16_t color);

/**
 * @brief Liste bornée des rectangles à recomposer.
 *
 * Les zones proches sont fusionnées. Un dépassement de capacité demande une image
 * complète.
 */
typedef struct {
    Rect items[DAMAGE_CAPACITY]; /**< Rectangles invalidés, déjà limités à l'écran. */
    uint8_t count; /**< Nombre de rectangles utilisés, entre zéro et DAMAGE_CAPACITY. */
} Damage;
/* Toute modification d'un objet invalide son ancienne et sa nouvelle emprise. */
/**
 * @brief Ajouter une zone à recomposer et fusionner les voisines.
 *
 * Limite la zone à l'écran, fusionne les enveloppes peu coûteuses puis ajoute la zone
 * restante. Si la capacité est atteinte, une image complète remplace la liste.
 * @param[in,out] damage Liste de rectangles à recomposer. Pointeur valide.
 * @param[in] r Rectangle écran, avec bord droit et bas exclus.
 */
void damage_add(Damage *damage, Rect r);
/**
 * @brief Demander une recomposition de tout l'écran.
 *
 * Remplace la liste de rectangles par LCD_RECT. Ce repli est aussi utilisé quand une liste
 * de dommages ne peut plus contenir un changement supplémentaire.
 * @param[in,out] damage Liste de rectangles à recomposer. Pointeur valide.
 */
void damage_full(Damage *damage);

/**
 * @brief Propriété du tampon après un départ de transfert.
 */
typedef enum {
    TRANSFER_BUSY, /**< Le transport possède encore le tampon. */
    TRANSFER_DONE, /**< Le transport a terminé et libéré le tampon. */
    TRANSFER_ERROR, /**< Le transport a échoué et libéré le tampon. */
} TransferState;
/**
 * @brief Transport abstrait d’une bande vers un écran.
 *
 * send() prend le tampon jusqu’à poll() DONE ou ERROR. Le contexte peut pointer vers un
 * pilote ou une simulation PC. Le moteur ne connaît pas la HAL.
 */
typedef struct {
    void *context; /**< Contexte opaque transmis aux deux callbacks. NULL est permis pour le port TFT unique. */
    bool (*send)(void *context, Rect r, const uint8_t *data, uint16_t count); /**< Callback synchrone de départ. true signifie que le transport possède maintenant les données jusqu'à la fin signalée par poll. */
    TransferState (*poll)(void *context); /**< Callback de consultation de l'état du dernier envoi. Il rend BUSY, DONE ou ERROR sans dessiner de pixels. */
} DisplayPort;

/**
 * @brief Type du callback qui recompose une bande de la scène courante.
 * @param[in] scene État applicatif consulté pendant cet appel synchrone.
 * @param[in,out] band Vue RGB565 de la bande à remplir, limitée par band->clip.
 * @note Appelé dans la boucle principale. Le callback ne conserve pas ce tampon.
 */
typedef void (*DrawScene)(const void *scene, Gfx *band);
/**
 * @brief Machine d’état du rendu par bandes.
 *
 * La liste batch est figée pour le lot courant. Les nouvelles invalidations restent dans
 * la liste pending de l’application. Le tampon est exclusivement possédé par le CPU ou par
 * le transport.
 */
typedef struct {
    DisplayPort port; /**< Callbacks et contexte du transport TFT ou de sa simulation. */
    Damage batch; /**< Rectangles du lot actuellement en cours. */
    Rect sent; /**< Dernière bande confiée au transport. */
    uint8_t index; /**< Indice du rectangle courant dans batch. */
    int16_t rows; /**< Nombre de lignes déjà achevées dans ce rectangle. */
    bool in_flight; /**< true tant que le transport possède buffer. */
    uint32_t batches; /**< Nombre de lots de rectangles complètement achevés. */
    uint32_t bands; /**< Nombre de bandes transférées avec succès. */
    uint32_t pixels; /**< Nombre cumulé de pixels transmis avec succès. */
    uint32_t errors; /**< Nombre de départs ou de transferts échoués. */
    uint8_t buffer[LCD_WIDTH * BAND_ROWS * 2]; /**< Seul tampon RGB565 de 320 fois 16 pixels, soit 10 240 octets. */
} Renderer;

/* Un seul tampon de bande. Le CPU le possède jusqu'à send(), puis le DMA
 * jusqu'à poll() DONE/ERROR. Une nouvelle invalidation va dans pending. */
/**
 * @brief Avancer l'envoi non bloquant d'une bande.
 *
 * Une bande en vol est d'abord contrôlée. Sinon, un lot de rectangles est capturé et une
 * bande de 16 lignes au plus est recomposée puis confiée au transport. Le même tampon est
 * réutilisé seulement après la fin du transfert.
 * @param[in,out] r Machine d’état et tampon du rendu par bandes. Pointeur valide.
 * @param[in,out] pending Liste de rectangles à recomposer. Pointeur valide.
 * @param[in] draw Callback de recomposition d’une bande, appelé avant send().
 * @param[in] scene Contexte opaque transmis sans allocation. Le port physique peut ne pas
 *   l’utiliser.
 */
void renderer_task(Renderer *r, Damage *pending, DrawScene draw, const void *scene);
/**
 * @brief Tester si tous les rectangles demandés ont été transmis.
 *
 * Le DMA doit être libre, le lot courant terminé et la liste en attente vide avant une
 * transaction flash différée.
 * @param[in] r Machine d’état et tampon du rendu par bandes, consulté sans
 *   modification. Pointeur valide.
 * @param[in] pending Liste de rectangles à recomposer, consulté sans modification. Pointeur
 *   valide.
 * @return true si aucune bande ni invalidation n'attend de traitement.
 */
bool renderer_idle(const Renderer *r, const Damage *pending);

#endif
