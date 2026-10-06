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
    "origin=S cause=illegal-instruction registers=preserved "
    "hart-context=routed primary-stack=selected sscratch=anchor"
)
TIMER_TEST_MARKER = "MICROS_TIMER_TEST"
TIMER_TEST_PASS = (
    "MICROS_TIMER_TEST_PASS "
    "ticks=0x0000000000000003 "
    "interval=0x00000000000186a0 "
    "active=0x0000000000000000 "
    "deadline=0xffffffffffffffff owner=hart"
)
FRAME_ALLOCATOR_READY_MARKER = "MICROS_FRAME_ALLOCATOR_READY"
FRAME_ALLOCATOR_TEST_MARKER = "MICROS_FRAME_ALLOCATOR_TEST"
FRAME_ALLOCATOR_READY_PATTERN = re.compile(
    r"^MICROS_FRAME_ALLOCATOR_READY "
    r"managed=0x([0-9a-f]{16}) "
    r"free=0x([0-9a-f]{16})$"
)
FRAME_ALLOCATOR_TEST_PASS = (
    "MICROS_FRAME_ALLOCATOR_TEST_PASS "
    "allocations=0x0000000000000004 "
    "reuse=lowest invariants=preserved"
)
MMU_READY_MARKER = "MICROS_MMU_READY"
MMU_TEST_MARKER = "MICROS_MMU_TEST"
MMU_READY_PATTERN = re.compile(
    r"^MICROS_MMU_READY "
    r"mode=sv39 "
    r"root=0x([0-9a-f]{16}) "
    r"tables=0x([0-9a-f]{16})$"
)
MMU_TEST_PASS = (
    "MICROS_MMU_TEST_PASS "
    "store-fault=text "
    "execute-fault=writable "
    "traps=0x0000000000000002"
)
FRAME_OWNERSHIP_READY_MARKER = "MICROS_FRAME_OWNERSHIP_READY"
FRAME_OWNERSHIP_TEST_MARKER = "MICROS_FRAME_OWNERSHIP_TEST"
FRAME_OWNERSHIP_READY_PATTERN = re.compile(
    r"^MICROS_FRAME_OWNERSHIP_READY "
    r"owned=0x([0-9a-f]{16}) "
    r"kernel-tables=0x([0-9a-f]{16}) "
    r"phase=bootstrap$"
)
FRAME_OWNERSHIP_TEST_PASS = (
    "MICROS_FRAME_OWNERSHIP_TEST_PASS "
    "stale=rejected "
    "release=blocked "
    "handoff=atomic "
    "invariants=preserved"
)
USER_ADDRESS_SPACE_TEST_MARKER = "MICROS_USER_ADDRESS_SPACE_TEST"
USER_ADDRESS_SPACE_TEST_PASS = (
    "MICROS_USER_ADDRESS_SPACE_TEST_PASS "
    "roots=isolated "
    "reuse=zeroed "
    "active=guarded "
    "ownership=validated "
    "sum=cleared"
)
USER_EXECUTION_TEST_MARKER = "MICROS_USER_EXECUTION_TEST"
USER_EXECUTION_TEST_PASS = (
    "MICROS_USER_EXECUTION_TEST_PASS "
    "mode=entered "
    "faults=isolated "
    "context=preserved "
    "stack=owned "
    "return=resumed"
)
SCHEDULER_TEST_MARKER = "MICROS_SCHEDULER_TEST"
SCHEDULER_TEST_PASS = (
    "MICROS_SCHEDULER_TEST_PASS "
    "queues=minix-priority "
    "current=reachable "
    "accounting=separate "
    "switches=alternating "
    "idle=resumed "
    "registers=preserved"
)
ENDPOINT_TEST_MARKER = "MICROS_ENDPOINT_TEST"
ENDPOINT_TEST_PASS = (
    "MICROS_ENDPOINT_TEST_PASS "
    "generation=validated "
    "profiles=immutable "
    "visibility=staged "
    "authorization=separate"
)
OBJECTS_READY_MARKER = "MICROS_OBJECTS_READY"
OBJECT_MODEL_TEST_MARKER = "MICROS_OBJECT_MODEL_TEST"
NESTED_TRAP_TEST_MARKER = "MICROS_NESTED_TRAP_TEST"
OBJECTS_READY_PATTERN = re.compile(
    r"^MICROS_OBJECTS_READY "
    r"processes=0x([0-9a-f]{16}) "
    r"threads=0x([0-9a-f]{16}) "
    r"harts=0x([0-9a-f]{16}) "
    r"max-threads=0x([0-9a-f]{16}) "
    r"max-harts=0x([0-9a-f]{16}) "
    r"boot-hart=0x([0-9a-f]{16})$"
)
OBJECT_MODEL_TEST_PASS = (
    "MICROS_OBJECT_MODEL_TEST_PASS "
    "process-generation=advanced "
    "stale=rejected "
    "thread-limit=enforced "
    "hart-local=preserved"
)
NESTED_TRAP_TEST_PASS = (
    "MICROS_NESTED_TRAP_TEST_PASS "
    "hart=routed emergency-stack=selected"
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


def parse_objects_ready(output):
    output_lines, terminated = _split_output_records(output)
    indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(OBJECTS_READY_MARKER)
    ]
    if len(indices) != 1 or not terminated[indices[0]]:
        return None

    match = OBJECTS_READY_PATTERN.fullmatch(output_lines[indices[0]])
    if match is None:
        return None
    return tuple(int(value, 16) for value in match.groups())


def _has_complete_objects_ready(output):
    output_lines = output.splitlines()
    boot_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith("MICROS_BOOT ")
    ]
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(OBJECTS_READY_MARKER)
    ]
    trap_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(TRAP_READY_MARKER)
    ]
    ready = parse_objects_ready(output)
    return (
        len(boot_indices) == 1
        and len(ready_indices) == 1
        and len(trap_indices) == 1
        and boot_indices[0] < ready_indices[0] < trap_indices[0]
        and ready is not None
        and ready == (0, 0, 1, 1, 1, 0)
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


def _has_complete_timer_test_report(output):
    output_lines, terminated = _split_output_records(output)
    fdt_ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith("MICROS_FDT_READY")
    ]
    test_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(TIMER_TEST_MARKER)
    ]
    return (
        len(fdt_ready_indices) == 1
        and output_lines[fdt_ready_indices[0]] == "MICROS_FDT_READY"
        and len(test_indices) == 1
        and output_lines[test_indices[0]] == TIMER_TEST_PASS
        and terminated[test_indices[0]]
        and fdt_ready_indices[0] < test_indices[0]
    )


def parse_frame_allocator_ready_counts(output):
    output_lines, terminated = _split_output_records(output)
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(FRAME_ALLOCATOR_READY_MARKER)
    ]
    if (
        len(ready_indices) != 1
        or not terminated[ready_indices[0]]
    ):
        return None

    match = FRAME_ALLOCATOR_READY_PATTERN.fullmatch(
        output_lines[ready_indices[0]]
    )
    if match is None:
        return None
    managed, free = (int(value, 16) for value in match.groups())
    return managed, free


def _has_complete_frame_allocator_ready(output):
    output_lines = output.splitlines()
    fdt_ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith("MICROS_FDT_READY")
    ]
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(FRAME_ALLOCATOR_READY_MARKER)
    ]
    counts = parse_frame_allocator_ready_counts(output)
    return (
        len(fdt_ready_indices) == 1
        and output_lines[fdt_ready_indices[0]] == "MICROS_FDT_READY"
        and len(ready_indices) == 1
        and fdt_ready_indices[0] < ready_indices[0]
        and counts is not None
        and counts[0] != 0
        and counts[1] == counts[0]
    )


def _frame_allocator_ready_precedes_target_outcome(output):
    output_lines = output.splitlines()
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(FRAME_ALLOCATOR_READY_MARKER)
    ]
    outcome_indices = [
        index
        for index, line in enumerate(output_lines)
        if (
            PANIC_CORE_PATTERNS[0].fullmatch(line) is not None
            or line == TRAP_TEST_PASS
            or line == TIMER_TEST_PASS
            or line == FRAME_ALLOCATOR_TEST_PASS
            or line == FRAME_OWNERSHIP_TEST_PASS
            or line == USER_ADDRESS_SPACE_TEST_PASS
            or line == USER_EXECUTION_TEST_PASS
            or line == SCHEDULER_TEST_PASS
            or line == ENDPOINT_TEST_PASS
        )
    ]
    return (
        len(ready_indices) == 1
        and all(ready_indices[0] < index for index in outcome_indices)
    )


def has_expected_frame_allocator_growth(outputs, expected_delta):
    if len(outputs) != 2:
        return False
    counts = [
        parse_frame_allocator_ready_counts(output)
        for output in outputs
    ]
    return (
        all(value is not None for value in counts)
        and counts[1][0] >= counts[0][0]
        and counts[1][0] - counts[0][0] == expected_delta
    )


def _has_complete_frame_allocator_test_report(output):
    if not _has_complete_frame_allocator_ready(output):
        return False

    output_lines, terminated = _split_output_records(output)
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(FRAME_ALLOCATOR_READY_MARKER)
    ]
    test_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(FRAME_ALLOCATOR_TEST_MARKER)
    ]
    return (
        len(test_indices) == 1
        and output_lines[test_indices[0]] == FRAME_ALLOCATOR_TEST_PASS
        and terminated[test_indices[0]]
        and ready_indices[0] < test_indices[0]
    )


def parse_mmu_ready(output):
    output_lines, terminated = _split_output_records(output)
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(MMU_READY_MARKER)
    ]
    if (
        len(ready_indices) != 1
        or not terminated[ready_indices[0]]
    ):
        return None

    match = MMU_READY_PATTERN.fullmatch(output_lines[ready_indices[0]])
    if match is None:
        return None
    return tuple(int(value, 16) for value in match.groups())


def _has_complete_mmu_ready(output):
    output_lines = output.splitlines()
    allocator_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(FRAME_ALLOCATOR_READY_MARKER)
    ]
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(MMU_READY_MARKER)
    ]
    ready = parse_mmu_ready(output)
    return (
        len(allocator_indices) == 1
        and len(ready_indices) == 1
        and allocator_indices[0] < ready_indices[0]
        and ready is not None
        and ready[0] != 0
        and ready[0] % 4096 == 0
        and ready[1] != 0
    )


def _mmu_ready_precedes_target_outcome(output):
    output_lines = output.splitlines()
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(MMU_READY_MARKER)
    ]
    outcome_indices = [
        index
        for index, line in enumerate(output_lines)
        if (
            PANIC_CORE_PATTERNS[0].fullmatch(line) is not None
            or line == TRAP_TEST_PASS
            or line == TIMER_TEST_PASS
            or line == FRAME_ALLOCATOR_TEST_PASS
            or line == MMU_TEST_PASS
            or line == OBJECT_MODEL_TEST_PASS
            or line == NESTED_TRAP_TEST_PASS
            or line == FRAME_OWNERSHIP_TEST_PASS
            or line == USER_ADDRESS_SPACE_TEST_PASS
            or line == USER_EXECUTION_TEST_PASS
            or line == SCHEDULER_TEST_PASS
            or line == ENDPOINT_TEST_PASS
        )
    ]
    return (
        len(ready_indices) == 1
        and all(ready_indices[0] < index for index in outcome_indices)
    )


def _has_complete_mmu_test_report(output):
    if not _has_complete_mmu_ready(output):
        return False

    output_lines, terminated = _split_output_records(output)
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(MMU_READY_MARKER)
    ]
    test_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(MMU_TEST_MARKER)
    ]
    return (
        len(test_indices) == 1
        and output_lines[test_indices[0]] == MMU_TEST_PASS
        and terminated[test_indices[0]]
        and ready_indices[0] < test_indices[0]
    )


def parse_frame_ownership_ready(output):
    output_lines, terminated = _split_output_records(output)
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(FRAME_OWNERSHIP_READY_MARKER)
    ]
    if len(ready_indices) != 1 or not terminated[ready_indices[0]]:
        return None

    match = FRAME_OWNERSHIP_READY_PATTERN.fullmatch(
        output_lines[ready_indices[0]]
    )
    if match is None:
        return None
    return tuple(int(value, 16) for value in match.groups())


def _has_complete_frame_ownership_ready(output):
    output_lines = output.splitlines()
    mmu_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(MMU_READY_MARKER)
    ]
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(FRAME_OWNERSHIP_READY_MARKER)
    ]
    mmu_ready = parse_mmu_ready(output)
    ready = parse_frame_ownership_ready(output)
    return (
        len(mmu_indices) == 1
        and len(ready_indices) == 1
        and mmu_indices[0] < ready_indices[0]
        and mmu_ready is not None
        and ready is not None
        and ready[0] != 0
        and ready[0] == ready[1]
        and ready[1] == mmu_ready[1]
    )


def _frame_ownership_ready_precedes_target_outcome(output):
    output_lines = output.splitlines()
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(FRAME_OWNERSHIP_READY_MARKER)
    ]
    outcome_indices = [
        index
        for index, line in enumerate(output_lines)
        if (
            PANIC_CORE_PATTERNS[0].fullmatch(line) is not None
            or line == TRAP_TEST_PASS
            or line == TIMER_TEST_PASS
            or line == FRAME_ALLOCATOR_TEST_PASS
            or line == MMU_TEST_PASS
            or line == OBJECT_MODEL_TEST_PASS
            or line == NESTED_TRAP_TEST_PASS
            or line == FRAME_OWNERSHIP_TEST_PASS
            or line == USER_ADDRESS_SPACE_TEST_PASS
            or line == USER_EXECUTION_TEST_PASS
            or line == SCHEDULER_TEST_PASS
            or line == ENDPOINT_TEST_PASS
        )
    ]
    return (
        len(ready_indices) == 1
        and all(ready_indices[0] < index for index in outcome_indices)
    )


def _has_complete_frame_ownership_test_report(output):
    if not _has_complete_frame_ownership_ready(output):
        return False

    output_lines, terminated = _split_output_records(output)
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(FRAME_OWNERSHIP_READY_MARKER)
    ]
    test_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(FRAME_OWNERSHIP_TEST_MARKER)
    ]
    return (
        len(test_indices) == 1
        and output_lines[test_indices[0]] == FRAME_OWNERSHIP_TEST_PASS
        and terminated[test_indices[0]]
        and ready_indices[0] < test_indices[0]
    )


def _has_complete_user_address_space_test_report(output):
    if not _has_complete_frame_ownership_ready(output):
        return False

    output_lines, terminated = _split_output_records(output)
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(FRAME_OWNERSHIP_READY_MARKER)
    ]
    test_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(USER_ADDRESS_SPACE_TEST_MARKER)
    ]
    return (
        len(test_indices) == 1
        and output_lines[test_indices[0]] == USER_ADDRESS_SPACE_TEST_PASS
        and terminated[test_indices[0]]
        and ready_indices[0] < test_indices[0]
    )


def _has_complete_user_execution_test_report(output):
    if not _has_complete_frame_ownership_ready(output):
        return False

    output_lines, terminated = _split_output_records(output)
    trap_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(TRAP_READY_MARKER)
    ]
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(FRAME_OWNERSHIP_READY_MARKER)
    ]
    test_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(USER_EXECUTION_TEST_MARKER)
    ]
    return (
        len(trap_indices) == 1
        and output_lines[trap_indices[0]] == TRAP_READY_MARKER
        and len(test_indices) == 1
        and output_lines[test_indices[0]] == USER_EXECUTION_TEST_PASS
        and terminated[test_indices[0]]
        and trap_indices[0] < ready_indices[0] < test_indices[0]
    )


def _has_complete_scheduler_test_report(output):
    if not _has_complete_frame_ownership_ready(output):
        return False

    output_lines, terminated = _split_output_records(output)
    trap_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(TRAP_READY_MARKER)
    ]
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(FRAME_OWNERSHIP_READY_MARKER)
    ]
    test_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(SCHEDULER_TEST_MARKER)
    ]
    return (
        len(trap_indices) == 1
        and output_lines[trap_indices[0]] == TRAP_READY_MARKER
        and len(test_indices) == 1
        and output_lines[test_indices[0]] == SCHEDULER_TEST_PASS
        and terminated[test_indices[0]]
        and trap_indices[0] < ready_indices[0] < test_indices[0]
    )


def _has_complete_endpoint_test_report(output):
    if not _has_complete_frame_ownership_ready(output):
        return False

    output_lines, terminated = _split_output_records(output)
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(FRAME_OWNERSHIP_READY_MARKER)
    ]
    test_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(ENDPOINT_TEST_MARKER)
    ]
    return (
        len(test_indices) == 1
        and output_lines[test_indices[0]] == ENDPOINT_TEST_PASS
        and terminated[test_indices[0]]
        and ready_indices[0] < test_indices[0]
    )


def _objects_ready_precedes_target_outcome(output):
    output_lines = output.splitlines()
    ready_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(OBJECTS_READY_MARKER)
    ]
    outcome_indices = [
        index
        for index, line in enumerate(output_lines)
        if (
            PANIC_CORE_PATTERNS[0].fullmatch(line) is not None
            or line == TRAP_TEST_PASS
            or line == TIMER_TEST_PASS
            or line == FRAME_ALLOCATOR_TEST_PASS
            or line == MMU_TEST_PASS
            or line == OBJECT_MODEL_TEST_PASS
            or line == NESTED_TRAP_TEST_PASS
            or line == FRAME_OWNERSHIP_TEST_PASS
            or line == USER_ADDRESS_SPACE_TEST_PASS
            or line == USER_EXECUTION_TEST_PASS
            or line == SCHEDULER_TEST_PASS
            or line == ENDPOINT_TEST_PASS
        )
    ]
    return (
        len(ready_indices) == 1
        and all(ready_indices[0] < index for index in outcome_indices)
    )


def _has_complete_object_model_test_report(output):
    if not _has_complete_mmu_ready(output):
        return False

    output_lines, terminated = _split_output_records(output)
    mmu_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(MMU_READY_MARKER)
    ]
    test_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(OBJECT_MODEL_TEST_MARKER)
    ]
    return (
        len(test_indices) == 1
        and output_lines[test_indices[0]] == OBJECT_MODEL_TEST_PASS
        and terminated[test_indices[0]]
        and mmu_indices[0] < test_indices[0]
    )


def _has_complete_nested_trap_test_report(output):
    if not _has_complete_mmu_ready(output):
        return False

    output_lines, terminated = _split_output_records(output)
    mmu_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(MMU_READY_MARKER)
    ]
    test_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(NESTED_TRAP_TEST_MARKER)
    ]
    return (
        len(test_indices) == 1
        and output_lines[test_indices[0]] == NESTED_TRAP_TEST_PASS
        and terminated[test_indices[0]]
        and mmu_indices[0] < test_indices[0]
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
        or any(
            line.startswith(SCHEDULER_TEST_MARKER)
            for line in output_lines
        )
    ):
        return False
    return (
        expected_sepc is None
        or int(match.group(1), 16) == expected_sepc
    )


def _has_complete_scheduler_invalid_context(output, expected_state):
    output_lines, terminated = _split_output_records(output)
    expected = (
        "MICROS_SCHEDULER_INVALID_CONTEXT "
        f"state={expected_state} "
        "ownership=preserved accounting=kernel"
    )
    diagnostic_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith("MICROS_SCHEDULER_INVALID_CONTEXT")
    ]
    panic_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(PANIC_MARKER)
    ]
    return (
        expected_state in ("outgoing", "next")
        and len(diagnostic_indices) == 1
        and output_lines[diagnostic_indices[0]] == expected
        and terminated[diagnostic_indices[0]]
        and len(panic_indices) == len(PANIC_CORE_PATTERNS)
        and diagnostic_indices[0] < panic_indices[0]
        and not any(
            line.startswith(SCHEDULER_TEST_MARKER)
            for line in output_lines
        )
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
    require_timer_test_report=False,
    require_frame_allocator_ready=False,
    require_frame_allocator_test_report=False,
    require_mmu_ready=False,
    require_mmu_test_report=False,
    require_frame_ownership_ready=False,
    require_frame_ownership_test_report=False,
    require_user_address_space_test_report=False,
    require_user_execution_test_report=False,
    require_scheduler_test_report=False,
    require_endpoint_test_report=False,
    require_scheduler_invalid_context=None,
    require_objects_ready=False,
    require_object_model_test_report=False,
    require_nested_trap_test_report=False,
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
    if (
        require_timer_test_report
        and not _has_complete_timer_test_report(result.output)
    ):
        return False
    if (
        require_frame_allocator_ready
        and (
            not _has_complete_frame_allocator_ready(result.output)
            or not _frame_allocator_ready_precedes_target_outcome(
                result.output
            )
        )
    ):
        return False
    if (
        require_frame_allocator_test_report
        and not _has_complete_frame_allocator_test_report(result.output)
    ):
        return False
    if (
        require_mmu_ready
        and (
            not _has_complete_mmu_ready(result.output)
            or not _mmu_ready_precedes_target_outcome(result.output)
        )
    ):
        return False
    if (
        require_mmu_test_report
        and not _has_complete_mmu_test_report(result.output)
    ):
        return False
    if (
        require_frame_ownership_ready
        and (
            not _has_complete_frame_ownership_ready(result.output)
            or not _frame_ownership_ready_precedes_target_outcome(
                result.output
            )
        )
    ):
        return False
    if (
        require_frame_ownership_test_report
        and not _has_complete_frame_ownership_test_report(result.output)
    ):
        return False
    if (
        require_user_address_space_test_report
        and not _has_complete_user_address_space_test_report(
            result.output
        )
    ):
        return False
    if (
        require_user_execution_test_report
        and not _has_complete_user_execution_test_report(
            result.output
        )
    ):
        return False
    if (
        require_scheduler_test_report
        and not _has_complete_scheduler_test_report(result.output)
    ):
        return False
    if (
        require_endpoint_test_report
        and not _has_complete_endpoint_test_report(result.output)
    ):
        return False
    if (
        require_scheduler_invalid_context is not None
        and not _has_complete_scheduler_invalid_context(
            result.output,
            require_scheduler_invalid_context,
        )
    ):
        return False
    if (
        require_objects_ready
        and (
            not _has_complete_objects_ready(result.output)
            or not _objects_ready_precedes_target_outcome(result.output)
        )
    ):
        return False
    if (
        require_object_model_test_report
        and not _has_complete_object_model_test_report(result.output)
    ):
        return False
    if (
        require_nested_trap_test_report
        and not _has_complete_nested_trap_test_report(result.output)
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


def build_qemu_command(*, qemu, kernel, memory="128M"):
    return [
        qemu,
        "-machine",
        "virt,aia=none",
        "-cpu",
        "rv64",
        "-smp",
        "1",
        "-m",
        memory,
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
        "--memory",
        action="append",
        default=[],
        help="QEMU guest RAM size; may be repeated for one ELF",
    )
    parser.add_argument(
        "--expected-frame-allocator-managed-delta",
        type=lambda value: int(value, 0),
        help="Required managed-frame growth across exactly two memory sizes",
    )
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
        "--require-timer-test-report",
        action="store_true",
        help="Require the ordered timer interrupt test record",
    )
    parser.add_argument(
        "--require-frame-allocator-ready",
        action="store_true",
        help="Require a nonempty frame allocator ready record",
    )
    parser.add_argument(
        "--require-frame-allocator-test-report",
        action="store_true",
        help="Require the ordered frame allocator test record",
    )
    parser.add_argument(
        "--require-mmu-ready",
        action="store_true",
        help="Require an ordered Sv39 MMU ready record",
    )
    parser.add_argument(
        "--require-mmu-test-report",
        action="store_true",
        help="Require the ordered MMU permission test record",
    )
    parser.add_argument(
        "--require-frame-ownership-ready",
        action="store_true",
        help="Require the ordered typed frame ownership record",
    )
    parser.add_argument(
        "--require-frame-ownership-test-report",
        action="store_true",
        help="Require the ordered frame ownership test record",
    )
    parser.add_argument(
        "--require-user-address-space-test-report",
        action="store_true",
        help="Require the ordered user address-space test record",
    )
    parser.add_argument(
        "--require-user-execution-test-report",
        action="store_true",
        help="Require the ordered user execution test record",
    )
    parser.add_argument(
        "--require-scheduler-test-report",
        action="store_true",
        help="Require the ordered scheduler test record",
    )
    parser.add_argument(
        "--require-endpoint-test-report",
        action="store_true",
        help="Require the ordered endpoint and profile test record",
    )
    parser.add_argument(
        "--require-scheduler-invalid-context",
        choices=("outgoing", "next"),
        help="Require one exact invalid scheduler-context diagnostic",
    )
    parser.add_argument(
        "--require-objects-ready",
        action="store_true",
        help="Require the ordered kernel-object readiness record",
    )
    parser.add_argument(
        "--require-object-model-test-report",
        action="store_true",
        help="Require the ordered kernel-object model test record",
    )
    parser.add_argument(
        "--require-nested-trap-test-report",
        action="store_true",
        help="Require the ordered per-hart nested-trap test record",
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
    if not arguments.memory:
        arguments.memory = ["128M"]
    if any(not memory for memory in arguments.memory):
        parser.error("--memory must not be empty")
    if (
        arguments.expected_frame_allocator_managed_delta is not None
        and (
            len(arguments.memory) != 2
            or not arguments.require_frame_allocator_ready
                and not arguments.require_frame_allocator_test_report
        )
    ):
        parser.error(
            "--expected-frame-allocator-managed-delta requires "
            "two --memory values and a frame allocator report"
        )
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

    expected_outcome = SmokeOutcome(arguments.expect)
    successful_runs = []
    for memory in arguments.memory:
        command = build_qemu_command(
            qemu=arguments.qemu,
            kernel=str(arguments.kernel.resolve()),
            memory=memory,
        )
        try:
            result = run_qemu(command, arguments.timeout)
        except FileNotFoundError:
            print(
                f"QEMU executable not found: {arguments.qemu}",
                file=sys.stderr,
            )
            return 2

        outcome = classify_smoke(
            output=result.output,
            return_code=result.return_code,
            timed_out=result.timed_out,
            markers=arguments.marker,
            require_fdt_events=arguments.require_fdt_events,
            require_fdt_reservations=arguments.require_fdt_reservations,
        )
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
            require_timer_test_report=arguments.require_timer_test_report,
            require_frame_allocator_ready=(
                arguments.require_frame_allocator_ready
            ),
            require_frame_allocator_test_report=(
                arguments.require_frame_allocator_test_report
            ),
            require_mmu_ready=arguments.require_mmu_ready,
            require_mmu_test_report=arguments.require_mmu_test_report,
            require_frame_ownership_ready=(
                arguments.require_frame_ownership_ready
            ),
            require_frame_ownership_test_report=(
                arguments.require_frame_ownership_test_report
            ),
            require_user_address_space_test_report=(
                arguments.require_user_address_space_test_report
            ),
            require_user_execution_test_report=(
                arguments.require_user_execution_test_report
            ),
            require_scheduler_test_report=(
                arguments.require_scheduler_test_report
            ),
            require_endpoint_test_report=(
                arguments.require_endpoint_test_report
            ),
            require_scheduler_invalid_context=(
                arguments.require_scheduler_invalid_context
            ),
            require_objects_ready=arguments.require_objects_ready,
            require_object_model_test_report=(
                arguments.require_object_model_test_report
            ),
            require_nested_trap_test_report=(
                arguments.require_nested_trap_test_report
            ),
            require_trap_context=arguments.require_trap_context,
            expected_trap_context_sepc=expected_trap_context_sepc,
        )
        if not accepted:
            return print_tap_result(
                outcome,
                result,
                command,
                expected_outcome=expected_outcome,
                accepted=False,
            )
        successful_runs.append((memory, result, outcome, command))

    if arguments.expected_frame_allocator_managed_delta is not None:
        outputs = [run[1].output for run in successful_runs]
        if not has_expected_frame_allocator_growth(
            outputs,
            arguments.expected_frame_allocator_managed_delta,
        ):
            print("TAP version 13")
            print(
                "not ok 1 - frame allocator managed-frame growth "
                "did not match"
            )
            print(
                "# expected managed-frame delta: "
                f"0x{arguments.expected_frame_allocator_managed_delta:016x}"
            )
            for memory, result, _, _ in successful_runs:
                counts = parse_frame_allocator_ready_counts(result.output)
                print(f"# memory {memory}: counts={counts}")
            return 3

    if len(successful_runs) > 1:
        print("TAP version 13")
        print(
            "ok 1 - QEMU smoke test observed expected "
            f"{expected_outcome.value} for every memory size"
        )
        for memory, result, outcome, _ in successful_runs:
            counts = parse_frame_allocator_ready_counts(result.output)
            print(
                f"# memory {memory}: outcome={outcome.value} "
                f"frame-counts={counts}"
            )
        return 0

    _, result, outcome, command = successful_runs[0]
    return print_tap_result(
        outcome,
        result,
        command,
        expected_outcome=expected_outcome,
        accepted=True,
    )


if __name__ == "__main__":
    sys.exit(main())
