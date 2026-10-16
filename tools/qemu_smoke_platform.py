from tools.qemu_smoke_records import (
    TRAP_READY_MARKER,
    TRAP_TEST_MARKER,
    TRAP_TEST_PASS,
    TIMER_TEST_MARKER,
    TIMER_TEST_PASS,
    FRAME_ALLOCATOR_READY_MARKER,
    FRAME_ALLOCATOR_TEST_MARKER,
    FRAME_ALLOCATOR_READY_PATTERN,
    FRAME_ALLOCATOR_TEST_PASS,
    MMU_READY_MARKER,
    MMU_TEST_MARKER,
    MMU_READY_PATTERN,
    MMU_TEST_PASS,
    FRAME_OWNERSHIP_READY_MARKER,
    FRAME_OWNERSHIP_TEST_MARKER,
    FRAME_OWNERSHIP_READY_PATTERN,
    FRAME_OWNERSHIP_TEST_PASS,
    USER_ADDRESS_SPACE_TEST_PASS,
    USER_EXECUTION_TEST_PASS,
    SCHEDULER_TEST_PASS,
    ENDPOINT_TEST_PASS,
    IPC_TEST_PASS,
    OBJECT_MODEL_TEST_PASS,
    NESTED_TRAP_TEST_PASS,
    FDT_COUNTS_PATTERN,
    FDT_EVENT_NAMES,
    FDT_RANGE_PATTERN,
    PANIC_CORE_PATTERNS,
    _split_output_records,
)


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
            or line == IPC_TEST_PASS
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
            or line == IPC_TEST_PASS
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
            or line == IPC_TEST_PASS
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
