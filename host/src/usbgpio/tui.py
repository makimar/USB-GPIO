"""Interactive terminal UI (CLAUDE.md, "TUI (menu interface)").

Built with Textual. Contains no protocol logic - every board interaction
goes through `Board`. Launched via `usbgpio tui [--profile <name>]`.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from textual.app import App, ComposeResult
from textual.containers import Vertical
from textual.screen import ModalScreen
from textual.widgets import DataTable, Footer, Header, Input, Label, ListItem, ListView, Static

from usbgpio.board import MAX_GPIO, MIN_GPIO, Board, PinMode
from usbgpio.exceptions import BoardError
from usbgpio.profiles import (
    apply_profile,
    list_profiles,
    load_profile,
    save_profile,
    snapshot_profile,
)

PIN_MODES: tuple[PinMode, ...] = ("in", "in_pu", "in_pd", "out", "out_od")

# Mirrors firmware/main/pins.c. Kept here only to skip these rows in the
# table; the firmware remains the source of truth and would reject any
# command against them anyway.
RESERVED_PINS = frozenset({8, 9, 12, 13, *range(24, 31)})
USABLE_PINS = tuple(p for p in range(MIN_GPIO, MAX_GPIO + 1) if p not in RESERVED_PINS)


@dataclass
class PinState:
    mode: PinMode = "in"
    level: bool = False
    pwm_freq: int | None = None
    pwm_duty: float | None = None
    adc_watch: bool = False
    adc_mv: int | None = None


class ProfilePickerScreen(ModalScreen[str | None]):
    """Startup screen: pick a saved profile, or 'blank' for none."""

    def compose(self) -> ComposeResult:
        items = [ListItem(Label("blank (no profile)"), id="blank")]
        for name in list_profiles():
            items.append(ListItem(Label(name), id=f"profile-{name}"))
        with Vertical(id="picker"):
            yield Label("Select a profile (Enter to confirm, Esc for blank)")
            yield ListView(*items)

    def on_list_view_selected(self, event: ListView.Selected) -> None:
        item_id = event.item.id or ""
        if item_id == "blank":
            self.dismiss(None)
        else:
            self.dismiss(item_id.removeprefix("profile-"))

    def on_key(self, event: Any) -> None:
        if event.key == "escape":
            self.dismiss(None)


class TextPromptScreen(ModalScreen[str | None]):
    """A single-line text prompt, used for pwm freq/duty and profile names."""

    def __init__(self, prompt: str, initial: str = "") -> None:
        super().__init__()
        self._prompt = prompt
        self._initial = initial

    def compose(self) -> ComposeResult:
        with Vertical(id="prompt"):
            yield Label(self._prompt)
            yield Input(value=self._initial, id="value")

    def on_mount(self) -> None:
        self.query_one(Input).focus()

    def on_input_submitted(self, event: Input.Submitted) -> None:
        self.dismiss(event.value)

    def on_key(self, event: Any) -> None:
        if event.key == "escape":
            self.dismiss(None)


class PinTableApp(App[None]):
    CSS = """
    #picker, #prompt { padding: 1 2; border: round $accent; width: 60; height: auto; }
    """

    BINDINGS = [
        ("m", "cycle_mode", "Cycle mode"),
        ("t", "toggle_output", "Toggle"),
        ("f", "edit_pwm_freq", "PWM freq"),
        ("d", "edit_pwm_duty", "PWM duty"),
        ("a", "toggle_adc_watch", "ADC watch"),
        ("s", "save_profile_action", "Save profile"),
        ("q", "quit", "Quit"),
    ]

    def __init__(self, board: Board, initial_profile: str | None) -> None:
        super().__init__()
        self._board = board
        self._initial_profile = initial_profile
        self._pins: dict[int, PinState] = {pin: PinState() for pin in USABLE_PINS}

    def compose(self) -> ComposeResult:
        yield Header()
        yield DataTable(id="pins")
        yield Static(id="status")
        yield Footer()

    async def on_mount(self) -> None:
        table = self.query_one(DataTable)
        table.cursor_type = "row"
        table.add_columns("Pin", "Mode", "State", "PWM freq", "PWM duty", "ADC (mV)")
        for pin in USABLE_PINS:
            table.add_row(*self._row_for(pin), key=str(pin))

        profile_name = self._initial_profile
        if profile_name is None:
            profile_name = await self.push_screen_wait(ProfilePickerScreen())
        if profile_name:
            self._apply_profile_name(profile_name)

        self.set_interval(0.5, self._refresh)

    def _row_for(self, pin: int) -> tuple[str, ...]:
        st = self._pins[pin]
        return (
            str(pin),
            st.mode,
            "1" if st.level else "0",
            str(st.pwm_freq) if st.pwm_freq is not None else "-",
            str(st.pwm_duty) if st.pwm_duty is not None else "-",
            str(st.adc_mv) if st.adc_mv is not None else "-",
        )

    def _selected_pin(self) -> int | None:
        table = self.query_one(DataTable)
        if table.cursor_row is None:
            return None
        row_key, _ = table.coordinate_to_cell_key(table.cursor_coordinate)
        return int(row_key.value) if row_key.value is not None else None

    def _update_row(self, pin: int) -> None:
        table = self.query_one(DataTable)
        table.update_cell(str(pin), "1", self._pins[pin].mode, update_width=True)
        table.update_cell(str(pin), "2", "1" if self._pins[pin].level else "0")
        st = self._pins[pin]
        table.update_cell(str(pin), "3", str(st.pwm_freq) if st.pwm_freq is not None else "-")
        table.update_cell(str(pin), "4", str(st.pwm_duty) if st.pwm_duty is not None else "-")
        table.update_cell(str(pin), "5", str(st.adc_mv) if st.adc_mv is not None else "-")

    def _status(self, message: str) -> None:
        self.query_one("#status", Static).update(message)

    def _refresh(self) -> None:
        try:
            levels = self._board.read_all()
            for pin, st in self._pins.items():
                st.level = levels.get(pin, False)
                if st.adc_watch:
                    _, st.adc_mv = self._board.adc(pin)
                self._update_row(pin)
        except BoardError as exc:
            self._status(f"error: {exc}")

    def _apply_profile_name(self, name: str) -> None:
        try:
            data = load_profile(name)
            apply_profile(self._board, data)
            for pin_key, cfg in data.get("pins", {}).items():
                pin = int(pin_key)
                if pin not in self._pins:
                    continue
                st = self._pins[pin]
                st.mode = cfg.get("mode", st.mode)
                if "state" in cfg:
                    st.level = bool(cfg["state"])
                if st.mode == "pwm":
                    st.pwm_freq = cfg.get("freq")
                    st.pwm_duty = cfg.get("duty")
                self._update_row(pin)
            self._status(f"applied profile {name!r}")
        except BoardError as exc:
            self._status(f"error applying profile: {exc}")

    def action_cycle_mode(self) -> None:
        pin = self._selected_pin()
        if pin is None:
            return
        st = self._pins[pin]
        next_mode = (
            PIN_MODES[(PIN_MODES.index(st.mode) + 1) % len(PIN_MODES)]
            if st.mode in PIN_MODES
            else PIN_MODES[0]
        )
        try:
            self._board.mode(pin, next_mode)
            st.mode = next_mode
            st.pwm_freq = st.pwm_duty = None
            self._update_row(pin)
            self._status(f"pin {pin}: mode -> {next_mode}")
        except BoardError as exc:
            self._status(f"error: {exc}")

    def action_toggle_output(self) -> None:
        pin = self._selected_pin()
        if pin is None:
            return
        st = self._pins[pin]
        if st.mode not in ("out", "out_od"):
            self._status(f"pin {pin} is not an output (mode={st.mode})")
            return
        try:
            self._board.write(pin, not st.level)
            st.level = not st.level
            self._update_row(pin)
        except BoardError as exc:
            self._status(f"error: {exc}")

    async def action_edit_pwm_freq(self) -> None:
        await self._edit_pwm(edit_freq=True)

    async def action_edit_pwm_duty(self) -> None:
        await self._edit_pwm(edit_freq=False)

    async def _edit_pwm(self, *, edit_freq: bool) -> None:
        pin = self._selected_pin()
        if pin is None:
            return
        st = self._pins[pin]
        label = "PWM frequency (Hz)" if edit_freq else "PWM duty (%)"
        current = st.pwm_freq if edit_freq else st.pwm_duty
        value = await self.push_screen_wait(
            TextPromptScreen(f"{label} for pin {pin}:", str(current or ""))
        )
        if value is None or not value.strip():
            return
        try:
            freq = int(value) if edit_freq else int(st.pwm_freq or 1000)
            duty = float(value) if not edit_freq else float(st.pwm_duty or 50)
            self._board.pwm(pin, freq, duty)
            st.mode = "pwm"
            st.pwm_freq, st.pwm_duty = freq, duty
            self._update_row(pin)
        except (BoardError, ValueError) as exc:
            self._status(f"error: {exc}")

    def action_toggle_adc_watch(self) -> None:
        pin = self._selected_pin()
        if pin is None:
            return
        st = self._pins[pin]
        st.adc_watch = not st.adc_watch
        if not st.adc_watch:
            st.adc_mv = None
            self._update_row(pin)
        self._status(f"pin {pin}: adc watch {'on' if st.adc_watch else 'off'}")

    async def action_save_profile_action(self) -> None:
        name = await self.push_screen_wait(TextPromptScreen("Save current setup as profile:"))
        if not name or not name.strip():
            return
        configured = {
            pin: {
                "mode": st.mode,
                **({"state": int(st.level)} if st.mode in ("out", "out_od") else {}),
                **({"freq": st.pwm_freq, "duty": st.pwm_duty} if st.mode == "pwm" else {}),
            }
            for pin, st in self._pins.items()
            if st.mode != "in"
        }
        path = save_profile(name.strip(), snapshot_profile(name.strip(), configured))
        self._status(f"saved {path}")


def run_tui(*, port: str | None, baudrate: int, profile: str | None) -> None:
    with Board.open(port, baudrate=baudrate) as board:
        PinTableApp(board, profile).run()
