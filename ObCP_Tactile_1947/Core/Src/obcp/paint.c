/**
 * @file paint.c
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
/** @var canvas
 * @brief Limites du canevas, 320 × 200 pixels.
 */
/** @var PAINT_PALETTE
 * @brief Palette RGB565 initiale copiée dans chaque nouveau dessin.
 */
/** @var PAINT_DIAMETERS
 * @brief Six diamètres de trait et de gomme, 1, 3, 5, 9, 13 et 19 pixels.
 */

#include "obcp_paint.h"

#include <stdlib.h>
#include <string.h>

static const Rect canvas = {0, 0, LCD_WIDTH, PAINT_HEIGHT};
const uint16_t PAINT_PALETTE[16] = {
    RGB565(250,248,240), RGB565(23,26,34), RGB565(225,56,84), RGB565(245,132,49),
    RGB565(251,199,68), RGB565(111,199,85), RGB565(30,158,124), RGB565(42,192,216),
    RGB565(40,100,219), RGB565(103,72,194), RGB565(205,82,174), RGB565(244,165,193),
    RGB565(143,91,54), RGB565(110,122,139), RGB565(187,200,209), RGB565(255,255,255)
};
const uint8_t PAINT_DIAMETERS[PAINT_BRUSHES] = {1, 3, 5, 9, 13, 19};
_Static_assert(sizeof(PaintImage) == PAINT_BYTES + 36, "Format dessin sans remplissage");

/*
 * Initialiser un canevas, une palette et le crayon.
 *
 * Met les pixels à l'indice zéro, copie la palette de départ, choisit l'encre huit et le
 * diamètre cinq pixels. Le dessin n'est pas marqué modifié à cette étape.
 */
void paint_init(Paint *p)
{
    memset(p, 0, sizeof(*p));
    memcpy(p->palette, PAINT_PALETTE, sizeof(p->palette));
    p->ink = 8;
    p->brush = 2;
}

/*
 * Effacer les pixels tout en gardant la palette.
 *
 * Le tableau est rempli de zéros, le geste en cours se termine et le dessin devient
 * modifié. La couleur d'effacement est la première case de la palette.
 */
void paint_clear(Paint *p)
{
    memset(p->image.pixels, 0, sizeof(p->image.pixels));
    p->stroke = false;
    p->modified = true;
}

/*
 * Lire l'indice de couleur d'un pixel.
 *
 * Calcule l'octet commun à deux abscisses, choisit le demi-octet bas ou haut puis applique
 * le masque 15. Une coordonnée hors canevas donne l'indice de fond.
 */
uint8_t paint_get(const Paint *p, int x, int y)
{
    if (!rect_contains(canvas, x, y)) {
        return 0;
    }
    /* Deux abscisses voisines partagent un octet. X pair utilise les bits 0 à 3, X impair les bits 4 à 7. */
    size_t offset = (size_t)(y * LCD_WIDTH + x) / 2U;
    unsigned shift = (x & 1) == 0 ? 0U : 4U;
    return (uint8_t)((p->image.pixels[offset] >> shift) & 15U);
}

/**
 * @brief Modifier le demi-octet d'un seul pixel.
 *
 * Le masque efface uniquement les quatre bits du pixel choisi et conserve son voisin. Le
 * test du canevas protège aussi les disques tracés près d'un bord.
 * @param[in,out] p Canevas et palette du dessin. Pointeur valide.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 * @param[in] index Indice de palette compris entre zéro et quinze.
 */
static void plot(Paint *p, int x, int y, uint8_t index)
{
    if (!rect_contains(canvas, x, y)) {
        return;
    }
    /* Deux abscisses voisines partagent un octet. X pair utilise les bits 0 à 3, X impair les bits 4 à 7. */
    size_t offset = (size_t)(y * LCD_WIDTH + x) / 2U;
    unsigned shift = (x & 1) == 0 ? 0U : 4U;
    /* Le masque 0x0F ou 0xF0 protège le demi-octet du voisin pendant la modification. */
    uint8_t mask = (uint8_t)(15U << shift);
    p->image.pixels[offset] = (uint8_t)((p->image.pixels[offset] & (uint8_t)~mask) | (index << shift));
}

/**
 * @brief Déposer un disque de crayon ou de gomme.
 *
 * Parcourt une petite emprise carrée et écrit les pixels dont la distance au centre ne
 * dépasse pas le rayon. plot() garde le disque à l'intérieur du canevas.
 * @param[in,out] p Canevas et palette du dessin. Pointeur valide.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 * @param[in] radius Rayon entier positif ou nul, en pixels.
 * @param[in] index Indice de palette compris entre zéro et quinze.
 */
static void dab(Paint *p, int x, int y, int radius, uint8_t index)
{
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius) {
                plot(p, x + dx, y + dy, index);
            }
        }
    }
}

/*
 * Relier deux relevés tactiles par un trait continu.
 *
 * Bresenham choisit des centres entiers successifs et dab() y applique le diamètre
 * courant. Le rectangle renvoyé couvre les deux extrémités et l'épaisseur du trait.
 */
Rect paint_segment(Paint *p, int x0, int y0, int x1, int y1)
{
    if (!rect_contains(canvas, x0, y0) || !rect_contains(canvas, x1, y1)) {
        return (Rect){0};
    }
    int radius = PAINT_DIAMETERS[p->brush] / 2;
    uint8_t index = p->erase ? 0U : (uint8_t)(p->ink & 15U);
    Rect from = {(int16_t)(x0 - radius), (int16_t)(y0 - radius),
                 (int16_t)(2 * radius + 1), (int16_t)(2 * radius + 1)};
    Rect to = {(int16_t)(x1 - radius), (int16_t)(y1 - radius), from.w, from.h};

    /* Bresenham relie deux relevés tactiles par des pixels entiers. */
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        dab(p, x0, y0, radius, index);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        /* L’erreur cumulée décide des avances sur X et Y. Les deux tests peuvent être vrais pour un pas diagonal. */
        int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
    p->modified = true;
    return rect_intersection(rect_union(from, to), canvas);
}

/*
 * Convertir les indices du canevas vers RGB565.
 *
 * Lit chaque indice dans la seule intersection entre canevas et bande, puis consulte la
 * palette attachée au dessin. Modifier une case recolore donc tous les pixels de cet
 * indice.
 */
void paint_draw(const Paint *p, Gfx *g)
{
    Rect area = rect_intersection(canvas, g->clip);
    for (int y = area.y; y < area.y + area.h; ++y) {
        for (int x = area.x; x < area.x + area.w; ++x) {
            gfx_pixel(g, x, y, p->palette[paint_get(p, x, y)]);
        }
    }
}

/*
 * Remplacer la case d'encre active de la palette.
 *
 * Une couleur réellement différente marque le dessin modifié. L'outil repasse au crayon,
 * car l'utilisateur vient de choisir une encre.
 */
void paint_set_color(Paint *p, uint16_t color)
{
    if (p->palette[p->ink] != color) {
        p->palette[p->ink] = color;
        p->modified = true;
    }
    p->erase = false;
}

/*
 * Encoder la palette et sauvegarder le dessin enrichi.
 *
 * Ajoute signature PT, version, choix de diamètre et couleurs little-endian, puis confie
 * l'image complète au service A/B.
 */
StoreResult paint_save(Paint *p, Storage *storage, SaveObject object)
{
    /* Le format sur flash est écrit octet par octet. Il ne dépend pas de l’ordre mémoire des uint16_t du processeur. */
    uint8_t *data = p->image.metadata;
    data[0] = 'P';
    data[1] = 'T';
    data[2] = 1;
    data[3] = p->brush;
    for (unsigned i = 0; i < 16U; ++i) {
        data[4U + i * 2U] = (uint8_t)p->palette[i];
        data[5U + i * 2U] = (uint8_t)(p->palette[i] >> 8);
    }
    return storage_save(storage, object, &p->image, sizeof(p->image));
}

/*
 * Lire un dessin historique ou enrichi et sa palette.
 *
 * La longueur permet de distinguer les pixels historiques de PaintImage. Le format enrichi
 * est validé avant de décoder les couleurs. Un succès termine le geste et retire
 * l'indicateur modifié.
 */
StoreResult paint_load(Paint *p, Storage *storage, SaveObject object)
{
    size_t size = 0;
    StoreResult result = storage_read_sized(storage, object, &p->image, sizeof(p->image), &size);
    if (result != STORE_OK) {
        return result;
    }
    if (size == PAINT_BYTES) {
        /* Les dessins 0.1 à 0.3 utilisaient la palette fixe d'origine. */
        memcpy(p->palette, PAINT_PALETTE, sizeof(p->palette));
        p->brush = 2;
    } else {
        const uint8_t *data = p->image.metadata;
        if (size != sizeof(p->image) || data[0] != 'P' || data[1] != 'T' ||
            data[2] != 1U || data[3] >= PAINT_BRUSHES) {
            return STORE_INVALID;
        }
        p->brush = data[3];
        for (unsigned i = 0; i < 16U; ++i) {
            p->palette[i] = (uint16_t)(data[4U + i * 2U] | (uint16_t)data[5U + i * 2U] << 8);
        }
    }
    p->stroke = p->modified = false;
    return STORE_OK;
}
