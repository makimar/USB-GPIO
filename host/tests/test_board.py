from __future__ import annotations

import pytest
from conftest import QueuedLineSerial, ScriptedSerial

from usbgpio.board import Board
from usbgpio.exceptions import ConnectionTimeoutError, ProtocolError
from usbgpio.transport import LineTransport


def make_board(script: list[tuple[str, str]]) -> Board:
    return Board(LineTransport(ScriptedSerial(script)))


def test_mode_sends_expected_command():
    make_board([("MODE 4 out", "OK")]).mode(4, "out")


def test_write_sends_0_or_1():
    make_board([("WRITE 4 1", "OK")]).write(4, True)
    make_board([("WRITE 4 0", "OK")]).write(4, False)


def test_read_parses_level():
    assert make_board([("READ 4", "OK 1")]).read(4) is True
    assert make_board([("READ 4", "OK 0")]).read(4) is False


def test_read_all_decodes_bitmask():
    result = make_board([("READALL", "OK 11")]).read_all()  # 0x11 = pins 0 and 4
    assert result[0] is True
    assert result[4] is True
    assert result[1] is False
    assert result[30] is False


def test_pwm_sends_freq_and_rounded_duty():
    make_board([("PWM 5 1000 50", "OK")]).pwm(5, 1000, 50.4)


def test_pwm_stop():
    make_board([("PWMSTOP 5", "OK")]).pwm_stop(5)


def test_adc_parses_raw_and_millivolts():
    raw, mv = make_board([("ADC 2", "OK 2048 1650")]).adc(2)
    assert (raw, mv) == (2048, 1650)


def test_version_returns_full_reply_data():
    assert make_board([("VERSION", "OK usbgpio 0.1.0")]).version() == "usbgpio 0.1.0"


def test_reset():
    make_board([("RESET", "OK")]).reset()


def test_err_reply_raises_protocol_error_with_code_and_message():
    with pytest.raises(ProtocolError) as exc_info:
        make_board([("MODE 12 out", "ERR 2 pin reserved")]).mode(12, "out")
    assert exc_info.value.code == 2
    assert exc_info.value.message == "pin reserved"


def test_boot_ready_banner_is_skipped_before_first_reply():
    ser = QueuedLineSerial([b"READY\n", b"OK usbgpio 0.1.0\n"])
    assert Board(LineTransport(ser)).version() == "usbgpio 0.1.0"


def test_arbitrary_noise_lines_are_skipped_not_just_ready():
    # docs/protocol.md: any line that isn't a well-formed OK/ERR reply is
    # noise to skip - not just the literal "READY" banner. Regression
    # test for a real bug: a stray line (observed in practice as firmware
    # log output landing on the same stream) was previously treated as
    # if it were the actual reply, corrupting the result instead of being
    # skipped.
    ser = QueuedLineSerial([b"W (1234) wifi: some log line\n", b"OK usbgpio 0.1.0\n"])
    assert Board(LineTransport(ser)).version() == "usbgpio 0.1.0"


def test_no_reply_raises_timeout():
    ser = QueuedLineSerial([])  # readline() returns b"" immediately (simulated timeout)
    with pytest.raises(ConnectionTimeoutError):
        Board(LineTransport(ser)).version()


def test_close_closes_underlying_serial():
    ser = ScriptedSerial([])
    Board(LineTransport(ser)).close()
    assert ser.closed
