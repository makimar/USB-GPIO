"""TUI regression tests.

These exist because a plain `import usbgpio.tui` (which is all that was
checked before) does not catch Textual API misuse: `push_screen_wait`
needs an active worker (on_mount doesn't have one -> NoActiveWorker), and
DataTable.update_cell needs the *actual* column keys, not guessed ones
(-> CellDoesNotExist). Both shipped and were only caught by actually
running the app. Textual's `run_test()` drives it headlessly - no real
terminal or hardware needed - so these run as part of the normal suite.
"""

from __future__ import annotations

import asyncio

from usbgpio.board import Board
from usbgpio.profiles import load_profile
from usbgpio.transport import LineTransport
from usbgpio.tui import HEADER_LEFT, PinTableApp


class GenericFakeSerial:
    """Replies plausibly to any command, regardless of order/arguments.

    Good enough for exercising TUI control flow; exact wire traffic for
    each command is already covered by test_board.py's ScriptedSerial.
    """

    _REPLIES = {
        "MODE": "OK",
        "WRITE": "OK",
        "READ": "OK 0",
        "READALL": "OK 0",
        "PWM": "OK",
        "PWMSTOP": "OK",
        "ADC": "OK 2048 1650",
        "VERSION": "OK usbgpio 0.2.0",
        "RESET": "OK",
    }

    def __init__(self) -> None:
        self._pending: str | None = None
        self.closed = False

    def write(self, data: bytes) -> int:
        cmd = data.decode("ascii").split()[0]
        self._pending = self._REPLIES.get(cmd, "ERR 1 unknown command")
        return len(data)

    def readline(self) -> bytes:
        reply, self._pending = self._pending, None
        return (reply + "\n").encode("ascii")

    def close(self) -> None:
        self.closed = True


def make_board() -> Board:
    return Board(LineTransport(GenericFakeSerial()))


def run_async(coro_fn, *args, **kwargs):
    """Runs an async test body without depending on pytest-asyncio."""
    asyncio.run(coro_fn(*args, **kwargs))


# HEADER_LEFT starts 3V3, RST (both fixed, non-selectable) before the
# first real GPIO row - two "down" presses from the default cursor
# (row 0) reaches it. See tui.py's HEADER_LEFT/HEADER_RIGHT.
_FIRST_GPIO_INDEX = next(i for i, p in enumerate(HEADER_LEFT) if p.gpio is not None)
_FIRST_GPIO = HEADER_LEFT[_FIRST_GPIO_INDEX].gpio


async def _select_first_gpio(pilot) -> int:
    for _ in range(_FIRST_GPIO_INDEX):
        await pilot.press("down")
    await pilot.pause()
    return _FIRST_GPIO


def test_startup_shows_profile_picker_and_escape_dismisses_it():
    async def body():
        app = PinTableApp(make_board(), initial_profile=None)
        async with app.run_test() as pilot:
            await pilot.pause()
            assert len(app.screen_stack) == 2  # base screen + picker
            await pilot.press("escape")
            await pilot.pause()
            assert len(app.screen_stack) == 1

    run_async(body)


def test_default_cursor_on_a_fixed_pin_is_not_selectable():
    """Row 0 (3V3) isn't a GPIO - actions on it must be no-ops, not crash."""

    async def body():
        app = PinTableApp(make_board(), initial_profile=None)
        async with app.run_test() as pilot:
            await pilot.pause()
            await pilot.press("escape")
            await pilot.pause()
            assert app._selected_pin() is None
            await pilot.press("m")  # must not raise
            await pilot.pause()

    run_async(body)


def test_cycle_mode_and_toggle_output_updates_table():
    async def body():
        app = PinTableApp(make_board(), initial_profile=None)
        async with app.run_test() as pilot:
            await pilot.pause()
            await pilot.press("escape")
            await pilot.pause()
            pin = await _select_first_gpio(pilot)
            assert app._selected_pin() == pin

            for _ in range(3):  # in -> in_pu -> in_pd -> out
                await pilot.press("m")
                await pilot.pause()
            assert app._pins[pin].mode == "out"

            await pilot.press("t")
            await pilot.pause()
            assert app._pins[pin].level is True

            # This is exactly what CellDoesNotExist broke: updating the
            # DataTable after changing pin state.
            table = app._pin_table[pin]
            assert table.get_cell(str(pin), "status") == "out, 1"

    run_async(body)


def test_pwm_prompt_updates_pin_state_and_table():
    async def body():
        app = PinTableApp(make_board(), initial_profile=None)
        async with app.run_test() as pilot:
            await pilot.pause()
            await pilot.press("escape")
            await pilot.pause()
            pin = await _select_first_gpio(pilot)

            await pilot.press("f")
            await pilot.pause()
            assert len(app.screen_stack) == 2  # prompt pushed

            await pilot.press(*"500")
            await pilot.press("enter")
            await pilot.pause()

            assert app._pins[pin].pwm_freq == 500
            assert app._pins[pin].mode == "pwm"
            table = app._pin_table[pin]
            # Exact match, not a substring check: a DataTable column that
            # doesn't auto-widen (missing update_width=True) silently
            # truncates the *end* of the text, which "in" would miss.
            assert table.get_cell(str(pin), "status") == "pwm, 0 (500 Hz, 50.0%)"

    run_async(body)


def test_save_profile_writes_expected_toml(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))

    async def body():
        app = PinTableApp(make_board(), initial_profile=None)
        async with app.run_test() as pilot:
            await pilot.pause()
            await pilot.press("escape")
            await pilot.pause()
            pin = await _select_first_gpio(pilot)

            await pilot.press("m", "m", "m")  # -> out
            await pilot.pause()

            await pilot.press("s")
            await pilot.pause()
            assert len(app.screen_stack) == 2  # save-name prompt pushed

            await pilot.press(*"unit-test-profile")
            await pilot.press("enter")
            await pilot.pause()
            return pin

    pin = asyncio.run(body())
    saved = load_profile("unit-test-profile")
    assert saved["pins"][str(pin)]["mode"] == "out"


def test_adc_watch_toggle():
    async def body():
        app = PinTableApp(make_board(), initial_profile=None)
        async with app.run_test() as pilot:
            await pilot.pause()
            await pilot.press("escape")
            await pilot.pause()
            pin = await _select_first_gpio(pilot)

            await pilot.press("a")
            await pilot.pause()
            assert app._pins[pin].adc_watch is True

            await pilot.press("a")
            await pilot.pause()
            assert app._pins[pin].adc_watch is False

    run_async(body)


def test_reserved_pin_rows_show_as_reserved_and_are_not_actionable():
    """GPIO4/5/8/9/12/13/15/24-30 are shown (matching the physical board)
    but must never be actionable - the firmware would reject them anyway.
    """

    async def body():
        app = PinTableApp(make_board(), initial_profile=None)
        async with app.run_test() as pilot:
            await pilot.pause()
            await pilot.press("escape")
            await pilot.pause()

            # Row 4 of HEADER_LEFT is GPIO4 (reserved, strapping).
            for _ in range(4):
                await pilot.press("down")
            await pilot.pause()
            assert app._selected_pin() is None  # reserved -> not selectable

            table = app.query_one("#j1")
            assert "reserved" in table.get_cell("4", "status")

    run_async(body)
