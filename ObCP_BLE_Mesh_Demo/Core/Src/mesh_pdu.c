/**
 ******************************************************************************
 * @file    mesh_pdu.c
 * @brief   Codage et décodage des trames du réseau maillé (voir mesh_pdu.h
 *          pour le format).
 *
 *  Fonctions « pures » : elles ne touchent ni au matériel ni à une variable
 *  globale. On peut donc les tester sur PC (Tests/test_unit.c).
 ******************************************************************************
 */
#include "mesh_pdu.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Petits outils                                                              */
/* -------------------------------------------------------------------------- */

/* Voir mesh_pdu.h */
void mesh_put_u16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

/* Voir mesh_pdu.h */
uint16_t mesh_get_u16(const uint8_t *p)
{
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/* Voir mesh_pdu.h */
void mesh_put_u32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

/* Voir mesh_pdu.h */
uint32_t mesh_get_u32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* -------------------------------------------------------------------------- */
/* Codage et décodage                                                         */
/* -------------------------------------------------------------------------- */

/* Voir mesh_pdu.h */
uint8_t mesh_pdu_encode(const mesh_frame_t *f, uint8_t *out, size_t size)
{
  size_t total = (size_t)MESH_HEADER_LEN + f->len;

  if ((f->len > MESH_PAYLOAD_MAX) || (size < total))
  {
    return 0U;
  }
  out[MESH_OFS_MAGIC]   = MESH_MAGIC;
  out[MESH_OFS_VERSION] = MESH_VERSION;
  out[MESH_OFS_NET]     = f->net;
  out[MESH_OFS_SRC]     = f->src;
  out[MESH_OFS_DST]     = f->dst;
  out[MESH_OFS_SEQ]     = f->seq;
  out[MESH_OFS_TTL]     = f->ttl;
  out[MESH_OFS_HOPS]    = f->hops;
  out[MESH_OFS_LAST]    = f->last;
  out[MESH_OFS_TYPE]    = f->type;
  if (f->len > 0U)
  {
    memcpy(&out[MESH_OFS_PAYLOAD], f->payload, f->len);
  }
  return (uint8_t)total;
}

/* Voir mesh_pdu.h */
bool mesh_pdu_decode(const uint8_t *in, size_t len, mesh_frame_t *f)
{
  if ((len < MESH_HEADER_LEN) || (len > MESH_PDU_MAX))
  {
    return false;                       /* trop court ou trop long */
  }
  if ((in[MESH_OFS_MAGIC] != MESH_MAGIC) || (in[MESH_OFS_VERSION] != MESH_VERSION))
  {
    return false;                       /* pas une trame de ce réseau (ou autre version) */
  }
  if (!mesh_addr_is_node(in[MESH_OFS_SRC]) || !mesh_addr_is_node(in[MESH_OFS_LAST])
      || (in[MESH_OFS_DST] == MESH_ADDR_NONE))
  {
    return false;                       /* origine, émetteur ou destination impossibles */
  }
  f->net  = in[MESH_OFS_NET];
  f->src  = in[MESH_OFS_SRC];
  f->dst  = in[MESH_OFS_DST];
  f->seq  = in[MESH_OFS_SEQ];
  f->ttl  = in[MESH_OFS_TTL];
  f->hops = in[MESH_OFS_HOPS];
  f->last = in[MESH_OFS_LAST];
  f->type = in[MESH_OFS_TYPE];
  f->len  = (uint8_t)(len - MESH_HEADER_LEN);
  memset(f->payload, 0, sizeof(f->payload));
  if (f->len > 0U)
  {
    memcpy(f->payload, &in[MESH_OFS_PAYLOAD], f->len);
  }
  return true;
}

/* -------------------------------------------------------------------------- */
/* Emballage dans une publicité Bluetooth                                     */
/* -------------------------------------------------------------------------- */

/* Voir mesh_pdu.h */
uint8_t mesh_adv_build(const uint8_t *pdu, uint8_t pdu_len, uint8_t *adv, size_t size)
{
  size_t total = (size_t)MESH_ADV_OVERHEAD + pdu_len;

  if ((pdu_len > MESH_PDU_MAX) || (size < total))
  {
    return 0U;
  }
  adv[0] = (uint8_t)(3U + pdu_len);             /* longueur du champ (type compris) */
  adv[1] = 0xFFU;                               /* type : données du fabricant */
  adv[2] = (uint8_t)(MESH_COMPANY_ID & 0xFFU);  /* identifiant, poids faible */
  adv[3] = (uint8_t)(MESH_COMPANY_ID >> 8);     /* identifiant, poids fort */
  memcpy(&adv[MESH_ADV_OVERHEAD], pdu, pdu_len);
  return (uint8_t)total;
}

/* Voir mesh_pdu.h. Une publicité est une suite de champs :
 *   [longueur L][type][L - 1 octets de données] [longueur][type]...
 * Une longueur nulle marque la fin des données utiles.                    */
bool mesh_adv_find(const uint8_t *adv, size_t adv_len, const uint8_t **pdu, uint8_t *pdu_len)
{
  size_t i = 0U;

  while (i < adv_len)
  {
    size_t field_len = adv[i];                  /* type + données */

    if ((field_len == 0U) || ((i + 1U + field_len) > adv_len))
    {
      return false;                             /* fin, ou champ qui déborde */
    }
    if ((adv[i + 1U] == 0xFFU) && (field_len >= (3U + MESH_HEADER_LEN))
        && (adv[i + 2U] == (uint8_t)(MESH_COMPANY_ID & 0xFFU))
        && (adv[i + 3U] == (uint8_t)(MESH_COMPANY_ID >> 8))
        && (adv[i + 4U] == MESH_MAGIC))
    {
      *pdu     = &adv[i + 4U];
      *pdu_len = (uint8_t)(field_len - 3U);
      return true;
    }
    i += 1U + field_len;                        /* champ suivant */
  }
  return false;
}

/* -------------------------------------------------------------------------- */
/* Adresses et noms                                                           */
/* -------------------------------------------------------------------------- */

/* Voir mesh_pdu.h */
bool mesh_addr_is_node(uint8_t addr)
{
  return (addr >= MESH_ADDR_NODE_MIN) && (addr <= MESH_ADDR_NODE_MAX);
}

/* Voir mesh_pdu.h */
bool mesh_addr_is_group(uint8_t addr)
{
  return (addr >= MESH_ADDR_GROUP_FIRST) && (addr <= MESH_ADDR_GROUP_LAST);
}

/* Voir mesh_pdu.h */
uint8_t mesh_addr_group(uint8_t addr)
{
  return mesh_addr_is_group(addr) ? (uint8_t)(addr - 239U) : 0U;
}

/* Voir mesh_pdu.h */
int mesh_addr_format(uint8_t addr, char *text, size_t size)
{
  if (addr == MESH_ADDR_ALL)
  {
    return snprintf(text, size, "tous");
  }
  if (mesh_addr_is_group(addr))
  {
    return snprintf(text, size, "g%u", (unsigned)mesh_addr_group(addr));
  }
  return snprintf(text, size, "%u", (unsigned)addr);
}

/* Lit un entier décimal de 1 à 3 chiffres, seul dans le texte. */
static bool parse_small_number(const char *text, unsigned *value)
{
  unsigned v = 0U;
  size_t   n = 0U;

  while (text[n] != '\0')
  {
    if ((text[n] < '0') || (text[n] > '9') || (n >= 3U))
    {
      return false;
    }
    v = (v * 10U) + (unsigned)(text[n] - '0');
    n++;
  }
  *value = v;
  return n > 0U;
}

/* Voir mesh_pdu.h */
bool mesh_addr_parse(const char *text, uint8_t *addr)
{
  unsigned v;

  if ((strcmp(text, "tous") == 0) || (strcmp(text, "*") == 0))
  {
    *addr = MESH_ADDR_ALL;
    return true;
  }
  if ((text[0] == 'g') || (text[0] == 'G'))
  {
    if (parse_small_number(&text[1], &v) && (v >= 1U) && (v <= MESH_GROUP_COUNT))
    {
      *addr = MESH_GROUP_ADDR(v);
      return true;
    }
    return false;
  }
  if (parse_small_number(text, &v) && (v >= MESH_ADDR_NODE_MIN) && (v <= MESH_ADDR_NODE_MAX))
  {
    *addr = (uint8_t)v;
    return true;
  }
  return false;
}

/* Voir mesh_pdu.h */
const char *mesh_type_name(uint8_t type)
{
  switch (type)
  {
    case MESH_T_HELLO:       return "HELLO";
    case MESH_T_PING:        return "PING";
    case MESH_T_PONG:        return "PONG";
    case MESH_T_TRACE:       return "TRACE";
    case MESH_T_TRACE_BACK:  return "TRACE_RET";
    case MESH_T_TEXT:        return "TEXTE";
    case MESH_T_LED:         return "LED";
    case MESH_T_WAVE:        return "VAGUE";
    case MESH_T_IDENT:       return "IDENT";
    case MESH_T_MEASURE_REQ: return "DEM_MESURE";
    case MESH_T_MEASURE:     return "MESURE";
    case MESH_T_CONFIG:      return "REGLAGE";
    default:                 return "?";
  }
}
