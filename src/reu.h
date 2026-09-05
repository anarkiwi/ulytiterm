/* RAM Expansion Controller (1750/Ultimate REU) DMA.
 *
 * The Ultimate II+ and Ultimate 64 provide a REU alongside the command
 * interface, so a terminal that already requires the cartridge can use DMA
 * for screen movement and keep a large scrollback in expansion RAM.
 */
#ifndef ULYTITERM_REU_H
#define ULYTITERM_REU_H

#include <stdint.h>

typedef uint32_t reu_addr;

/* Expansion RAM in 64k banks, 0 when no REU is present. */
extern uint16_t reu_banks;

uint8_t reu_init(void);
void reu_stash(reu_addr dst, const void *src, uint16_t len);
void reu_fetch(void *dst, reu_addr src, uint16_t len);
/* C64 to C64 block move through expansion RAM, at DMA speed. */
void reu_move(void *dst, const void *src, uint16_t len);

#endif
