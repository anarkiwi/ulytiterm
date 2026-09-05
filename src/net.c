/* See net.h. The Ultimate command interface is preferred when it answers,
 * since it is a real TCP stack; an ACIA is a modem line and has to be
 * dialled. */
#include "net.h"
#include "acia.h"
#include "uii.h"

const net_driver *net;

static int16_t sock = -1;

static int8_t uii_open(const char *host, uint16_t port) {
  sock = uii_connect(host, port);
  return sock < 0 ? -1 : 0;
}

static void uii_shut(void) {
  if (sock >= 0)
    uii_close(sock);
  sock = -1;
}

static int16_t uii_rx(uint8_t **data, uint16_t want) {
  return uii_read(sock, data, want);
}

static int8_t uii_tx(const uint8_t *data, uint16_t len) {
  return uii_write(sock, data, len);
}

static const char *uii_last(void) { return uii_status; }

static const net_driver uii_driver = {"ultimate", uii_ipconfig, uii_open,
                                      uii_shut,   uii_rx,       uii_tx,
                                      uii_last};

uint8_t net_init(void) {
  if (uii_present())
    net = &uii_driver;
  else if (acia_probe())
    net = &acia_driver;
  else
    return 0;
  return 1;
}
