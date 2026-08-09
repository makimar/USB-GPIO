"""Exceptions raised by the usbgpio host library."""

from __future__ import annotations


class BoardError(Exception):
    """Base class for all usbgpio errors."""


class ProtocolError(BoardError):
    """The board replied `ERR <code> <message>` to a command.

    Attributes:
        code: The numeric error code (see docs/protocol.md).
        message: The human-readable message that followed the code.
        command: The command line that was sent.
    """

    def __init__(self, code: int, message: str, command: str) -> None:
        self.code = code
        self.message = message
        self.command = command
        super().__init__(f"{command!r} -> ERR {code} {message}")


class ConnectionTimeoutError(BoardError):
    """No reply (or an unparseable reply) was received in time."""
