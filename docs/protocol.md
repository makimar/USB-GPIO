# USB GPIO Extender protocol

Line-based, ASCII, human-debuggable (you can talk to the board with
`screen`/`minicom`/`pyserial-miniterm` and no special tooling). Every line
sent to the board is a command; every command gets exactly one reply line.

Transport: USB CDC over the ESP32-C6's built-in USB Serial/JTAG port,
115200 baud (ignored by USB CDC, kept for tooling that insists on a rate).
Lines are terminated with `\n`; `\r\n` is also accepted.

On boot the firmware prints one line, `READY`, before it's ready to accept
commands. The host must tolerate this - in practice, treat any line that
isn't a well-formed `OK ...` / `ERR ...` reply to something you sent as
noise to be skipped, not just the literal string `READY`. (Firmware error
logs, if any, use the same convention: they're never mistaken for a reply
because they never start with `OK` or `ERR <digits>`.)

## Reply framing

Every command produces exactly one of:

```
OK [data]
ERR <code> <message>
```

`data` and `message` never contain a newline. `code` is a small integer:

| Code | Meaning                                              |
|------|-------------------------------------------------------|
| 1    | unknown command                                       |
| 2    | pin reserved (USB Serial/JTAG, strapping, or flash)   |
| 3    | bad arguments (wrong count, non-numeric, out of range)|
| 4    | invalid pin (out of range for this chip, or no ADC channel for `ADC`) |
| 5    | invalid mode string (`MODE`)                          |
| 6    | no free PWM channel (`PWM`) - all 6 LEDC channels in use |
| 7    | PWM not active on this pin (`PWMSTOP`)                |
| 8    | ADC unavailable (driver/calibration init failed)      |

Codes 1 and 2 are part of the protocol's compatibility contract; new
firmware versions won't repurpose them. Codes 3+ are firmware-internal and
may grow, but existing codes won't be repurposed either.

## Commands

```
MODE <pin> <in|in_pu|in_pd|out|out_od>
WRITE <pin> <0|1>
READ <pin>              -> OK 0|1
READALL                 -> OK <hex bitmask>
PWM <pin> <freq_hz> <duty_pct>
PWMSTOP <pin>
ADC <pin>               -> OK <raw 0-4095> <millivolts>
VERSION                 -> OK usbgpio <semver>
RESET                   -> all pins to default (input, no pull), all PWM stopped
```

### `MODE <pin> <mode>`

Sets a pin's mode. `out` and `out_od` both keep the pin's input buffer
enabled (not plain "output-only"), so `READ`/`READALL` reflect what the pin
is actually driving, not just what you last wrote:

- `in` - floating input
- `in_pu` - input with internal pull-up
- `in_pd` - input with internal pull-down
- `out` - push-pull output, readable back
- `out_od` - open-drain output, readable back. Reading `1` after writing
  `1` (released) requires a pull-up - internal (`in_pu`-style, not exposed
  as a combined mode in v1) or external. Without one, a released open-drain
  pin floats and will typically read back `0`.

### `WRITE <pin> <0|1>`

Drives `pin` high or low. `pin` must currently be in `out` or `out_od`
mode; the firmware does not implicitly change a pin's mode.

### `READ <pin>` / `READALL`

`READ` returns one pin's current digital level. `READALL` returns every
GPIO0-GPIO30 pin's level packed into a hex bitmask (bit N = pin N);
reserved pins always read as 0 in the mask regardless of their electrical
state.

### `PWM <pin> <freq_hz> <duty_pct>` / `PWMSTOP <pin>`

Starts (or reconfigures) PWM output on `pin` using one of the chip's 6 LEDC
channels; `duty_pct` is 0-100 and is rounded to the LEDC hardware's 10-bit
duty resolution. `PWMSTOP` releases the channel and returns the pin to a
plain GPIO (equivalent to `gpio_reset_pin`).

**Hardware limit:** the ESP32-C6 has 6 LEDC channels but only 4 timers,
so channel N and channel N+4 share a timer. Two *simultaneously active*
channels sharing a timer cannot run independent frequencies - reconfiguring
one's frequency silently changes the other's too. Stick to 4 or fewer
concurrent distinct frequencies if this matters to you; duty cycle is
always independent per channel regardless.

### `ADC <pin>`

Reads `pin` as an analog input via ADC1 (only GPIO0-GPIO6 have an ADC1
channel on this chip; `ADC` on any other pin returns `ERR 4`). Uses 12 dB
attenuation, giving the widest input range (up to ~3.3V, accuracy drops
near the rails). `millivolts` comes from the chip's factory-calibrated
curve-fitting scheme when available; if calibration eFuses aren't burnt
for this chip/scheme, the firmware falls back to a linear approximation
(`raw * 3300 / 4095`), which is measurably less accurate, especially at
the extremes. There's no way to tell from the reply alone which path was
used.

### `VERSION`

Replies `OK usbgpio <semver>`. This is the compatibility check between
firmware and host - see CLAUDE.md's semver rules for what bumps mean.

### `RESET`

Stops all active PWM channels and returns every usable pin to `in` (no
pull). Reserved pins are untouched (they're never configurable in the
first place).

## Reserved pins

`MODE`, `WRITE`, `PWM`, and (implicitly, via range/ADC-capability checks)
`ADC` reject reserved pins with `ERR 2 pin reserved`:

- **USB Serial/JTAG:** GPIO12, GPIO13 - this is the transport itself.
- **Strapping:** GPIO4, GPIO5, GPIO8, GPIO9, GPIO15 - the full ESP32-C6
  strapping set (GPIO8/GPIO9 select boot mode; GPIO4/GPIO5/GPIO15 affect
  JTAG signal source selection), confirmed against esptool's boot-mode
  docs. An earlier version of this list only had GPIO8/GPIO9.
- **Embedded flash:** GPIO24-GPIO30, assumed present on-chip/in-package
  (true for the ESP32-C6-WROOM-1 and ESP32-C6-MINI-1 modules, and for
  bare ESP32-C6FH4 chips with embedded flash). If you're on hardware with
  external flash wiring instead, verify this range against your exact
  board before trusting it.

`READ`/`WRITE`/`MODE` on a pin number outside GPIO0-GPIO30 returns
`ERR 4 pin out of range` rather than `ERR 2`.

## Compatibility

This is v1 of the protocol. Deliberately left out, but reserved room for
without breaking changes: interrupts/events pushed unprompted from board
to host, I2C/SPI passthrough, pulse counting, RMT, and a binary framing
mode. See CLAUDE.md's Roadmap.

## WiFi status page (out of band, v0.2.0+)

The optional WiFi status page (see CLAUDE.md's "WiFi status" section and
the README) is **not part of this protocol** - it's a read-only HTML page
served over HTTP once connected, has no relation to the USB serial line
protocol above, and requires no changes here to add or change. USB serial
remains the only way to send commands to the board.
