/* See screen.h.
 *
 * The character generator is copied to RAM so that the glyphs the C64 font
 * lacks (backslash, braces, tilde and the DEC graphics extras) can be added;
 * vt.c addresses them by screen code. Rows are painted only when dirty, and
 * scrolling is handed to the REU, which moves the visible image with DMA
 * instead of repainting every row.
 */
#include <string.h>

#include "mem.h"
#include "reu.h"
#include "screen.h"
#include "vt.h"

#define VIC_ADDR (*(volatile uint8_t *)0xd018)
#define CIA2_PRA (*(volatile uint8_t *)0xdd00)
#define VIC_BORDER (*(volatile uint8_t *)0xd020)
#define VIC_BG (*(volatile uint8_t *)0xd021)
#define CPU_PORT (*(volatile uint8_t *)0x0001)
#define JIFFY (*(volatile uint8_t *)0x00a2)
#define BLNSW (*(volatile uint8_t *)0x00cc)
#define SHFLAG (*(volatile uint8_t *)0x0291)
#define SCREEN MEM_SCREEN
#define COLRAM ((uint8_t *)0xd800)
#define CHARROM ((const uint8_t *)0xd800) /* lower case set, I/O banked out */
#define NMI_VECTOR (*(volatile uint16_t *)0x0318)
/* Screen codes $80 and up are the reversed glyphs, one kilobyte on. */
#define REVERSED 0x400

#define FONT_SIZE 2048

uint8_t scr_pan;

static uint8_t *srow[VT_ROWS];
static uint8_t *crow[VT_ROWS];
static uint8_t curdrawn = 0xff;
static uint8_t shflag;
static uint8_t colorpainted;
static uint8_t bellend;

/* Screen code followed by eight bitmap rows. */
static const uint8_t glyphs[] = {
    0x5c, 0x00, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x00, /* backslash */
    0x5e, 0x18, 0x20, 0x20, 0x40, 0x20, 0x20, 0x18, 0x00, /* { */
    0x5f, 0x30, 0x08, 0x08, 0x04, 0x08, 0x08, 0x30, 0x00, /* } */
    0x68, 0x00, 0x00, 0x32, 0x4c, 0x00, 0x00, 0x00, 0x00, /* ~ */
    0x69, 0x10, 0x28, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, /* ^ */
    0x6a, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ` */
    0x74, 0x00, 0x10, 0x38, 0x7c, 0x38, 0x10, 0x00, 0x00, /* diamond */
    0x75, 0x38, 0x28, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, /* degree */
    0x76, 0x00, 0x10, 0x10, 0x7c, 0x10, 0x10, 0x00, 0x7c, /* plus minus */
    0x77, 0x00, 0x08, 0x10, 0x20, 0x10, 0x08, 0x00, 0x7c, /* <= */
    0x78, 0x00, 0x20, 0x10, 0x08, 0x10, 0x20, 0x00, 0x7c, /* >= */
    0x79, 0x00, 0x04, 0x7c, 0x08, 0x7c, 0x10, 0x20, 0x00, /* != */
    0x7a, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, /* middle dot */
    0};

static uint8_t scr_scroll(uint8_t t, uint8_t b, int8_t n);
static void paint(uint8_t cursor);

/* The character ROM replaces I/O while it is being copied, so the KERNAL NMI
 * handler must not run: it reads CIA 2. RESTORE is edge triggered and needs no
 * acknowledgement, so ignoring it outright is enough. */
__attribute__((interrupt)) static void nmi_ignore(void) {}

/* Copies the character generator into RAM and adds the missing glyphs, in
 * their normal and reverse video forms. */
static void font_setup(void) {
  uint16_t nmi;
  uint8_t port;
  const uint8_t *g;

  __asm__ volatile("sei");
  nmi = NMI_VECTOR;
  NMI_VECTOR = (uint16_t)nmi_ignore;
  port = CPU_PORT;
  CPU_PORT = port & ~0x04;
  memcpy(MEM_FONT, CHARROM, FONT_SIZE);
  CPU_PORT = port;
  NMI_VECTOR = nmi;
  __asm__ volatile("cli");
  for (g = glyphs; *g; g += 9) {
    uint16_t off = (uint16_t)*g << 3;
    uint8_t i;
    for (i = 0; i < 8; i++) {
      MEM_FONT[off + i] = g[1 + i];
      MEM_FONT[off + REVERSED + i] = ~g[1 + i];
    }
  }
}

void scr_init(void) {
  uint8_t r;

  font_setup();
  for (r = 0; r < VT_ROWS; r++) {
    srow[r] = SCREEN + (uint16_t)r * VT_VIEW;
    crow[r] = COLRAM + (uint16_t)r * VT_VIEW;
  }
  VIC_BORDER = VT_DEFBG;
  VIC_BG = VT_DEFBG;
  CIA2_PRA = (CIA2_PRA & 0xfc) | (3 - MEM_VICBANK);
  VIC_ADDR =
      (((uint16_t)SCREEN >> 6) & 0xf0) | (((uint16_t)MEM_FONT >> 10) & 0x0e);
  BLNSW = 1; /* stop the KERNAL cursor */
  shflag = SHFLAG;
  SHFLAG = 0x80; /* stop shift+Commodore switching the character set */
  memset(SCREEN, 0x20, VT_ROWS * VT_VIEW);
  memset(COLRAM, VT_DEFFG, VT_ROWS * VT_VIEW);
  vt_onscroll = scr_scroll;
}

void scr_done(void) {
  vt_onscroll = 0;
  vt_onscrollout = 0;
  CIA2_PRA |= 0x03;
  VIC_ADDR = 0x15;
  VIC_BORDER = 14;
  VIC_BG = 6;
  BLNSW = 0;
  SHFLAG = shflag;
  memset((uint8_t *)0x0400, 0x20, VT_ROWS * VT_VIEW); /* the KERNAL screen */
  memset(COLRAM, 14, VT_ROWS * VT_VIEW);
}

void scr_border(uint8_t colour) { VIC_BORDER = colour; }

void scr_repaint(void) { memset(vt_dirty, 1, VT_ROWS); }

static void autopan(void) {
  uint8_t want = scr_pan;

  if (vt_cols <= VT_VIEW) {
    want = 0;
  } else if (vt_x < scr_pan) {
    want = vt_x & ~7;
  } else if (vt_x >= scr_pan + VT_VIEW) {
    want = (vt_x - VT_VIEW + 8) & ~7;
    if (want > vt_cols - VT_VIEW)
      want = vt_cols - VT_VIEW;
  }
  if (want != scr_pan) {
    scr_pan = want;
    scr_repaint();
  }
}

static void paint(uint8_t cursor) {
  uint8_t r;

  if (vt_colored && !colorpainted) {
    colorpainted = 1;
    scr_repaint();
  }
  if (curdrawn != 0xff) {
    vt_dirty[curdrawn] = 1;
    curdrawn = 0xff;
  }
  autopan();
  for (r = 0; r < VT_ROWS; r++) {
    const uint8_t *src;
    if (!vt_dirty[r])
      continue;
    vt_dirty[r] = 0;
    src = vt_chr[r] + scr_pan;
    if (vt_scnm) {
      uint8_t i;
      for (i = 0; i < VT_VIEW; i++)
        srow[r][i] = src[i] ^ 0x80;
    } else {
      memcpy(srow[r], src, VT_VIEW);
    }
    if (colorpainted)
      memcpy(crow[r], vt_att[r] + scr_pan, VT_VIEW);
  }
  if (cursor && vt_curvis && (JIFFY & 0x10)) {
    uint8_t x = vt_x - scr_pan;
    if (vt_x >= scr_pan && x < VT_VIEW) {
      srow[vt_y][x] ^= 0x80;
      curdrawn = vt_y;
    }
  }
}

/* vt.c hook: move the displayed image with the REU so that a scroll costs one
 * DMA pair instead of repainting every row. */
static uint8_t scr_scroll(uint8_t t, uint8_t b, int8_t n) {
  uint8_t cnt = n > 0 ? n : -n;
  uint16_t len = (uint16_t)(b - t + 1 - cnt) * VT_VIEW;

  if (!reu_banks || cnt >= b - t + 1)
    return 0;
  paint(0);
  if (n > 0) {
    reu_move(srow[t], srow[t + cnt], len);
    if (colorpainted)
      reu_move(crow[t], crow[t + cnt], len);
  } else {
    reu_move(srow[t + cnt], srow[t], len);
    if (colorpainted)
      reu_move(crow[t + cnt], crow[t], len);
  }
  return 1;
}

void scr_flush(void) {
  if (vt_bell) {
    vt_bell = 0;
    bellend = (JIFFY + 12) | 1;
    VIC_BORDER = 2;
  } else if (bellend && (uint8_t)(JIFFY - bellend) < 0x80) {
    bellend = 0;
    VIC_BORDER = VT_DEFBG;
  }
  paint(1);
}
