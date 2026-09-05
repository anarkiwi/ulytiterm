/* VT102/ANSI terminal emulation over a C64 screen code cell buffer.
 *
 * Cells hold C64 screen codes with bit 7 set for reverse video, and a colour
 * nibble per cell, so rendering a row is a straight copy into video and
 * colour RAM. Rows are indirected through a pointer table: scrolling rotates
 * pointers instead of moving character data.
 *
 * This module is hardware independent and is unit tested on the host.
 */
#ifndef ULYTITERM_VT_H
#define ULYTITERM_VT_H

#include <stdint.h>

#define VT_ROWS 25
#define VT_MAXCOLS 80
#define VT_VIEW 40 /* visible columns */

#define VT_DEFFG 15 /* light grey */
#define VT_DEFBG 0  /* black */

extern uint8_t *vt_chr[VT_ROWS]; /* screen codes, bit 7 = reverse */
extern uint8_t *vt_att[VT_ROWS]; /* C64 colour per cell */
extern uint8_t vt_dirty[VT_ROWS];

extern uint8_t vt_cols;    /* 40 or 80 */
extern uint8_t vt_x, vt_y; /* cursor, 0 based */
extern uint8_t vt_curvis;  /* DECTCEM */
extern uint8_t vt_scnm;    /* DECSCNM, reverse screen */
extern uint8_t vt_ckm;     /* DECCKM, application cursor keys */
extern uint8_t vt_kam;     /* DECKPAM, application keypad */
extern uint8_t vt_lnm;     /* LNM, return sends CR LF */
extern uint8_t vt_colored; /* a non default colour has been used */
extern uint8_t vt_bell;    /* pending bells, cleared by the renderer */

/* Terminal to host replies (device attributes, cursor position reports). */
extern uint8_t vt_reply[16];
extern uint8_t vt_replylen;

/* Renderer hooks. vt_onscroll lets the renderer move the displayed image
 * itself (REU DMA) and returns non zero when it did, which saves repainting
 * every row. vt_onscrollout sees each row as it leaves the top of the screen,
 * for the scrollback history. */
extern uint8_t (*vt_onscroll)(uint8_t t, uint8_t b, int8_t n);
extern void (*vt_onscrollout)(uint8_t row);

void vt_init(uint8_t cols);
void vt_reset(void);
void vt_setcols(uint8_t cols);
void vt_write(const uint8_t *p, uint16_t n);
void vt_puts(const char *s);
uint8_t vt_screencode(uint8_t ascii);

#endif
