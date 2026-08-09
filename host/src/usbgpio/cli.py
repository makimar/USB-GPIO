"""`usbgpio` command-line entry point: one-shot commands, `tui`, `apply-profile`.

CLAUDE.md, "Architecture": all three host entry points call the same
`Board` API; this module contains no protocol logic of its own.
"""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence

from usbgpio.board import Board, PinMode
from usbgpio.exceptions import BoardError

PIN_MODES: tuple[PinMode, ...] = ("in", "in_pu", "in_pd", "out", "out_od")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="usbgpio", description=__doc__)
    parser.add_argument(
        "--port",
        default=None,
        help="serial port (default: auto-detect by USB VID, see README)",
    )
    parser.add_argument(
        "--baudrate",
        type=int,
        default=115200,
        help="ignored by USB CDC; kept for symmetry with other serial tools",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("mode", help="set a pin's mode")
    p.add_argument("pin", type=int)
    p.add_argument("mode", choices=PIN_MODES)

    p = sub.add_parser("write", help="drive an output pin high/low")
    p.add_argument("pin", type=int)
    p.add_argument("level", type=int, choices=(0, 1))

    p = sub.add_parser("read", help="read a single pin")
    p.add_argument("pin", type=int)

    sub.add_parser("readall", help="read every pin (hex bitmask)")

    p = sub.add_parser("pwm", help="start/update PWM on a pin")
    p.add_argument("pin", type=int)
    p.add_argument("freq_hz", type=int)
    p.add_argument("duty_pct", type=float)

    p = sub.add_parser("pwmstop", help="stop PWM on a pin")
    p.add_argument("pin", type=int)

    p = sub.add_parser("adc", help="read a pin as analog input")
    p.add_argument("pin", type=int)

    sub.add_parser("version", help="print the firmware's version string")
    sub.add_parser("reset", help="reset every pin to input/no-pull, stop all PWM")

    p = sub.add_parser("tui", help="launch the interactive terminal UI")
    p.add_argument("--profile", default=None, help="profile name to start from")

    p = sub.add_parser("apply-profile", help="apply a saved profile headlessly")
    p.add_argument("name")

    return parser


def _run(args: argparse.Namespace) -> int:
    if args.command == "tui":
        from usbgpio.tui import run_tui

        run_tui(port=args.port, baudrate=args.baudrate, profile=args.profile)
        return 0

    with Board.open(args.port, baudrate=args.baudrate) as board:
        if args.command == "mode":
            board.mode(args.pin, args.mode)
        elif args.command == "write":
            board.write(args.pin, args.level)
        elif args.command == "read":
            print(1 if board.read(args.pin) else 0)
        elif args.command == "readall":
            mask = 0
            for pin, level in board.read_all().items():
                mask |= (1 << pin) if level else 0
            print(f"{mask:x}")
        elif args.command == "pwm":
            board.pwm(args.pin, args.freq_hz, args.duty_pct)
        elif args.command == "pwmstop":
            board.pwm_stop(args.pin)
        elif args.command == "adc":
            raw, millivolts = board.adc(args.pin)
            print(f"{raw} {millivolts}")
        elif args.command == "version":
            print(board.version())
        elif args.command == "reset":
            board.reset()
        elif args.command == "apply-profile":
            from usbgpio.profiles import apply_profile, load_profile

            apply_profile(board, load_profile(args.name))
        else:  # pragma: no cover - argparse guarantees a valid command
            raise AssertionError(f"unhandled command: {args.command}")
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return _run(args)
    except BoardError as exc:
        print(f"usbgpio: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
