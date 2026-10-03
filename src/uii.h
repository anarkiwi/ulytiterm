/* Ultimate II+/Ultimate 64 command interface, network target.
 *
 * Clean room implementation from the vendor register API documentation
 * ("Ultimate-II Command Interface", Gideon Zweijtzer) and the "Ultimate
 * Networking Command Technical Reference" (Scott Hutter).
 */
#ifndef ULYTITERM_UII_H
#define ULYTITERM_UII_H

#include <stdint.h>

#define UII_ID 0xc9 /* identification register reset value */

/* Status of the last command, as an ASCII string ("00,OK" on success). */
extern char uii_status[64];

uint8_t uii_present(void);
uint8_t uii_ok(void);

/* Returns the network configuration, address then netmask then gateway, four
 * bytes each, or 0 on failure. */
#define UII_IPCONFIG_LEN 12
const uint8_t *uii_ipconfig(void);

/* Socket id (>= 0) or -1. */
int16_t uii_connect(const char *host, uint16_t port);
void uii_close(uint8_t sock);

/* Points *data at up to `want` received bytes (in a static buffer, valid
 * until the next call). Returns the count, 0 when nothing is pending, or
 * -1 when the connection is gone. */
int16_t uii_read(uint8_t sock, uint8_t **data, uint16_t want);
/* Interprets a read reply: the status text, then the reply bytes, a 16 bit
 * length and the payload. The firmware answers a read that times out on an
 * empty socket with status 02 (older firmware with 00 and a length of $ffff),
 * and one on a socket the peer closed with status 01 and a length of zero. */
int16_t uii_read_len(const char *status, const uint8_t *reply, uint16_t n);
int8_t uii_write(uint8_t sock, const uint8_t *data, uint16_t len);

#endif
