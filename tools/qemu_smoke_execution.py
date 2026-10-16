import os
import selectors
import subprocess
import time

from tools.qemu_smoke_records import (
    QemuResult,
    _split_output_records,
)


def _decode_output(output):
    if output is None:
        return ""
    if isinstance(output, bytes):
        return output.decode("utf-8", errors="replace")
    return output


def _serial_trigger_observed(output, trigger):
    output_lines, terminated = _split_output_records(
        _decode_output(output)
    )
    return any(
        line == trigger and is_terminated
        for line, is_terminated in zip(output_lines, terminated)
    )


def _run_qemu_with_serial_input(
    command,
    timeout_seconds,
    serial_input_trigger,
    serial_input,
):
    process = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=False,
    )
    if process.stdin is None or process.stdout is None:
        raise RuntimeError("failed to open QEMU serial pipes")

    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    output = bytearray()
    serial_input_sent = False
    stdout_open = True
    deadline = time.monotonic() + timeout_seconds

    try:
        while process.poll() is None or stdout_open:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                process.kill()
                trailing_output, _ = process.communicate()
                if trailing_output:
                    output.extend(trailing_output)
                return QemuResult(
                    output=_decode_output(bytes(output)),
                    return_code=None,
                    timed_out=True,
                    serial_input_sent=serial_input_sent,
                )

            events = selector.select(remaining)
            if not events:
                continue
            for key, _ in events:
                chunk = os.read(key.fd, 4096)
                if not chunk:
                    selector.unregister(process.stdout)
                    stdout_open = False
                    continue
                output.extend(chunk)
                if (
                    not serial_input_sent
                    and _serial_trigger_observed(
                        bytes(output),
                        serial_input_trigger,
                    )
                ):
                    try:
                        written = process.stdin.write(serial_input)
                        process.stdin.flush()
                    except BrokenPipeError:
                        written = 0
                    serial_input_sent = written == len(serial_input)
    finally:
        selector.close()
        if process.stdin is not None:
            try:
                process.stdin.close()
            except BrokenPipeError:
                pass
        if process.stdout is not None:
            process.stdout.close()

    return QemuResult(
        output=_decode_output(bytes(output)),
        return_code=process.returncode,
        timed_out=False,
        serial_input_sent=serial_input_sent,
    )


def run_qemu(
    command,
    timeout_seconds,
    serial_input_trigger=None,
    serial_input=None,
):
    if (serial_input_trigger is None) != (serial_input is None):
        raise ValueError(
            "serial input trigger and payload must be provided together"
        )
    if serial_input_trigger is not None:
        if not serial_input_trigger:
            raise ValueError("serial input trigger must not be empty")
        if not serial_input:
            raise ValueError("serial input payload must not be empty")
        return _run_qemu_with_serial_input(
            command,
            timeout_seconds,
            serial_input_trigger,
            serial_input,
        )

    try:
        completed = subprocess.run(
            command,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout_seconds,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        return QemuResult(
            output=_decode_output(error.stdout),
            return_code=None,
            timed_out=True,
        )

    return QemuResult(
        output=completed.stdout,
        return_code=completed.returncode,
        timed_out=False,
    )
