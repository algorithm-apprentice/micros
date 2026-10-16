from tools.qemu_smoke_records import (
    TRAP_READY_MARKER,
    TRAP_TEST_PASS,
    TIMER_TEST_PASS,
    FRAME_ALLOCATOR_TEST_PASS,
    MMU_READY_MARKER,
    MMU_TEST_PASS,
    FRAME_OWNERSHIP_READY_MARKER,
    FRAME_OWNERSHIP_TEST_PASS,
    USER_ADDRESS_SPACE_TEST_MARKER,
    USER_ADDRESS_SPACE_TEST_PASS,
    USER_EXECUTION_TEST_MARKER,
    USER_EXECUTION_TEST_PASS,
    SCHEDULER_TEST_MARKER,
    SCHEDULER_TEST_PASS,
    ENDPOINT_TEST_MARKER,
    ENDPOINT_TEST_PASS,
    IPC_TEST_MARKER,
    IPC_ADDRESS_SPACES_MARKER,
    IPC_ADDRESS_SPACES_PASS,
    IPC_TEST_PASS,
    OBJECTS_READY_MARKER,
    OBJECT_MODEL_TEST_MARKER,
    NESTED_TRAP_TEST_MARKER,
    OBJECTS_READY_PATTERN,
    OBJECT_MODEL_TEST_PASS,
    NESTED_TRAP_TEST_PASS,
    PANIC_CORE_PATTERNS,
    _split_output_records,
)

from tools.qemu_smoke_platform import (
    _has_complete_mmu_ready,
    _has_complete_frame_ownership_ready,
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


def _has_complete_ipc_test_report(output):
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
        if line.startswith(IPC_TEST_MARKER)
    ]
    address_space_indices = [
        index
        for index, line in enumerate(output_lines)
        if line.startswith(IPC_ADDRESS_SPACES_MARKER)
    ]
    return (
        len(address_space_indices) == 1
        and output_lines[address_space_indices[0]]
            == IPC_ADDRESS_SPACES_PASS
        and terminated[address_space_indices[0]]
        and len(test_indices) == 1
        and output_lines[test_indices[0]] == IPC_TEST_PASS
        and terminated[test_indices[0]]
        and ready_indices[0]
            < address_space_indices[0]
            < test_indices[0]
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
            or line == IPC_TEST_PASS
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
