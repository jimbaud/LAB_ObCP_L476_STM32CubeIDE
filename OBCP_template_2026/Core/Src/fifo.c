/**
 ******************************************************************************
 * @file    fifo.c
 * @brief   Tampon circulaire d'octets (voir fifo.h pour le principe).
 ******************************************************************************
 */
#include "fifo.h"

/* Case du tableau qui correspond au compteur c (c modulo la taille). */
static inline uint16_t fifo_index(const fifo_t *f, uint16_t c)
{
  return (uint16_t)(c & (uint16_t)(f->size - 1U));
}

/* Voir fifo.h */
void fifo_init(fifo_t *f, uint8_t *storage, uint16_t size)
{
  f->buf  = storage;
  f->size = size;
  f->head = 0U;
  f->tail = 0U;
}

/* Voir fifo.h. head - tail calculé sur 16 bits : juste même quand head a
 * « débordé » (repassé par 0) et pas encore tail. */
uint16_t fifo_used(const fifo_t *f)
{
  return (uint16_t)(f->head - f->tail);
}

/* Voir fifo.h */
uint16_t fifo_free(const fifo_t *f)
{
  return (uint16_t)(f->size - fifo_used(f));
}

/* Voir fifo.h */
bool fifo_write(fifo_t *f, const uint8_t *data, uint16_t len)
{
  uint16_t head = f->head;

  if (len > fifo_free(f))
  {
    return false;                       /* pas assez de place : rien n'est écrit */
  }
  for (uint16_t i = 0U; i < len; i++)
  {
    f->buf[fifo_index(f, head)] = data[i];
    head++;
  }
  f->head = head;                       /* les octets sont maintenant visibles */
  return true;
}

/* Voir fifo.h */
uint16_t fifo_peek(const fifo_t *f, uint16_t offset, uint8_t *out, uint16_t len)
{
  uint16_t used = fifo_used(f);
  uint16_t n;

  if (offset >= used)
  {
    return 0U;
  }
  n = (uint16_t)(used - offset);
  if (n > len)
  {
    n = len;
  }
  for (uint16_t i = 0U; i < n; i++)
  {
    out[i] = f->buf[fifo_index(f, (uint16_t)(f->tail + offset + i))];
  }
  return n;
}

/* Voir fifo.h */
void fifo_drop(fifo_t *f, uint16_t len)
{
  uint16_t used = fifo_used(f);

  if (len > used)
  {
    len = used;
  }
  f->tail = (uint16_t)(f->tail + len);
}

/* Voir fifo.h */
void fifo_clear(fifo_t *f)
{
  f->tail = f->head;
}
