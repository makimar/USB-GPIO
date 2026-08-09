from __future__ import annotations

from usbgpio import cli
from usbgpio.exceptions import BoardError


class FakeBoard:
    """Stands in for Board so CLI dispatch can be tested without hardware."""

    def __init__(self) -> None:
        self.calls: list[tuple] = []

    def __enter__(self) -> FakeBoard:
        return self

    def __exit__(self, *exc_info: object) -> None:
        pass

    def mode(self, pin, mode):
        self.calls.append(("mode", pin, mode))

    def write(self, pin, level):
        self.calls.append(("write", pin, level))

    def read(self, pin):
        self.calls.append(("read", pin))
        return True

    def read_all(self):
        return {0: True, 1: False, 4: True}

    def pwm(self, pin, freq_hz, duty_pct):
        self.calls.append(("pwm", pin, freq_hz, duty_pct))

    def pwm_stop(self, pin):
        self.calls.append(("pwm_stop", pin))

    def adc(self, pin):
        return (2048, 1650)

    def version(self):
        return "usbgpio 0.1.0"

    def reset(self):
        self.calls.append(("reset",))


def _patch_open(monkeypatch, fake: FakeBoard) -> None:
    monkeypatch.setattr(cli.Board, "open", classmethod(lambda cls, *a, **k: fake))


def test_write_dispatches_to_board(monkeypatch):
    fake = FakeBoard()
    _patch_open(monkeypatch, fake)
    assert cli.main(["write", "4", "1"]) == 0
    assert fake.calls == [("write", 4, 1)]


def test_read_prints_level(monkeypatch, capsys):
    fake = FakeBoard()
    _patch_open(monkeypatch, fake)
    assert cli.main(["read", "4"]) == 0
    assert capsys.readouterr().out.strip() == "1"


def test_readall_prints_hex_mask(monkeypatch, capsys):
    fake = FakeBoard()
    _patch_open(monkeypatch, fake)
    assert cli.main(["readall"]) == 0
    assert capsys.readouterr().out.strip() == "11"  # bits 0 and 4 set


def test_pwm_dispatches_with_parsed_args(monkeypatch):
    fake = FakeBoard()
    _patch_open(monkeypatch, fake)
    assert cli.main(["pwm", "5", "1000", "50"]) == 0
    assert fake.calls == [("pwm", 5, 1000, 50.0)]


def test_adc_prints_raw_and_millivolts(monkeypatch, capsys):
    fake = FakeBoard()
    _patch_open(monkeypatch, fake)
    assert cli.main(["adc", "2"]) == 0
    assert capsys.readouterr().out.strip() == "2048 1650"


def test_version_prints_reply(monkeypatch, capsys):
    fake = FakeBoard()
    _patch_open(monkeypatch, fake)
    assert cli.main(["version"]) == 0
    assert capsys.readouterr().out.strip() == "usbgpio 0.1.0"


def test_reset_dispatches(monkeypatch):
    fake = FakeBoard()
    _patch_open(monkeypatch, fake)
    assert cli.main(["reset"]) == 0
    assert fake.calls == [("reset",)]


def test_board_error_prints_to_stderr_and_returns_1(monkeypatch, capsys):
    def raise_error(cls, *a, **k):
        raise BoardError("no board found")

    monkeypatch.setattr(cli.Board, "open", classmethod(raise_error))
    assert cli.main(["version"]) == 1
    assert "no board found" in capsys.readouterr().err
