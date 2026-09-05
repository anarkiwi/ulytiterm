/* See kbd.h. The C64 keyboard has no backslash, braces, bar, backtick,
 * underscore or tilde, so they live on the Commodore key. */
#include "kbd.h"
#include "vt.h"

/* PETSCII key, byte sent. */
static const uint8_t simple[] = {0x14, 0x7f, /* DEL */
                                 0x5c, '\\', /* pound */
                                 0x5e, '^',  /* up arrow */
                                 0x5f, 0x1b, /* left arrow: ESC */
                                 0xde, '~',  /* shift + up arrow */
                                 0xbf, '{',  /* C= B */
                                 0xaa, '}',  /* C= N */
                                 0xaf, '|',  /* C= P */
                                 0xab, '`',  /* C= Q */
                                 0xb8, '_',  /* C= U */
                                 0xae, '\\', /* C= S */
                                 0xa3, '~',  /* C= T */
                                 0};

/* PETSCII key, final byte of an ESC O / ESC [ sequence. */
static const uint8_t cursor[] = {KBD_UP, 'A',      KBD_DOWN, 'B', KBD_RIGHT,
                                 'C',    KBD_LEFT, 'D',      0};

/* PETSCII key, VT function key (ESC O x). */
static const uint8_t fkey[] = {0x85, 'P', 0x89, 'Q', 0x86, 'R', 0x8a, 'S', 0};

static uint8_t seq(uint8_t *out, uint8_t mid, uint8_t last) {
  out[0] = 0x1b;
  out[1] = mid;
  out[2] = last;
  return 3;
}

static const uint8_t *find(const uint8_t *tab, uint8_t key) {
  while (*tab) {
    if (*tab == key)
      return tab + 1;
    tab += 2;
  }
  return 0;
}

uint8_t kbd_map(uint8_t key, uint8_t *out) {
  const uint8_t *p;

  if ((p = find(cursor, key)))
    return seq(out, vt_ckm ? 'O' : '[', *p);
  if ((p = find(fkey, key)))
    return seq(out, 'O', *p);
  if ((p = find(simple, key))) {
    out[0] = *p;
    return 1;
  }
  switch (key) {
  case KBD_RETURN:
    out[0] = '\r';
    if (!vt_lnm)
      return 1;
    out[1] = '\n';
    return 2;
  case 0x13: /* HOME */
  case 0x93: /* CLR */
    return seq(out, '[', 'H');
  case 0x94: /* INST */
    out[0] = 0x1b;
    out[1] = '[';
    out[2] = '2';
    out[3] = '~';
    return 4;
  case 0x8b: /* F6 */
    out[0] = 0x1b;
    out[1] = '[';
    out[2] = '1';
    out[3] = '7';
    out[4] = '~';
    return 5;
  }
  if (key <= 0x40) {
    out[0] = key; /* controls, digits and punctuation are already ASCII */
    return 1;
  }
  if (key >= 0x41 && key <= 0x5a) {
    out[0] = key + 0x20; /* unshifted letters are lower case */
    return 1;
  }
  if (key >= 0xc1 && key <= 0xda) {
    out[0] = key - 0x80; /* shifted letters are upper case */
    return 1;
  }
  if (key == 0x5b || key == 0x5d) {
    out[0] = key;
    return 1;
  }
  return 0;
}
