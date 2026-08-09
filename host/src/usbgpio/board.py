"""The `Board` API: one method per protocol command (docs/protocol.md).

Every other host entry point (CLI, TUI, profile loader) is built on this
class and this class alone (CLAUDE.md, "Architecture" / "TUI").
"""

from __future__ import annotations

from typing import Literal

from usbgpio.transport import DEFAULT_BAUDRATE, DEFAULT_TIMEOUT, LineTransport

PinMode = Literal["in", "in_pu", "in_pd", "out", "out_od"]

# Mirrors firmware/main/pins.h. The host does not re-validate pin numbers
# beyond basic type/range sanity - the firmware is authoritative and
# returns `ERR 2 pin reserved` / `ERR 4 <bad pin>` for anything it rejects.
MIN_GPIO = 0
MAX_GPIO = 30


class Board:
    """A connected USB GPIO Extender."""

    def __init__(self, transport: LineTransport) -> None:
        self._t = transport

    @classmethod
    def open(
        cls,
        port: str | None = None,
        *,
        baudrate: int = DEFAULT_BAUDRATE,
        timeout: float = DEFAULT_TIMEOUT,
    ) -> Board:
        """Opens `port` (or auto-detects one by USB VID) and returns a Board."""
        return cls(LineTransport.open(port, baudrate=baudrate, timeout=timeout))

    def close(self) -> None:
        self._t.close()

    def __enter__(self) -> Board:
        return self

    def __exit__(self, *exc_info: object) -> None:
        self.close()

    # -- Pin mode / digital I/O -------------------------------------------

    def mode(self, pin: int, mode: PinMode) -> None:
        """Sets `pin`'s mode. See PinMode for the allowed values."""
        self._t.send(f"MODE {pin} {mode}")

    def write(self, pin: int, level: bool | int) -> None:
        """Drives `pin` high (truthy) or low (falsy). Pin must be an output."""
        self._t.send(f"WRITE {pin} {1 if level else 0}")

    def read(self, pin: int) -> bool:
        """Reads `pin`'s current digital level."""
        return self._t.send(f"READ {pin}") == "1"

    def read_all(self) -> dict[int, bool]:
        """Reads every usable pin's level in one round-trip.

        Returns:
            A dict covering GPIO0-GPIO30. Reserved pins are always False.
        """
        mask = int(self._t.send("READALL"), 16)
        return {pin: bool(mask & (1 << pin)) for pin in range(MIN_GPIO, MAX_GPIO + 1)}

    # -- PWM ----------------------------------------------------------------

    def pwm(self, pin: int, freq_hz: int, duty_pct: float) -> None:
        """Starts (or reconfigures) PWM output on `pin`.

        Args:
            freq_hz: PWM frequency in Hz.
            duty_pct: Duty cycle, 0-100.
        """
        self._t.send(f"PWM {pin} {int(freq_hz)} {round(duty_pct)}")

    def pwm_stop(self, pin: int) -> None:
        """Stops PWM on `pin` and detaches it back to a plain GPIO."""
        self._t.send(f"PWMSTOP {pin}")

    # -- ADC ------------------------------------------------------------------

    def adc(self, pin: int) -> tuple[int, int]:
        """Reads `pin` as an analog input.

        Returns:
            `(raw, millivolts)` - raw is 0-4095, millivolts is calibrated
            (or a linear approximation; see docs/protocol.md).
        """
        raw_str, mv_str = self._t.send(f"ADC {pin}").split()
        return int(raw_str), int(mv_str)

    # -- Housekeeping ---------------------------------------------------------

    def version(self) -> str:
        """Returns the firmware's `usbgpio <semver>` version string."""
        return self._t.send("VERSION")

    def reset(self) -> None:
        """Returns every usable pin to input/no-pull and stops all PWM."""
        self._t.send("RESET")
