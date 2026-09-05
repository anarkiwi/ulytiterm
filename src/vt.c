/* See vt.h. */
#include <string.h>

#include "vt.h"

#define CELLS (VT_ROWS * VT_MAXCOLS)
#define BLANK 0x20 /* screen code for space */
#define DEFCOL 16  /* "use the default" sentinel for fg/bg */
#define MAXPAR 8

#ifdef ULYTITERM_HOST
static uint8_t cellmem[2 * CELLS];
#define CELLCHR cellmem
#define CELLATT (cellmem + CELLS)
#else
#include "mem.h"
#define CELLCHR MEM_CELLCHR
#define CELLATT MEM_CELLATT
#endif

uint8_t *vt_chr[VT_ROWS];
uint8_t *vt_att[VT_ROWS];
uint8_t vt_dirty[VT_ROWS];
uint8_t vt_cols = VT_VIEW;
uint8_t vt_x, vt_y;
uint8_t vt_curvis, vt_scnm, vt_ckm, vt_kam, vt_lnm, vt_colored, vt_bell;
uint8_t vt_reply[16];
uint8_t vt_replylen;
uint8_t (*vt_onscroll)(uint8_t t, uint8_t b, int8_t n);
void (*vt_onscrollout)(uint8_t row);

/* ANSI colour to C64 colour. */
static const uint8_t c64col[16] = {0,  2,  5,  8, 6,  4, 3, 15,
                                   11, 10, 13, 7, 14, 4, 3, 1};

/* ASCII to screen code exceptions; the rest is filled in by rule. Values in
 * 0x5c..0x7a name glyphs patched into the RAM font by scr_init(). */
static const uint8_t asc_exc[] = {
    0x40, 0x00, 0x5b, 0x1b, 0x5c, 0x5c, 0x5d, 0x1d, 0x5e, 0x69, 0x5f, 0x64,
    0x60, 0x6a, 0x7b, 0x5e, 0x7c, 0x5d, 0x7d, 0x5f, 0x7e, 0x68, 0};

/* DEC special graphics (0x5f..0x7e) to screen code. */
static const uint8_t gfx_exc[] = {
    0x5f, 0x20, 0x60, 0x74, 0x61, 0x66, 0x62, 0x20, 0x63, 0x20, 0x64,
    0x20, 0x65, 0x20, 0x66, 0x75, 0x67, 0x76, 0x68, 0x20, 0x69, 0x20,
    0x6a, 0x7d, 0x6b, 0x6e, 0x6c, 0x70, 0x6d, 0x6d, 0x6e, 0x5b, 0x6f,
    0x63, 0x70, 0x63, 0x71, 0x40, 0x72, 0x64, 0x73, 0x64, 0x74, 0x6b,
    0x75, 0x73, 0x76, 0x71, 0x77, 0x72, 0x78, 0x5d, 0x79, 0x77, 0x7a,
    0x78, 0x7b, 0x2a, 0x7c, 0x79, 0x7d, 0x1c, 0x7e, 0x7a, 0};

/* Approximations for the glyphs that only exist in the patched RAM font. */
static const uint8_t asc_rom[] = {0x5c, 0x2f, 0x5e, 0x1e, 0x60, 0x27, 0x7b,
                                  0x28, 0x7d, 0x29, 0x7e, 0x2d, 0};
static const uint8_t gfx_rom[] = {0x60, 0x2a, 0x66, 0x27, 0x67,
                                  0x2b, 0x79, 0x3c, 0x7a, 0x3e,
                                  0x7c, 0x3d, 0x7e, 0x2e, 0};

static uint8_t asctab[256], gfxtab[256];
static const uint8_t *lut;

static uint8_t top, bot;     /* scrolling region, inclusive */
static uint8_t att, rev;     /* colour nibble, 0x80 when reversed */
static uint8_t fgc, bgc;     /* ANSI colours or DEFCOL */
static uint8_t bold, sgrrev; /* SGR 1, SGR 7 */
static uint8_t awm, om, irm; /* DECAWM, DECOM, IRM */
static uint8_t wrapnext;     /* deferred wrap at the last column */
static uint8_t g0, g1, gl;   /* character set slots, 1 = DEC graphics */
static uint8_t tabs[VT_MAXCOLS / 8];
static uint8_t state, priv, inter, npar;
static uint16_t par[MAXPAR];
static uint8_t sx, sy, satt, srev, sfg, sbg, sbold, ssgrrev, sg0, sg1, sgl, som;

enum { S_GND, S_ESC, S_CSI, S_IGN, S_STR, S_STREND };

static void apply(uint8_t *tab, const uint8_t *exc) {
  while (*exc) {
    tab[exc[0]] = exc[1];
    exc += 2;
  }
}

static void set_attr(void) {
  uint8_t f = fgc, b = bgc, t;

  f = (f == DEFCOL) ? VT_DEFFG : c64col[(bold && f < 8) ? f + 8 : f];
  b = (b == DEFCOL) ? VT_DEFBG : c64col[b];
  if (sgrrev) {
    t = f;
    f = b;
    b = t;
  }
  if (b == VT_DEFBG) {
    rev = 0;
    att = f;
  } else {
    rev = 0x80;
    att = b;
  }
  if (att != VT_DEFFG || rev)
    vt_colored = 1;
}

static void mark(uint8_t a, uint8_t b) {
  while (a <= b)
    vt_dirty[a++] = 1;
}

static void clear_row(uint8_t r) {
  memset(vt_chr[r], BLANK | rev, vt_cols);
  memset(vt_att[r], att, vt_cols);
}

/* Rotates rows [t, b] up (n > 0) or down (n < 0) by |n|, blanking what
 * scrolls in. Only the row pointers move. */
static void scroll(uint8_t t, uint8_t b, int8_t n) {
  uint8_t h = b - t + 1;
  uint8_t up = n > 0;
  uint8_t cnt = up ? n : -n;
  uint8_t moved = vt_onscroll && vt_onscroll(t, b, n);
  uint8_t i;
  uint8_t *pc, *pa;

  if (up && !t && vt_onscrollout)
    for (i = 0; i < (cnt > h ? h : cnt); i++)
      vt_onscrollout(i);

  if (cnt >= h) {
    for (i = t; i <= b; i++)
      clear_row(i);
    mark(t, b);
    return;
  }
  for (i = 0; i < cnt; i++) {
    if (up) {
      pc = vt_chr[t];
      pa = vt_att[t];
      memmove(&vt_chr[t], &vt_chr[t + 1], (h - 1) * sizeof(pc));
      memmove(&vt_att[t], &vt_att[t + 1], (h - 1) * sizeof(pa));
      vt_chr[b] = pc;
      vt_att[b] = pa;
      clear_row(b);
    } else {
      pc = vt_chr[b];
      pa = vt_att[b];
      memmove(&vt_chr[t + 1], &vt_chr[t], (h - 1) * sizeof(pc));
      memmove(&vt_att[t + 1], &vt_att[t], (h - 1) * sizeof(pa));
      vt_chr[t] = pc;
      vt_att[t] = pa;
      clear_row(t);
    }
  }
  /* When the renderer moved the image, only the blanked rows are stale. */
  if (!moved)
    mark(t, b);
  else if (up)
    mark(b - cnt + 1, b);
  else
    mark(t, t + cnt - 1);
}

static void fill(uint8_t r, uint8_t from, uint8_t to) {
  if (to >= vt_cols)
    to = vt_cols - 1;
  if (from > to)
    return;
  memset(vt_chr[r] + from, BLANK | rev, to - from + 1);
  memset(vt_att[r] + from, att, to - from + 1);
  vt_dirty[r] = 1;
}

static void index_down(void) {
  if (vt_y == bot)
    scroll(top, bot, 1);
  else if (vt_y < VT_ROWS - 1)
    vt_y++;
}

static void reverse_index(void) {
  if (vt_y == top)
    scroll(top, bot, -1);
  else if (vt_y)
    vt_y--;
}

static void put_glyph(uint8_t c) {
  uint8_t *r;

  if (wrapnext) {
    vt_x = 0;
    index_down();
    wrapnext = 0;
  }
  if (irm && vt_x + 1 < vt_cols) {
    r = vt_chr[vt_y];
    memmove(r + vt_x + 1, r + vt_x, vt_cols - vt_x - 1);
    r = vt_att[vt_y];
    memmove(r + vt_x + 1, r + vt_x, vt_cols - vt_x - 1);
  }
  vt_chr[vt_y][vt_x] = lut[c] | rev;
  vt_att[vt_y][vt_x] = att;
  vt_dirty[vt_y] = 1;
  if (vt_x + 1 < vt_cols)
    vt_x++;
  else if (awm)
    wrapnext = 1;
}

static void reply(const char *s) {
  while (*s && vt_replylen < sizeof(vt_reply))
    vt_reply[vt_replylen++] = *s++;
}

static void reply_num(uint16_t n) {
  char b[6];
  uint8_t i = sizeof(b) - 1;

  b[i] = 0;
  do {
    b[--i] = '0' + n % 10;
    n /= 10;
  } while (n);
  reply(b + i);
}

static void set_charset(void) { lut = (gl ? g1 : g0) ? gfxtab : asctab; }

static void goto_xy(uint8_t x, uint8_t y) {
  if (om) {
    y += top;
    if (y > bot)
      y = bot;
  }
  vt_x = x < vt_cols ? x : vt_cols - 1;
  vt_y = y < VT_ROWS ? y : VT_ROWS - 1;
  wrapnext = 0;
}

static void save_cursor(void) {
  sx = vt_x;
  sy = vt_y;
  satt = att;
  srev = rev;
  sfg = fgc;
  sbg = bgc;
  sbold = bold;
  ssgrrev = sgrrev;
  sg0 = g0;
  sg1 = g1;
  sgl = gl;
  som = om;
}

static void restore_cursor(void) {
  att = satt;
  rev = srev;
  fgc = sfg;
  bgc = sbg;
  bold = sbold;
  sgrrev = ssgrrev;
  g0 = sg0;
  g1 = sg1;
  gl = sgl;
  om = som;
  set_charset();
  vt_x = sx < vt_cols ? sx : vt_cols - 1;
  vt_y = sy;
  wrapnext = 0;
}

static void tab_default(void) {
  memset(tabs, 0x01, sizeof(tabs)); /* every eighth column */
}

void vt_reset(void) {
  uint8_t i;

  fgc = bgc = DEFCOL;
  bold = sgrrev = 0;
  set_attr();
  vt_colored = 0;
  top = 0;
  bot = VT_ROWS - 1;
  vt_x = vt_y = 0;
  wrapnext = irm = om = vt_scnm = vt_ckm = vt_kam = vt_lnm = 0;
  awm = vt_curvis = 1;
  g0 = g1 = gl = 0;
  set_charset();
  tab_default();
  state = S_GND;
  vt_replylen = 0;
  for (i = 0; i < VT_ROWS; i++)
    clear_row(i);
  save_cursor();
  mark(0, VT_ROWS - 1);
}

void vt_setcols(uint8_t cols) {
  uint8_t i;

  vt_cols = cols;
  for (i = 0; i < VT_ROWS; i++) {
    vt_chr[i] = CELLCHR + (uint16_t)i * VT_MAXCOLS;
    vt_att[i] = CELLATT + (uint16_t)i * VT_MAXCOLS;
  }
  vt_reset();
}

void vt_init(uint8_t cols, uint8_t romfont) {
  uint16_t i;

  for (i = 0; i < 256; i++) {
    uint8_t c = i;
    asctab[i] = (c >= 0x20 && c < 0x40) || (c >= 0x41 && c <= 0x5a) ? c
                : (c >= 0x61 && c <= 0x7a)                          ? c - 0x60
                                                                    : BLANK;
  }
  apply(asctab, asc_exc);
  if (romfont)
    apply(asctab, asc_rom);
  memcpy(gfxtab, asctab, sizeof(gfxtab));
  apply(gfxtab, gfx_exc);
  if (romfont)
    apply(gfxtab, gfx_rom);
  vt_setcols(cols);
}

static uint16_t p1(uint8_t i) { return (i < npar && par[i]) ? par[i] : 1; }

static uint16_t p0(uint8_t i) { return i < npar ? par[i] : 0; }

static uint8_t map256(uint16_t n) {
  uint8_t r, g, b, v;

  if (n < 16)
    return n;
  if (n >= 232)
    return n < 244 ? 0 : (n < 250 ? 8 : 7);
  n -= 16;
  r = n / 36;
  g = (n / 6) % 6;
  b = n % 6;
  v = (r > 2) | ((g > 2) << 1) | ((b > 2) << 2);
  if (r > 3 || g > 3 || b > 3)
    v |= 8;
  return v;
}

static void sgr(void) {
  uint8_t i;

  if (!npar) {
    par[0] = 0;
    npar = 1;
  }
  for (i = 0; i < npar; i++) {
    uint16_t n = par[i];
    if (n == 0) {
      fgc = bgc = DEFCOL;
      bold = sgrrev = 0;
    } else if (n == 1) {
      bold = 1;
    } else if (n == 2 || n == 21 || n == 22) {
      bold = 0;
    } else if (n == 7) {
      sgrrev = 1;
    } else if (n == 27) {
      sgrrev = 0;
    } else if (n >= 30 && n <= 37) {
      fgc = n - 30;
    } else if (n >= 40 && n <= 47) {
      bgc = n - 40;
    } else if (n == 39) {
      fgc = DEFCOL;
    } else if (n == 49) {
      bgc = DEFCOL;
    } else if (n >= 90 && n <= 97) {
      fgc = n - 90 + 8;
    } else if (n >= 100 && n <= 107) {
      bgc = n - 100 + 8;
    } else if (n == 38 || n == 48) {
      uint8_t v = DEFCOL;
      if (p0(i + 1) == 5) {
        v = map256(p0(i + 2));
        i += 2;
      } else if (p0(i + 1) == 2) {
        v = ((p0(i + 2) > 127) | ((p0(i + 3) > 127) << 1) |
             ((p0(i + 4) > 127) << 2));
        i += 4;
      }
      if (n == 38)
        fgc = v;
      else
        bgc = v;
    }
  }
  set_attr();
}

static void set_mode(uint8_t on) {
  uint8_t i;

  for (i = 0; i < npar; i++) {
    uint16_t n = par[i];
    if (priv == '?') {
      switch (n) {
      case 1:
        vt_ckm = on;
        break;
      case 3:
        vt_setcols(on ? 80 : VT_VIEW);
        break;
      case 5:
        vt_scnm = on;
        mark(0, VT_ROWS - 1);
        break;
      case 6:
        om = on;
        goto_xy(0, 0);
        break;
      case 7:
        awm = on;
        break;
      case 25:
        vt_curvis = on;
        break;
      }
    } else {
      switch (n) {
      case 4:
        irm = on;
        break;
      case 20:
        vt_lnm = on;
        break;
      }
    }
  }
}

static void erase_display(void) {
  uint8_t m = p0(0), i;

  if (m == 0) {
    fill(vt_y, vt_x, vt_cols - 1);
    for (i = vt_y + 1; i < VT_ROWS; i++)
      fill(i, 0, vt_cols - 1);
  } else if (m == 1) {
    for (i = 0; i < vt_y; i++)
      fill(i, 0, vt_cols - 1);
    fill(vt_y, 0, vt_x);
  } else {
    for (i = 0; i < VT_ROWS; i++)
      fill(i, 0, vt_cols - 1);
  }
}

static void erase_line(void) {
  uint8_t m = p0(0);

  if (m == 0)
    fill(vt_y, vt_x, vt_cols - 1);
  else if (m == 1)
    fill(vt_y, 0, vt_x);
  else
    fill(vt_y, 0, vt_cols - 1);
}

static void del_chars(uint16_t n) {
  uint8_t keep;

  if (n > vt_cols - vt_x)
    n = vt_cols - vt_x;
  keep = vt_cols - vt_x - n;
  memmove(vt_chr[vt_y] + vt_x, vt_chr[vt_y] + vt_x + n, keep);
  memmove(vt_att[vt_y] + vt_x, vt_att[vt_y] + vt_x + n, keep);
  fill(vt_y, vt_cols - n, vt_cols - 1);
}

static void ins_chars(uint16_t n) {
  uint8_t keep;

  if (n > vt_cols - vt_x)
    n = vt_cols - vt_x;
  keep = vt_cols - vt_x - n;
  memmove(vt_chr[vt_y] + vt_x + n, vt_chr[vt_y] + vt_x, keep);
  memmove(vt_att[vt_y] + vt_x + n, vt_att[vt_y] + vt_x, keep);
  fill(vt_y, vt_x, vt_x + n - 1);
}

static void tab_fwd(uint16_t n) {
  while (n--) {
    do {
      if (vt_x + 1 >= vt_cols)
        return;
      vt_x++;
    } while (!(tabs[vt_x >> 3] & (1 << (vt_x & 7))));
  }
}

static void tab_back(uint16_t n) {
  while (n--) {
    do {
      if (!vt_x)
        return;
      vt_x--;
    } while (!(tabs[vt_x >> 3] & (1 << (vt_x & 7))));
  }
}

static void csi(uint8_t c) {
  uint16_t n = p1(0);

  switch (c) {
  case 'A': {
    uint8_t lim = vt_y >= top ? top : 0;
    vt_y = n > vt_y - lim ? lim : vt_y - n;
    wrapnext = 0;
    break;
  }
  case 'B':
  case 'e': {
    uint8_t lim = vt_y <= bot ? bot : VT_ROWS - 1;
    vt_y = n > lim - vt_y ? lim : vt_y + n;
    wrapnext = 0;
    break;
  }
  case 'C':
  case 'a':
    vt_x = vt_x + n >= vt_cols ? vt_cols - 1 : vt_x + n;
    wrapnext = 0;
    break;
  case 'D':
    vt_x = n > vt_x ? 0 : vt_x - n;
    wrapnext = 0;
    break;
  case 'E':
    vt_x = 0;
    vt_y = n > bot - vt_y ? bot : vt_y + n;
    wrapnext = 0;
    break;
  case 'F':
    vt_x = 0;
    vt_y = n > vt_y - top ? top : vt_y - n;
    wrapnext = 0;
    break;
  case 'G':
  case '`':
    goto_xy(n - 1, om ? vt_y - top : vt_y);
    break;
  case 'd':
    goto_xy(vt_x, n - 1);
    break;
  case 'H':
  case 'f':
    goto_xy(p1(1) - 1, n - 1);
    break;
  case 'I':
    tab_fwd(n);
    break;
  case 'Z':
    tab_back(n);
    break;
  case 'J':
    erase_display();
    break;
  case 'K':
    erase_line();
    break;
  case 'L':
    if (vt_y >= top && vt_y <= bot)
      scroll(vt_y, bot, -(int8_t)(n > VT_ROWS ? VT_ROWS : n));
    break;
  case 'M':
    if (vt_y >= top && vt_y <= bot)
      scroll(vt_y, bot, n > VT_ROWS ? VT_ROWS : n);
    break;
  case 'P':
    del_chars(n);
    break;
  case '@':
    ins_chars(n);
    break;
  case 'X':
    fill(vt_y, vt_x, n >= vt_cols - vt_x ? vt_cols - 1 : vt_x + n - 1);
    break;
  case 'S':
    scroll(top, bot, n > VT_ROWS ? VT_ROWS : n);
    break;
  case 'T':
    scroll(top, bot, -(int8_t)(n > VT_ROWS ? VT_ROWS : n));
    break;
  case 'g':
    if (p0(0) == 3)
      memset(tabs, 0, sizeof(tabs));
    else
      tabs[vt_x >> 3] &= ~(1 << (vt_x & 7));
    break;
  case 'h':
    set_mode(1);
    break;
  case 'l':
    set_mode(0);
    break;
  case 'm':
    sgr();
    break;
  case 'n':
    if (p0(0) == 5)
      reply("\033[0n");
    else if (p0(0) == 6) {
      reply("\033[");
      reply_num((om ? vt_y - top : vt_y) + 1);
      reply(";");
      reply_num(vt_x + 1);
      reply("R");
    }
    break;
  case 'c':
    reply("\033[?6c"); /* VT102 */
    break;
  case 'r': {
    uint8_t t = n - 1, b = npar > 1 && par[1] ? par[1] - 1 : VT_ROWS - 1;
    if (b >= VT_ROWS)
      b = VT_ROWS - 1;
    if (t < b) {
      top = t;
      bot = b;
      goto_xy(0, 0);
    }
    break;
  }
  case 's':
    save_cursor();
    break;
  case 'u':
    restore_cursor();
    break;
  case 'p':
    if (inter == '!')
      vt_reset();
    break;
  }
}

static void esc(uint8_t c) {
  switch (inter) {
  case '(':
    g0 = c == '0';
    set_charset();
    return;
  case ')':
    g1 = c == '0';
    set_charset();
    return;
  case '#':
    if (c == '8') { /* DECALN */
      uint8_t i;
      for (i = 0; i < VT_ROWS; i++) {
        memset(vt_chr[i], asctab['E'] | rev, vt_cols);
        memset(vt_att[i], att, vt_cols);
      }
      mark(0, VT_ROWS - 1);
    }
    return;
  }
  switch (c) {
  case '7':
    save_cursor();
    break;
  case '8':
    restore_cursor();
    break;
  case 'D':
    index_down();
    break;
  case 'E':
    vt_x = 0;
    index_down();
    break;
  case 'M':
    reverse_index();
    break;
  case 'H':
    tabs[vt_x >> 3] |= 1 << (vt_x & 7);
    break;
  case 'c':
    vt_reset();
    break;
  case '=':
    vt_kam = 1;
    break;
  case '>':
    vt_kam = 0;
    break;
  case 'Z':
    reply("\033[?6c");
    break;
  }
}

static void c0(uint8_t c) {
  switch (c) {
  case 0x07:
    vt_bell++;
    break;
  case 0x08:
    if (vt_x)
      vt_x--;
    wrapnext = 0;
    break;
  case 0x09:
    tab_fwd(1);
    break;
  case 0x0a:
  case 0x0b:
  case 0x0c:
    index_down();
    if (vt_lnm)
      vt_x = 0;
    wrapnext = 0;
    break;
  case 0x0d:
    vt_x = 0;
    wrapnext = 0;
    break;
  case 0x0e:
    gl = 1;
    set_charset();
    break;
  case 0x0f:
    gl = 0;
    set_charset();
    break;
  case 0x18:
  case 0x1a:
    state = S_GND;
    break;
  case 0x1b:
    state = S_ESC;
    inter = priv = npar = 0;
    break;
  }
}

static void step(uint8_t c) {
  switch (state) {
  case S_STR:
    if (c == 0x07)
      state = S_GND;
    else if (c == 0x1b)
      state = S_STREND;
    return;
  case S_STREND:
    state = c == 0x1b ? S_STREND : S_GND;
    return;
  }
  if (c < 0x20) {
    c0(c);
    return;
  }
  switch (state) {
  case S_GND:
    if (c != 0x7f)
      put_glyph(c);
    break;
  case S_ESC:
    if (c < 0x30)
      inter = c;
    else {
      if (c == '[') {
        state = S_CSI;
        npar = priv = inter = 0;
        memset(par, 0, sizeof(par));
        return;
      }
      if (c == ']' || c == 'P' || c == 'X' || c == '^' || c == '_') {
        state = S_STR;
        return;
      }
      esc(c);
      state = S_GND;
    }
    break;
  case S_CSI:
    if (c >= '0' && c <= '9') {
      if (!npar)
        npar = 1;
      par[npar - 1] = par[npar - 1] * 10 + (c - '0');
      if (par[npar - 1] > 9999)
        par[npar - 1] = 9999;
    } else if (c == ';') {
      if (!npar)
        npar = 1;
      if (npar < MAXPAR)
        npar++;
    } else if (c >= 0x3c && c <= 0x3f) {
      priv = c;
    } else if (c >= 0x20 && c <= 0x2f) {
      inter = c;
    } else if (c >= 0x40 && c <= 0x7e) {
      csi(c);
      state = S_GND;
    } else if (c != 0x7f) {
      state = S_IGN;
    }
    break;
  case S_IGN:
    if (c >= 0x40 && c <= 0x7e)
      state = S_GND;
    break;
  }
}

void vt_write(const uint8_t *p, uint16_t n) {
  while (n--) {
    uint8_t c = *p++;
    if (state == S_GND && c >= 0x20 && c != 0x7f)
      put_glyph(c);
    else
      step(c);
  }
}

uint8_t vt_screencode(uint8_t ascii) { return asctab[ascii]; }

void vt_puts(const char *s) {
  while (*s)
    vt_write((const uint8_t *)s++, 1);
}
