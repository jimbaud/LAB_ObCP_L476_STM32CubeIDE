/**
 * @file obcp_storage.h
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
#ifndef OBCP_STORAGE_H
#define OBCP_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Dimensions, bornes et constantes du module.
 */
enum {
    OBCP_FLASH_PAGE = 2048, /**< Page physique du STM32L476 de 2 048 octets. */
    OBCP_STORAGE_BYTES = 144 * 1024, /**< Réserve persistante totale de 144 Kio en banque 2. */
};
/* Conserver les valeurs d'objets utilisées dans les en-têtes précédents. */
/**
 * @brief Identifiants stables des objets persistants.
 */
typedef enum {
    SAVE_DRAW_1, /**< Premier dessin, identifiant historique zéro. */
    SAVE_DRAW_2, /**< Deuxième dessin. */
    SAVE_MUSIC, /**< Livre des quatre motifs musicaux. */
    SAVE_TOUCH_CAL, /**< Deux profils de calibrage résistif. */
    SAVE_TILT_AXES, /**< Orientation commune aux trois jeux. */
    SAVE_PREFERENCES, /**< Réactivités X et Y. */
    SAVE_OBJECTS, /**< Nombre d'identifiants, borne exclusive des requêtes. */
} SaveObject;
/**
 * @brief Résultats communs aux lectures et écritures de stockage.
 */
typedef enum {
    STORE_OK, /**< L'opération est complètement validée. */
    STORE_EMPTY, /**< Objet absent, copie valide introuvable ou longueur exacte différente. */
    STORE_IO, /**< Lecture, effacement, programmation ou relecture physique échouée. */
    STORE_INVALID, /**< Requête ou format hors des valeurs prises en charge. */
} StoreResult;

/* Toutes les adresses sont des offsets en octets dans la réserve du linker.
 * program doit écrire un double mot aligné. erase reçoit des pages entières.
 * Les fonctions sont synchrones et appelées uniquement dans la boucle principale. */
/**
 * @brief Interface physique de stockage injectable dans les tests.
 *
 * Toutes les adresses sont des offsets dans la réserve du linker. Les callbacks synchrones
 * sont appelés dans la boucle principale. Le port STM32 et la flash simulée partagent ce
 * contrat.
 */
typedef struct {
    void *context; /**< Contexte opaque du port. Peut être NULL pour la flash physique unique. */
    bool (*read)(void *context, uint32_t offset, void *data, size_t count); /**< Copier count octets depuis offset vers data. true signifie que toute la lecture a réussi, y compris le contrôle ECC du port STM32. */
    bool (*erase)(void *context, uint32_t offset, uint32_t count); /**< Effacer count octets à partir de offset. Offset et count doivent représenter des pages complètes de 2 Kio. true confirme l'effacement. */
    bool (*program)(void *context, uint32_t offset, const uint8_t bytes[8]); /**< Programmer les huit octets bytes sur un offset aligné de huit octets encore vierges. true confirme le succès physique du double mot. */
} Storage;

/**
 * @brief Lire un objet dont la longueur exacte est connue.
 *
 * Sélectionne d'abord la copie récente valide, puis vérifie que sa longueur égale celle
 * demandée avant de copier les octets vers l'appelant.
 * @param[in,out] s Port de stockage et ses callbacks synchrones. Pointeur valide.
 * @param[in] object Identifiant stable d’objet, strictement inférieur à SAVE_OBJECTS.
 * @param[out] data Tampon de destination valide. Sa capacité est donnée par size, capacity
 *   ou count.
 * @param[in] size Longueur du bloc en octets.
 * @return STORE_OK, STORE_EMPTY si absent ou de longueur différente, STORE_IO ou STORE_INVALID.
 */
StoreResult storage_read(Storage *s, SaveObject object, void *data, size_t size);
/* Lire la dernière copie, dont la longueur peut appartenir à un ancien format.
 * actual reçoit la longueur enregistrée. Une capacité insuffisante laisse data intact. */
/**
 * @brief Lire un objet en découvrant sa longueur de format.
 *
 * Le tampon est donné avec sa capacité. La taille réellement enregistrée est fournie
 * seulement au succès. Une copie trop grande est rejetée avant la copie des données.
 * @param[in,out] s Port de stockage et ses callbacks synchrones. Pointeur valide.
 * @param[in] object Identifiant stable d’objet, strictement inférieur à SAVE_OBJECTS.
 * @param[out] data Tampon de destination valide. Sa capacité est donnée par size, capacity
 *   ou count.
 * @param[in] capacity Capacité du tampon de destination en octets.
 * @param[out] actual Longueur réelle enregistrée, écrite seulement au succès.
 * @return STORE_OK, STORE_EMPTY, STORE_IO ou STORE_INVALID.
 */
StoreResult storage_read_sized(Storage *s, SaveObject object, void *data, size_t capacity, size_t *actual);
/**
 * @brief Écrire et valider la copie inactive d'un objet.
 *
 * Garde la copie active, efface l'autre, écrit l'en-tête incomplet et les données, relit
 * les CRC puis programme le marqueur final. La génération active est reconnue même si la
 * nouvelle longueur de format diffère.
 * @param[in,out] s Port de stockage et ses callbacks synchrones. Pointeur valide.
 * @param[in] object Identifiant stable d’objet, strictement inférieur à SAVE_OBJECTS.
 * @param[in] data Bloc source valide de size octets.
 * @param[in] size Longueur du bloc en octets.
 * @return STORE_OK au succès, STORE_IO en cas d'échec physique ou STORE_INVALID pour une requête hors contrat.
 */
StoreResult storage_save(Storage *s, SaveObject object, const void *data, size_t size);
/**
 * @brief Calculer le CRC32 d'un bloc de données.
 *
 * Initialise le registre à tous les bits à un et complémente le résultat. Les vecteurs de
 * tests contrôlent notamment la chaîne 123456789.
 * @param[in] data Bloc source valide de size octets.
 * @param[in] size Longueur du bloc en octets.
 * @return CRC32 du bloc fourni.
 */
uint32_t storage_crc32(const void *data, size_t size);

#endif
