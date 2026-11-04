/**
 ******************************************************************************
 * @file    fifo.h
 * @brief   Tampon circulaire d'octets (FIFO : « premier entré, premier
 *          sorti »), utilisé pour les messages en attente d'envoi vers le
 *          PC (USB) et vers le téléphone (Bluetooth).
 *
 *  Principe : un tableau de taille fixe et deux compteurs.
 *    - head : nombre total d'octets écrits depuis le début ;
 *    - tail : nombre total d'octets lus (retirés) depuis le début.
 *  Le nombre d'octets présents est head - tail. La case du tableau qui
 *  correspond à un compteur c est c modulo la taille ; avec une taille
 *  puissance de 2, ce modulo s'écrit simplement c & (taille - 1).
 *
 *      tableau :  [ . . . D A T A . . . ]
 *                         ^       ^
 *                       tail     head      (indices pris modulo la taille)
 *
 *  Les compteurs sont des entiers 16 bits non signés qui « débordent »
 *  (65535 + 1 = 0) : la soustraction head - tail reste juste tant que la
 *  taille du tableau ne dépasse pas 32768 octets.
 *
 *  Une seule partie du programme doit écrire et une seule doit lire. Si l'une
 *  d'elles est une interruption, l'appelant protège les appels (voir
 *  usb_console.c, qui masque les interruptions pendant ces opérations).
 *
 *  Pour aller plus loin : https://fr.wikipedia.org/wiki/Buffer_circulaire
 ******************************************************************************
 */
#ifndef FIFO_H
#define FIFO_H

#include <stdbool.h>
#include <stdint.h>

/** Tampon circulaire. Les champs ne doivent être lus ou modifiés que par les
 *  fonctions fifo_xxx() ci-dessous. */
typedef struct
{
  uint8_t          *buf;   /**< tableau de stockage, fourni par l'utilisateur */
  uint16_t          size;  /**< taille du tableau : puissance de 2, <= 32768 */
  volatile uint16_t head;  /**< compteur des octets écrits */
  volatile uint16_t tail;  /**< compteur des octets retirés */
} fifo_t;

/** Initialisation « statique » d'un fifo_t, au moment de sa déclaration :
 *  @code
 *    static uint8_t s_storage[1024];
 *    static fifo_t  s_fifo = FIFO_INIT(s_storage);
 *  @endcode
 *  Le tampon est alors utilisable dès le démarrage, sans appel de fonction,
 *  même par une interruption qui arriverait très tôt. */
#define FIFO_INIT(array)  { (array), (uint16_t)sizeof(array), 0U, 0U }

/**
 * @brief  Prépare un tampon circulaire (même rôle que FIFO_INIT, à l'exécution).
 * @param  f        tampon à préparer
 * @param  storage  tableau qui contiendra les octets
 * @param  size     taille du tableau en octets (puissance de 2, <= 32768)
 */
void fifo_init(fifo_t *f, uint8_t *storage, uint16_t size);

/**
 * @brief  Nombre d'octets présents dans le tampon.
 */
uint16_t fifo_used(const fifo_t *f);

/**
 * @brief  Nombre d'octets qu'on peut encore écrire.
 */
uint16_t fifo_free(const fifo_t *f);

/**
 * @brief  Ajoute len octets à la fin du tampon, « tout ou rien » : s'il n'y a
 *         pas assez de place, rien n'est écrit (un message n'est jamais
 *         coupé en deux).
 * @param  f     tampon
 * @param  data  octets à ajouter
 * @param  len   nombre d'octets
 * @retval true si les octets ont été ajoutés, false si la place manquait
 */
bool fifo_write(fifo_t *f, const uint8_t *data, uint16_t len);

/**
 * @brief  Copie des octets du début du tampon sans les retirer (on regarde
 *         sans prendre ; fifo_drop() les retire ensuite).
 * @param  f       tampon
 * @param  offset  nombre d'octets à sauter depuis le début
 * @param  out     destination de la copie
 * @param  len     nombre d'octets souhaités
 * @retval nombre d'octets réellement copiés (moins de len si le tampon en
 *         contient moins)
 */
uint16_t fifo_peek(const fifo_t *f, uint16_t offset, uint8_t *out, uint16_t len);

/**
 * @brief  Retire len octets du début du tampon (ou tous s'il y en a moins).
 */
void fifo_drop(fifo_t *f, uint16_t len);

/**
 * @brief  Vide le tampon.
 */
void fifo_clear(fifo_t *f);

#endif /* FIFO_H */
