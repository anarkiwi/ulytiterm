/* 6551 ACIA (SwiftLink or Turbo232) at $de00, polled.
 *
 * The line is expected to reach a modem, so opening dials it. This is what
 * the WiFi modems in common use want, and what VICE emulates when its ACIA
 * is pointed at a TCP endpoint. */
#ifndef ULYTITERM_ACIA_H
#define ULYTITERM_ACIA_H

#include "net.h"

uint8_t acia_probe(void);
extern const net_driver acia_driver;

#endif
