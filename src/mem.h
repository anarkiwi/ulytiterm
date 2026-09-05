/* Fixed memory map for everything the VIC or the DMA has to reach.
 *
 * These live in VIC bank 2 ($8000-$bfff), well above where the linker
 * allocates code and data, and clear of the character ROM image the VIC sees
 * at $9000. BASIC is unmapped by the llvm-mos runtime, so $a000-$bfff is RAM
 * for the CPU and the REU alike.
 */
#ifndef ULYTITERM_MEM_H
#define ULYTITERM_MEM_H

#define MEM_VICBANK 2
#define MEM_CELLCHR ((uint8_t *)0xa000) /* 25 x 80 screen codes */
#define MEM_CELLATT ((uint8_t *)0xa800) /* 25 x 80 colours */
#define MEM_SCREEN ((uint8_t *)0xb400)  /* video matrix, 1000 bytes */
#define MEM_FONT ((uint8_t *)0xb800)    /* character generator, 2048 bytes */

#endif
