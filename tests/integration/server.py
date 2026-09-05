"""Fake BBS: a host side TCP server the emulated ACIA dials into.

VICE connects out to this port when the C64 program opens the modem line, so
the listener has to be up before the emulator starts. One connection is served
at a time; everything received is appended to a single buffer that tests slice
with marks, and scripted bytes go out through :meth:`send`.
"""

from __future__ import annotations

import socket
import threading
import time

ACCEPT_POLL = 0.2
SEND_CHUNK = 32  # bytes per write; the 6551 has no receive FIFO
SEND_DELAY = 0.02  # pause between chunks, so the terminal keeps up


class FakeServer:
    """Single connection recording server on an ephemeral port."""

    def __init__(self, host: str = "0.0.0.0") -> None:
        self._lsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._lsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._lsock.bind((host, 0))
        self._lsock.listen(1)
        self._lsock.settimeout(ACCEPT_POLL)
        self.port = self._lsock.getsockname()[1]
        self._buf = bytearray()
        self._lock = threading.Lock()
        self._conn: socket.socket | None = None
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    # ---- lifecycle ------------------------------------------------------

    def _serve(self) -> None:
        while not self._stop.is_set():
            try:
                conn, _ = self._lsock.accept()
            except (socket.timeout, TimeoutError):
                continue
            except OSError:
                return
            conn.settimeout(ACCEPT_POLL)
            self._conn = conn
            self._drain(conn)
            self._conn = None
            conn.close()

    def _drain(self, conn: socket.socket) -> None:
        while not self._stop.is_set():
            try:
                data = conn.recv(4096)
            except (socket.timeout, TimeoutError):
                continue
            except OSError:
                return
            if not data:
                return
            with self._lock:
                self._buf += data

    def close(self) -> None:
        self._stop.set()
        conn = self._conn
        if conn is not None:
            conn.close()
        self._lsock.close()
        self._thread.join(timeout=2.0)

    # ---- received data --------------------------------------------------

    @property
    def connected(self) -> bool:
        return self._conn is not None

    def mark(self) -> int:
        """Current length of the receive buffer, for slicing later arrivals."""
        with self._lock:
            return len(self._buf)

    def since(self, mark: int) -> bytes:
        with self._lock:
            return bytes(self._buf[mark:])

    def wait(self, mark: int, ok, what: str, timeout: float = 20.0) -> bytes:
        """Poll bytes received after ``mark`` until ``ok`` accepts them."""
        deadline = time.monotonic() + timeout
        while True:
            got = self.since(mark)
            if ok(got):
                return got
            if time.monotonic() > deadline:
                raise AssertionError(f"timed out waiting for {what}; got {got!r}")
            time.sleep(0.02)

    # ---- scripted output ------------------------------------------------

    def send(self, data: bytes, chunk: int = SEND_CHUNK, delay: float = SEND_DELAY) -> None:
        conn = self._wait_conn()
        for i in range(0, len(data), chunk):
            conn.sendall(data[i : i + chunk])
            if delay:
                time.sleep(delay)

    def _wait_conn(self, timeout: float = 20.0) -> socket.socket:
        deadline = time.monotonic() + timeout
        while self._conn is None:
            if time.monotonic() > deadline:
                raise AssertionError("the emulated ACIA never connected")
            time.sleep(0.05)
        return self._conn
