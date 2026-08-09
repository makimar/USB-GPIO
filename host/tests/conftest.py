"""Shared fixtures. No hardware is touched: every test substitutes a fake
serial object for LineTransport (CLAUDE.md, "Host tests must run without
hardware").
"""

from __future__ import annotations


class ScriptedSerial:
    """A fake serial port that asserts each write against a script.

    `script` is a list of `(expected_command, reply)` pairs, both without
    trailing newlines. Each `write()` must match the next expected command;
    the following `readline()` returns that command's scripted reply.
    """

    def __init__(self, script: list[tuple[str, str]]) -> None:
        self._script = list(script)
        self._pending_reply: str | None = None
        self.closed = False

    def write(self, data: bytes) -> int:
        text = data.decode("ascii").rstrip("\n")
        assert self._script, f"unexpected extra command: {text!r}"
        expected, reply = self._script.pop(0)
        assert text == expected, f"expected {expected!r}, got {text!r}"
        self._pending_reply = reply
        return len(data)

    def readline(self) -> bytes:
        assert self._pending_reply is not None, "readline() called with no pending reply"
        reply, self._pending_reply = self._pending_reply, None
        return (reply + "\n").encode("ascii")

    def close(self) -> None:
        self.closed = True


class QueuedLineSerial:
    """A fake serial port that ignores writes and returns queued lines."""

    def __init__(self, lines: list[bytes]) -> None:
        self._lines = list(lines)
        self.written: list[bytes] = []
        self.closed = False

    def write(self, data: bytes) -> int:
        self.written.append(data)
        return len(data)

    def readline(self) -> bytes:
        return self._lines.pop(0) if self._lines else b""

    def close(self) -> None:
        self.closed = True
