from __future__ import annotations

import pytest
from conftest import ScriptedSerial

from usbgpio.board import Board
from usbgpio.exceptions import BoardError
from usbgpio.profiles import (
    apply_profile,
    list_profiles,
    load_profile,
    profiles_dir,
    save_profile,
    snapshot_profile,
)
from usbgpio.transport import LineTransport


@pytest.fixture(autouse=True)
def _xdg_config_home(tmp_path, monkeypatch):
    """Every test gets an isolated, empty profiles directory."""
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))


def test_profiles_dir_respects_xdg_config_home(tmp_path):
    assert profiles_dir() == tmp_path / "usbgpio" / "profiles"


def test_save_and_load_roundtrip():
    data = {"name": "relay-tester", "pins": {"4": {"mode": "out", "state": 0}}}
    path = save_profile("relay-tester", data)
    assert path.is_file()
    assert load_profile("relay-tester") == data


def test_list_profiles_empty_when_dir_missing():
    assert list_profiles() == []


def test_list_profiles_sorted():
    save_profile("b", {"pins": {}})
    save_profile("a", {"pins": {}})
    assert list_profiles() == ["a", "b"]


def test_load_missing_profile_raises_board_error():
    with pytest.raises(BoardError):
        load_profile("does-not-exist")


def test_snapshot_profile_stringifies_pin_keys():
    result = snapshot_profile("foo", {4: {"mode": "out", "state": 1}})
    assert result == {"name": "foo", "pins": {"4": {"mode": "out", "state": 1}}}


def test_apply_profile_sends_mode_write_and_pwm_commands():
    profile = {
        "pins": {
            "4": {"mode": "out", "state": 0},
            "5": {"mode": "pwm", "freq": 1000, "duty": 50},
            "3": {"mode": "in_pu"},
        }
    }
    script = [
        ("MODE 4 out", "OK"),
        ("WRITE 4 0", "OK"),
        ("PWM 5 1000 50", "OK"),
        ("MODE 3 in_pu", "OK"),
    ]
    board = Board(LineTransport(ScriptedSerial(script)))
    apply_profile(board, profile)


def test_apply_profile_rejects_missing_mode():
    board = Board(LineTransport(ScriptedSerial([])))
    with pytest.raises(BoardError):
        apply_profile(board, {"pins": {"4": {}}})


def test_apply_profile_rejects_incomplete_pwm():
    board = Board(LineTransport(ScriptedSerial([])))
    with pytest.raises(BoardError):
        apply_profile(board, {"pins": {"4": {"mode": "pwm", "freq": 1000}}})
