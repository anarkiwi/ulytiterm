"""Drives ulytiterm.d64 in VICE and asserts terminal behaviour end to end.

The emulator gets an REU and a SwiftLink whose modem line is a TCP connection
back to :mod:`server`, so the C64 dials the fake BBS. One container serves the
whole module and the tests run in order, each leaving the state the next wants.
"""

from __future__ import annotations

import os
import struct
import subprocess
import time

import pytest
from vice_driver import BinMon, BinmonError, DiskMount, ViceContainer
from vice_driver.binmon import CHECK_LOAD, MEMSPACE_MAIN, OPCODE, TAP_MODE_FIXED
from vice_driver.keys import chord_to_keys, text_to_chords

import screen as scr
from server import FakeServer

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
D64 = os.path.join(REPO, "ulytiterm.d64")
IMAGE = os.environ.get("VICE_IMAGE", "anarkiwi/asid-vice:latest")
BINMON_PORT = 6502
GATEWAY_FALLBACK = "172.17.0.1"
BOOT_TIMEOUT = 120.0
TAP_FRAMES = 4  # long enough for a KERNAL scan, short of the repeat delay

# The 6551's data register: acia.c's REG_DATA, at the ACIA base ($de00).
ACIA_DATA = 0xDE00

DIAL = b"ATDTbbs.fozztexx.com:23\r"

IAC, SE, SB, WILL, DO = 0xFF, 0xF0, 0xFA, 0xFB, 0xFD
OPT_SGA, OPT_TTYPE, OPT_NAWS = 3, 24, 31
SUB_IS, SUB_SEND = 0, 1


def _docker(*args: str) -> str:
    return subprocess.run(
        ["docker", *args], check=True, capture_output=True, text=True
    ).stdout.strip()


def _gateway() -> str:
    """Address the container reaches a host listener on."""
    try:
        gw = _docker("network", "inspect", "bridge", "-f", "{{(index .IPAM.Config 0).Gateway}}")
    except (subprocess.CalledProcessError, OSError):
        return GATEWAY_FALLBACK
    return gw or GATEWAY_FALLBACK


def _connect_binmon(container: ViceContainer) -> BinMon:
    """Connect to the monitor, falling back to the container address where
    published ports do not work."""
    hosts = ["127.0.0.1"]
    try:
        ip = _docker(
            "inspect",
            container.name,
            "-f",
            "{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}",
        )
        if ip:
            hosts.append(ip)
    except (subprocess.CalledProcessError, OSError):
        pass
    last: Exception | None = None
    for attempt, host in enumerate(hosts * 30):
        bm = BinMon(host, BINMON_PORT)
        try:
            bm.connect(timeout=2.0, attempts=1, retry_delay=0.0)
            return bm
        except BinmonError as e:
            last = e
            if attempt % len(hosts) == len(hosts) - 1:
                time.sleep(0.5)
    raise AssertionError(f"no binmon on {hosts}: {last}")


class Term:
    """The running terminal: binary monitor plus the fake BBS it dialled."""

    def __init__(self, bm: BinMon, server: FakeServer) -> None:
        self.bm = bm
        self.server = server
        self._acia_checknum: int | None = None

    def screen(self) -> scr.Screen:
        return scr.read(self.bm)

    def wait_screen(self, ok, what: str, timeout: float = 30.0) -> scr.Screen:
        deadline = time.monotonic() + timeout
        while True:
            snap = self.screen()
            if ok(snap):
                return snap
            if time.monotonic() > deadline:
                raise AssertionError(
                    f"timed out waiting for {what}\nscreen:\n{snap.text}\n"
                    f"basic screen:\n{scr.read_kernal(self.bm)}"
                )
            time.sleep(0.1)

    def key(self, *names: str) -> None:
        self.bm.keymatrix_tap(chord_to_keys(*names), mode=TAP_MODE_FIXED, frames=TAP_FRAMES)
        deadline = time.monotonic() + 5.0
        while any(self.bm.keymatrix_get().keyarr):
            if time.monotonic() > deadline:
                raise AssertionError(f"key {names} never released")
            time.sleep(0.01)

    def type(self, text: str) -> None:
        for chord in text_to_chords(text):
            self.key(*chord)

    def _acia_checkpoint(self) -> int:
        """A CHECK_LOAD breakpoint on the ACIA data register, created once and
        reused: it halts the CPU the instant acia_read() consumes a received
        byte, giving a real event to pace sends on."""
        if self._acia_checknum is None:
            body = struct.pack("<HHBBBBB", ACIA_DATA, ACIA_DATA, 1, 0, CHECK_LOAD, 0, MEMSPACE_MAIN)
            resp = self.bm.call(OPCODE.CHECKPOINT_SET, body)
            self._acia_checknum = struct.unpack("<I", resp.body[:4])[0]
        return self._acia_checknum

    def _toggle_acia_checkpoint(self, enabled: bool) -> None:
        self.bm.call(
            OPCODE.CHECKPOINT_TOGGLE,
            struct.pack("<IB", self._acia_checkpoint(), 1 if enabled else 0),
            require_ok=False,
        )

    def send(self, data: bytes) -> None:
        """Feed bytes to the emulated ACIA one at a time, each gated on the
        CPU actually consuming the previous one. VICE's rs232 network device
        silently drops received bytes that outrun the CPU's own drain rate,
        which a burst over a local TCP loopback easily does; pacing on the
        real consumption event (rather than a guessed delay) is exact
        regardless of host speed."""
        checknum = self._acia_checkpoint()
        self._toggle_acia_checkpoint(True)
        try:
            for byte in data:
                self.server.send(bytes([byte]))
                self.bm.wait_for_checkpoint(checknum)
        finally:
            self._toggle_acia_checkpoint(False)


@pytest.fixture(scope="module")
def term():
    assert os.path.exists(D64), f"{D64} missing; run make first"
    server = FakeServer()
    container = ViceContainer(
        image=IMAGE,
        autostart="/work/ulytiterm.d64",
        mounts=[DiskMount(D64, "/work/ulytiterm.d64", read_only=True)],
        extra_args=[
            "-reu",
            "-reusize",
            "16384",
            "-acia1",
            "-acia1base",
            "0xde00",
            "-acia1irq",
            "0",
            "-acia1mode",
            "1",
            "-myaciadev",
            "0",
            "-rsdev1",
            f"{_gateway()}:{server.port}",
            "-rsdev1baud",
            "38400",
        ],
    )
    container.start()
    bm = None
    try:
        bm = _connect_binmon(container)
        bm.exit()
        t = Term(bm, server)
        t.wait_screen(lambda s: s.has("host:"), "the connect screen", BOOT_TIMEOUT)
        yield t
    finally:
        if bm is not None:
            bm.close()
        container.stop()
        server.close()


def test_connect_screen(term):
    snap = term.screen()
    assert "ulytiterm" in snap.row(0)
    assert snap.has("device  swiftlink")
    assert snap.has("reu     16384k")
    assert snap.has("host: bbs.fozztexx.com")


def test_dial(term):
    mark = term.server.mark()
    term.key("RETURN")
    term.wait_screen(lambda s: s.has("port: 23"), "the port prompt")
    term.key("RETURN")
    got = term.server.wait(mark, lambda b: b.endswith(b"\r"), "the dial string")
    assert got == DIAL


def test_telnet_negotiation(term):
    mark = term.server.mark()
    term.send(bytes([IAC, DO, OPT_TTYPE, IAC, DO, OPT_NAWS, IAC, WILL, OPT_SGA]))
    want = [
        bytes([IAC, WILL, OPT_TTYPE]),
        bytes([IAC, WILL, OPT_NAWS]),
        bytes([IAC, SB, OPT_NAWS, 0, 40, 0, 25, IAC, SE]),
        bytes([IAC, DO, OPT_SGA]),
    ]
    term.server.wait(mark, lambda b: all(w in b for w in want), "the negotiation replies")

    mark = term.server.mark()
    term.send(bytes([IAC, SB, OPT_TTYPE, SUB_SEND, IAC, SE]))
    ttype = bytes([IAC, SB, OPT_TTYPE, SUB_IS]) + b"VT102" + bytes([IAC, SE])
    term.server.wait(mark, lambda b: ttype in b, "the terminal type reply")


def test_text_and_ansi(term):
    term.send(
        b"\x1b[2J\x1b[H"
        b"\x1b[31mred\x1b[0m\r\n"
        b"\x1b[1;32mgreen\x1b[0m\r\n"
        b"\x1b[7mrev\x1b[0m\r\n"
        b"plain\r\n"
    )
    snap = term.wait_screen(lambda s: s.row(3).startswith("plain"), "the ansi text")
    assert snap.row(0).startswith("red")
    assert snap.row(1).startswith("green")
    assert snap.row(2).startswith("rev")
    assert [snap.colour(0, c) for c in range(3)] == [2, 2, 2]
    assert [snap.colour(1, c) for c in range(5)] == [13] * 5
    assert [snap.colour(3, c) for c in range(5)] == [15] * 5
    assert all(snap.code(2, c) & 0x80 for c in range(3))
    assert not snap.code(0, 0) & 0x80


def test_dec_special_graphics(term):
    term.send(b"\x1b[2J\x1b[H\x1b(0lqk\r\nx x\r\nmqj\r\n\x1b(Babc\r\n")
    snap = term.wait_screen(lambda s: s.row(3).startswith("abc"), "the box and the ascii row")
    assert [snap.code(0, c) for c in range(3)] == [scr.GFX_UL, scr.GFX_HORIZ, scr.GFX_UR]
    assert [snap.code(1, c) for c in range(3)] == [scr.GFX_VERT, 0x20, scr.GFX_VERT]
    assert [snap.code(2, c) for c in range(3)] == [scr.GFX_LL, scr.GFX_HORIZ, scr.GFX_LR]
    assert [snap.code(3, c) for c in range(3)] == [0x01, 0x02, 0x03]


def test_scroll(term):
    lines = 40
    term.send(b"\x1b[2J\x1b[H" + b"".join(b"line %d\r\n" % n for n in range(1, lines + 1)))
    snap = term.wait_screen(
        lambda s: s.row(scr.ROWS - 2).startswith("line %d" % lines), "the last line to scroll in"
    )
    first = lines - (scr.ROWS - 1) + 1
    got = [snap.row(r).rstrip() for r in range(scr.ROWS - 1)]
    assert got == ["line %d" % n for n in range(first, lines + 1)]


def test_keyboard(term):
    mark = term.server.mark()
    term.type("hello")
    term.server.wait(mark, lambda b: b == b"hello", "the typed text")
    mark = term.server.mark()
    term.key("RETURN")
    term.server.wait(mark, lambda b: b == b"\r\x00", "carriage return with the telnet NUL")


def test_scrollback(term):
    live = term.screen()
    scrolled = "line %d" % (40 - (scr.ROWS - 1))
    term.key("F5")
    back = term.wait_screen(
        lambda s: s.row(0).startswith(scrolled) and s.row(scr.ROWS - 1).startswith("scrollback"),
        "the scrolled off line and its status row",
    )
    assert back.row(scr.ROWS - 1).startswith("scrollback")
    assert all(back.code(scr.ROWS - 1, c) & 0x80 for c in range(10))
    term.key("CRSRUD")
    restored = term.wait_screen(
        lambda s: s.masked() == live.masked(), "the live screen to come back"
    )
    assert restored.colours == live.colours


def test_80_columns(term):
    mark = term.server.mark()
    term.key("LSHIFT", "F7")  # f8
    naws80 = bytes([IAC, SB, OPT_NAWS, 0, 80, 0, 25, IAC, SE])
    term.server.wait(mark, lambda b: naws80 in b, "the 80 column window size")
    term.send(b"a" * 39)
    snap = term.wait_screen(lambda s: s.row(0).startswith("a" * 39), "the unpanned row")
    assert not snap.has("panned")
    term.send(b"panned")
    snap = term.wait_screen(lambda s: s.has("panned"), "the view to pan right")
    assert snap.row(0).startswith("a" * 31 + "panned")


def test_disconnect(term):
    term.key("F7")
    term.wait_screen(lambda s: s.has("connection closed"), "the disconnect notice")
    term.key("RETURN")
    snap = term.wait_screen(lambda s: s.has("host:"), "the connect screen again")
    assert "ulytiterm" in snap.row(0)
