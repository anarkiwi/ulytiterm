# Design

## Modules

| file | role |
| --- | --- |
| `src/vt.c` | VT102/ANSI parser and cell buffer, hardware independent |
| `src/telnet.c` | NVT option negotiation, hardware independent |
| `src/kbd.c` | PETSCII to VT key sequences, hardware independent |
| `src/screen.c` | VIC-II renderer, RAM character generator |
| `src/uii.c` | Ultimate command interface, network target |
| `src/reu.c` | REU DMA |
| `src/hist.c` | scrollback in expansion RAM |
| `src/main.c` | connect dialogue and session loop |

The first three have no hardware dependencies and are unit tested on the host
by `tests/test.c` (`make test`).

## Cell buffer

`vt.c` stores C64 screen codes directly, with bit 7 as reverse video, plus one
colour nibble per cell. Translation from ASCII or DEC special graphics happens
once, when the character is written, through a 256 byte table selected by the
active character set. Painting a row is then a `memcpy` into video RAM and
another into colour RAM; colour RAM is left alone entirely until a
non-default colour is used.

Rows are reached through a pointer table, so scrolling rotates 25 pointers
rather than moving 2000 bytes. Insert and delete line inside a scrolling
region cost the same.

## Scrolling

Moving the cell buffer is free, but the VIC still has to see the result. A
scroll therefore invalidates every row, and repainting 25 rows costs about as
much as scrolling video RAM would have.

Instead `vt.c` offers the renderer a hook. `screen.c` answers it by moving the
displayed image with the REU: video RAM is stashed to expansion RAM and
fetched back one row higher, at DMA speed, and only the row blanked at the far
end is left dirty. Without a REU the hook declines and the rows are repainted.

## Memory map

`src/mem.h` fixes the buffers the VIC and the DMA must reach in VIC bank 2,
clear of the character ROM image the VIC sees at $9000:

| address | contents |
| --- | --- |
| $a000 | 25 x 80 screen codes |
| $a800 | 25 x 80 colours |
| $b400 | video matrix |
| $b800 | character generator |

BASIC is unmapped by the llvm-mos runtime, so this is all RAM for the CPU and
the REU. The build fails if the linker ever places code or data at $a000 or
above.

## Expansion RAM

| address | contents |
| --- | --- |
| $000000 | `reu_move()` scratch |
| $001000 | live screen, saved while browsing scrollback |
| $002000 | scrollback ring, 160 bytes per line |

Each line that scrolls off the top is stashed with two DMA transfers.
Browsing fetches rows straight back into the cell buffer, after saving the
live screen. Capacity is measured at startup by bank aliasing: 16MB of
expansion RAM holds the 8000 line maximum.

## Session loop

Each pass reads up to 892 bytes from the socket in one command, strips telnet
commands from the buffer in place, feeds the rest to the parser, paints the
dirty rows, then drains the keyboard and sends what it produced (together with
any terminal reply, such as a cursor position report) in one socket write.
