/* Émetteur YMODEM simulé (tests sur PC) : réagit octet par octet aux réponses
 * du récepteur ('C', ACK, NAK, CAN) et produit les blocs à lui transmettre.
 * Des erreurs peuvent être injectées (CRC faux, ACK perdu, annulation...). */
#ifndef YMODEM_SENDER_H
#define YMODEM_SENDER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef void (*ys_emit_t)(const uint8_t *data, size_t len, void *ctx);

typedef struct
{
  /* configuration (ys_init puis modification éventuelle) */
  const char    *name;
  const uint8_t *data;
  size_t         size;
  bool           use_1k;          /* blocs STX de 1024 o (sinon SOH de 128 o)     */
  bool           send_size;       /* taille annoncée dans le bloc 0               */
  bool           xmodem;          /* pas de bloc 0 (émetteur XMODEM)              */
  int            corrupt_block;   /* 1re émission de ce bloc avec un CRC faux     */
  int            drop_ack_block;  /* ACK de ce bloc « perdu » : bloc renvoyé      */
  int            cancel_at_block; /* CAN CAN à la place de ce bloc                */
  int            corrupt_header;  /* octet 0 de l'image remplacé (en-tête faux)   */
  long           announce;        /* taille annoncée si >= 0 (sinon la vraie)     */
  ys_emit_t      emit;
  void          *ctx;
  /* état */
  int            state;
  unsigned       blk;
  size_t         off;
  int            cans;
  int            naks;
  int            resent;
  int            blocks_sent;
  bool           corrupted_done;
  bool           dropped_done;
  bool           done;
  bool           finished_ok;
  bool           cancelled_by_receiver;
} ys_t;

void ys_init(ys_t *s, const char *name, const uint8_t *data, size_t size, ys_emit_t emit, void *ctx);
/* Octet émis par le récepteur */
void ys_on_byte(ys_t *s, uint8_t c);

uint16_t ys_crc16(const uint8_t *d, size_t n);

#endif
