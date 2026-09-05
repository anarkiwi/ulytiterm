"""Capture docs/demo.png: the terminal dialling a scripted BBS inside VICE.

Run from the repository root after `make`:

    python3 tools/demo.py
"""

import socket
import subprocess
import sys
import threading
import time

sys.path.insert(0, "tools")

from apng import write_apng  # noqa: E402

from vice_driver import (  # noqa: E402
    BinMon,
    DiskMount,
    ViceContainer,
    chord_to_keys,
    text_to_chords,
)
from vice_driver.display import (  # noqa: E402
    parse_display_response,
    parse_palette_response,
)

PORT = 25299
OUT = "docs/demo.png"

ESC = "\x1b"
GFX, ASCII = ESC + "(0", ESC + "(B"


def sgr(*n):
    return ESC + "[" + ";".join(str(i) for i in n) + "m"


SCRIPT = [
    "\r\n",
    sgr(1, 36) + GFX + "lqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqk" + ASCII + "\r\n",
    GFX + "x" + ASCII + sgr(1, 37) + "     t h e   n i g h t   o w l      " + sgr(1, 36) + GFX + "x" + ASCII + "\r\n",
    GFX + "x" + ASCII + sgr(36) + "      40 columns  ~  8 bits         " + sgr(1, 36) + GFX + "x" + ASCII + "\r\n",
    GFX + "mqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqj" + ASCII + sgr(0) + "\r\n",
    "\r\n",
    "".join(sgr(30 + c) + sgr(7) + "  " + sgr(0) for c in range(8)) + "\r\n",
    "\r\n",
    sgr(1, 33) + "login: " + sgr(0),
]

MENU = [
    "\r\n\r\n",
    sgr(1, 32) + "  welcome back, commodore.\r\n" + sgr(0),
    "\r\n",
    sgr(36) + "  [1]" + sgr(0) + " messages     " + sgr(36) + "[4]" + sgr(0) + " doors\r\n",
    sgr(36) + "  [2]" + sgr(0) + " files        " + sgr(36) + "[5]" + sgr(0) + " chat\r\n",
    sgr(36) + "  [3]" + sgr(0) + " news         " + sgr(36) + "[q]" + sgr(0) + " logoff\r\n",
    "\r\n",
    sgr(1, 30) + "  last 5 callers:\r\n" + sgr(0),
] + [f"  {n:02d}. caller from node {n}\r\n" for n in range(1, 9)]

IAC, SB, SE, WILL, DO = 255, 250, 240, 251, 253
OPT_TTYPE, OPT_NAWS, OPT_SGA = 24, 31, 3


class Bbs(threading.Thread):
    """Scripted BBS: negotiates telnet, then paints a screen a line at a time."""

    daemon = True

    def __init__(self):
        super().__init__()
        self.rx = bytearray()
        self.conn = None

    def send(self, text):
        self.conn.sendall(text.encode("latin-1") if isinstance(text, str) else text)

    def run(self):
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("0.0.0.0", PORT))
        srv.listen(1)
        srv.settimeout(180)
        self.conn, _ = srv.accept()
        self.conn.settimeout(0.2)
        while b"\r" not in self.rx:  # the dial string
            self._drain()
        time.sleep(0.8)
        self.send("\r\nCONNECT 38400\r\n")
        time.sleep(1.2)
        self.send(bytes([IAC, DO, OPT_TTYPE, IAC, DO, OPT_NAWS, IAC, WILL, OPT_SGA]))
        time.sleep(0.6)
        self.send(bytes([IAC, SB, OPT_TTYPE, 1, IAC, SE]))
        time.sleep(0.4)
        for line in SCRIPT:
            self.send(line)
            self._drain()
            time.sleep(0.35)
        start = time.time()
        while time.time() - start < 12 and b"\r" not in self.rx[-2:]:
            self._drain()
        for line in MENU:
            self.send(line)
            self._drain()
            time.sleep(0.3)
        while True:
            self._drain()

    def _drain(self):
        try:
            b = self.conn.recv(256)
            if b:
                self.rx.extend(b)
        except socket.timeout:
            pass
        except OSError:
            pass


def gateway():
    out = subprocess.run(
        ["docker", "network", "inspect", "bridge", "-f", "{{(index .IPAM.Config 0).Gateway}}"],
        capture_output=True,
        text=True,
        check=True,
    )
    return out.stdout.strip()


def container_ip(name):
    out = subprocess.run(
        ["docker", "inspect", name, "-f", "{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}"],
        capture_output=True,
        text=True,
        check=True,
    )
    return out.stdout.strip()


def screen_text(bm):
    mem = bm.mem_get(0xB400, 0xB7E7)
    out = []
    for b in mem:
        c = b & 0x7F
        if 0x01 <= c <= 0x1A:
            out.append(chr(c + 0x60))
        elif 0x20 <= c <= 0x5A:
            out.append(chr(c))
        else:
            out.append(" ")
    return "".join(out)


def main():
    bbs = Bbs()
    bbs.start()
    time.sleep(0.5)

    container = ViceContainer(
        autostart="/work/ulytiterm.d64",
        mounts=[DiskMount("ulytiterm.d64", "/work/ulytiterm.d64", read_only=True)],
        warp=False,  # real time, so the capture looks like the real thing
        extra_args=[
            "-reu", "-reusize", "16384",
            "-acia1", "-acia1base", "0xde00", "-acia1irq", "0",
            "-acia1mode", "1", "-myaciadev", "0",
            "-rsdev1", f"{gateway()}:{PORT}", "-rsdev1baud", "38400",
        ],
    )
    frames = []
    with container:
        bm = BinMon(container_ip(container.name), 6502)
        bm.connect(timeout=30.0, attempts=120, retry_delay=0.25)
        bm.exit()
        palette = parse_palette_response(bm.palette_get())

        def grab():
            snap = parse_display_response(bm.display_get())
            frames.append((snap.debug_width, snap.debug_height, snap.bitmap))

        def wait(seconds, capture=True):
            end = time.time() + seconds
            while time.time() < end:
                if capture:
                    grab()
                time.sleep(0.12)

        for _ in range(200):  # the connect screen
            if "host:" in screen_text(bm):
                break
            time.sleep(0.5)
        wait(1.4)
        for _ in range(2):  # accept host, accept port
            bm.keymatrix_tap(chord_to_keys("RETURN"), frames=6)
            wait(1.0)
        wait(11.0)
        for chord in text_to_chords("commodore"):  # log in
            bm.keymatrix_tap(chord_to_keys(*chord), frames=6)
            wait(0.3)
        bm.keymatrix_tap(chord_to_keys("RETURN"), frames=6)
        wait(7.0)
        bm.close()

    width, height, _ = frames[0]
    write_apng(OUT, [f[2] for f in frames], palette, width, height, delay=(3, 25))
    print(f"{OUT}: {len(frames)} frames, {width}x{height}")


if __name__ == "__main__":
    main()
