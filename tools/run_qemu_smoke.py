#!/usr/bin/env python3

import argparse
import enum
import os
import re
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


PANIC_MARKER = "MICROS_PANIC"
FAILURE_MARKER = "MICROS_TEST_FAILURE"
FDT_COUNTS_PATTERN = re.compile(
    r"^MICROS_FDT_COUNTS "
    r"memory=0x([0-9a-f]{16}) "
    r"reservation=0x([0-9a-f]{16}) "
    r"reserved-memory=0x([0-9a-f]{16})$"
)
FDT_EVENT_NAMES = (
    "MICROS_FDT_MEMORY",
    "MICROS_FDT_RESERVATION",
    "MICROS_FDT_RESERVED_MEMORY",
)
FDT_RANGE_PATTERN = re.compile(
    r"^(MICROS_FDT_MEMORY|"
    r"MICROS_FDT_RESERVATION|"
    r"MICROS_FDT_RESERVED_MEMORY) "
    r"base=0x[0-9a-f]{16} size=0x[0-9a-f]{16}$"
)


class SmokeOutcome(enum.Enum):
    PASS = "pass"
    FAILURE = "failure"
    PANIC = "panic"
    UNEXPECTED_EXIT = "unexpected-exit"
    TIMEOUT = "timeout"


@dataclass(frozen=True)
class QemuResult:
    output: str
    return_code: Optional[int]
    timed_out: bool


def _has_complete_fdt_events(output_lines, require_reservations):
    summaries = []
    actual_counts = [0, 0, 0]

    for line in output_lines:
        if line.startswith("MICROS_FDT_COUNTS"):
            match = FDT_COUNTS_PATTERN.fullmatch(line)
            if match is None:
                return False
            summaries.append(tuple(int(value, 16) for value in match.groups()))
            continue
        if line.startswith(FDT_EVENT_NAMES):
            match = FDT_RANGE_PATTERN.fullmatch(line)
            if match is None:
                return False
            actual_counts[FDT_EVENT_NAMES.index(match.group(1))] += 1
    if len(summaries) != 1:
        return False
    if require_reservations and summaries[0][1] + summaries[0][2] == 0:
        return False

    return tuple(actual_counts) == summaries[0]


def classify_smoke(
    *,
    output,
    return_code,
    timed_out,
    markers,
    require_fdt_events=False,
    require_fdt_reservations=False,
):
    if PANIC_MARKER in output:
        return SmokeOutcome.PANIC
    if FAILURE_MARKER in output or any(
        line.startswith("not ok ") for line in output.splitlines()
    ):
        return SmokeOutcome.FAILURE
    if timed_out:
        return SmokeOutcome.TIMEOUT
    output_lines = output.splitlines()
    if (
        return_code == 0
        and all(marker in output_lines for marker in markers)
        and (
            not (require_fdt_events or require_fdt_reservations)
            or _has_complete_fdt_events(
                output_lines,
                require_reservations=require_fdt_reservations,
            )
        )
    ):
        return SmokeOutcome.PASS
    return SmokeOutcome.UNEXPECTED_EXIT


def build_qemu_command(*, qemu, kernel):
    return [
        qemu,
        "-machine",
        "virt,aia=none",
        "-cpu",
        "rv64",
        "-smp",
        "1",
        "-m",
        "128M",
        "-display",
        "none",
        "-monitor",
        "none",
        "-serial",
        "stdio",
        "-nic",
        "none",
        "-bios",
        "default",
        "-kernel",
        kernel,
        "-no-reboot",
    ]


def _decode_output(output):
    if output is None:
        return ""
    if isinstance(output, bytes):
        return output.decode("utf-8", errors="replace")
    return output


def run_qemu(command, timeout_seconds):
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


def print_tap_result(outcome, result, command):
    print("TAP version 13")
    if outcome is SmokeOutcome.PASS:
        print("ok 1 - versioned boot marker followed by clean QEMU shutdown")
        print("# outcome: pass")
        return 0

    print(f"not ok 1 - QEMU smoke test reported {outcome.value}")
    print(f"# outcome: {outcome.value}")
    print(f"# command: {shlex.join(command)}")
    if result.return_code is None:
        print("# qemu return code: none")
    else:
        print(f"# qemu return code: {result.return_code}")
    for line in result.output.rstrip().splitlines():
        print(f"# {line}")

    return {
        SmokeOutcome.FAILURE: 1,
        SmokeOutcome.PANIC: 2,
        SmokeOutcome.UNEXPECTED_EXIT: 3,
        SmokeOutcome.TIMEOUT: 4,
    }[outcome]


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        description="Run the deterministic micros QEMU boot smoke test."
    )
    parser.add_argument(
        "--qemu",
        default=os.environ.get("MICROS_QEMU", "qemu-system-riscv64"),
        help="QEMU RISC-V system executable",
    )
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument(
        "--marker",
        action="append",
        required=True,
        help="Exact serial line required for success; may be repeated",
    )
    parser.add_argument(
        "--require-fdt-events",
        action="store_true",
        help="Require FDT range events to match the guest's count summary",
    )
    parser.add_argument(
        "--require-fdt-reservations",
        action="store_true",
        help="Require at least one firmware reservation in the FDT events",
    )
    parser.add_argument("--timeout", type=float, default=10.0)
    arguments = parser.parse_args(argv)

    if arguments.timeout <= 0:
        parser.error("--timeout must be greater than zero")
    if any(not marker for marker in arguments.marker):
        parser.error("--marker values must not be empty")
    if not arguments.kernel.is_file():
        parser.error(f"kernel image does not exist: {arguments.kernel}")
    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)
    command = build_qemu_command(
        qemu=arguments.qemu,
        kernel=str(arguments.kernel.resolve()),
    )
    try:
        result = run_qemu(command, arguments.timeout)
    except FileNotFoundError:
        print(f"QEMU executable not found: {arguments.qemu}", file=sys.stderr)
        return 2

    outcome = classify_smoke(
        output=result.output,
        return_code=result.return_code,
        timed_out=result.timed_out,
        markers=arguments.marker,
        require_fdt_events=arguments.require_fdt_events,
        require_fdt_reservations=arguments.require_fdt_reservations,
    )
    return print_tap_result(outcome, result, command)


if __name__ == "__main__":
    sys.exit(main())
