"""Interactive terminal UI (CLAUDE.md, "TUI (menu interface)").

Built with Textual. Contains no protocol logic - every board interaction
goes through `Board`. Launched via `usbgpio tui [--profile <name>]`.

Layout mirrors the WiFi status page (firmware/main/http_status.c): two
columns matching the ESP32-C6-DevKitM-1's J1 (left) / J3 (right) header
pinout, top to bottom, including fixed (power/ground) pins in their
physical position. Keep HEADER_LEFT/HEADER_RIGHT here in sync with
http_status.c's J1_LEFT/J3_RIGHT if you're on different hardware.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical
from textual.screen import ModalScreen
from textual.widgets import DataTable, Footer, Header, Input, Label, ListItem, ListView, Static

from usbgpio.board import Board, PinMode
from usbgpio.exceptions import BoardError
from usbgpio.profiles import (
    apply_profile,
    list_profiles,
    load_profile,
    save_profile,
    snapshot_profile,
)

PIN_MODES: tuple[PinMode, ...] = ("in", "in_pu", "in_pd", "out", "out_od")


@dataclass(frozen=True)
class HeaderPin:
    """One row of a pin header, top to bottom as physically silkscreened."""

    label: str | None = None  # set for fixed pins (3V3, RST, GND, 5V)
    gpio: int | None = None  # set for GPIO pins
    note: str | None = None  # optional annotation (e.g. "strapping")


# ESP32-C6-DevKitM-1 J1 (left) and J3 (right) headers, top to bottom -
# straight from Espressif's official user guide, cross-checked against a
# photo of the actual board. Mirrors firmware/main/http_status.c's
# J1_LEFT/J3_RIGHT; see that file's comment if you're on different
# hardware and need to change both.
HEADER_LEFT: tuple[HeaderPin, ...] = (
    HeaderPin(label="3V3"),
    HeaderPin(label="RST"),
    HeaderPin(gpio=2),
    HeaderPin(gpio=3),
    HeaderPin(gpio=4, note="strapping"),
    HeaderPin(gpio=5, note="strapping"),
    HeaderPin(gpio=0),
    HeaderPin(gpio=1),
    HeaderPin(gpio=8, note="status LED"),
    HeaderPin(gpio=6),
    HeaderPin(gpio=7),
    HeaderPin(gpio=14),
    HeaderPin(label="GND"),
    HeaderPin(label="5V"),
    HeaderPin(label="GND"),
)

HEADER_RIGHT: tuple[HeaderPin, ...] = (
    HeaderPin(label="GND"),
    HeaderPin(gpio=16, note="U0TXD"),
    HeaderPin(gpio=17, note="U0RXD"),
    HeaderPin(gpio=23),
    HeaderPin(gpio=22),
    HeaderPin(gpio=21),
    HeaderPin(gpio=20),
    HeaderPin(gpio=19),
    HeaderPin(gpio=18),
    HeaderPin(gpio=15, note="strapping"),
    HeaderPin(gpio=9, note="strapping"),
    HeaderPin(label="GND"),
    HeaderPin(gpio=13, note="USB D+"),
    HeaderPin(gpio=12, note="USB D-"),
    HeaderPin(label="GND"),
)

_ALL_HEADER_PINS = (*HEADER_LEFT, *HEADER_RIGHT)

# Mirrors firmware/main/pins.c. GPIO rows for these pins are shown (in
# their real physical position) but are never actionable - the firmware
# would reject MODE/WRITE/PWM on them anyway.
RESERVED_PINS = frozenset({4, 5, 8, 9, 12, 13, 15, *range(24, 31)})
USABLE_PINS = tuple(
    p.gpio for p in _ALL_HEADER_PINS if p.gpio is not None and p.gpio not in RESERVED_PINS
)
PIN_NOTES: dict[int, str] = {
    p.gpio: p.note for p in _ALL_HEADER_PINS if p.gpio is not None and p.note
}


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
    #headers { height: 1fr; }
    .header-col { width: 1fr; margin: 0 1; }
    .header-col > Label { text-style: bold; padding: 0 1; }
    DataTable { height: 1fr; }
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
        self._pin_table: dict[int, DataTable] = {}

    def compose(self) -> ComposeResult:
        yield Header()
        with Horizontal(id="headers"):
            with Vertical(classes="header-col"):
                yield Label("J1 (left)")
                yield DataTable(id="j1")
            with Vertical(classes="header-col"):
                yield Label("J3 (right)")
                yield DataTable(id="j3")
        yield Static(id="status")
        yield Footer()

    def on_mount(self) -> None:
        self._build_table(self.query_one("#j1", DataTable), HEADER_LEFT)
        self._build_table(self.query_one("#j3", DataTable), HEADER_RIGHT)

        if self._initial_profile is not None:
            self._apply_profile_name(self._initial_profile)
        else:
            # push_screen(..., wait_for_dismiss=True) (what push_screen_wait
            # does under the hood) only works from inside a worker task -
            # on_mount isn't one. The callback form works from anywhere and
            # needs no worker.
            self.push_screen(ProfilePickerScreen(), self._on_profile_picked)

        self.set_interval(0.5, self._refresh)

    def _build_table(self, table: DataTable, rows: tuple[HeaderPin, ...]) -> None:
        table.cursor_type = "row"
        table.add_columns(("Pin", "pin"), ("Status", "status"))
        for i, row in enumerate(rows):
            if row.gpio is not None:
                key = str(row.gpio)
                self._pin_table[row.gpio] = table
                pin_label = f"GPIO{row.gpio}"
            else:
                key = f"fixed-{i}"
                pin_label = row.label or ""
            table.add_row(pin_label, self._status_text(row.gpio), key=key)

    def _status_text(self, gpio: int | None) -> str:
        if gpio is None:
            return "-"
        if gpio in RESERVED_PINS:
            note = PIN_NOTES.get(gpio)
            return f"reserved ({note})" if note else "reserved"
        st = self._pins[gpio]
        text = f"{st.mode}, {'1' if st.level else '0'}"
        if st.mode == "pwm" and st.pwm_freq is not None:
            text += f" ({st.pwm_freq} Hz, {st.pwm_duty}%)"
        if st.adc_watch and st.adc_mv is not None:
            text += f" [{st.adc_mv} mV]"
        return text

    def _on_profile_picked(self, profile_name: str | None) -> None:
        if profile_name:
            self._apply_profile_name(profile_name)

    def _selected_pin(self) -> int | None:
        table = self.focused
        if not isinstance(table, DataTable) or table.cursor_row is None:
            return None
        row_key, _ = table.coordinate_to_cell_key(table.cursor_coordinate)
        key = row_key.value
        if key is None or key.startswith("fixed-"):
            return None
        pin = int(key)
        return None if pin in RESERVED_PINS else pin

    def _update_row(self, pin: int) -> None:
        table = self._pin_table[pin]
        # update_width: status text length varies a lot (bare level vs.
        # PWM/ADC detail appended) and the column doesn't auto-widen
        # without this - it silently truncates otherwise.
        table.update_cell(str(pin), "status", self._status_text(pin), update_width=True)

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

    def action_edit_pwm_freq(self) -> None:
        self._edit_pwm(edit_freq=True)

    def action_edit_pwm_duty(self) -> None:
        self._edit_pwm(edit_freq=False)

    def _edit_pwm(self, *, edit_freq: bool) -> None:
        pin = self._selected_pin()
        if pin is None:
            return
        st = self._pins[pin]
        label = "PWM frequency (Hz)" if edit_freq else "PWM duty (%)"
        current = st.pwm_freq if edit_freq else st.pwm_duty
        self.push_screen(
            TextPromptScreen(f"{label} for pin {pin}:", str(current or "")),
            lambda value: self._apply_pwm_edit(pin, edit_freq, value),
        )

    def _apply_pwm_edit(self, pin: int, edit_freq: bool, value: str | None) -> None:
        if value is None or not value.strip():
            return
        st = self._pins[pin]
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

    def action_save_profile_action(self) -> None:
        self.push_screen(
            TextPromptScreen("Save current setup as profile:"), self._on_save_profile_name
        )

    def _on_save_profile_name(self, name: str | None) -> None:
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
