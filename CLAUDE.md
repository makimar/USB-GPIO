# CLAUDE.md

## Project: USB GPIO Extender (ESP32-C6)

Extend GPIO pins of an ESP32-C6 to the host computer over USB. The host
controls pins as if they were local. Host is macOS today, Linux later —
all host code must be portable across both (no macOS-only dependencies).

- **Repository:** https://github.com/makimar/USB-GPIO — **public**
- **License:** MIT (`LICENSE` file in repo root, copyright Marko)

## Architecture

Two components in one repo:

```
firmware/     ESP32-C6 firmware (ESP-IDF, C)
host/         Host library + CLI (Python 3.10+)
docs/         Protocol spec and notes
README.md     User manual (see Documentation section)
CHANGELOG.md  Version history (see Documentation section)
LICENSE       MIT
```

- **Transport:** USB CDC via the C6's built-in USB Serial/JTAG port.
  Appears as `/dev/cu.usbmodem*` on macOS, `/dev/ttyACM*` on Linux.
  No custom USB driver — plain serial, 115200 baud (rate is ignored by
  USB CDC anyway).
- **Firmware:** ESP-IDF (not Arduino). Single task reads commands from
  USB, executes, replies. Optional WiFi (see "WiFi status" below) runs
  independently of that task and can never block or replace it.
- **Host:** Python package `usbgpio` using `pyserial`. Three entry
  points, all built on the same `Board` API:
  - Library: `from usbgpio import Board`
  - CLI one-shots: `usbgpio write 4 1`
  - TUI: `usbgpio tui` — interactive terminal UI (see below)

## v1 Feature Scope

| Feature      | Details                                            |
|--------------|----------------------------------------------------|
| Pin mode     | input, input_pullup, input_pulldown, output, output_od |
| Digital write| high / low                                         |
| Digital read | single pin or all pins at once                     |
| PWM          | LEDC, up to 6 channels, freq + duty (0–100 %)      |
| ADC          | 12-bit read, ADC1 channels (GPIO0–GPIO6)           |

## TUI (menu interface)

Built with **Textual**. Launched with `usbgpio tui [--profile <name>]`.
Purpose: configure how the board behaves each session without writing
code.

- **Pin table view:** all usable pins with mode, current state/value,
  PWM freq/duty. Refreshes ~2×/s (READALL + ADC polls).
- **Actions:** arrow keys to select a pin; keybindings to set mode,
  toggle output, edit PWM freq/duty, start/stop ADC watch.
- **Profiles:** named pin configurations stored as TOML in
  `~/.config/usbgpio/profiles/<name>.toml` (XDG path — same on macOS
  and Linux). A profile lists mode + initial state/PWM per pin.
  On TUI start: pick a profile from a list (or "blank"), it is applied
  to the board via MODE/WRITE/PWM commands. Current setup can be saved
  as a new profile at any time.
- Profile example:

```toml
# ~/.config/usbgpio/profiles/relay-tester.toml
name = "relay-tester"
[pins.4]
mode = "out"
state = 0
[pins.5]
mode = "pwm"
freq = 1000
duty = 50
[pins.3]
mode = "in_pu"
```

- The TUI contains **no protocol logic** — it only calls the `Board`
  API. Profiles are also loadable headlessly:
  `usbgpio apply-profile relay-tester` (for scripts / boot setup on
  the future Linux box).

Explicitly **out of scope for v1:** interrupts/events pushed to host,
I2C/SPI passthrough, pulse counting, RMT. Design the protocol so these
can be added without breaking changes.

## WiFi status (optional, v0.2.0+)

USB serial is, and remains, the only way to control pins — this is
additive and read-only, never a second control path. Off by default;
configured via `idf.py menuconfig` → "USB GPIO Extender" (WiFi
SSID/password/mDNS hostname). With SSID blank, none of this code runs
and the board behaves exactly like a USB-only build.

- **`firmware/main/wifi.c`:** station-mode connect, entirely
  event/callback-driven. Called once from `app_main`, *after* the USB
  driver is up and `READY` has been sent — must never delay or block USB
  availability, and a WiFi failure (bad password, AP unreachable, driver
  init failure) must never crash or hang the device. Retries
  indefinitely on disconnect.
- **`firmware/main/led_status.c`:** on connect, sets the onboard
  addressable status LED (`led_strip` component, GPIO8 on the
  ESP32-C6-DevKitC-1) solid blue; off while disconnected.
- **`firmware/main/http_status.c`:** a **read-only** status page (GET
  `/`) once connected — current level and (if active) PWM freq/duty for
  every usable pin. No write/control endpoints; adding one would break
  the "USB is the only control path" invariant above.
- **mDNS:** board is reachable at `<hostname>.local` (default
  `esp32.local`) once connected.
- Credentials are set via `idf.py menuconfig` into the local (gitignored)
  `sdkconfig` — never in `sdkconfig.defaults` or anywhere else committed.
  See "Public Repo Rules".

## Protocol (line-based, human-debuggable)

ASCII lines terminated with `\n`. Every command gets exactly one reply:
`OK [data]` or `ERR <code> <message>`.

```
MODE <pin> <in|in_pu|in_pd|out|out_od>
WRITE <pin> <0|1>
READ <pin>              -> OK 0|1
READALL                 -> OK <hex bitmask>
PWM <pin> <freq_hz> <duty_pct>
PWMSTOP <pin>
ADC <pin>               -> OK <raw 0-4095> <millivolts>
VERSION                 -> OK usbgpio <semver>
RESET                   -> all pins to default (input, no pull)
```

Rules:
- Firmware validates pin numbers; reserved pins (USB: GPIO12/13,
  strapping: GPIO8/9, flash pins) return `ERR 2 pin reserved`.
- Unknown command → `ERR 1 unknown command`. Never crash on bad input.
- Host must tolerate a `READY` banner line the firmware prints on boot.

## Build & Run

Firmware:
```bash
cd firmware
idf.py set-target esp32c6
idf.py menuconfig   # optional: WiFi SSID/password under "USB GPIO Extender"
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

Host:
```bash
cd host
python -m venv .venv && source .venv/bin/activate
pip install -e ".[dev]"
usbgpio --port /dev/cu.usbmodem101 read 4
pytest
```

## Conventions

- Python: type hints everywhere, `ruff` for lint/format, `pytest` for
  tests. Serial port auto-detect by USB VID/PID (Espressif 303a),
  overridable with `--port`. Dependencies: `pyserial`, `textual`,
  `tomli-w` (TOML read is stdlib `tomllib`).
- C: ESP-IDF style, one module per protocol area (`cmd_gpio.c`,
  `cmd_pwm.c`, `cmd_adc.c`), no dynamic allocation in the command path.
- Protocol changes require updating `docs/protocol.md`, firmware, and
  host in the same commit. `VERSION` reply is the compatibility check.
- Host tests must run without hardware: mock the serial layer. Hardware
  smoke test lives in `host/tests/hw/` and is skipped unless
  `USBGPIO_HW_TEST=1`.

## Documentation

- **README.md is the user manual.** It must always cover: what the
  project does, hardware setup (which pins are usable/reserved, wiring
  warnings — 3.3V logic, max pin current), flashing the firmware,
  installing the host package, TUI usage with keybindings, profile file
  format, CLI examples, and Linux notes (udev/dialout). Written for a
  user who has never seen the code. Update README.md in the same commit
  as any user-visible change — a feature that isn't in the manual
  doesn't exist.
- **CHANGELOG.md is the version history.** Keep a Changelog format
  (keepachangelog.com), newest first, sections Added/Changed/Fixed/
  Removed. Every release gets an entry with date and semver. The
  version in CHANGELOG.md, the firmware `VERSION` reply, and the Python
  package version must always match — bump all three together.
- Semver rules: protocol-breaking change = major, new command/feature =
  minor, fix = patch.

## Public Repo Rules

This is a public repository. Non-negotiables:

- **Never commit secrets** — no WiFi credentials, API keys, or tokens
  anywhere, including firmware `sdkconfig` and test scripts. This is why
  `firmware/sdkconfig` (the generated, locally-editable config —
  WiFi credentials in particular go here via `idf.py menuconfig`) is
  gitignored rather than committed, unlike most ESP-IDF example projects;
  only `sdkconfig.defaults` (no secrets, baseline options only) is
  tracked.
- `.gitignore` covers: `firmware/build/`, `firmware/sdkconfig`,
  `sdkconfig.old`, `firmware/managed_components/` (Component-Manager
  fetched deps — `dependencies.lock` itself is committed), `host/.venv/`,
  `__pycache__/`, `*.egg-info/`, `dist/`, `.DS_Store`.
- No personal data in code or examples (paths like `/Users/marko/...`
  → use generic placeholders).
- Releases are git tags `v<semver>` matching the CHANGELOG entry.
- Write commit messages and all docs in English; assume strangers read
  everything.

## Portability Notes (macOS → Linux)

- Never hardcode `/dev/cu.*`; use `serial.tools.list_ports` and match
  on VID/PID.
- On Linux the user needs dialout group or a udev rule — document in
  README, don't work around it in code.
- No shell-outs to OS-specific tools.

## Roadmap (post-v1)

1. Async event push (pin change interrupts → host callbacks)
2. I2C/SPI bridge commands
3. Pulse counter for encoders / RPM
4. Optional binary protocol mode if line protocol becomes a bottleneck
