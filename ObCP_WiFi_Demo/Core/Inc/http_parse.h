/**
 ******************************************************************************
 * @file    http_parse.h
 * @brief   Analyse minimale des requêtes HTTP/1.x pour le serveur web de la
 *          démo 3 (indépendant du matériel, testé sur PC).
 *
 *  Une requête HTTP est un texte envoyé par le navigateur :
 *    GET /api/led?v=40&c=00FF00 HTTP/1.1      <- méthode, chemin, paramètres
 *    Host: 10.19.96.1                         <- en-têtes, un par ligne
 *    (ligne vide)                             <- fin des en-têtes
 *    [corps, pour un POST]
 *  Elle peut arriver en plusieurs morceaux : http_parse_request() indique
 *  s'il faut attendre la suite.
 ******************************************************************************
 */
#ifndef HTTP_PARSE_H
#define HTTP_PARSE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Méthode de la requête. */
typedef enum
{
  HTTP_GET = 0,
  HTTP_POST,
  HTTP_HEAD,
  HTTP_OTHER
} http_method_t;

/** Requête analysée. */
typedef struct
{
  http_method_t method;
  char          path[64];        /* chemin décodé, sans la chaîne de requête */
  char          query[256];      /* chaîne de requête brute (après '?'), ou corps
                                    application/x-www-form-urlencoded d'un POST */
  uint32_t      content_length;
  size_t        total_len;       /* en-têtes + corps : octets consommés */
} http_req_t;

#define HTTP_PARSE_OK          1   /* requête complète                    */
#define HTTP_PARSE_MORE        0   /* il manque des octets                */
#define HTTP_PARSE_BAD       (-1)  /* requête mal formée                  */
#define HTTP_PARSE_TOO_BIG   (-2)  /* requête trop longue pour le tampon  */

/**
 * @brief  Analyse les octets reçus d'une connexion.
 * @param  buf  octets reçus (pas forcément terminés par '\0').
 * @param  len  nombre d'octets reçus.
 * @param  cap  taille du tampon (au-delà : HTTP_PARSE_TOO_BIG).
 * @param  req  reçoit la requête analysée.
 * @retval HTTP_PARSE_OK, HTTP_PARSE_MORE (attendre la suite),
 *         HTTP_PARSE_BAD ou HTTP_PARSE_TOO_BIG.
 */
int    http_parse_request(const char *buf, size_t len, size_t cap, http_req_t *req);

/**
 * @brief  Cherche name=valeur dans une chaîne « a=1&b=2 » et décode la valeur
 *         (%XX et '+' pour l'espace).
 * @retval false si le paramètre est absent.
 */
bool   http_get_param(const char *query, const char *name, char *out, size_t outlen);

/** @brief  Décodage URL (%XX, '+') ; retourne la longueur écrite (troncature sûre). */
size_t http_url_decode(char *out, size_t outlen, const char *in, size_t inlen);

/** @brief  Texte d'un code de réponse HTTP : 200 -> « OK », 404 -> « Not Found »... */
const char *http_status_text(int code);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_PARSE_H */
