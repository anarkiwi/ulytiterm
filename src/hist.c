/* See hist.h. Every line that scrolls off the top is stashed in expansion RAM
 * with one DMA transfer per row; browsing fetches rows straight back into the
 * cell buffer after saving the live screen.
 *
 * Expansion RAM map: reu_move() scratch at 0, the saved live screen at
 * $1000, then the history ring.
 */
#include <cbm.h>
#include <string.h>

#include "hist.h"
#include "kbd.h"
#include "reu.h"
#include "screen.h"
#include "vt.h"

#define SAVE_BASE 0x1000ul
#define HIST_BASE 0x2000ul
#define LINE 160 /* VT_MAXCOLS characters plus VT_MAXCOLS colours */
#define HIST_LIMIT 8000

uint16_t hist_max;
uint16_t hist_count;

static uint16_t head;

static reu_addr slot(uint16_t line) {
  return HIST_BASE + (reu_addr)line * LINE;
}

static void hist_push(uint8_t row) {
  reu_stash(slot(head), vt_chr[row], VT_MAXCOLS);
  reu_stash(slot(head) + VT_MAXCOLS, vt_att[row], VT_MAXCOLS);
  if (++head == hist_max)
    head = 0;
  if (hist_count < hist_max)
    hist_count++;
}

void hist_init(void) {
  uint32_t bytes;

  hist_count = head = hist_max = 0;
  if (!reu_banks)
    return;
  bytes = ((uint32_t)reu_banks << 16) - HIST_BASE;
  bytes /= LINE;
  hist_max = bytes > HIST_LIMIT ? HIST_LIMIT : bytes;
  vt_onscrollout = hist_push;
}

static void save_screen(void) {
  uint8_t r;

  for (r = 0; r < VT_ROWS; r++) {
    reu_stash(SAVE_BASE + (reu_addr)r * LINE, vt_chr[r], VT_MAXCOLS);
    reu_stash(SAVE_BASE + (reu_addr)r * LINE + VT_MAXCOLS, vt_att[r],
              VT_MAXCOLS);
  }
}

static void fetch_row(uint8_t r, reu_addr a) {
  reu_fetch(vt_chr[r], a, VT_MAXCOLS);
  reu_fetch(vt_att[r], a + VT_MAXCOLS, VT_MAXCOLS);
}

static void status(uint16_t off) {
  static const char *const help = "scrollback  crsr/f5=exit";
  uint8_t i;
  uint16_t n = hist_count - off;

  for (i = 0; i < vt_cols; i++) {
    vt_chr[VT_ROWS - 1][i] = 0x20 | 0x80;
    vt_att[VT_ROWS - 1][i] = 1;
  }
  for (i = 0; help[i] && i < vt_cols; i++)
    vt_chr[VT_ROWS - 1][i] = vt_screencode(help[i]) | 0x80;
  i = vt_cols - 6;
  do {
    vt_chr[VT_ROWS - 1][i--] = vt_screencode('0' + n % 10) | 0x80;
    n /= 10;
  } while (n && i);
}

static void render(uint16_t off) {
  uint8_t r;

  for (r = 0; r < VT_ROWS; r++) {
    uint16_t line = hist_count - off + r;
    if (line < hist_count) {
      uint16_t idx = head + hist_max - (hist_count - line);
      fetch_row(r, slot(idx >= hist_max ? idx - hist_max : idx));
    } else {
      fetch_row(r, SAVE_BASE + (reu_addr)(line - hist_count) * LINE);
    }
  }
  status(off);
  scr_repaint();
}

void hist_view(void) {
  uint16_t off = 1;
  uint8_t curvis = vt_curvis;
  uint8_t r;

  if (!hist_count)
    return;
  save_screen();
  vt_curvis = 0;
  for (;;) {
    uint8_t k;
    if (off > hist_count)
      off = hist_count;
    render(off);
    scr_flush();
    do {
      k = cbm_k_getin();
    } while (!k);
    if (k == KBD_UP)
      off++;
    else if (k == KBD_DOWN)
      off = off ? off - 1 : 0;
    else if (k == KBD_LEFT)
      off += VT_ROWS - 1;
    else if (k == KBD_RIGHT)
      off = off > VT_ROWS - 1 ? off - (VT_ROWS - 1) : 0;
    else if (k == 0x13)
      off = hist_count;
    else if (k == 0x93)
      off = 0;
    else
      break;
    if (!off)
      break;
  }
  for (r = 0; r < VT_ROWS; r++)
    fetch_row(r, SAVE_BASE + (reu_addr)r * LINE);
  vt_curvis = curvis;
  scr_repaint();
  scr_flush();
}
