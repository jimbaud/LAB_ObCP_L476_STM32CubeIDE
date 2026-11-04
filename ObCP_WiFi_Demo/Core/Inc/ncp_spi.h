/**
 ******************************************************************************
 * @file    ncp_spi.h
 * @brief   Transport SPI vers le coprocesseur réseau ST67W611M1.
 *
 *  Format d'une trame (identique au pilote officiel ST67W6X_Network_Driver,
 *  X-CUBE-ST67W61 v1.3.0) : en-tête de 8 octets suivi de la charge utile,
 *  longueur totale arrondie au multiple de 4 supérieur.
 *
 *     octet 0-1 : magic 0x55AA (petit-boutiste : AA 55)
 *     octet 2-3 : longueur de la charge utile (petit-boutiste)
 *     octet 4   : bits 0-1 version (=0), bit 2 rx_stall, bits 3-7 flags
 *     octet 5   : type de trafic (0 = commandes AT)
 *     octet 6-7 : réservé (=0)
 *
 *  Une transaction : CS=1, attente RDY=1, échange full-duplex des en-têtes
 *  (+ charges utiles), lecture du reliquat éventuel envoyé par le module,
 *  attente RDY=0 (acquittement), CS=0.
 *
 *  Pour un débutant : sur un bus SPI, chaque octet émis fait revenir un octet
 *  reçu (échange « full-duplex »). Le STM32 et le module s'envoient donc
 *  leurs en-têtes en même temps ; chacun y lit la longueur de ce que l'autre
 *  a à transmettre. « Petit-boutiste » (little-endian) : l'octet de poids
 *  faible d'un nombre de 16 bits est envoyé en premier.
 *  « rx_stall » : le module signale qu'il n'a plus de place pour recevoir ;
 *  la trame émise est alors renvoyée plus tard (ncp_spi_send() réessaie).
 ******************************************************************************
 */
#ifndef NCP_SPI_H
#define NCP_SPI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NCP_SPI_HDR_LEN        8U      /* taille de l'en-tête de trame       */
#define NCP_SPI_MTU            2048U   /* charge utile maximale acceptée     */
#define NCP_SPI_TX_MAX         1520U   /* charge utile maximale émise/trame  */
#define NCP_SPI_TYPE_AT        0U      /* type de trafic : commandes AT      */

/* Codes de retour des fonctions ncp_spi_xxx() */
#define NCP_SPI_OK             0
#define NCP_SPI_ERR_PARAM     (-1)     /* longueur nulle ou trop grande      */
#define NCP_SPI_ERR_TIMEOUT   (-2)     /* RDY n'est pas monté à temps        */
#define NCP_SPI_ERR_IO        (-3)     /* erreur du périphérique SPI         */
#define NCP_SPI_ERR_HDR       (-4)     /* en-tête reçu invalide (magic)      */
#define NCP_SPI_ERR_LEN       (-5)     /* trame reçue trop longue, ignorée   */
#define NCP_SPI_ERR_BUSY      (-6)     /* transaction déjà en cours          */

/**
 * @brief  Type de la fonction appelée pour chaque trame reçue du module
 *         (« callback », fournie par ncp_at.c à ncp_spi_init()).
 * @param  type  type de trafic (NCP_SPI_TYPE_AT).
 * @param  data  charge utile reçue (valide seulement pendant l'appel).
 * @param  len   nombre d'octets.
 */
typedef void (*ncp_spi_rx_cb_t)(uint8_t type, const uint8_t *data, uint16_t len);

/** Compteurs de la liaison SPI, affichés par la commande « stats ». */
typedef struct
{
  uint32_t tx_frames;        /* trames émises et acceptées par le module   */
  uint32_t tx_bytes;
  uint32_t rx_frames;        /* trames reçues du module                    */
  uint32_t rx_bytes;
  uint32_t hdr_errors;       /* en-têtes invalides ou trames trop longues  */
  uint32_t timeouts;         /* RDY absent                                 */
  uint32_t stalls;           /* trames refusées (module saturé)            */
  uint32_t io_errors;        /* erreurs du périphérique SPI                */
  uint16_t last_bad_magic;   /* dernier « magic » invalide reçu (0 si aucun) :
                                0x5501 = firmware du module trop ancien      */
} ncp_spi_stats_t;

/**
 * @brief  Initialise la couche SPI (compteurs à zéro, CS relâché).
 * @param  cb  fonction appelée pour chaque trame reçue.
 */
void  ncp_spi_init(ncp_spi_rx_cb_t cb);

/**
 * @brief  Émet une charge utile vers le module, en réessayant tant qu'il la
 *         refuse (rx_stall), 3 s au plus. Les données reçues pendant ces
 *         transactions sont transmises au callback.
 * @param  data  octets à émettre (une commande AT, des données TCP...).
 * @param  len   1 à NCP_SPI_TX_MAX octets.
 * @retval NCP_SPI_OK, ou NCP_SPI_ERR_xxx (valeur négative).
 */
int   ncp_spi_send(const uint8_t *data, uint16_t len);

/**
 * @brief  Si le module signale des données (RDY = 1), effectue une
 *         transaction de lecture. Non bloquante si RDY = 0.
 * @retval 1 si une transaction a eu lieu, 0 sinon, < 0 en cas d'erreur.
 */
int   ncp_spi_poll(void);

/** @brief  Compteurs de la liaison (commande « stats »). */
const ncp_spi_stats_t *ncp_spi_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* NCP_SPI_H */
