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
PANIC_CORE_PATTERNS = (
    re.compile(r"^MICROS_PANIC reason=[a-z0-9]+(?:-[a-z0-9]+)*$"),
    re.compile(
        r"^MICROS_PANIC_BUILD "
        r"version=[0-9]+[.][0-9]+[.][0-9]+$"
    ),
    re.compile(
        r"^MICROS_PANIC_SOURCE "
        r"file=([A-Za-z0-9_./-]+) line=0x[0-9a-f]{16}$"
    ),
    re.compile(
        r"^MICROS_PANIC_HART mode=S id=0x[0-9a-f]{16}$"
    ),
    re.compile(
        r"^MICROS_PANIC_MACHINE "
        r"sstatus=0x[0-9a-f]{16} "
        r"scause=0x[0-9a-f]{16} "
        r"stval=0x[0-9a-f]{16} "
        r"sepc=0x[0-9a-f]{16} "
        r"ra=0x[0-9a-f]{16} "
        r"sp=0x[0-9a-f]{16}$"
    ),
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


def _has_complete_panic_report(output):
    records = output.splitlines(keepends=True)
    output_lines = []
    terminated = []

    for record in records:
        if record.endswith("\r\n"):
            output_lines.append(record[:-2])
            terminated.append(True)
        elif record.endswith("\n"):
            output_lines.append(record[:-1])
            terminated.append(True)
        else:
            output_lines.append(record)
            terminated.append(False)

    indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(PANIC_MARKER)
    ]
    if len(indices) != len(PANIC_CORE_PATTERNS):
        return False
    if indices != list(range(indices[0], indices[0] + len(indices))):
        return False
    if not all(terminated[index] for index in indices):
        return False

    for pattern, index in zip(PANIC_CORE_PATTERNS, indices):
        match = pattern.fullmatch(output_lines[index])
        if match is None:
            return False
        if pattern is PANIC_CORE_PATTERNS[2]:
            source_path = Path(match.group(1))
            if source_path.is_absolute() or ".." in source_path.parts:
                return False
    return True


def _has_required_output(output_lines, markers, patterns):
    if not all(marker in output_lines for marker in markers):
        return False
    return all(
        any(re.fullmatch(pattern, line) for line in output_lines)
        for pattern in patterns
    )


def matches_expected_result(
    *,
    result,
    observed_outcome,
    expected_outcome,
    markers,
    patterns,
    require_fdt_events=False,
    require_fdt_reservations=False,
    require_panic_report=False,
):
    output_lines = result.output.splitlines()

    if (
        observed_outcome is not expected_outcome
        or result.timed_out
        or result.return_code != 0
        or FAILURE_MARKER in result.output
        or any(line.startswith("not ok ") for line in output_lines)
        or not _has_required_output(output_lines, markers, patterns)
    ):
        return False
    if (
        require_fdt_events or require_fdt_reservations
    ) and not _has_complete_fdt_events(
        output_lines,
        require_reservations=require_fdt_reservations,
    ):
        return False
    if require_panic_report and not _has_complete_panic_report(result.output):
        return False
    return True


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


def print_tap_result(
    outcome,
    result,
    command,
    expected_outcome=SmokeOutcome.PASS,
    accepted=None,
):
    if accepted is None:
        accepted = outcome is SmokeOutcome.PASS

    print("TAP version 13")
    if accepted:
        print(
            "ok 1 - QEMU smoke test observed expected "
            f"{expected_outcome.value}"
        )
        print(f"# outcome: {outcome.value}")
        print(f"# expected outcome: {expected_outcome.value}")
        return 0

    print(
        "not ok 1 - QEMU smoke test expected "
        f"{expected_outcome.value} but reported {outcome.value}"
    )
    print(f"# outcome: {outcome.value}")
    print(f"# expected outcome: {expected_outcome.value}")
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
    }.get(outcome, 3)


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
        "--pattern",
        action="append",
        default=[],
        help="Full-line regular expression required in serial output",
    )
    parser.add_argument(
        "--expect",
        choices=("pass", "panic"),
        default="pass",
        help="Observed outcome required for a successful test",
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
    parser.add_argument(
        "--require-panic-report",
        action="store_true",
        help="Require the ordered five-record panic report",
    )
    parser.add_argument("--timeout", type=float, default=10.0)
    arguments = parser.parse_args(argv)

    if arguments.timeout <= 0:
        parser.error("--timeout must be greater than zero")
    if any(not marker for marker in arguments.marker):
        parser.error("--marker values must not be empty")
    for pattern in arguments.pattern:
        try:
            re.compile(pattern)
        except re.error as error:
            parser.error(f"invalid --pattern {pattern!r}: {error}")
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
    expected_outcome = SmokeOutcome(arguments.expect)
    accepted = matches_expected_result(
        result=result,
        observed_outcome=outcome,
        expected_outcome=expected_outcome,
        markers=arguments.marker,
        patterns=arguments.pattern,
        require_fdt_events=arguments.require_fdt_events,
        require_fdt_reservations=arguments.require_fdt_reservations,
        require_panic_report=arguments.require_panic_report,
    )
    return print_tap_result(
        outcome,
        result,
        command,
        expected_outcome=expected_outcome,
        accepted=accepted,
    )


if __name__ == "__main__":
    sys.exit(main())
