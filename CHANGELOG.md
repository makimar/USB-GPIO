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
  columns matching the ESP32-C6-DevKitM-1's actual J1/J3 header pinout
  (top to bottom) rather than a plain numeric list. No control
  endpoints - USB serial remains the only way to change pin state.
- mDNS hostname (`esp32.local` by default, configurable).
- Onboard status LED turns solid blue once connected (off otherwise).

### Fixed

- The TUI (`usbgpio tui`) never actually ran - `push_screen_wait()` (used
  for the profile picker and PWM/save-profile prompts) requires an active
  Textual worker, which `on_mount`/action handlers aren't, so every one
  of those raised `NoActiveWorker` on first use. Separately,
  `DataTable.update_cell()` referenced guessed column keys ("1".."5")
  instead of the actual ones, raising `CellDoesNotExist` the moment any
  row was updated. Both were only caught by actually running the app
  (Textual's `run_test()`, headlessly, against real hardware) - a plain
  `import usbgpio.tui` check, which is all that had been done before,
  doesn't exercise either path. Rewritten to use `push_screen(...,
  callback)` (no worker needed) and explicit column keys.
- `DataTable.update_cell()` also needs `update_width=True` to avoid
  silently truncating text longer than the column's initial width -
  caught visually in a screenshot (PWM status showing "50.0" with no
  trailing "%)"), not by the tests, which only checked substrings. The
  regression test now checks the full string.
- Reserved-pin list only covered GPIO8/GPIO9 as strapping pins; the
  ESP32-C6's full strapping set is GPIO4, GPIO5, GPIO8, GPIO9, and
  GPIO15 (confirmed against esptool's boot-mode-selection docs). Those
  three additional pins are now correctly rejected with
  `ERR 2 pin reserved` instead of being silently configurable.
- The status page's pinout diagram (and this project's dev board,
  everywhere it was mentioned) was built against the ESP32-C6-DevKitC-1's
  official pinout - the actual board is an ESP32-C6-DevKitM-1, a
  similarly-named but different board (different module, different pin
  set: DevKitM-1 exposes GPIO14 where DevKitC-1 exposes GPIO10/GPIO11).
  Caught from a photo of the physical board; corrected throughout
  (firmware, README, docs/protocol.md, CLAUDE.md). GPIO8 as the LED pin
  was unaffected - verified correct on both boards.

### Changed

- TUI layout now matches the WiFi status page: two columns following the
  board's physical J1 (left) / J3 (right) header pinout, top to bottom,
  instead of a single flat numeric pin list. `host/tests/test_tui.py`
  added to run every interactive flow headlessly against a mocked board
  (no hardware needed) so regressions like the ones above are caught
  automatically going forward.
- README now includes screenshots of both the TUI and the status page
  (`docs/images/`). The TUI one is genuine, captured by driving the app
  headlessly against real hardware. The status page one is rendered
  locally from the exact template using real USB pin readings, since
  this board's WiFi wasn't connected to fetch the live page directly -
  noted as such in the README.
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
