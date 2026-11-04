/**
 ******************************************************************************
 * @file    ymodem.h
 * @brief   Réception d'un fichier par le protocole YMODEM (CRC 16 bits, blocs
 *          de 128 ou 1024 octets), indépendante du matériel : les octets
 *          passent par les fonctions getc / putc fournies par l'appelant.
 *
 *  Déroulement côté récepteur :
 *    récepteur -> 'C'                     répété toutes les 2 s jusqu'au début
 *    émetteur  -> bloc 0 « nom\0taille »  récepteur -> ACK puis 'C'
 *    émetteur  -> blocs 1, 2, ...          récepteur -> ACK (NAK : à renvoyer)
 *    émetteur  -> EOT                      récepteur -> NAK
 *    émetteur  -> EOT                      récepteur -> ACK puis 'C'
 *    émetteur  -> bloc 0 vide (fin du lot) récepteur -> ACK
 *  Bloc : SOH (128 o) ou STX (1024 o), n, 255-n, données, CRC-16/XMODEM
 *  (polynôme 0x1021, poids fort en premier). Annulation : CAN CAN.
 *
 *  Logiciels émetteurs : Tera Term (Fichier > Transfert > YMODEM > Envoyer,
 *  en anglais File > Transfer > YMODEM > Send), ExtraPuTTY, sz --ymodem (lrzsz),
 *  Tools/maj_module_ota.py.
 ******************************************************************************
 */
#ifndef YMODEM_H
#define YMODEM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Résultat d'une réception YMODEM. */
typedef enum
{
  YM_OK                =  0,
  YM_ERR_NOSTART       = -1,   /* aucun émetteur dans le délai             */
  YM_ERR_TIMEOUT       = -2,   /* l'émetteur ne répond plus                */
  YM_ERR_REMOTE_CANCEL = -3,   /* CAN CAN reçus                            */
  YM_ERR_USER_CANCEL   = -4,   /* touche Ctrl-C, Échap ou q avant le début */
  YM_ERR_PROTOCOL      = -5,   /* trop d'erreurs, bloc hors séquence       */
  YM_ERR_SINK          = -6,   /* fichier ou données refusés par l'appelant */
  YM_ERR_SIZE          = -7,   /* taille reçue différente de l'annonce     */
  YM_ERR_NOTYMODEM     = -8,   /* émetteur XMODEM (pas de bloc 0)          */
  YM_ERR_NOFILE        = -9    /* lot vide                                 */
} ymodem_status_t;

/** Paramètres de la réception : fonctions d'entrée / sortie et destinataire
 *  des données (pointeurs de fonctions fournis par l'appelant, module_ota.c). */
typedef struct
{
  /* Entrées / sorties */
  int      (*getc)(uint32_t timeout_ms, void *io);  /* octet, ou -1 après timeout_ms */
  void     (*putc)(uint8_t c, void *io);
  uint32_t (*tick)(void *io);                       /* temps écoulé en ms             */
  void      *io;
  /* Destinataire du fichier : 0 = accepté, autre valeur = refus (annulation) */
  int      (*on_file)(const char *name, uint32_t size, void *ctx);
  int      (*on_data)(const uint8_t *data, uint32_t len, void *ctx);
  void      *ctx;
  uint32_t   start_timeout_ms;                      /* attente du début du transfert  */
} ymodem_cfg_t;

/** Bilan de la réception. */
typedef struct
{
  char     name[64];
  uint32_t size;          /* taille annoncée dans le bloc 0 (0 si absente) */
  uint32_t received;      /* octets transmis à on_data                     */
  uint32_t blocks;
  uint32_t retries;       /* blocs redemandés ou répétés                   */
} ymodem_result_t;

/**
 * @brief  Reçoit un fichier (bloquant) : chaque bloc reçu sans erreur est
 *         transmis à cfg->on_data, dans l'ordre.
 * @param  cfg  entrées / sorties et destinataire.
 * @param  res  reçoit le nom, la taille et les compteurs.
 * @retval YM_OK ou un code YM_ERR_xxx (négatif).
 */
int         ymodem_receive(const ymodem_cfg_t *cfg, ymodem_result_t *res);

/** @brief  CRC-16/XMODEM (polynôme 0x1021) d'un bloc. */
uint16_t    ymodem_crc16(const uint8_t *data, uint32_t len);

/** @brief  Texte en français d'un code ymodem_status_t. */
const char *ymodem_status_str(int st);

#ifdef __cplusplus
}
#endif

#endif /* YMODEM_H */
