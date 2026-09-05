/* Telnet NVT option negotiation, RFC 854 and friends. */
#ifndef ULYTITERM_TELNET_H
#define ULYTITERM_TELNET_H

#include <stdint.h>

extern uint8_t tn_active;    /* negotiation seen, or forced on */
extern uint8_t tn_binary_tx; /* peer agreed to 8 bit output */

/* send is used for negotiation replies. mode: 0 raw, 1 telnet, 2 auto
 * (telnet once an IAC arrives). */
void tn_init(void (*send)(const uint8_t *, uint16_t), uint8_t mode,
             uint8_t cols);
/* Tells the peer about a new window width. */
void tn_resize(uint8_t cols);
/* Strips commands in place and returns the length of the remaining data. */
uint16_t tn_filter(uint8_t *buf, uint16_t n);
/* Escapes IAC and completes bare CR, returns the encoded length. */
uint16_t tn_encode(const uint8_t *in, uint16_t n, uint8_t *out, uint16_t max);

#endif
