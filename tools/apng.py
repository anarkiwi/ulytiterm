"""Minimal APNG writer for palette indexed frames (standard library only)."""

import struct
import zlib


def _chunk(tag, data):
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def _deflate(indexed, width, height):
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: none
        raw += indexed[y * width : (y + 1) * width]
    return zlib.compress(bytes(raw), 9)


def write_apng(path, frames, palette, width, height, delay=(1, 5)):
    """frames: indexed bitmaps of width*height bytes. delay: (num, den) seconds."""
    plte = b"".join(bytes(c) for c in palette)
    out = [
        b"\x89PNG\r\n\x1a\n",
        _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)),
        _chunk(b"PLTE", plte),
        _chunk(b"acTL", struct.pack(">II", len(frames), 0)),
    ]
    seq = 0
    for i, frame in enumerate(frames):
        out.append(
            _chunk(
                b"fcTL",
                struct.pack(">IIIIIHHBB", seq, width, height, 0, 0, delay[0], delay[1], 0, 0),
            )
        )
        seq += 1
        data = _deflate(frame, width, height)
        if i:
            out.append(_chunk(b"fdAT", struct.pack(">I", seq) + data))
            seq += 1
        else:
            out.append(_chunk(b"IDAT", data))
    out.append(_chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(b"".join(out))
