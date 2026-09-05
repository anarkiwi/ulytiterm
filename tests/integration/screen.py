"""Decoding of the ulytiterm video matrix.

The program runs its own RAM font (the lower case charset plus patched
glyphs) with the video matrix at $b400 and colour RAM at $d800, so cells hold
C64 screen codes with bit 7 meaning reverse video. The cursor is drawn by
XORing bit 7, so text comparisons mask it off.
"""

from __future__ import annotations

from dataclasses import dataclass

from vice_driver import screencode_to_ascii

SCREEN_BASE = 0xB400
COLOUR_BASE = 0xD800
KERNAL_BASE = 0x0400
COLS = 40
ROWS = 25
CELLS = COLS * ROWS
UNKNOWN = "�"

# DEC special graphics screen codes used by the box drawing test.
GFX_UL = 0x70
GFX_UR = 0x6E
GFX_LL = 0x6D
GFX_LR = 0x7D
GFX_HORIZ = 0x40
GFX_VERT = 0x5D


def code_to_char(code: int) -> str:
    """Screen code (lower case charset) to ASCII; reverse video ignored."""
    c = code & 0x7F
    if c == 0x00:
        return "@"
    if 0x01 <= c <= 0x1A:
        return chr(c + 0x60)
    if c == 0x1B:
        return "["
    if c == 0x1D:
        return "]"
    if 0x20 <= c <= 0x3F or 0x41 <= c <= 0x5A:
        return chr(c)
    return UNKNOWN


@dataclass(frozen=True)
class Screen:
    """One snapshot of the video matrix and colour RAM."""

    codes: bytes
    colours: bytes

    def code(self, row: int, col: int) -> int:
        return self.codes[row * COLS + col]

    def colour(self, row: int, col: int) -> int:
        return self.colours[row * COLS + col] & 0x0F

    def row(self, row: int) -> str:
        base = row * COLS
        return "".join(code_to_char(c) for c in self.codes[base : base + COLS])

    def rows(self) -> list[str]:
        return [self.row(r) for r in range(ROWS)]

    @property
    def text(self) -> str:
        return "\n".join(self.rows())

    def find(self, needle: str) -> tuple[int, int] | None:
        """Row and column of the first row containing needle."""
        for r, line in enumerate(self.rows()):
            col = line.find(needle)
            if col >= 0:
                return r, col
        return None

    def has(self, needle: str) -> bool:
        return self.find(needle) is not None

    def masked(self) -> bytes:
        """Screen codes with the reverse bit cleared, i.e. cursor blind."""
        return bytes(c & 0x7F for c in self.codes)


def read(bm) -> Screen:
    """Snapshot the video matrix and colour RAM over the binary monitor."""
    codes = bm.mem_get(SCREEN_BASE, SCREEN_BASE + CELLS - 1)
    colours = bm.mem_get(COLOUR_BASE, COLOUR_BASE + CELLS - 1)
    return Screen(codes=codes, colours=colours)


def read_kernal(bm) -> str:
    """The BASIC screen at $0400, where a preflight failure is reported."""
    codes = bm.mem_get(KERNAL_BASE, KERNAL_BASE + CELLS - 1)
    lines = [
        "".join(screencode_to_ascii(c) for c in codes[r * COLS : (r + 1) * COLS]).rstrip()
        for r in range(ROWS)
    ]
    return "\n".join(line for line in lines if line)
