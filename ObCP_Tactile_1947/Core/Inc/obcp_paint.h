/**
 * @file obcp_paint.h
 * @brief Canevas indexé, diamètres de trait et palette persistante.
 * @ingroup obcp_apps
 *
 * Deux pixels tiennent dans un octet. Les seize indices désignent la palette du dessin. La
 * case zéro sert au fond et à la gomme. Un dessin enrichi ajoute 36 octets aux pixels, et
 * le lecteur accepte aussi le format historique de 32 000 octets.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
#ifndef OBCP_PAINT_H
#define OBCP_PAINT_H

/** @var PAINT_PALETTE
 * @brief Palette RGB565 initiale copiée dans chaque nouveau dessin.
 */
/** @var PAINT_DIAMETERS
 * @brief Six diamètres de trait et de gomme, 1, 3, 5, 9, 13 et 19 pixels.
 */

#include "obcp_gfx.h"
#include "obcp_storage.h"

/**
 * @brief Dimensions, bornes et constantes du module.
 */
enum {
    PAINT_HEIGHT = 200, /**< Hauteur utile du canevas, 200 pixels. */
    PAINT_BYTES = LCD_WIDTH * PAINT_HEIGHT / 2, /**< 32 000 octets pour 320 × 200 indices sur quatre bits. */
    PAINT_BRUSHES = 6, /**< Six diamètres proposés au crayon et à la gomme. */
};
extern const uint16_t PAINT_PALETTE[16];
extern const uint8_t PAINT_DIAMETERS[PAINT_BRUSHES];

/**
 * @brief Format contigu des pixels et métadonnées du dessin.
 *
 * Les membres sont des octets et un _Static_assert vérifie la taille totale. La palette y
 * est encodée explicitement en little-endian.
 */
typedef struct {
    uint8_t pixels[PAINT_BYTES]; /**< 32 000 octets contenant deux indices de couleur par octet. */
    uint8_t metadata[36]; /**< Signature PT, version, indice de diamètre et seize couleurs RGB565, soit 36 octets. */
} PaintImage;
/**
 * @brief Canevas et état des outils de dessin.
 *
 * L’encre est un indice dans une palette de seize couleurs. Les pixels conservent cet
 * indice, ce qui permet de recolorer une case de palette.
 */
typedef struct {
    PaintImage image; /**< Pixels et espace de métadonnées utilisés par la sauvegarde. */
    uint16_t palette[16]; /**< Seize couleurs RGB565 propres au dessin chargé. */
    uint8_t ink; /**< Indice de la couleur du crayon, de 0 à 15. */
    uint8_t brush; /**< Indice dans PAINT_DIAMETERS, de 0 à PAINT_BRUSHES moins un. */
    bool erase; /**< true pour déposer l'indice zéro avec la gomme. */
    bool stroke; /**< true si le contact courant a commencé sur le canevas. */
    bool modified; /**< true si pixels ou palette ont changé depuis la dernière lecture ou sauvegarde réussie. */
    int16_t last_x; /**< Abscisse précédente du trait, en pixels. */
    int16_t last_y; /**< Ordonnée précédente du trait, en pixels. */
} Paint;

/**
 * @brief Initialiser un canevas, une palette et le crayon.
 *
 * Met les pixels à l'indice zéro, copie la palette de départ, choisit l'encre huit et le
 * diamètre cinq pixels. Le dessin n'est pas marqué modifié à cette étape.
 * @param[out] p Canevas et palette du dessin, à initialiser par cet appel. Pointeur
 *   valide.
 */
void paint_init(Paint *p);
/**
 * @brief Effacer les pixels tout en gardant la palette.
 *
 * Le tableau est rempli de zéros, le geste en cours se termine et le dessin devient
 * modifié. La couleur d'effacement est la première case de la palette.
 * @param[in,out] p Canevas et palette du dessin. Pointeur valide.
 */
void paint_clear(Paint *p);
/**
 * @brief Lire l'indice de couleur d'un pixel.
 *
 * Calcule l'octet commun à deux abscisses, choisit le demi-octet bas ou haut puis applique
 * le masque 15. Une coordonnée hors canevas donne l'indice de fond.
 * @param[in] p Canevas et palette du dessin, consulté sans modification. Pointeur
 *   valide.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 * @return Indice compris entre zéro et quinze.
 */
uint8_t paint_get(const Paint *p, int x, int y);
/**
 * @brief Relier deux relevés tactiles par un trait continu.
 *
 * Bresenham choisit des centres entiers successifs et dab() y applique le diamètre
 * courant. Le rectangle renvoyé couvre les deux extrémités et l'épaisseur du trait.
 * @param[in,out] p Canevas et palette du dessin. Pointeur valide.
 * @param[in] x0 Abscisse du début du segment, en pixels.
 * @param[in] y0 Ordonnée du début du segment, en pixels.
 * @param[in] x1 Abscisse de la fin du segment, en pixels.
 * @param[in] y1 Ordonnée de la fin du segment, en pixels.
 * @return Zone de canevas à recomposer, ou rectangle vide si une extrémité est hors canevas.
 */
Rect paint_segment(Paint *p, int x0, int y0, int x1, int y1);
/**
 * @brief Convertir les indices du canevas vers RGB565.
 *
 * Lit chaque indice dans la seule intersection entre canevas et bande, puis consulte la
 * palette attachée au dessin. Modifier une case recolore donc tous les pixels de cet
 * indice.
 * @param[in] p Canevas et palette du dessin, consulté sans modification. Pointeur
 *   valide.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
void paint_draw(const Paint *p, Gfx *g);
/**
 * @brief Remplacer la case d'encre active de la palette.
 *
 * Une couleur réellement différente marque le dessin modifié. L'outil repasse au crayon,
 * car l'utilisateur vient de choisir une encre.
 * @param[in,out] p Canevas et palette du dessin. Pointeur valide.
 * @param[in] color Couleur encodée au format RGB565.
 */
void paint_set_color(Paint *p, uint16_t color);
/**
 * @brief Encoder la palette et sauvegarder le dessin enrichi.
 *
 * Ajoute signature PT, version, choix de diamètre et couleurs little-endian, puis confie
 * l'image complète au service A/B.
 * @param[in,out] p Canevas et palette du dessin. Pointeur valide.
 * @param[in,out] storage Port de stockage et ses callbacks synchrones. Pointeur valide.
 * @param[in] object Identifiant stable d’objet, strictement inférieur à SAVE_OBJECTS.
 * @return Résultat de storage_save().
 */
StoreResult paint_save(Paint *p, Storage *storage, SaveObject object);
/**
 * @brief Lire un dessin historique ou enrichi et sa palette.
 *
 * La longueur permet de distinguer les pixels historiques de PaintImage. Le format enrichi
 * est validé avant de décoder les couleurs. Un succès termine le geste et retire
 * l'indicateur modifié.
 * @param[in,out] p Canevas et palette du dessin. Pointeur valide.
 * @param[in,out] storage Port de stockage et ses callbacks synchrones. Pointeur valide.
 * @param[in] object Identifiant stable d’objet, strictement inférieur à SAVE_OBJECTS.
 * @return STORE_OK, résultat du port flash, ou STORE_INVALID si les métadonnées sont incompatibles.
 */
StoreResult paint_load(Paint *p, Storage *storage, SaveObject object);

#endif
