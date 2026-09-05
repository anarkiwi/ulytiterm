/* Network transport, either the Ultimate cartridge or a 6551 ACIA. */
#ifndef ULYTITERM_NET_H
#define ULYTITERM_NET_H

#include <stdint.h>

typedef struct {
  const char *name;
  /* Network configuration, or 0 when the transport has none of its own. */
  const uint8_t *(*ipconfig)(void);
  int8_t (*open)(const char *host, uint16_t port);
  void (*close)(void);
  /* Points *data at received bytes, valid until the next call. Returns the
   * count, 0 when nothing is pending, or -1 when the connection is gone. */
  int16_t (*read)(uint8_t **data, uint16_t want);
  int8_t (*write)(const uint8_t *data, uint16_t len);
  /* Human readable detail for the last failure. */
  const char *(*status)(void);
} net_driver;

extern const net_driver *net;

/* Picks a transport. Returns 0 when there is none. */
uint8_t net_init(void);

#endif
