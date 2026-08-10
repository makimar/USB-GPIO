# Changelog

All notable changes to this project are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/); this project
uses [Semantic Versioning](https://semver.org/). The version here, the
firmware's `VERSION` reply, and the Python package version are always kept
in sync (CLAUDE.md, "Semver rules").

## [0.2.0] - 2026-08-10

Optional WiFi status: connect the board to your network for a read-only
status page and a status LED, without changing the USB protocol at all.

### Added

- Optional WiFi station mode, configured via `idf.py menuconfig` ->
  "USB GPIO Extender" (SSID/password/mDNS hostname). Off by default;
  connects and retries in the background without ever blocking or
  breaking USB serial operation. See `CLAUDE.md`'s "WiFi status" section.
- Read-only HTTP status page (`http://esp32.local/` by default) showing
  every usable pin's live digital level and PWM state, laid out as two
  columns matching the ESP32-C6-DevKitC-1's actual J1/J3 header pinout
  (top to bottom) rather than a plain numeric list. No control
  endpoints - USB serial remains the only way to change pin state.
- mDNS hostname (`esp32.local` by default, configurable).
- Onboard status LED turns solid blue once connected (off otherwise).

### Changed

- Firmware now uses a custom partition table (`firmware/partitions.csv`)
  with a 2MB app partition and the chip's real 4MB flash size, instead of
  the previous 1MB/2MB defaults - room for this and future growth. Built
  size with WiFi+mDNS+HTTP+LED: ~988KB, 52% of the 2MB partition free
  (the WiFi/TCP-IP/crypto stack accounts for most of that - expected and
  unavoidable on any ESP32 WiFi build, not something this project adds
  on top of).
- `firmware/sdkconfig` is no longer committed (WiFi credentials would
  otherwise land in git history via `idf.py menuconfig`); only
  `sdkconfig.defaults` (no secrets) is tracked. See `CLAUDE.md`'s Public
  Repo Rules.

[0.2.0]: https://github.com/makimar/USB-GPIO/releases/tag/v0.2.0

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
