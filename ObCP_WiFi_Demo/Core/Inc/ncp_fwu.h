/**
 ******************************************************************************
 * @file    ncp_fwu.h
 * @brief   Mise à jour du firmware du ST67W611M1 par le lien SPI (« FWU »,
 *          firmware update, appelée aussi OTA) : l'image est transmise au
 *          module par blocs, le module la vérifie puis redémarre dessus.
 *
 *  Séquence reprise du pilote officiel ST67W6X_Network_Driver (W6X_FWU_*,
 *  exemple ST67W6X_FOTA de X-CUBE-ST67W61) :
 *    AT+OTASTART=0 puis AT+OTASTART=1      ouverture d'une session propre
 *    AT+OTASEND=512   + en-tête de 512 o   (en une seule fois, exigé par le module)
 *    AT+OTASEND=<n>   + données            (blocs multiples de 256 o conseillés)
 *    AT+OTAFIN                              vérification, puis redémarrage
 *                                           sur le nouveau firmware (« ready »)
 *  AT+OTASEND suit le schéma de AT+CIPSEND : OK, invite '>', données,
 *  « Recv n bytes ».
 *
 *  Fichiers acceptés : st67w611m_mission_t01_vX.Y.Z.bin.ota ou .xz.ota
 *  (dossier Projects/ST67W6X_Scripts/Binaries/NCP_Binaries de X-CUBE-ST67W61).
 *  En-tête de 512 octets :
 *    0..15   "QCC74X_OTAVer1.0"
 *    16..19  type d'image "RAW " ou "XZ  " (compressée)
 *    20..23  longueur de l'image qui suit l'en-tête (petit-boutiste)
 *    32..47  nom du module ("QCC_Module_v1.1")
 *    48..63  version ("2.0.106")
 *    64..95  SHA-256 de l'image (vérifiée par le module)
 *    96..511 0xFF
 *  L'en-tête ne distingue pas les firmwares T01, T02 et « mfg » : seul le
 *  nom du fichier le fait (contrôle effectué par l'application).
 ******************************************************************************
 */
#ifndef NCP_FWU_H
#define NCP_FWU_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NCP_FWU_HEADER_SIZE     512U                      /* en-tête d'un fichier .ota */
#define NCP_FWU_MAX_IMAGE       (4UL * 1024UL * 1024UL)   /* contrôle de vraisemblance */

#define NCP_FWU_ERR_REJECTED    (-42)   /* image refusée par le module (AT+OTAFIN)   */
#define NCP_FWU_ERR_STATE       (-43)   /* séquence incorrecte / image trop courte   */

/** Contenu décodé de l'en-tête d'un fichier .ota. */
typedef struct
{
  char     type[5];        /* "RAW" ou "XZ"               */
  uint32_t length;         /* octets après l'en-tête      */
  char     module[17];
  char     version[17];
  uint8_t  sha256[32];
} ncp_fwu_header_t;

/**
 * @brief  Analyse l'en-tête d'une image .ota.
 * @param  h    début du fichier.
 * @param  len  octets disponibles (96 au moins).
 * @param  out  reçoit le type, la longueur, le module, la version, le SHA-256.
 * @retval false si ce n'est pas un en-tête .ota valide.
 */
bool     ncp_fwu_parse_header(const uint8_t *h, size_t len, ncp_fwu_header_t *out);

/** @brief  Ouvre une session de mise à jour (AT+OTASTART=0 puis =1). */
int      ncp_fwu_begin(void);

/**
 * @brief  Transmet une partie de l'image (en-tête compris), dans l'ordre du
 *         fichier, au fur et à mesure de sa réception. Les données sont
 *         regroupées : 512 octets pour l'en-tête, puis blocs de 2048 octets.
 */
int      ncp_fwu_write(const uint8_t *data, uint32_t len);

/** @brief  Transmet le dernier bloc, incomplet, en fin de fichier. */
int      ncp_fwu_end(void);

/** @brief  Abandonne la session (AT+OTASTART=0) ; le firmware en place est conservé. */
int      ncp_fwu_abort(void);

/**
 * @brief  Demande l'installation (AT+OTAFIN) et attend le redémarrage du
 *         module sur le nouveau firmware (« ready »).
 * @param  ready_timeout_ms  attente maximale du redémarrage.
 * @retval AT_OK, NCP_FWU_ERR_REJECTED si le module refuse l'image (empreinte
 *         SHA-256 fausse...), AT_TIMEOUT s'il ne redémarre pas.
 */
int      ncp_fwu_finish(uint32_t ready_timeout_ms);

/** @brief  Octets effectivement transmis au module depuis ncp_fwu_begin(). */
uint32_t ncp_fwu_sent(void);

/** @brief  Vrai si une session de mise à jour est ouverte. */
bool     ncp_fwu_active(void);

#ifdef __cplusplus
}
#endif

#endif /* NCP_FWU_H */
