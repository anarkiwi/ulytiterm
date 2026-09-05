/* C64 renderer: paints the vt cell buffer into video and colour RAM. */
#ifndef ULYTITERM_SCREEN_H
#define ULYTITERM_SCREEN_H

#include <stdint.h>

extern uint8_t scr_pan; /* leftmost visible column in 80 column mode */

/* Sets up the VIC and the RAM font. */
void scr_init(void);
void scr_done(void);
void scr_flush(void);
void scr_repaint(void);
void scr_border(uint8_t colour);

#endif
