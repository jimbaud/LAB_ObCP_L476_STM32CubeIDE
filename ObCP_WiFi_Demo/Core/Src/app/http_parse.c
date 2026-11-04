/**
 ******************************************************************************
 * @file    http_parse.c
 * @brief   Analyse minimale des requêtes HTTP (voir http_parse.h).
 *
 *  La requête est complète quand la ligne vide « \r\n\r\n » qui termine les
 *  en-têtes a été reçue, plus le corps annoncé par Content-Length (POST).
 *  Dans une URL, les caractères spéciaux sont codés « %XX » (code
 *  hexadécimal) et l'espace « + » : http_url_decode() les rétablit.
 ******************************************************************************
 */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "http_parse.h"
#include "util.h"

/* Recherche bornée (memmem n'est pas disponible partout) */
static const char *find(const char *buf, size_t len, const char *pat)
{
  size_t n = strlen(pat);
  size_t i;

  if (n > len)
  {
    return NULL;
  }
  for (i = 0U; i <= (len - n); i++)
  {
    if (memcmp(&buf[i], pat, n) == 0)
    {
      return &buf[i];
    }
  }
  return NULL;
}

/* Valeur d'un chiffre hexadécimal, -1 si ce n'en est pas un */
static int hexval(int c)
{
  if ((c >= '0') && (c <= '9'))
  {
    return c - '0';
  }
  c = tolower(c);
  if ((c >= 'a') && (c <= 'f'))
  {
    return (c - 'a') + 10;
  }
  return -1;
}

/* Voir http_parse.h */
size_t http_url_decode(char *out, size_t outlen, const char *in, size_t inlen)
{
  size_t o = 0U;
  size_t i = 0U;

  if (outlen == 0U)
  {
    return 0U;
  }
  while ((i < inlen) && (in[i] != '\0') && ((o + 1U) < outlen))
  {
    char c = in[i];
    if ((c == '%') && ((i + 2U) < inlen) && (hexval((unsigned char)in[i + 1U]) >= 0) &&
        (hexval((unsigned char)in[i + 2U]) >= 0))
    {
      c = (char)((hexval((unsigned char)in[i + 1U]) << 4) | hexval((unsigned char)in[i + 2U]));
      i += 3U;
    }
    else
    {
      if (c == '+')
      {
        c = ' ';
      }
      i++;
    }
    out[o++] = c;
  }
  out[o] = '\0';
  return o;
}

/* Voir http_parse.h : parcourt les paires nom=valeur séparées par '&' */
bool http_get_param(const char *query, const char *name, char *out, size_t outlen)
{
  size_t      nlen = strlen(name);
  const char *p = query;

  while ((p != NULL) && (*p != '\0'))
  {
    const char *amp = strchr(p, '&');
    size_t      seg = (amp != NULL) ? (size_t)(amp - p) : strlen(p);

    if ((seg > nlen) && (strncmp(p, name, nlen) == 0) && (p[nlen] == '='))
    {
      (void)http_url_decode(out, outlen, &p[nlen + 1U], seg - nlen - 1U);
      return true;
    }
    if ((seg == nlen) && (strncmp(p, name, nlen) == 0))
    {
      if (outlen > 0U)
      {
        out[0] = '\0';                  /* paramètre sans valeur */
      }
      return true;
    }
    p = (amp != NULL) ? (amp + 1) : NULL;
  }
  return false;
}

/* Valeur de l'en-tête Content-Length (insensible à la casse) */
static bool content_length(const char *hdr, size_t len, uint32_t *out)
{
  static const char key[] = "content-length:";
  size_t klen = sizeof(key) - 1U;
  size_t i = 0U;

  *out = 0U;
  while (i < len)
  {
    /* début de ligne */
    size_t k;
    bool   match = (len - i) > klen;
    for (k = 0U; match && (k < klen); k++)
    {
      if (tolower((unsigned char)hdr[i + k]) != key[k])
      {
        match = false;
      }
    }
    if (match)
    {
      uint32_t v = 0U;
      size_t   j = i + klen;
      bool     digits = false;
      while ((j < len) && (hdr[j] == ' '))
      {
        j++;
      }
      while ((j < len) && (hdr[j] >= '0') && (hdr[j] <= '9'))
      {
        v = (v * 10U) + (uint32_t)(hdr[j] - '0');
        digits = true;
        j++;
        if (v > 1000000UL)
        {
          return false;
        }
      }
      if (!digits)
      {
        return false;
      }
      *out = v;
      return true;
    }
    /* ligne suivante */
    while ((i < len) && (hdr[i] != '\n'))
    {
      i++;
    }
    i++;
  }
  return true;                          /* absent : 0 */
}

/* Voir http_parse.h. Étapes : fin des en-têtes trouvée ? ligne de requête
 * « MÉTHODE /chemin?paramètres HTTP/1.x » découpée ; corps complet ?    */
int http_parse_request(const char *buf, size_t len, size_t cap, http_req_t *req)
{
  const char *end_hdr = find(buf, len, "\r\n\r\n");
  const char *sp1;
  const char *sp2;
  const char *line_end;
  const char *q;
  size_t      hdr_len;
  size_t      mlen;
  size_t      ulen;
  size_t      plen;

  memset(req, 0, sizeof(*req));

  if (end_hdr == NULL)
  {
    line_end = find(buf, len, "\r\n");
    if ((line_end != NULL) && (find(buf, (size_t)(line_end - buf), " HTTP/") == NULL))
    {
      return HTTP_PARSE_BAD;           /* première ligne complète mais pas HTTP */
    }
    return (len >= cap) ? HTTP_PARSE_TOO_BIG : HTTP_PARSE_MORE;
  }
  hdr_len = (size_t)(end_hdr - buf) + 4U;

  /* Ligne de requête : MÉTHODE SP CIBLE SP HTTP/x.y */
  line_end = find(buf, hdr_len, "\r\n");
  sp1 = memchr(buf, ' ', (size_t)(line_end - buf));
  if (sp1 == NULL)
  {
    return HTTP_PARSE_BAD;
  }
  sp2 = memchr(sp1 + 1, ' ', (size_t)(line_end - (sp1 + 1)));
  if ((sp2 == NULL) || (strncmp(sp2 + 1, "HTTP/", 5) != 0))
  {
    return HTTP_PARSE_BAD;
  }
  mlen = (size_t)(sp1 - buf);
  if ((mlen == 3U) && (strncmp(buf, "GET", 3) == 0))
  {
    req->method = HTTP_GET;
  }
  else if ((mlen == 4U) && (strncmp(buf, "POST", 4) == 0))
  {
    req->method = HTTP_POST;
  }
  else if ((mlen == 4U) && (strncmp(buf, "HEAD", 4) == 0))
  {
    req->method = HTTP_HEAD;
  }
  else
  {
    req->method = HTTP_OTHER;
  }

  ulen = (size_t)(sp2 - (sp1 + 1));
  if ((ulen == 0U) || (sp1[1] != '/'))
  {
    return HTTP_PARSE_BAD;
  }
  q = memchr(sp1 + 1, '?', ulen);
  plen = (q != NULL) ? (size_t)(q - (sp1 + 1)) : ulen;
  if (plen >= sizeof(req->path))
  {
    return HTTP_PARSE_TOO_BIG;
  }
  (void)http_url_decode(req->path, sizeof(req->path), sp1 + 1, plen);
  if (q != NULL)
  {
    size_t qlen = ulen - plen - 1U;
    if (qlen >= sizeof(req->query))
    {
      return HTTP_PARSE_TOO_BIG;
    }
    memcpy(req->query, q + 1, qlen);
    req->query[qlen] = '\0';
  }

  if (!content_length(line_end + 2, hdr_len - (size_t)((line_end + 2) - buf), &req->content_length))
  {
    return HTTP_PARSE_BAD;
  }
  if ((hdr_len + req->content_length) > cap)
  {
    return HTTP_PARSE_TOO_BIG;
  }
  if ((hdr_len + req->content_length) > len)
  {
    return HTTP_PARSE_MORE;
  }
  if ((req->method == HTTP_POST) && (req->content_length > 0U))
  {
    /* corps de formulaire : remplace la chaîne de requête */
    if (req->content_length >= sizeof(req->query))
    {
      return HTTP_PARSE_TOO_BIG;
    }
    memcpy(req->query, &buf[hdr_len], req->content_length);
    req->query[req->content_length] = '\0';
  }
  req->total_len = hdr_len + req->content_length;
  return HTTP_PARSE_OK;
}

/* Voir http_parse.h */
const char *http_status_text(int code)
{
  switch (code)
  {
    case 200: return "OK";
    case 204: return "No Content";
    case 302: return "Found";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default:  return "Unknown";
  }
}
