# Changelog

All notable changes to this project are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/); this project
uses [Semantic Versioning](https://semver.org/). The version here, the
firmware's `VERSION` reply, and the Python package version are always kept
in sync (CLAUDE.md, "Semver rules").

## [0.1.0] - 2026-08-09

Initial scaffold: v1 protocol, firmware, and host package.

### Added

- Firmware (ESP-IDF, ESP32-C6): line protocol over USB Serial/JTAG
  (`MODE`, `WRITE`, `READ`, `READALL`, `PWM`, `PWMSTOP`, `ADC`, `VERSION`,
  `RESET`), reserved-pin validation, `READY` boot banner. See
  [`docs/protocol.md`](docs/protocol.md).
- Host Python package `usbgpio`: `Board` API over pyserial, port
  auto-detection by USB VID, one-shot CLI (`usbgpio <command>`), a
  Textual-based interactive TUI (`usbgpio tui`), and TOML profiles
  (`usbgpio apply-profile <name>`).
- Host test suite runs without hardware (mocked serial); a hardware smoke
  test suite at `host/tests/hw/` runs against a real board when
  `USBGPIO_HW_TEST=1` is set.
- `README.md` user manual, `docs/protocol.md` protocol reference.

[0.1.0]: https://github.com/makimar/USB-GPIO/releases/tag/v0.1.0
