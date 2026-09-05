/* See reu.h. Transfers run with $ff00 decoding disabled, so writing the
 * command register starts them immediately. */
#include <string.h>

#include "reu.h"

#define REU_STATUS (*(volatile uint8_t *)0xdf00)
#define REU_CMD (*(volatile uint8_t *)0xdf01)
#define REU_C64 (*(volatile uint16_t *)0xdf02)
#define REU_LO (*(volatile uint8_t *)0xdf04)
#define REU_HI (*(volatile uint8_t *)0xdf05)
#define REU_BANK (*(volatile uint8_t *)0xdf06)
#define REU_LEN (*(volatile uint16_t *)0xdf07)
#define REU_MASK (*(volatile uint8_t *)0xdf09)
#define REU_CTRL (*(volatile uint8_t *)0xdf0a)

#define CMD_EXEC 0x90 /* execute, $ff00 decode disabled */
#define CMD_STASH 0x00
#define CMD_FETCH 0x01

/* reu_move() scratch; see hist.c for the rest of the expansion RAM map. */
#define MOVE_SCRATCH 0x000000ul

uint16_t reu_banks;

static void xfer(uint8_t cmd, reu_addr ra, const void *ca, uint16_t len) {
  REU_C64 = (uint16_t)ca;
  REU_LO = ra;
  REU_HI = ra >> 8;
  REU_BANK = ra >> 16;
  REU_LEN = len;
  REU_MASK = 0;
  REU_CTRL = 0;
  REU_CMD = CMD_EXEC | cmd;
}

void reu_stash(reu_addr dst, const void *src, uint16_t len) {
  xfer(CMD_STASH, dst, src, len);
}

void reu_fetch(void *dst, reu_addr src, uint16_t len) {
  xfer(CMD_FETCH, src, dst, len);
}

void reu_move(void *dst, const void *src, uint16_t len) {
  if (!len)
    return;
  if (!reu_banks) {
    memmove(dst, src, len);
    return;
  }
  xfer(CMD_STASH, MOVE_SCRATCH, src, len);
  xfer(CMD_FETCH, MOVE_SCRATCH, dst, len);
}

/* Probes for a REU and measures it. Banks alias modulo the real size, so the
 * smallest power of two bank that overwrites bank 0 is the bank count. */
uint8_t reu_init(void) {
  static uint8_t buf[4];
  static const uint8_t magic[4] = {0x55, 0xaa, 0x5a, 0xa5};
  uint16_t bank;

  reu_banks = 0;
  memcpy(buf, magic, sizeof(buf));
  xfer(CMD_STASH, 0, buf, sizeof(buf));
  memset(buf, 0, sizeof(buf));
  xfer(CMD_FETCH, 0, buf, sizeof(buf));
  if (memcmp(buf, magic, sizeof(buf)))
    return 0;

  for (bank = 1; bank < 256; bank <<= 1) {
    uint8_t i;
    for (i = 0; i < sizeof(buf); i++)
      buf[i] = ~magic[i];
    xfer(CMD_STASH, (reu_addr)bank << 16, buf, sizeof(buf));
    xfer(CMD_FETCH, 0, buf, sizeof(buf));
    if (memcmp(buf, magic, sizeof(buf)))
      break;
  }
  reu_banks = bank;
  return 1;
}
