/* SHA-256 (FIPS 180-4) pour les tests sur PC : vérification des images .ota
 * par le module simulé et fabrication d'images de test. */
#ifndef TEST_SHA256_H
#define TEST_SHA256_H

#include <stdint.h>
#include <stddef.h>

typedef struct
{
  uint32_t h[8];
  uint64_t len;
  uint8_t  buf[64];
  size_t   n;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *c);
void sha256_update(sha256_ctx_t *c, const void *data, size_t len);
void sha256_final(sha256_ctx_t *c, uint8_t out[32]);
void sha256(const void *data, size_t len, uint8_t out[32]);

#endif
