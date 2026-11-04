/* Émetteur YMODEM simulé (voir ymodem_sender.h). */
#include <stdio.h>
#include <string.h>
#include "ymodem_sender.h"

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18

enum
{
  YS_WAIT_C0 = 0,
  YS_WAIT_ACK0,
  YS_WAIT_C1,
  YS_WAIT_ACK,
  YS_WAIT_EOT1,
  YS_WAIT_EOT2,
  YS_WAIT_CEND,
  YS_WAIT_ACKEND,
  YS_DONE
};

uint16_t ys_crc16(const uint8_t *d, size_t n)
{
  uint16_t crc = 0;
  size_t   i;
  int      b;
  for (i = 0; i < n; i++)
  {
    crc ^= (uint16_t)(d[i] << 8);
    for (b = 0; b < 8; b++)
    {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

void ys_init(ys_t *s, const char *name, const uint8_t *data, size_t size, ys_emit_t emit, void *ctx)
{
  memset(s, 0, sizeof(*s));
  s->name = name;
  s->data = data;
  s->size = size;
  s->use_1k = true;
  s->send_size = true;
  s->corrupt_block = -1;
  s->drop_ack_block = -1;
  s->cancel_at_block = -1;
  s->corrupt_header = -1;
  s->announce = -1;
  s->emit = emit;
  s->ctx = ctx;
  s->state = YS_WAIT_C0;
}

static void send_packet(ys_t *s, unsigned blk, const uint8_t *payload, size_t n, bool big, bool bad_crc)
{
  uint8_t  pkt[1029];
  size_t   size = big ? 1024 : 128;
  uint16_t crc;

  pkt[0] = big ? STX : SOH;
  pkt[1] = (uint8_t)blk;
  pkt[2] = (uint8_t)(255 - (uint8_t)blk);
  memset(&pkt[3], (blk == 0) ? 0x00 : 0x1A, size);
  memcpy(&pkt[3], payload, n);
  crc = ys_crc16(&pkt[3], size);
  if (bad_crc)
  {
    crc ^= 0x5555;
  }
  pkt[3 + size] = (uint8_t)(crc >> 8);
  pkt[4 + size] = (uint8_t)crc;
  s->emit(pkt, size + 5, s->ctx);
  s->blocks_sent++;
}

static void send_block0(ys_t *s, bool empty)
{
  uint8_t b[128];
  memset(b, 0, sizeof(b));
  if (!empty)
  {
    int n = snprintf((char *)b, sizeof(b), "%s", s->name);
    if (s->send_size)
    {
      (void)snprintf((char *)&b[n + 1], sizeof(b) - (size_t)n - 1, "%lu 15012345670 100644",
                     (unsigned long)((s->announce >= 0) ? (size_t)s->announce : s->size));
    }
  }
  send_packet(s, 0, b, sizeof(b), false, false);
}

static void send_data(ys_t *s)
{
  size_t   bs = s->use_1k ? 1024 : 128;
  size_t   n = s->size - s->off;
  uint8_t  tmp[1024];
  bool     bad = false;

  if ((s->cancel_at_block >= 0) && ((int)s->blk == s->cancel_at_block))
  {
    const uint8_t c[2] = { CAN, CAN };
    s->emit(c, 2, s->ctx);
    s->done = true;
    s->state = YS_DONE;
    return;
  }
  if (n > bs)
  {
    n = bs;
  }
  memcpy(tmp, &s->data[s->off], n);
  if ((s->corrupt_header >= 0) && (s->off == 0))
  {
    tmp[0] = (uint8_t)s->corrupt_header;
  }
  if ((s->corrupt_block >= 0) && ((int)s->blk == s->corrupt_block) && !s->corrupted_done)
  {
    bad = true;
    s->corrupted_done = true;
  }
  send_packet(s, s->blk & 0xFF, tmp, n, s->use_1k, bad);
}

static void send_eot(ys_t *s)
{
  const uint8_t e = EOT;
  s->emit(&e, 1, s->ctx);
}

void ys_on_byte(ys_t *s, uint8_t c)
{
  if (s->done)
  {
    return;
  }
  if (c == CAN)
  {
    if (++s->cans >= 2)
    {
      s->cancelled_by_receiver = true;
      s->done = true;
      s->state = YS_DONE;
    }
    return;
  }
  s->cans = 0;

  switch (s->state)
  {
    case YS_WAIT_C0:
      if (c == 'C')
      {
        if (s->xmodem)
        {
          s->blk = 1;
          send_data(s);
          s->state = YS_WAIT_ACK;
        }
        else
        {
          send_block0(s, false);
          s->state = YS_WAIT_ACK0;
        }
      }
      break;
    case YS_WAIT_ACK0:
      if (c == ACK)
      {
        s->state = YS_WAIT_C1;
      }
      else if ((c == NAK) || (c == 'C'))
      {
        s->resent++;
        send_block0(s, false);
      }
      break;
    case YS_WAIT_C1:
      if (c == 'C')
      {
        s->blk = 1;
        s->off = 0;
        send_data(s);
        s->state = YS_WAIT_ACK;
      }
      break;
    case YS_WAIT_ACK:
      if (c == ACK)
      {
        if ((s->drop_ack_block >= 0) && ((int)s->blk == s->drop_ack_block) && !s->dropped_done)
        {
          s->dropped_done = true;          /* ACK « perdu » : le même bloc est renvoyé */
          s->resent++;
          send_data(s);
          break;
        }
        s->off += s->use_1k ? 1024 : 128;
        s->blk++;
        if (s->off >= s->size)
        {
          send_eot(s);
          s->state = YS_WAIT_EOT1;
        }
        else
        {
          send_data(s);
        }
      }
      else if (c == NAK)
      {
        s->naks++;
        s->resent++;
        send_data(s);
      }
      break;
    case YS_WAIT_EOT1:
      if (c == NAK)
      {
        send_eot(s);
        s->state = YS_WAIT_EOT2;
      }
      else if (c == ACK)
      {
        s->state = YS_WAIT_CEND;
      }
      break;
    case YS_WAIT_EOT2:
      if (c == ACK)
      {
        s->state = YS_WAIT_CEND;
      }
      break;
    case YS_WAIT_CEND:
      if (c == 'C')
      {
        send_block0(s, true);
        s->state = YS_WAIT_ACKEND;
      }
      break;
    case YS_WAIT_ACKEND:
      if (c == ACK)
      {
        s->finished_ok = true;
        s->done = true;
        s->state = YS_DONE;
      }
      break;
    default:
      break;
  }
}
