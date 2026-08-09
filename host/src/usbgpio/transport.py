"""Serial line transport: port discovery + the `OK`/`ERR` reply framing.

Kept separate from `Board` so tests can substitute any object that quacks
like `serial.Serial` (`.write()`, `.readline()`, `.close()`) without needing
real hardware (CLAUDE.md, "Host tests must run without hardware").
"""

from __future__ import annotations

from typing import Protocol

from usbgpio.exceptions import BoardError, ConnectionTimeoutError, ProtocolError

# Espressif's USB VID. Every ESP32-C6 board enumerates under this VID
# regardless of PID, so port auto-detection matches on VID alone.
ESPRESSIF_USB_VID = 0x303A

DEFAULT_BAUDRATE = 115200  # ignored by USB CDC, kept for portability/logging
DEFAULT_TIMEOUT = 2.0


class SerialLike(Protocol):
    """The subset of `serial.Serial` this module depends on."""

    def write(self, data: bytes) -> int | None: ...
    def readline(self) -> bytes: ...
    def close(self) -> None: ...


def find_port() -> str:
    """Returns the device path of the first port matching Espressif's VID.

    Raises:
        BoardError: no matching port was found.
    """
    from serial.tools import list_ports

    for info in list_ports.comports():
        if info.vid == ESPRESSIF_USB_VID:
            return info.device
    raise BoardError(
        "no USB GPIO board found (no serial port with Espressif VID "
        f"0x{ESPRESSIF_USB_VID:04X}); pass --port explicitly"
    )


class LineTransport:
    """Sends one line, reads back exactly one `OK`/`ERR` reply line."""

    def __init__(self, ser: SerialLike) -> None:
        self._ser = ser

    @classmethod
    def open(
        cls,
        port: str | None = None,
        *,
        baudrate: int = DEFAULT_BAUDRATE,
        timeout: float = DEFAULT_TIMEOUT,
    ) -> LineTransport:
        """Opens `port` (or the first auto-detected board) and wraps it."""
        import serial

        if port is None:
            port = find_port()
        ser = serial.Serial(port, baudrate=baudrate, timeout=timeout)
        return cls(ser)

    def close(self) -> None:
        self._ser.close()

    def __enter__(self) -> LineTransport:
        return self

    def __exit__(self, *exc_info: object) -> None:
        self.close()

    def send(self, line: str) -> str:
        """Sends `line`, returns the data portion of an `OK` reply.

        Raises:
            ProtocolError: the board replied `ERR <code> <message>`.
            ConnectionTimeoutError: no reply line arrived before the
                transport's read timeout.
        """
        self._ser.write((line + "\n").encode("ascii"))
        reply = self._read_reply(line)
        if reply == "OK":
            return ""
        if reply.startswith("OK "):
            return reply[len("OK ") :]
        if reply.startswith("ERR"):
            parts = reply.split(" ", 2)
            code = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else -1
            message = parts[2] if len(parts) > 2 else reply
            raise ProtocolError(code, message, line)
        raise ConnectionTimeoutError(f"unparseable reply to {line!r}: {reply!r}")

    def _read_reply(self, command: str) -> str:
        # Skip the boot-time READY banner (CLAUDE.md, "Protocol" -> "Rules"):
        # it may show up before the reply to the first command sent after
        # opening the port, so treat it as noise rather than a reply.
        while True:
            raw = self._ser.readline()
            if not raw:
                raise ConnectionTimeoutError(f"no reply to {command!r}")
            text = raw.decode("ascii", errors="replace").strip()
            if text == "" or text == "READY":
                continue
            return text
