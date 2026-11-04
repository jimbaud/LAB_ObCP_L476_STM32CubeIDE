/**
 ******************************************************************************
 * @file    ncp.c
 * @brief   Démarrage du ST67W611M1 et aiguillage des évènements (voir ncp.h).
 *
 *  Séquence de démarrage reprise de W6X_Init() du pilote officiel :
 *  CHIP_EN 0 -> 1, attente « ready », AT, AT+CWNETMODE? (1 = T01),
 *  AT+GET_CLOCK (1 = RC interne, sinon AT+SET_CLOCK=1 et redémarrage),
 *  AT+GMR (versions), AT+PWR=0 (économie d'énergie désactivée).
 *
 *  Aiguillage des évènements : le module envoie, sans qu'on le lui demande,
 *  des lignes appelées URC (« Unsolicited Result Code ») : +CW:CONNECTED,
 *  +CIP:0,CONNECTED, +MQTT:CONNECTED... ncp_at.c les transmet toutes à
 *  urc_router(), qui les passe successivement aux couches Wi-Fi, réseau puis
 *  MQTT : la première qui reconnaît la ligne la traite.
 ******************************************************************************
 */
#include <string.h>
#include <stdlib.h>
#include "ncp.h"

#define READY_TIMEOUT_MS   6000U      /* délai maximal entre CHIP_EN = 1 et « ready » */

/* Mis à vrai quand le module annonce « ready » en dehors de ncp_start() :
 * il a redémarré tout seul (voir ncp_restarted()). « volatile » : la
 * variable peut changer pendant l'attente d'une réponse.               */
static volatile bool s_restarted;

/* Distribue une ligne non sollicitée (URC) à la couche qui la comprend. */
static void urc_router(const char *line)
{
  if (strcmp(line, "ready") == 0)
  {
    s_restarted = true;
    return;
  }
  if (wifi_urc(line))
  {
    return;
  }
  if (net_urc(line))
  {
    return;
  }
  (void)mqtt_urc(line);
}

/* Rappel de at_cmd() : réponse de la forme « +XXX:<entier> » -> *ctx */
static void on_int(const char *args, void *ctx)
{
  *(int *)ctx = atoi(args);
}

/* Copie bornée d'une chaîne, toujours terminée par '\0' */
static void copy_field(char *dst, size_t len, const char *src)
{
  strncpy(dst, src, len - 1U);
  dst[len - 1U] = '\0';
}

/* Rappel de AT+GMR : relève les trois versions affichées par la bannière */
static void on_gmr(const char *line, void *ctx)
{
  ncp_info_t *info = (ncp_info_t *)ctx;
  if (at_starts(line, "AT version:"))
  {
    copy_field(info->at_version, sizeof(info->at_version), &line[11]);
  }
  else if (at_starts(line, "component_version_sdk_"))
  {
    copy_field(info->sdk_version, sizeof(info->sdk_version), &line[22]);
  }
  else if (at_starts(line, "component_version_macsw_"))
  {
    copy_field(info->mac_version, sizeof(info->mac_version), &line[24]);
  }
  else
  {
    /* autres composants ignorés */
  }
}

/* Attend « ready » puis vérifie que le module répond « OK » à « AT »
 * (3 essais) : le dialogue est alors établi.                          */
static int boot_and_sync(void)
{
  int st = AT_TIMEOUT;
  int i;

  if (at_wait_ready(READY_TIMEOUT_MS) != AT_OK)
  {
    return NCP_ERR_NOREADY;
  }
  ncp_port_delay(100U);              /* laisse le module finir son démarrage */
  for (i = 0; i < 3; i++)
  {
    st = at_cmd("AT", NULL, NULL, NULL, 2000U);
    if (st == AT_OK)
    {
      break;
    }
  }
  return st;
}

int ncp_start(ncp_info_t *info)
{
  int st;
  int mode = -1;
  int clk = -1;

  memset(info, 0, sizeof(*info));
  info->net_mode = -1;
  info->clock_source = -1;

  /* 1) Reset matériel du module : CHIP_EN à 0 pendant 20 ms, BOOT à 0 pour
   *    démarrer le firmware (et non le chargeur en ROM).                  */
  at_init(urc_router);
  ncp_port_cs(false);
  ncp_port_boot(false);              /* mode mission */
  ncp_port_chip_enable(false);
  ncp_port_delay(20U);
  at_clear_ready();
  ncp_port_chip_enable(true);

  st = boot_and_sync();
  if (st != AT_OK)
  {
    return st;
  }

  /* 2) Type de firmware : 1 = pile TCP/IP dans le module (T01, attendu) */
  if (at_cmd("AT+CWNETMODE?", "+CWNETMODE:", on_int, &mode, 2000U) == AT_OK)
  {
    info->net_mode = mode;
  }
  if (info->net_mode == 0)
  {
    return NCP_ERR_ARCH;              /* firmware T02 : pile IP attendue sur l'hôte */
  }

  /* 3) Source d'horloge du module : le pilote ST impose l'oscillateur RC
   *    interne (1) ; si besoin elle est changée, ce qui redémarre le module. */
  if (at_cmd("AT+GET_CLOCK", "+GET_CLOCK:", on_int, &clk, 2000U) == AT_OK)
  {
    info->clock_source = clk;
    if (clk != 1)
    {
      at_clear_ready();
      (void)at_cmd("AT+SET_CLOCK=1", NULL, NULL, NULL, 0U);   /* le module redémarre */
      st = boot_and_sync();
      if (st != AT_OK)
      {
        return st;
      }
      info->clock_source = 1;
    }
  }

  /* 4) Versions (bannière, commande « module ») et économie d'énergie
   *    désactivée : le module reste toujours prêt à répondre.            */
  (void)at_cmd("AT+GMR", "", on_gmr, info, 2000U);
  (void)at_cmd("AT+PWR=0", NULL, NULL, NULL, 2000U);         /* pas d'économie d'énergie */

  s_restarted = false;
  info->ok = true;
  return AT_OK;
}

/* Voir ncp.h : simple relais vers at_poll(), qui lit les trames SPI */
void ncp_poll(void)
{
  at_poll();
}

/* Voir ncp.h : lit puis efface l'indicateur (vrai une seule fois) */
bool ncp_restarted(void)
{
  bool r = s_restarted;
  s_restarted = false;
  return r;
}

/* Voir ncp.h : préfixe "" = toutes les lignes de réponse sont transmises */
int ncp_raw(const char *cmd, at_line_cb_t cb, void *ctx, uint32_t timeout_ms)
{
  return at_cmd(cmd, "", cb, ctx, timeout_ms);
}

/* Voir ncp.h ; les autres codes sont confiés à wifi_err_str() */
const char *ncp_err_str(int err)
{
  switch (err)
  {
    case NCP_ERR_NOREADY:
      return "le module ne répond pas (alimentation, cavaliers JP1/JP2, firmware ?)";
    case NCP_ERR_ARCH:
      return "firmware T02 détecté : programmer le binaire mission T01";
    case NCP_FWU_ERR_REJECTED:
      return "image refusée par le module (contrôle d'intégrité ou version)";
    case NCP_FWU_ERR_STATE:
      return "mise à jour non démarrée ou image incomplète";
    default:
      return wifi_err_str(err);
  }
}
