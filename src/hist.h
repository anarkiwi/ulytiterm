/* Scrollback held in expansion RAM. */
#ifndef ULYTITERM_HIST_H
#define ULYTITERM_HIST_H

#include <stdint.h>

extern uint16_t hist_max;   /* lines the REU can hold, 0 without one */
extern uint16_t hist_count; /* lines currently held */

void hist_init(void);
void hist_view(void); /* browse, returns when the user leaves */

#endif
