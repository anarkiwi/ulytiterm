/* See acia.h. */
#include "acia.h"

#define ACIA ((volatile uint8_t *)0xde00)
#define REG_DATA 0
#define REG_STATUS 1 /* write: programmed reset */
#define REG_COMMAND 2
#define REG_CONTROL 3

#define ST_RDRF 0x08 /* receive data register full */
#define ST_TDRE 0x10 /* transmit data register empty */

/* 8N1 at the top rate of the baud generator, which SwiftLink and Turbo232
 * both double, and polled operation: DTR asserted, RTS low, no interrupts. */
#define CONTROL 0x1f
#define CONTROL_PROBE 0x18
#define COMMAND 0x0b

#define TX_SPIN 8000 /* ~1s at the slowest rate worth waiting for */

static uint8_t rxbuf[192];

/* The control register reads back what was written, which open bus does not.
 * Two values are tried, each read back after an intervening read of another
 * register, so that a bus which merely echoes the last value cannot pass. */
static uint8_t holds(uint8_t value) {
  ACIA[REG_CONTROL] = value;
  (void)ACIA[REG_STATUS];
  return ACIA[REG_CONTROL] == value;
}

uint8_t acia_probe(void) {
  ACIA[REG_STATUS] = 0;
  if (!holds(CONTROL) || !holds(CONTROL_PROBE))
    return 0;
  ACIA[REG_CONTROL] = CONTROL;
  ACIA[REG_COMMAND] = COMMAND;
  return 1;
}

static int8_t acia_write(const uint8_t *p, uint16_t n) {
  while (n--) {
    uint16_t spin = TX_SPIN;
    while (!(ACIA[REG_STATUS] & ST_TDRE))
      if (!--spin)
        return -1;
    ACIA[REG_DATA] = *p++;
  }
  return 0;
}

static int8_t put(const char *s) {
  const char *p = s;

  while (*p)
    p++;
  return acia_write((const uint8_t *)s, p - s);
}

static int8_t acia_open(const char *host, uint16_t port) {
  static char num[6];
  uint8_t i = sizeof(num) - 1;

  num[i] = 0;
  do {
    num[--i] = '0' + port % 10;
    port /= 10;
  } while (port);
  if (put("ATDT") || put(host) || put(":") || put(num + i) || put("\r"))
    return -1;
  return 0;
}

static void acia_close(void) {
  /* Drop DTR briefly: a modem hangs up, a null modem peer does not care. */
  ACIA[REG_COMMAND] = COMMAND & ~0x01;
  ACIA[REG_COMMAND] = COMMAND;
}

static int16_t acia_read(uint8_t **data, uint16_t want) {
  uint16_t n = 0;

  *data = rxbuf;
  if (want > sizeof(rxbuf))
    want = sizeof(rxbuf);
  while (n < want && (ACIA[REG_STATUS] & ST_RDRF))
    rxbuf[n++] = ACIA[REG_DATA];
  return n;
}

static const char *acia_status(void) { return "THE LINE DID NOT ACCEPT THE DIAL STRING"; }

const net_driver acia_driver = {"swiftlink", 0,          acia_open,
                                acia_close, acia_read,   acia_write,
                                acia_status};
