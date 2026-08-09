"""Named pin configurations stored as TOML (CLAUDE.md, "TUI" -> "Profiles").

Profiles live at ~/.config/usbgpio/profiles/<name>.toml (XDG path, same on
macOS and Linux) and are loadable both from the TUI and headlessly via
`usbgpio apply-profile <name>`.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import Any

import tomli_w

# tomllib is stdlib from Python 3.11; this project's floor is 3.10
# (CLAUDE.md, "Conventions"), so fall back to the `tomli` backport there.
if sys.version_info >= (3, 11):
    import tomllib
else:
    import tomli as tomllib

from usbgpio.board import Board
from usbgpio.exceptions import BoardError

PROFILE_EXT = ".toml"


def profiles_dir() -> Path:
    """Returns ~/.config/usbgpio/profiles (respecting $XDG_CONFIG_HOME)."""
    base = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return Path(base) / "usbgpio" / "profiles"


def list_profiles() -> list[str]:
    """Returns the names of all saved profiles, sorted."""
    d = profiles_dir()
    if not d.is_dir():
        return []
    return sorted(p.stem for p in d.glob(f"*{PROFILE_EXT}"))


def load_profile(name: str) -> dict[str, Any]:
    """Loads and parses `<name>.toml`.

    Raises:
        BoardError: no such profile exists.
    """
    path = profiles_dir() / f"{name}{PROFILE_EXT}"
    if not path.is_file():
        raise BoardError(f"no such profile: {name!r} (looked in {path})")
    with path.open("rb") as f:
        return tomllib.load(f)


def save_profile(name: str, data: dict[str, Any]) -> Path:
    """Writes `data` to `<name>.toml`, creating the profiles dir if needed.

    Returns:
        The path written to.
    """
    d = profiles_dir()
    d.mkdir(parents=True, exist_ok=True)
    path = d / f"{name}{PROFILE_EXT}"
    with path.open("wb") as f:
        tomli_w.dump(data, f)
    return path


def apply_profile(board: Board, profile: dict[str, Any]) -> None:
    """Applies a parsed profile to `board` via MODE/WRITE/PWM commands.

    Unknown/malformed pin entries raise BoardError rather than being
    silently skipped, since a half-applied profile is worse than none.
    """
    pins = profile.get("pins", {})
    for pin_key, cfg in pins.items():
        try:
            pin = int(pin_key)
        except ValueError as exc:
            raise BoardError(f"invalid pin key in profile: {pin_key!r}") from exc
        mode = cfg.get("mode")
        if mode is None:
            raise BoardError(f"pin {pin}: missing 'mode'")

        if mode == "pwm":
            freq = cfg.get("freq")
            duty = cfg.get("duty")
            if freq is None or duty is None:
                raise BoardError(f"pin {pin}: pwm mode requires 'freq' and 'duty'")
            board.pwm(pin, int(freq), float(duty))
            continue

        board.mode(pin, mode)
        if mode in ("out", "out_od") and "state" in cfg:
            board.write(pin, bool(cfg["state"]))


def snapshot_profile(name: str, pins: dict[int, dict[str, Any]]) -> dict[str, Any]:
    """Builds a profile dict (as accepted by `save_profile`) from `pins`.

    `pins` maps pin number to the same per-pin fields used in the TOML
    (mode, state, freq, duty) - assembled by the caller (typically the TUI)
    from its current on-screen state.
    """
    return {"name": name, "pins": {str(pin): cfg for pin, cfg in pins.items()}}
