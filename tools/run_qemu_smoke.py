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
TRAP_READY_MARKER = "MICROS_TRAP_READY"
TRAP_TEST_MARKER = "MICROS_TRAP_TEST"
TRAP_TEST_PASS = (
    "MICROS_TRAP_TEST_PASS "
    "origin=S cause=illegal-instruction registers=preserved"
)
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
TRAP_CONTEXT_PATTERN = re.compile(
    r"^MICROS_TRAP_CONTEXT "
    r"origin=[SU] "
    r"sstatus=0x[0-9a-f]{16} "
    r"scause=0x[0-9a-f]{16} "
    r"stval=0x[0-9a-f]{16} "
    r"sepc=0x([0-9a-f]{16}) "
    r"ra=0x[0-9a-f]{16} "
    r"sp=0x[0-9a-f]{16}$"
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


def _split_output_records(output):
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
    return output_lines, terminated


def _has_complete_panic_report(output):
    output_lines, terminated = _split_output_records(output)

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


def _has_complete_trap_ready(output_lines):
    indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(TRAP_READY_MARKER)
    ]
    return (
        len(indices) == 1
        and output_lines[indices[0]] == TRAP_READY_MARKER
    )


def _has_complete_trap_test_report(output_lines):
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(TRAP_READY_MARKER)
    ]
    test_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(TRAP_TEST_MARKER)
    ]
    fdt_ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith("MICROS_FDT_READY")
    ]
    return (
        len(ready_indices) == 1
        and output_lines[ready_indices[0]] == TRAP_READY_MARKER
        and len(fdt_ready_indices) == 1
        and output_lines[fdt_ready_indices[0]] == "MICROS_FDT_READY"
        and len(test_indices) == 1
        and output_lines[test_indices[0]] == TRAP_TEST_PASS
        and ready_indices[0] < fdt_ready_indices[0] < test_indices[0]
    )


def _has_complete_trap_context(output, expected_sepc=None):
    output_lines, terminated = _split_output_records(output)
    panic_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(PANIC_MARKER)
    ]
    context_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith("MICROS_TRAP_CONTEXT")
    ]
    if (
        len(panic_indices) != len(PANIC_CORE_PATTERNS)
        or len(context_indices) != 1
    ):
        return False

    context_index = context_indices[0]
    match = TRAP_CONTEXT_PATTERN.fullmatch(output_lines[context_index])
    if (
        context_index != panic_indices[-1] + 1
        or not terminated[context_index]
        or match is None
        or TRAP_TEST_PASS in output_lines
    ):
        return False
    return (
        expected_sepc is None
        or int(match.group(1), 16) == expected_sepc
    )


def parse_nm_symbol_address(output, symbol):
    addresses = []

    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 3 or fields[-1] != symbol:
            continue
        try:
            addresses.append(int(fields[0], 16))
        except ValueError:
            continue
    if len(addresses) != 1:
        raise ValueError(
            f"expected exactly one defined symbol {symbol!r}, "
            f"found {len(addresses)}"
        )
    return addresses[0]


def resolve_symbol_address(nm, kernel, symbol):
    completed = subprocess.run(
        [nm, "-n", "--defined-only", str(kernel)],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or "no diagnostic"
        raise RuntimeError(f"symbol inspection failed: {detail}")
    return parse_nm_symbol_address(completed.stdout, symbol)


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
    require_trap_ready=False,
    require_trap_test_report=False,
    require_trap_context=False,
    expected_trap_context_sepc=None,
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
    if require_trap_ready and not _has_complete_trap_ready(output_lines):
        return False
    if (
        require_trap_test_report
        and not _has_complete_trap_test_report(output_lines)
    ):
        return False
    has_trap_context = any(
        line.startswith("MICROS_TRAP_CONTEXT")
        for line in output_lines
    )
    if (
        require_trap_context
        and not _has_complete_trap_context(
            result.output,
            expected_sepc=expected_trap_context_sepc,
        )
    ):
        return False
    if not require_trap_context and has_trap_context:
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
    parser.add_argument(
        "--nm",
        default=os.environ.get("MICROS_NM", "llvm-nm"),
        help="LLVM nm executable used for exact target-symbol checks",
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
    parser.add_argument(
        "--require-trap-ready",
        action="store_true",
        help="Require exactly one trap-ready record",
    )
    parser.add_argument(
        "--require-trap-test-report",
        action="store_true",
        help="Require the ordered trap recovery test records",
    )
    parser.add_argument(
        "--require-trap-context",
        action="store_true",
        help="Require one trap context immediately after the panic core",
    )
    parser.add_argument(
        "--trap-context-sepc-symbol",
        help="ELF symbol whose address must equal trap-context sepc",
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
    if (
        arguments.trap_context_sepc_symbol
        and not arguments.require_trap_context
    ):
        parser.error(
            "--trap-context-sepc-symbol requires "
            "--require-trap-context"
        )
    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)
    expected_trap_context_sepc = None
    if arguments.trap_context_sepc_symbol:
        try:
            expected_trap_context_sepc = resolve_symbol_address(
                arguments.nm,
                arguments.kernel,
                arguments.trap_context_sepc_symbol,
            )
        except (FileNotFoundError, RuntimeError, ValueError) as error:
            print(str(error), file=sys.stderr)
            return 2

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
        require_trap_ready=arguments.require_trap_ready,
        require_trap_test_report=arguments.require_trap_test_report,
        require_trap_context=arguments.require_trap_context,
        expected_trap_context_sepc=expected_trap_context_sepc,
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
