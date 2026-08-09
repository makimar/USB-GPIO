from __future__ import annotations

from dataclasses import dataclass

import pytest

from usbgpio.exceptions import BoardError
from usbgpio.transport import ESPRESSIF_USB_VID, find_port


@dataclass
class FakePortInfo:
    device: str
    vid: int | None


def test_find_port_matches_espressif_vid(monkeypatch):
    ports = [
        FakePortInfo("/dev/cu.Bluetooth-Incoming-Port", vid=None),
        FakePortInfo("/dev/cu.usbmodem101", vid=ESPRESSIF_USB_VID),
    ]
    monkeypatch.setattr("serial.tools.list_ports.comports", lambda: ports)
    assert find_port() == "/dev/cu.usbmodem101"


def test_find_port_raises_when_none_match(monkeypatch):
    monkeypatch.setattr(
        "serial.tools.list_ports.comports", lambda: [FakePortInfo("/dev/cu.foo", vid=0x1234)]
    )
    with pytest.raises(BoardError):
        find_port()
