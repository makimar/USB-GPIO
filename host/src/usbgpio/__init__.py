"""Host library for the USB GPIO Extender (ESP32-C6).

See ``Board`` for the primary API; the ``usbgpio`` CLI and TUI are both
built on top of it (CLAUDE.md, "Architecture").
"""

from usbgpio.board import Board
from usbgpio.exceptions import BoardError, ProtocolError

__all__ = ["Board", "BoardError", "ProtocolError"]

__version__ = "0.1.0"
