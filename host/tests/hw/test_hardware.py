"""Smoke test against real hardware. Skipped unless USBGPIO_HW_TEST=1
(CLAUDE.md, "Conventions").

Set --port via the USBGPIO_TEST_PORT env var if auto-detect doesn't find
your board, e.g.:

    USBGPIO_HW_TEST=1 USBGPIO_TEST_PORT=/dev/cu.usbmodem101 pytest tests/hw
"""

from __future__ import annotations

import os

import pytest

from usbgpio import Board

pytestmark = pytest.mark.skipif(
    os.environ.get("USBGPIO_HW_TEST") != "1",
    reason="set USBGPIO_HW_TEST=1 to run against real hardware",
)

# Pin used for the write/read loopback-free round trip below. Change if
# GPIO4 is wired to something on your board.
TEST_PIN = 4


def _open() -> Board:
    return Board.open(os.environ.get("USBGPIO_TEST_PORT"))


def test_version_replies_with_semver():
    with _open() as board:
        version = board.version()
    assert version.startswith("usbgpio ")


def test_write_then_read_back():
    with _open() as board:
        board.mode(TEST_PIN, "out")
        board.write(TEST_PIN, 1)
        assert board.read(TEST_PIN) is True
        board.write(TEST_PIN, 0)
        assert board.read(TEST_PIN) is False
        board.reset()


def test_reserved_pin_is_rejected():
    from usbgpio.exceptions import ProtocolError

    with _open() as board, pytest.raises(ProtocolError) as exc_info:
        board.mode(12, "out")  # USB Serial/JTAG pin, always reserved
    assert exc_info.value.code == 2
