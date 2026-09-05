/* C64 keyboard (PETSCII) to VT102 key sequences. */
#ifndef ULYTITERM_KBD_H
#define ULYTITERM_KBD_H

#include <stdint.h>

#define KBD_F5 0x87
#define KBD_F7 0x88
#define KBD_F8 0x8c
#define KBD_STOP 0x03
#define KBD_UP 0x91
#define KBD_DOWN 0x11
#define KBD_LEFT 0x9d
#define KBD_RIGHT 0x1d
#define KBD_RETURN 0x0d

/* Writes the bytes a key sends to the host into out (at least 8 bytes) and
 * returns the count, or 0 if the key sends nothing. */
uint8_t kbd_map(uint8_t key, uint8_t *out);

#endif
