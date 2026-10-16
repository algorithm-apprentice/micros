from pathlib import Path

from tools.qemu_smoke_records import (
    PANIC_MARKER,
    TRAP_TEST_PASS,
    SCHEDULER_TEST_MARKER,
    PANIC_CORE_PATTERNS,
    TRAP_CONTEXT_PATTERN,
    VM_SELF_FAULT_PATTERN,
    _split_output_records,
)


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


def _has_complete_trap_context(
    output,
    expected_sepc=None,
    expected_origin=None,
    expected_scause=None,
    expected_stval=None,
):
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
        (expected_origin is None or match.group(1) == expected_origin)
        and (
            expected_scause is None
            or int(match.group(2), 16) == expected_scause
        )
        and (
            expected_stval is None
            or int(match.group(3), 16) == expected_stval
        )
        and (
            expected_sepc is None
            or int(match.group(4), 16) == expected_sepc
        )
    )


def _has_vm_self_fault(
    output,
    *,
    expected_sepc,
    expected_scause,
    expected_stval,
    expected_ownership,
):
    output_lines, terminated = _split_output_records(output)
    indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith("MICROS_VM_SELF_FAULT")
    ]
    if len(indices) != 1 or not terminated[indices[0]]:
        return False
    match = VM_SELF_FAULT_PATTERN.fullmatch(output_lines[indices[0]])
    return (
        match is not None
        and int(match.group(5), 16) == expected_scause
        and int(match.group(6), 16) == expected_stval
        and int(match.group(7), 16) == expected_sepc
        and match.group(8) == expected_ownership
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
