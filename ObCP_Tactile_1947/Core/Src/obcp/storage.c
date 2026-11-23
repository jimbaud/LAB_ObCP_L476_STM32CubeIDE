/**
 * @file storage.c
 * @brief Format d’objet A/B, CRC32 et validation transactionnelle.
 * @ingroup obcp_storage
 *
 * Chaque objet possède deux emplacements sur des pages distinctes. La génération et les
 * CRC identifient la copie exploitable. Une nouvelle copie reçoit son marqueur final dans
 * un double mot encore vierge après contrôle de toutes ses données.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
/** @def STORAGE_MAGIC
 * @brief Signature commune des en-têtes, valeur ASCII logique OBCP sur 32 bits.
 */
/** @def COMMIT_MAGIC
 * @brief Marqueur 64 bits écrit seulement après relecture correcte de la nouvelle copie.
 */
/** @var slots
 * @brief Placement et taille des deux copies de chacun des six objets persistants.
 */

#include "obcp_storage.h"

#include <string.h>

/** @brief Constantes privées de ce module, avec leurs unités. */
enum {
    HEADER_BYTES = 32, /**< En-tête complet de 32 octets, marqueur final inclus. */
    META_BYTES = 24, /**< 24 octets de métadonnées écrits avant la validation finale. */
    DRAW_COPY_BYTES = 32 * 1024, /**< Capacité de 32 Kio pour une copie de dessin, en-tête inclus. */
    AXES_BASE = 2 * OBCP_FLASH_PAGE, /**< Offset des deux pages A/B du repère des jeux. */
    TOUCH_BASE = 4 * OBCP_FLASH_PAGE, /**< Offset des deux pages A/B du calibrage tactile. */
    LEGACY_BASE = 6 * OBCP_FLASH_PAGE, /**< Début des objets historiques dont les adresses sont conservées. */
};
#define STORAGE_MAGIC UINT32_C(0x4F424350)
#define COMMIT_MAGIC UINT64_C(0xA537F1C2694DB8E0)

/** @brief Géométrie des deux copies d’un objet.
 * La seconde copie commence à base plus span.
 */
typedef struct {
    uint32_t base; /**< Offset du début de la copie A, en octets dans la réserve. */
    uint32_t span; /**< Taille d'une copie en octets, toujours un nombre entier de pages. */
} Slot;
static const Slot slots[SAVE_OBJECTS] = {
    [SAVE_DRAW_1] = {LEGACY_BASE, DRAW_COPY_BYTES},
    [SAVE_DRAW_2] = {LEGACY_BASE + 2 * DRAW_COPY_BYTES, DRAW_COPY_BYTES},
    [SAVE_MUSIC] = {LEGACY_BASE + 4 * DRAW_COPY_BYTES, OBCP_FLASH_PAGE},
    [SAVE_TOUCH_CAL] = {TOUCH_BASE, OBCP_FLASH_PAGE},
    [SAVE_TILT_AXES] = {AXES_BASE, OBCP_FLASH_PAGE},
    [SAVE_PREFERENCES] = {0, OBCP_FLASH_PAGE},
};
_Static_assert(LEGACY_BASE + 4 * DRAW_COPY_BYTES + 2 * OBCP_FLASH_PAGE == OBCP_STORAGE_BYTES,
               "La table doit couvrir exactement la reserve du linker");

/** @brief Résultat d’inspection d’une copie flash.
 * Un état invalide reste distinct d’une erreur physique de lecture.
 */
typedef struct {
    uint32_t offset; /**< Offset de l'en-tête inspecté, en octets. */
    uint32_t generation; /**< Compteur de version de la copie validée. */
    uint32_t size; /**< Longueur réelle de sa charge utile, en octets. */
    bool valid; /**< true après contrôle du marqueur, de l'en-tête et du CRC des données. */
    bool read_error; /**< true si le port physique a empêché de conclure sur la copie. */
} Copy;

/**
 * @brief Décoder un entier little-endian de quatre octets.
 *
 * Les décalages explicites évitent de dépendre de l'alignement de l'adresse ou de
 * l'endianness du processeur.
 * @param[in] p Adresse de quatre octets encodés en little-endian.
 * @return Valeur entière recomposée sur 32 bits.
 */
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/**
 * @brief Encoder un entier sur quatre octets little-endian.
 *
 * Le format de sauvegarde garde le même ordre d'octets sur la cible et dans les tests PC.
 * @param[in,out] p Adresse de quatre octets encodés en little-endian.
 * @param[in] value Valeur à utiliser dans ce traitement.
 */
static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        p[i] = (uint8_t)(value >> (8U * i));
    }
}

/**
 * @brief Poursuivre le calcul bit à bit d'un CRC32.
 *
 * Le registre courant traite chaque octet et huit décalages avec le polynôme réfléchi
 * 0xEDB88320. La complémentation finale est laissée à l'appelant.
 * @param[in] crc Accumulateur CRC avant complément final.
 * @param[in] data Bloc source valide de size octets.
 * @param[in] size Longueur du bloc en octets.
 * @return Nouvel accumulateur de CRC avant complément final.
 */
static uint32_t crc_continue(uint32_t crc, const uint8_t *data, size_t size)
{
    while (size-- > 0) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; ++bit) {
            uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return crc;
}

/*
 * Calculer le CRC32 d'un bloc de données.
 *
 * Initialise le registre à tous les bits à un et complémente le résultat. Les vecteurs de
 * tests contrôlent notamment la chaîne 123456789.
 */
uint32_t storage_crc32(const void *data, size_t size)
{
    return ~crc_continue(UINT32_MAX, data, size);
}

/**
 * @brief Calculer un CRC en relisant la flash par petits blocs.
 *
 * Un tampon de 128 octets borne la pile. Une erreur de lecture interrompt le calcul et
 * reste distincte d'un CRC incorrect.
 * @param[in,out] s Port de stockage et ses callbacks synchrones. Pointeur valide.
 * @param[in] start Offset du premier octet à contrôler dans la réserve flash.
 * @param[in] size Longueur du bloc en octets.
 * @param[in] expected CRC de référence lu dans l’en-tête.
 * @return STORE_OK, STORE_IO ou STORE_INVALID.
 */
static StoreResult crc_flash(Storage *s, uint32_t start, size_t size, uint32_t expected)
{
    uint8_t block[128];
    uint32_t crc = UINT32_MAX;
    while (size > 0) {
        size_t n = size < sizeof(block) ? size : sizeof(block);
        if (!s->read(s->context, start, block, n)) {
            return STORE_IO;
        }
        crc = crc_continue(crc, block, n);
        start += (uint32_t)n;
        size -= n;
    }
    return ~crc == expected ? STORE_OK : STORE_INVALID;
}

/**
 * @brief Valider l'en-tête, la longueur et le contenu d'une copie.
 *
 * Vérifie marqueur, signature, type, longueur admise et CRC d'en-tête, puis relit le CRC
 * des données. La longueur de chaque copie est propre à sa version de format.
 * @param[in,out] s Port de stockage et ses callbacks synchrones. Pointeur valide.
 * @param[in] object Identifiant stable d’objet, strictement inférieur à SAVE_OBJECTS.
 * @param[in] offset Offset en octets depuis le début de la réserve flash.
 * @return Descripteur de copie avec validité, génération, longueur et éventuelle erreur de lecture.
 */
static Copy inspect(Storage *s, SaveObject object, uint32_t offset)
{
    Copy result = {.offset = offset};
    uint8_t header[HEADER_BYTES];
    if (!s->read(s->context, offset, header, sizeof(header))) {
        result.read_error = true;
        return result;
    }
    /* La marque occupe un double mot dédié, vierge jusqu'à la validation. */
    if (get32(header + 24) != (uint32_t)COMMIT_MAGIC ||
        get32(header + 28) != (uint32_t)(COMMIT_MAGIC >> 32) ||
        get32(header) != STORAGE_MAGIC || get32(header + 4) != (UINT32_C(0x100) | (uint32_t)object) ||
        get32(header + 8) == 0U || get32(header + 8) > slots[object].span - HEADER_BYTES ||
        get32(header + 20) != storage_crc32(header, 20)) {
        return result;
    }
    result.generation = get32(header + 12);
    result.size = get32(header + 8);
    StoreResult payload = crc_flash(s, offset + HEADER_BYTES, result.size, get32(header + 16));
    result.valid = payload == STORE_OK;
    result.read_error = payload == STORE_IO;
    return result;
}

/**
 * @brief Comparer deux générations avec débordement du compteur.
 *
 * Une différence non nulle inférieure à la moitié de l'espace uint32_t désigne la
 * génération plus récente.
 * @param[in] a Valeur numérique à comparer.
 * @param[in] b Valeur numérique à comparer.
 * @return true si a suit b selon la comparaison modulaire.
 */
static bool newer(uint32_t a, uint32_t b)
{
    /* Comparaison modulaire, y compris au passage de FFFFFFFF à 0. */
    return a != b && a - b < UINT32_C(0x80000000);
}

/**
 * @brief Choisir la dernière copie valide d'un objet.
 *
 * Inspecte A et B, puis compare leurs générations. Si aucune copie n'est valide, une
 * erreur de lecture est conservée pour empêcher une écriture à l'aveugle.
 * @param[in,out] s Port de stockage et ses callbacks synchrones. Pointeur valide.
 * @param[in] object Identifiant stable d’objet, strictement inférieur à SAVE_OBJECTS.
 * @return Copie choisie avec son état de validation.
 */
static Copy active_copy(Storage *s, SaveObject object)
{
    Slot slot = slots[object];
    Copy a = inspect(s, object, slot.base);
    Copy b = inspect(s, object, slot.base + slot.span);
    if (b.valid && (!a.valid || newer(b.generation, a.generation))) {
        return b;
    }
    a.read_error = a.read_error || b.read_error;
    return a;
}

/**
 * @brief Contrôler type d'objet, pointeur et longueur.
 *
 * Le numéro doit appartenir à la table et la charge utile tenir après les 32 octets d'en-
 * tête dans une copie. Une taille nulle est rejetée.
 * @param[in] object Identifiant stable d’objet, strictement inférieur à SAVE_OBJECTS.
 * @param[in] data Tampon valide contenant size octets de données.
 * @param[in] size Longueur du bloc en octets.
 * @return true si la requête tient dans le stockage attribué à l'objet.
 */
static bool valid_request(SaveObject object, const void *data, size_t size)
{
    return (unsigned)object < SAVE_OBJECTS && data != NULL && size > 0 &&
           size <= slots[object].span - HEADER_BYTES;
}

/*
 * Lire un objet dont la longueur exacte est connue.
 *
 * Sélectionne d'abord la copie récente valide, puis vérifie que sa longueur égale celle
 * demandée avant de copier les octets vers l'appelant.
 */
StoreResult storage_read(Storage *s, SaveObject object, void *data, size_t size)
{
    if (!valid_request(object, data, size)) {
        return STORE_INVALID;
    }
    if (s == NULL || s->read == NULL) {
        return STORE_IO;
    }
    Copy active = active_copy(s, object);
    if (!active.valid) {
        return active.read_error ? STORE_IO : STORE_EMPTY;
    }
    if (active.size != size) {
        return STORE_EMPTY;
    }
    return s->read(s->context, active.offset + HEADER_BYTES, data, size) ? STORE_OK : STORE_IO;
}

/*
 * Lire un objet en découvrant sa longueur de format.
 *
 * Le tampon est donné avec sa capacité. La taille réellement enregistrée est fournie
 * seulement au succès. Une copie trop grande est rejetée avant la copie des données.
 */
StoreResult storage_read_sized(Storage *s, SaveObject object, void *data, size_t capacity, size_t *actual)
{
    if (!valid_request(object, data, capacity) || actual == NULL) {
        return STORE_INVALID;
    }
    if (s == NULL || s->read == NULL) {
        return STORE_IO;
    }
    Copy active = active_copy(s, object);
    if (!active.valid) {
        return active.read_error ? STORE_IO : STORE_EMPTY;
    }
    if (active.size > capacity) {
        return STORE_INVALID;
    }
    if (!s->read(s->context, active.offset + HEADER_BYTES, data, active.size)) {
        return STORE_IO;
    }
    *actual = active.size;
    return STORE_OK;
}

/*
 * Écrire et valider la copie inactive d'un objet.
 *
 * Garde la copie active, efface l'autre, écrit l'en-tête incomplet et les données, relit
 * les CRC puis programme le marqueur final. La génération active est reconnue même si la
 * nouvelle longueur de format diffère.
 */
StoreResult storage_save(Storage *s, SaveObject object, const void *data, size_t size)
{
    if (!valid_request(object, data, size)) {
        return STORE_INVALID;
    }
    if (s == NULL || s->read == NULL || s->erase == NULL || s->program == NULL) {
        return STORE_IO;
    }
    Slot slot = slots[object];
    /* La longueur peut évoluer. Garder la dernière copie valide de l'ancien
     * format jusqu'à la validation complète de la nouvelle copie. */
    Copy active = active_copy(s, object);
    if (!active.valid && active.read_error) {
        return STORE_IO;
    }
    uint32_t dest = slot.base;
    if (active.valid && active.offset == dest) {
        dest += slot.span;
    }

    /* Offsets de l’en-tête en octets : 0 signature, 4 type/version, 8 longueur, 12 génération, 16 CRC données, 20 CRC en-tête, 24 marqueur final. */
    uint8_t header[HEADER_BYTES];
    put32(header, STORAGE_MAGIC);
    put32(header + 4, UINT32_C(0x100) | (uint32_t)object);
    put32(header + 8, (uint32_t)size);
    put32(header + 12, active.valid ? active.generation + 1U : 0U);
    uint32_t crc = storage_crc32(data, size);
    put32(header + 16, crc);
    put32(header + 20, storage_crc32(header, 20));
    put32(header + 24, (uint32_t)COMMIT_MAGIC);
    put32(header + 28, (uint32_t)(COMMIT_MAGIC >> 32));

    if (!s->erase(s->context, dest, slot.span)) {
        return STORE_IO;
    }
    /* Seuls les 24 premiers octets sont écrits ici. Le double mot de validation reste effacé pendant toute la copie. */
    for (uint32_t i = 0; i < META_BYTES; i += 8) {
        if (!s->program(s->context, dest + i, header + i)) {
            return STORE_IO;
        }
    }
    const uint8_t *source = data;
    for (size_t i = 0; i < size; i += 8) {
        uint8_t word[8];
        /* La flash se programme par double mot de huit octets. Le dernier bloc est complété avec l’état effacé 0xFF. */
        memset(word, 0xFF, sizeof(word));
        size_t n = size - i < sizeof(word) ? size - i : sizeof(word);
        memcpy(word, source + i, n);
        if (!s->program(s->context, dest + HEADER_BYTES + (uint32_t)i, word)) {
            return STORE_IO;
        }
    }

    /* Relire les métadonnées et le CRC des données avant d’écrire la marque. Une coupure avant cette marque laisse l’ancienne copie active. */
    uint8_t check[META_BYTES];
    if (!s->read(s->context, dest, check, sizeof(check)) ||
        memcmp(check, header, sizeof(check)) != 0 ||
        crc_flash(s, dest + HEADER_BYTES, size, crc) != STORE_OK ||
        !s->program(s->context, dest + META_BYTES, header + META_BYTES)) {
        return STORE_IO;
    }
    return inspect(s, object, dest).valid ? STORE_OK : STORE_IO;
}
