/**
 * @file gfx.c
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
/** @var LCD_RECT
 * @brief Rectangle de tout l'écran paysage, de 320 × 240 pixels.
 */
/** @var digits
 * @brief Glyphes des dix chiffres. Chaque octet décrit les cinq bits d'une rangée.
 */
/** @var letters
 * @brief Glyphes des vingt-six majuscules, sept rangées par caractère.
 */
/** @var symbols
 * @brief Correspondance entre signes de ponctuation et leurs sept rangées de pixels.
 */
/** @var lower_letters
 * @brief Glyphes minuscules, utilisés notamment par la signature du menu.
 */

#include "obcp_gfx.h"

const Rect LCD_RECT = {0, 0, LCD_WIDTH, LCD_HEIGHT};

/**
 * @brief Choisir la plus petite de deux valeurs entières.
 *
 * Helper utilisé pour limiter les emprises avant leur intersection avec la bande.
 * @param[in] a Valeur numérique à comparer.
 * @param[in] b Valeur numérique à comparer.
 * @return Minimum des deux arguments.
 */
static int min_int(int a, int b) { return a < b ? a : b; }
/**
 * @brief Choisir la plus grande de deux valeurs entières.
 *
 * Helper utilisé pour construire une enveloppe et vérifier des limites.
 * @param[in] a Valeur numérique à comparer.
 * @param[in] b Valeur numérique à comparer.
 * @return Maximum des deux arguments.
 */
static int max_int(int a, int b) { return a > b ? a : b; }

/*
 * Vérifier qu'un rectangle possède une surface.
 *
 * Un rectangle vide a une largeur ou une hauteur non positive et ne demande aucun dessin.
 */
bool rect_valid(Rect r) { return r.w > 0 && r.h > 0; }

/*
 * Tester l'appartenance d'un point à un rectangle.
 *
 * Les limites droite et basse sont exclues. Deux boutons adjacents ne peuvent donc pas
 * consommer le même pixel de frontière.
 */
bool rect_contains(Rect r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/*
 * Trouver la surface commune à deux rectangles.
 *
 * Le plus grand bord gauche et le plus petit bord droit donnent l'intervalle horizontal.
 * Le même calcul est appliqué à la hauteur.
 */
Rect rect_intersection(Rect a, Rect b)
{
    int x = max_int(a.x, b.x);
    int y = max_int(a.y, b.y);
    int w = min_int(a.x + a.w, b.x + b.w) - x;
    int h = min_int(a.y + a.h, b.y + b.h) - y;
    return (Rect){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
}

/*
 * Construire une enveloppe rectangulaire commune.
 *
 * L'enveloppe couvre les deux rectangles. Elle sert notamment à fusionner une ancienne et
 * une nouvelle emprise de sprite.
 */
Rect rect_union(Rect a, Rect b)
{
    int x = min_int(a.x, b.x);
    int y = min_int(a.y, b.y);
    return (Rect){(int16_t)x, (int16_t)y,
                  (int16_t)(max_int(a.x + a.w, b.x + b.w) - x),
                  (int16_t)(max_int(a.y + a.h, b.y + b.h) - y)};
}

/*
 * Écrire un pixel dans le tampon de bande.
 *
 * Le clipping précède le calcul d'offset. Le mot RGB565 est rangé octet fort puis octet
 * faible, ordre attendu par le SPI du TFT.
 */
void gfx_pixel(Gfx *g, int x, int y, uint16_t color)
{
    if (rect_contains(g->clip, x, y)) {
        size_t i = 2U * (size_t)((y - g->clip.y) * g->clip.w + (x - g->clip.x));
        g->pixels[i] = (uint8_t)(color >> 8);
        g->pixels[i + 1U] = (uint8_t)color;
    }
}

/*
 * Remplir la partie visible d'un rectangle.
 *
 * Intersecte d'abord l'emprise avec la bande, puis parcourt ses lignes. Toute écriture
 * conserve le pas de la bande d'origine.
 */
void gfx_fill(Gfx *g, Rect r, uint16_t color)
{
    r = rect_intersection(g->clip, r);
    if (!rect_valid(r)) {
        return;
    }
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)color;
    for (int y = r.y; y < r.y + r.h; ++y) {
        size_t offset = 2U * (size_t)((y - g->clip.y) * g->clip.w + r.x - g->clip.x);
        uint8_t *pixel = &g->pixels[offset];
        for (int x = 0; x < r.w; ++x) {
            *pixel++ = hi;
            *pixel++ = lo;
        }
    }
}

/*
 * Dessiner les quatre côtés d'un rectangle.
 *
 * Quatre rectangles minces sont remplis. Les primitives sous-jacentes effectuent leur
 * clipping même si un bord est hors de l'écran.
 */
void gfx_outline(Gfx *g, Rect r, uint16_t color)
{
    if (!rect_valid(r)) {
        return;
    }
    gfx_fill(g, (Rect){r.x, r.y, r.w, 1}, color);
    gfx_fill(g, (Rect){r.x, (int16_t)(r.y + r.h - 1), r.w, 1}, color);
    gfx_fill(g, (Rect){r.x, r.y, 1, r.h}, color);
    gfx_fill(g, (Rect){(int16_t)(r.x + r.w - 1), r.y, 1, r.h}, color);
}

/*
 * Dessiner un disque plein autour d'un centre.
 *
 * Intersecte le carré englobant avec la bande, puis conserve les pixels dont dx² + dy² ne
 * dépasse pas le rayon au carré. Le calcul évite une racine carrée et protège les bords.
 */
void gfx_disc(Gfx *g, int cx, int cy, int radius, uint16_t color)
{
    Rect box = {(int16_t)(cx - radius), (int16_t)(cy - radius),
                (int16_t)(2 * radius + 1), (int16_t)(2 * radius + 1)};
    box = rect_intersection(g->clip, box);
    for (int y = box.y; y < box.y + box.h; ++y) {
        for (int x = box.x; x < box.x + box.w; ++x) {
            int dx = x - cx;
            int dy = y - cy;
            if (dx * dx + dy * dy <= radius * radius) {
                gfx_pixel(g, x, y, color);
            }
        }
    }
}

/* Glyphes 5 × 7 dessinés pour le projet. Chaque octet est une rangée. */
static const uint8_t digits[][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,2,12}
};
static const uint8_t letters[][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,2,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
};
static const struct {
    char ch; /**< Caractère ASCII concerné. */
    uint8_t rows[7]; /**< Sept rangées de cinq bits de pixels. */
} symbols[] = {
    {' ',{0,0,0,0,0,0,0}}, {'-',{0,0,0,31,0,0,0}}, {'.',{0,0,0,0,0,6,6}},
    {'+',{0,4,4,31,4,4,0}}, {':',{0,6,6,0,6,6,0}}, {'/',{1,2,2,4,8,8,16}},
    {'<',{1,2,4,8,4,2,1}}, {'>',{16,8,4,2,4,8,16}}, {'!',{4,4,4,4,4,0,4}},
    {'?',{14,17,1,2,4,0,4}}, {'%',{24,25,2,4,8,19,3}}, {'=',{0,0,31,0,31,0,0}}
};

static const uint8_t lower_letters[26][7] = {
    {0,0,14,1,15,17,15}, {16,16,22,25,17,17,30}, {0,0,14,16,16,17,14},
    {1,1,13,19,17,17,15}, {0,0,14,17,31,16,14}, {6,9,8,28,8,8,8},
    {0,0,15,17,15,1,14}, {16,16,22,25,17,17,17}, {4,0,12,4,4,4,14},
    {2,0,6,2,2,18,12}, {16,16,18,20,24,20,18}, {12,4,4,4,4,4,14},
    {0,0,26,21,21,21,21}, {0,0,22,25,17,17,17}, {0,0,14,17,17,17,14},
    {0,0,30,17,30,16,16}, {0,0,15,17,15,1,1}, {0,0,22,25,16,16,16},
    {0,0,15,16,14,1,30}, {8,8,28,8,8,9,6}, {0,0,17,17,17,19,13},
    {0,0,17,17,17,10,4}, {0,0,17,17,21,21,10}, {0,0,17,10,4,10,17},
    {0,0,17,17,15,1,14}, {0,0,31,2,4,8,31}
};

/**
 * @brief Trouver les sept rangées d'un caractère.
 *
 * Les majuscules, minuscules, chiffres et signes utilisent des tables constantes. Un
 * caractère absent est remplacé par la case espace.
 * @param[in] ch Caractère à chercher dans la police.
 * @return Pointeur vers sept octets de glyphes en lecture seule.
 */
static const uint8_t *glyph(char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        return lower_letters[ch - 'a'];
    }
    if (ch >= 'A' && ch <= 'Z') {
        return letters[ch - 'A'];
    }
    if (ch >= '0' && ch <= '9') {
        return digits[ch - '0'];
    }
    for (size_t i = 0; i < sizeof(symbols) / sizeof(symbols[0]); ++i) {
        if (symbols[i].ch == ch) {
            return symbols[i].rows;
        }
    }
    return symbols[0].rows;
}

/*
 * Remplir un triangle par tests de demi-plans.
 *
 * Les produits croisés indiquent de quel côté de chaque arête se trouve un pixel. Accepter
 * les deux orientations évite d'imposer un ordre des sommets.
 */
void gfx_triangle(Gfx *g, int ax, int ay, int bx, int by, int cx, int cy, uint16_t color)
{
    int x0 = min_int(ax, min_int(bx, cx)), y0 = min_int(ay, min_int(by, cy));
    int x1 = max_int(ax, max_int(bx, cx)), y1 = max_int(ay, max_int(by, cy));
    Rect box = rect_intersection(g->clip, (Rect){(int16_t)x0, (int16_t)y0,
                                                (int16_t)(x1 - x0 + 1), (int16_t)(y1 - y0 + 1)});
    for (int y = box.y; y < box.y + box.h; ++y) {
        for (int x = box.x; x < box.x + box.w; ++x) {
            /* Un produit croisé par arête classe le pixel. Trois signes cohérents indiquent l’intérieur du triangle. */
            int ab = (bx - ax) * (y - ay) - (by - ay) * (x - ax);
            int bc = (cx - bx) * (y - by) - (cy - by) * (x - bx);
            int ca = (ax - cx) * (y - cy) - (ay - cy) * (x - cx);
            if ((ab >= 0 && bc >= 0 && ca >= 0) || (ab <= 0 && bc <= 0 && ca <= 0)) {
                gfx_pixel(g, x, y, color);
            }
        }
    }
}

/*
 * Dessiner une chaîne avec une police de cinq par sept pixels.
 *
 * Chaque bit allumé produit un petit rectangle agrandi. L'avance de six pixels réserve un
 * espace entre deux caractères. Le facteur d'échelle doit être positif.
 */
void gfx_text(Gfx *g, int x, int y, const char *text, unsigned scale, uint16_t color)
{
    int s = (int)scale;
    if (s <= 0 || y + 7 * s <= g->clip.y || y >= g->clip.y + g->clip.h) {
        return;
    }
    for (; *text && x < g->clip.x + g->clip.w; ++text, x += 6 * s) {
        if (x + 5 * s <= g->clip.x) {
            continue;
        }
        const uint8_t *rows = glyph(*text);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((rows[row] & (16U >> (unsigned)col)) != 0U) {
                    gfx_fill(g, (Rect){(int16_t)(x + col * s), (int16_t)(y + row * s),
                                       (int16_t)s, (int16_t)s}, color);
                }
            }
        }
    }
}

/*
 * Demander une recomposition de tout l'écran.
 *
 * Remplace la liste de rectangles par LCD_RECT. Ce repli est aussi utilisé quand une liste
 * de dommages ne peut plus contenir un changement supplémentaire.
 */
void damage_full(Damage *damage)
{
    damage->items[0] = LCD_RECT;
    damage->count = 1;
}

/*
 * Ajouter une zone à recomposer et fusionner les voisines.
 *
 * Limite la zone à l'écran, fusionne les enveloppes peu coûteuses puis ajoute la zone
 * restante. Si la capacité est atteinte, une image complète remplace la liste.
 */
void damage_add(Damage *damage, Rect r)
{
    r = rect_intersection(r, LCD_RECT);
    if (!rect_valid(r)) {
        return;
    }

    /* Fusionner des régions proches quand le rectangle résultant est peu coûteux. */
    unsigned i = 0;
    while (i < damage->count) {
        Rect other = damage->items[i];
        Rect merged = rect_union(r, other);
        /* La fusion est admise si elle ajoute au plus 32 pixels. Cela réduit les commandes SPI sans repeindre une grande zone vide. */
        if (merged.w * merged.h <= r.w * r.h + other.w * other.h + 32) {
            r = merged;
            --damage->count;
            /* Remplacer la case retirée par la dernière évite de décaler le tableau. Recommencer car la nouvelle emprise peut rejoindre une autre zone. */
            damage->items[i] = damage->items[damage->count];
            i = 0;
        } else {
            ++i;
        }
    }
    if (damage->count == DAMAGE_CAPACITY) {
        damage_full(damage);
    } else {
        damage->items[damage->count++] = r;
    }
}

/*
 * Tester si tous les rectangles demandés ont été transmis.
 *
 * Le DMA doit être libre, le lot courant terminé et la liste en attente vide avant une
 * transaction flash différée.
 */
bool renderer_idle(const Renderer *r, const Damage *pending)
{
    return !r->in_flight && r->index >= r->batch.count && pending->count == 0;
}

/*
 * Avancer l'envoi non bloquant d'une bande.
 *
 * Une bande en vol est d'abord contrôlée. Sinon, un lot de rectangles est capturé et une
 * bande de 16 lignes au plus est recomposée puis confiée au transport. Le même tampon est
 * réutilisé seulement après la fin du transfert.
 */
void renderer_task(Renderer *r, Damage *pending, DrawScene draw, const void *scene)
{
    if (r->in_flight) {
        TransferState state = r->port.poll(r->port.context);
        /* Le DMA possède encore le tampon. Tout dessin à cet instant pourrait corrompre la bande envoyée. */
        if (state == TRANSFER_BUSY) {
            return;
        }
        r->in_flight = false;
        if (state == TRANSFER_ERROR) {
            ++r->errors;
            r->index = r->batch.count;
            damage_full(pending);
            return;
        }
        ++r->bands;
        r->pixels += (uint32_t)(r->sent.w * r->sent.h);
        r->rows += r->sent.h;
        if (r->rows == r->batch.items[r->index].h) {
            ++r->index;
            r->rows = 0;
            if (r->index == r->batch.count) {
                ++r->batches;
            }
        }
        /* La prochaine itération laisse une fenêtre SPI au tactile. */
        return;
    }
    if (r->index >= r->batch.count) {
        if (pending->count == 0) {
            return;
        }
        r->batch = *pending;
        pending->count = 0;
        r->index = 0;
        r->rows = 0;
    }

    Rect band = r->batch.items[r->index];
    band.y += r->rows;
    band.h = (int16_t)min_int(band.h - r->rows, BAND_ROWS);
    Gfx g = {band, r->buffer};
    /* Le callback dessine la scène dans la bande seulement. Les coordonnées restent absolues dans l’écran. */
    draw(scene, &g);
    r->sent = band;
    r->in_flight = r->port.send(r->port.context, band, r->buffer,
                                (uint16_t)(2 * band.w * band.h));
    if (!r->in_flight) {
        ++r->errors;
        r->index = r->batch.count;
        damage_full(pending);
    }
}
