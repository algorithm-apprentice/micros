from __future__ import annotations

from pathlib import Path
import re
import unittest

from tools import validation_plan


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CMAKE_PATH = REPOSITORY_ROOT / "CMakeLists.txt"
FUNCTION_REVIEW_LINE_LIMIT = 120

SOURCE_SETS = {
    "MICROS_KERNEL_ADDRESS_SPACE_SOURCES": (
        "kernel/address_space.c",
        "kernel/address_space_validation.c",
    ),
    "MICROS_USER_ADDRESS_SPACE_CORE_SOURCES": (
        "kernel/user_address_space_core.c",
        "kernel/user_address_space_vm_plan.c",
    ),
    "MICROS_USER_ADDRESS_SPACE_SOURCES": (
        "kernel/user_address_space.c",
        "kernel/user_address_space_runtime.c",
        "kernel/user_address_space_table.c",
        "kernel/user_address_space_validation.c",
        "kernel/user_address_space_mapping.c",
        "kernel/user_address_space_mutation.c",
        "kernel/user_address_space_handoff.c",
        "kernel/user_address_space_tty.c",
        "kernel/user_address_space_vm_scratch.c",
        "kernel/user_address_space_vm_freeze.c",
        "kernel/user_address_space_vm_teardown.c",
        "kernel/user_address_space_activation.c",
    ),
    "MICROS_USER_EXECUTION_SOURCES": (
        "kernel/user_execution.c",
        "kernel/user_execution_validation.c",
        "kernel/user_execution_trap.c",
    ),
    "MICROS_USER_ADDRESS_SPACE_HOST_TEST_SOURCES": (
        "tests/host/user_address_space_core_test.c",
        "tests/host/user_address_space_owner_test.c",
        "tests/host/user_address_space_vm_plan_test.c",
    ),
    "MICROS_USER_ADDRESS_SPACE_QEMU_TEST_SOURCES": (
        "kernel/user_address_space_test.c",
        "kernel/user_address_space_test_fixture.c",
        "kernel/user_address_space_test_lifecycle.c",
        "kernel/user_address_space_test_mapping.c",
        "kernel/user_address_space_test_validation.c",
    ),
    "MICROS_USER_EXECUTION_QEMU_TEST_SOURCES": (
        "kernel/user_execution_test.c",
        "kernel/user_execution_test_fixture.c",
        "kernel/user_execution_test_prepare.c",
        "kernel/user_execution_test_trap.c",
        "kernel/user_execution_test_lifecycle.c",
    ),
}

PRIVATE_HEADERS = (
    "kernel/address_space_internal.h",
    "kernel/user_address_space_internal.h",
    "kernel/user_execution_internal.h",
    "kernel/user_address_space_test_internal.h",
    "kernel/user_execution_test_internal.h",
    "tests/host/user_address_space_core_test_suites.h",
)

TEST_PRIVATE_HEADERS = (
    "kernel/user_address_space_test_internal.h",
    "kernel/user_execution_test_internal.h",
    "tests/host/user_address_space_core_test_suites.h",
)

PATH_CLASSIFICATION_EQUIVALENTS = (
    (
        "kernel/address_space.c",
        (
            "kernel/address_space_validation.c",
            "kernel/address_space_internal.h",
        ),
    ),
    (
        "kernel/user_address_space.c",
        (
            "kernel/user_address_space_runtime.c",
            "kernel/user_address_space_table.c",
            "kernel/user_address_space_validation.c",
            "kernel/user_address_space_mapping.c",
            "kernel/user_address_space_mutation.c",
            "kernel/user_address_space_handoff.c",
            "kernel/user_address_space_tty.c",
            "kernel/user_address_space_vm_scratch.c",
            "kernel/user_address_space_vm_freeze.c",
            "kernel/user_address_space_vm_teardown.c",
            "kernel/user_address_space_activation.c",
            "kernel/user_address_space_internal.h",
        ),
    ),
    (
        "kernel/user_address_space_core.c",
        ("kernel/user_address_space_vm_plan.c",),
    ),
    (
        "kernel/user_execution.c",
        (
            "kernel/user_execution_validation.c",
            "kernel/user_execution_trap.c",
            "kernel/user_execution_internal.h",
        ),
    ),
    (
        "kernel/user_address_space_test.c",
        (
            "kernel/user_address_space_test_fixture.c",
            "kernel/user_address_space_test_lifecycle.c",
            "kernel/user_address_space_test_mapping.c",
            "kernel/user_address_space_test_validation.c",
            "kernel/user_address_space_test_internal.h",
        ),
    ),
    (
        "kernel/user_execution_test.c",
        (
            "kernel/user_execution_test_fixture.c",
            "kernel/user_execution_test_prepare.c",
            "kernel/user_execution_test_trap.c",
            "kernel/user_execution_test_lifecycle.c",
            "kernel/user_execution_test_internal.h",
        ),
    ),
    (
        "tests/host/user_address_space_core_test.c",
        (
            "tests/host/user_address_space_owner_test.c",
            "tests/host/user_address_space_vm_plan_test.c",
            "tests/host/user_address_space_core_test_suites.h",
        ),
    ),
)

FINAL_SYMBOL_OWNERS = {
    "micros_kernel_address_space_initialize":
        "kernel/address_space.c",
    "micros_kernel_address_space_report":
        "kernel/address_space.c",
    "micros_kernel_address_space_run_self_test":
        "kernel/mmu_test.c",
    "micros_kernel_address_space_handle_test_trap":
        "kernel/mmu_test.c",
    "micros_user_address_space_create":
        "kernel/user_address_space.c",
    "micros_user_address_space_destroy":
        "kernel/user_address_space.c",
    "micros_user_address_space_allocate_page":
        "kernel/user_address_space_mutation.c",
    "micros_user_address_space_release_page":
        "kernel/user_address_space_mutation.c",
    "micros_user_address_space_test_fail_after_allocations":
        "kernel/user_address_space_mutation.c",
    "micros_user_address_space_lookup":
        "kernel/user_address_space_mapping.c",
    "micros_user_address_space_translate":
        "kernel/user_address_space_mapping.c",
    "micros_user_address_space_inventory":
        "kernel/user_address_space_mapping.c",
    "micros_user_address_space_prepare_wired_handoff":
        "kernel/user_address_space_handoff.c",
    "micros_user_address_space_complete_wired_handoff":
        "kernel/user_address_space_handoff.c",
    "micros_user_address_space_activate":
        "kernel/user_address_space_activation.c",
    "micros_user_address_space_activate_kernel":
        "kernel/user_address_space_activation.c",
    "micros_user_address_space_validate":
        "kernel/user_address_space_validation.c",
    "micros_user_address_space_leaf_owner_kind":
        "kernel/user_address_space_core.c",
    "micros_user_address_space_validate_frame_owner":
        "kernel/user_address_space_core.c",
    "micros_user_address_space_classify_managed_leaf":
        "kernel/user_address_space_core.c",
    "micros_user_address_space_prepare_vm_table_plan":
        "kernel/user_address_space_vm_plan.c",
    "micros_user_address_space_runtime_run_self_test":
        "kernel/user_address_space_test.c",
    "micros_user_address_space_handle_test_trap":
        "kernel/user_address_space_test.c",
    "micros_user_execution_prepare":
        "kernel/user_execution.c",
    "micros_user_execution_preflight_post_handoff":
        "kernel/user_execution.c",
    "micros_user_execution_commit_post_handoff_prevalidated":
        "kernel/user_execution.c",
    "micros_user_execution_inspect":
        "kernel/user_execution.c",
    "micros_user_execution_detach":
        "kernel/user_execution.c",
    "micros_user_execution_test_stack_bounds":
        "kernel/user_execution.c",
    "micros_user_execution_validate_return":
        "kernel/user_execution_validation.c",
    "micros_user_execution_validate_context":
        "kernel/user_execution_validation.c",
    "micros_user_execution_capture_trap":
        "kernel/user_execution_trap.c",
    "micros_user_execution_store_context":
        "kernel/user_execution_trap.c",
    "micros_user_execution_install_return_frame":
        "kernel/user_execution_trap.c",
    "micros_user_execution_runtime_run_self_test":
        "kernel/user_execution_test.c",
    "micros_user_execution_test_enter_production":
        "kernel/user_execution_test.c",
    "micros_user_execution_test_pre_capture":
        "kernel/user_execution_test_trap.c",
    "micros_user_execution_handle_test_trap":
        "kernel/user_execution_test_trap.c",
    "micros_user_address_space_core_test_run":
        "tests/host/user_address_space_core_test.c",
}

INITIAL_SYMBOL_OWNER_MIGRATION = (
    (
        "kernel/user_address_space.c",
        "kernel/user_address_space_mutation.c",
        "micros_user_address_space_allocate_page",
    ),
    (
        "kernel/user_address_space.c",
        "kernel/user_address_space_mutation.c",
        "micros_user_address_space_release_page",
    ),
    (
        "kernel/user_address_space.c",
        "kernel/user_address_space_mutation.c",
        "micros_user_address_space_test_fail_after_allocations",
    ),
    (
        "kernel/user_address_space.c",
        "kernel/user_address_space_mapping.c",
        "micros_user_address_space_lookup",
    ),
    (
        "kernel/user_address_space.c",
        "kernel/user_address_space_mapping.c",
        "micros_user_address_space_translate",
    ),
    (
        "kernel/user_address_space.c",
        "kernel/user_address_space_mapping.c",
        "micros_user_address_space_inventory",
    ),
    (
        "kernel/user_address_space.c",
        "kernel/user_address_space_handoff.c",
        "micros_user_address_space_prepare_wired_handoff",
    ),
    (
        "kernel/user_address_space.c",
        "kernel/user_address_space_handoff.c",
        "micros_user_address_space_complete_wired_handoff",
    ),
    (
        "kernel/user_address_space.c",
        "kernel/user_address_space_activation.c",
        "micros_user_address_space_activate",
    ),
    (
        "kernel/user_address_space.c",
        "kernel/user_address_space_activation.c",
        "micros_user_address_space_activate_kernel",
    ),
    (
        "kernel/user_address_space.c",
        "kernel/user_address_space_validation.c",
        "micros_user_address_space_validate",
    ),
    (
        "kernel/user_address_space_core.c",
        "kernel/user_address_space_vm_plan.c",
        "micros_user_address_space_prepare_vm_table_plan",
    ),
    (
        "kernel/user_execution.c",
        "kernel/user_execution_validation.c",
        "micros_user_execution_validate_return",
    ),
    (
        "kernel/user_execution.c",
        "kernel/user_execution_validation.c",
        "micros_user_execution_validate_context",
    ),
    (
        "kernel/user_execution.c",
        "kernel/user_execution_trap.c",
        "micros_user_execution_capture_trap",
    ),
    (
        "kernel/user_execution.c",
        "kernel/user_execution_trap.c",
        "micros_user_execution_store_context",
    ),
    (
        "kernel/user_execution.c",
        "kernel/user_execution_trap.c",
        "micros_user_execution_install_return_frame",
    ),
    (
        "kernel/user_execution_test.c",
        "kernel/user_execution_test_trap.c",
        "micros_user_execution_test_pre_capture",
    ),
    (
        "kernel/user_execution_test.c",
        "kernel/user_execution_test_trap.c",
        "micros_user_execution_handle_test_trap",
    ),
)

SYMBOL_OWNER_MIGRATION = INITIAL_SYMBOL_OWNER_MIGRATION

INITIAL_LONG_FUNCTION_MIGRATION = (
    (
        "kernel/address_space.c",
        "tree_and_table_ownership_are_valid",
    ),
    (
        "kernel/user_address_space_core.c",
        "micros_user_address_space_prepare_vm_table_plan",
    ),
    ("kernel/user_address_space.c", "validate_locked"),
    ("kernel/user_address_space.c", "visit_user_mappings_locked"),
    (
        "kernel/user_address_space.c",
        "micros_user_address_space_allocate_page",
    ),
    (
        "kernel/user_address_space.c",
        "micros_user_address_space_release_page",
    ),
    (
        "kernel/user_address_space.c",
        "micros_user_address_space_prepare_tty_uart_mapping",
    ),
    (
        "kernel/user_address_space.c",
        "micros_user_address_space_prepare_vm_scratch_mapping",
    ),
    (
        "kernel/user_address_space.c",
        "micros_user_address_space_prepare_vm_scratch_removal",
    ),
    (
        "kernel/user_address_space.c",
        "micros_user_address_space_prepare_vm_freeze",
    ),
    (
        "kernel/user_address_space.c",
        "micros_user_address_space_prepare_vm_teardown",
    ),
    (
        "kernel/user_execution.c",
        "micros_user_execution_preflight_post_handoff",
    ),
    ("kernel/user_execution.c", "micros_user_execution_prepare"),
    (
        "kernel/user_address_space_test.c",
        "post_handoff_operations_are_revoked",
    ),
    (
        "kernel/user_address_space_test.c",
        "stale_operations_are_rejected",
    ),
    (
        "kernel/user_address_space_test.c",
        "micros_user_address_space_runtime_run_self_test",
    ),
    (
        "kernel/user_execution_test.c",
        "micros_user_execution_test_pre_capture",
    ),
    (
        "kernel/user_execution_test.c",
        "micros_user_execution_handle_test_trap",
    ),
    (
        "kernel/user_execution_test.c",
        "micros_user_execution_test_finish",
    ),
    (
        "kernel/user_execution_test.c",
        "micros_user_execution_runtime_run_self_test",
    ),
    (
        "tests/host/user_address_space_core_test.c",
        "test_classifies_exact_vm_scratch_leaf",
    ),
    (
        "tests/host/user_address_space_core_test.c",
        "test_prepares_exact_vm_table_plan",
    ),
    (
        "tests/host/user_address_space_core_test.c",
        "micros_user_address_space_core_test_run",
    ),
)

LONG_FUNCTION_MIGRATION = INITIAL_LONG_FUNCTION_MIGRATION

SCRATCH_OWNER = "kernel/user_address_space.c"
ACCEPTED_SCRATCH_OWNERS = (
    "kernel/user_address_space.c",
    "kernel/user_address_space_runtime.c",
)

ADDRESS_SPACE_TEST_STATE = (
    "ownership_snapshot",
    "allocator_snapshot",
    "objects_snapshot",
    "tree_snapshots",
    "tree_snapshot_addresses",
    "tree_snapshot_count",
    "user_frame_snapshots",
    "user_snapshot_addresses",
    "user_snapshot_count",
    "sum_fault_state",
    "sum_fault_virtual_address",
)

EXECUTION_TEST_STATE = (
    "objects_snapshot",
    "ownership_snapshot",
    "allocator_snapshot",
    "kernel_stack_snapshot",
    "second_kernel_stack_snapshot",
    "test_process",
    "second_process",
    "test_thread",
    "second_thread",
    "initial_context",
    "prepared_context",
    "code_physical_address",
    "stack_physical_address",
    "second_code_physical_address",
    "second_stack_physical_address",
    "kernel_stack_bottom",
    "kernel_stack_top",
    "second_kernel_stack_bottom",
    "second_kernel_stack_top",
    "live_sstatus_before_prepare",
    "baseline_owned",
    "baseline_free",
    "test_state",
)

STRUCTURAL_TEST_PATH = (
    "tests/host/test_kernel_address_space_source_layout.py"
)


def cmake_command(text: str, command: str, first_argument: str) -> str:
    match = re.search(
        rf"\b{re.escape(command)}\s*\(\s*{re.escape(first_argument)}\b",
        text,
    )
    if match is None:
        raise AssertionError(
            f"missing {command} command for {first_argument}"
        )

    open_index = text.find("(", match.start())
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return text[match.start() : index + 1]

    raise AssertionError(
        f"unterminated {command} command for {first_argument}"
    )


def cmake_if_blocks(text: str, condition: str) -> tuple[str, ...]:
    pattern = re.compile(
        rf"\bif\s*\(\s*{re.escape(condition)}\s*\)"
    )
    blocks = []
    offset = 0

    while True:
        match = pattern.search(text, offset)
        if match is None:
            break
        depth = 1
        for token in re.finditer(
            r"\b(?P<command>if|endif)\s*\(",
            text[match.end() :],
        ):
            command = token.group("command")
            if command == "if":
                depth += 1
            else:
                depth -= 1
                if depth == 0:
                    end = match.end() + token.end()
                    blocks.append(text[match.start() : end])
                    offset = end
                    break
        else:
            raise AssertionError(
                f"unterminated if block for {condition}"
            )

    if not blocks:
        raise AssertionError(f"missing if block for {condition}")
    return tuple(blocks)


def c_lexical_text(text: str) -> str:
    output = list(text)
    index = 0

    while index < len(text):
        if text.startswith("//", index):
            end = text.find("\n", index + 2)
            end = len(text) if end < 0 else end
            for selected in range(index, end):
                output[selected] = " "
            index = end
            continue
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            if end < 0:
                raise AssertionError("unterminated C comment")
            for selected in range(index, end + 2):
                if output[selected] != "\n":
                    output[selected] = " "
            index = end + 2
            continue
        if text[index] in ('"', "'"):
            quote = text[index]
            output[index] = " "
            index += 1
            while index < len(text):
                if text[index] == "\\":
                    output[index] = " "
                    if index + 1 < len(text):
                        if output[index + 1] != "\n":
                            output[index + 1] = " "
                    index += 2
                    continue
                if text[index] == quote:
                    output[index] = " "
                    index += 1
                    break
                if output[index] != "\n":
                    output[index] = " "
                index += 1
            else:
                raise AssertionError("unterminated C literal")
            continue
        index += 1

    return "".join(output)


def matching_open_parenthesis(text: str, close_index: int) -> int:
    depth = 0

    for index in range(close_index, -1, -1):
        if text[index] == ")":
            depth += 1
        elif text[index] == "(":
            depth -= 1
            if depth == 0:
                return index

    raise AssertionError("unterminated C parameter list")


def c_signature_start(text: str, segment_start: int, name_index: int) -> int:
    prefix = text[segment_start:name_index]
    lines = prefix.splitlines(keepends=True)
    offset = segment_start
    candidates = []

    for line in lines:
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            candidates.append(offset)
        elif stripped.startswith("#") or not stripped:
            candidates = []
        offset += len(line)

    return candidates[0] if candidates else name_index


def c_function_definitions(text: str) -> dict[str, int]:
    lexical = c_lexical_text(text)
    definitions = {}
    depth = 0
    segment_start = 0
    index = 0

    while index < len(lexical):
        selected = lexical[index]
        if selected == "{" and depth == 0:
            declaration = lexical[segment_start:index].rstrip()
            close_parenthesis = len(declaration) - 1
            if close_parenthesis >= 0 and declaration.endswith(")"):
                open_parenthesis = matching_open_parenthesis(
                    declaration,
                    close_parenthesis,
                )
                before = declaration[:open_parenthesis].rstrip()
                match = re.search(
                    r"([A-Za-z_][A-Za-z0-9_]*)$",
                    before,
                )
                if match is not None and "=" not in declaration:
                    name = match.group(1)
                    if name not in {"if", "for", "while", "switch"}:
                        body_depth = 1
                        body_end = index + 1
                        while body_end < len(lexical) and body_depth != 0:
                            if lexical[body_end] == "{":
                                body_depth += 1
                            elif lexical[body_end] == "}":
                                body_depth -= 1
                            body_end += 1
                        if body_depth != 0:
                            raise AssertionError(
                                f"unterminated body for {name}"
                            )
                        name_index = (
                            segment_start
                            + open_parenthesis
                            - len(match.group(0))
                        )
                        start = c_signature_start(
                            lexical,
                            segment_start,
                            name_index,
                        )
                        definitions[name] = (
                            text.count("\n", start, body_end - 1) + 1
                        )
                        index = body_end
                        segment_start = body_end
                        continue
            depth = 1
        elif selected == "{":
            depth += 1
        elif selected == "}":
            if depth == 0:
                raise AssertionError("unexpected C closing brace")
            depth -= 1
            if depth == 0:
                segment_start = index + 1
        elif selected == ";" and depth == 0:
            segment_start = index + 1
        index += 1

    return definitions


def all_source_set_paths() -> tuple[str, ...]:
    return tuple(
        dict.fromkeys(
            path
            for sources in SOURCE_SETS.values()
            for path in sources
        )
    )


def production_sources() -> tuple[str, ...]:
    names = (
        "MICROS_KERNEL_ADDRESS_SPACE_SOURCES",
        "MICROS_USER_ADDRESS_SPACE_CORE_SOURCES",
        "MICROS_USER_ADDRESS_SPACE_SOURCES",
        "MICROS_USER_EXECUTION_SOURCES",
    )
    return tuple(
        path
        for name in names
        for path in SOURCE_SETS[name]
    )


def symbol_scan_sources() -> tuple[str, ...]:
    return tuple(
        dict.fromkeys(
            (*all_source_set_paths(), "kernel/mmu_test.c")
        )
    )


def static_definition_paths(
    sources: tuple[str, ...],
    name: str,
) -> list[str]:
    pattern = re.compile(
        rf"(?ms)^static\b[^;{{}}]*?\b{re.escape(name)}\b"
        r"[^;{}]*;"
    )
    matches = []

    for relative_path in sources:
        path = REPOSITORY_ROOT / relative_path
        if (
            path.is_file()
            and pattern.search(path.read_text(encoding="utf-8"))
        ):
            matches.append(relative_path)

    return matches


class KernelAddressSpaceSourceLayoutTest(unittest.TestCase):
    def test_expected_sources_and_private_headers_exist(self) -> None:
        required = (*all_source_set_paths(), *PRIVATE_HEADERS)
        missing = [
            path
            for path in required
            if not (REPOSITORY_ROOT / path).is_file()
        ]

        self.assertEqual([], missing)

    def test_cmake_source_sets_have_exact_ordered_membership(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")

        for source_set_name, expected_sources in SOURCE_SETS.items():
            with self.subTest(source_set=source_set_name):
                source_set = cmake_command(
                    cmake,
                    "set",
                    source_set_name,
                )
                listed_sources = tuple(
                    re.findall(
                        r"(?:kernel|tests/host)/"
                        r"[A-Za-z0-9_]+\.c",
                        source_set,
                    )
                )
                self.assertEqual(expected_sources, listed_sources)

        for source in all_source_set_paths():
            self.assertEqual(1, cmake.count(source), source)

    def test_consumers_expand_exact_source_sets_once(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")
        main = cmake_command(cmake, "add_executable", "micros")
        endpoint = cmake_command(cmake, "add_executable", "endpoint_test")
        address_test = "\n".join(
            cmake_if_blocks(
                cmake,
                "MICROS_BUILD_USER_ADDRESS_SPACE_TEST",
            )
        )
        execution_test = "\n".join(
            cmake_if_blocks(
                cmake,
                "MICROS_BUILD_USER_EXECUTION_TEST",
            )
        )
        expected = {
            "main": (
                main,
                (
                    "MICROS_KERNEL_ADDRESS_SPACE_SOURCES",
                    "MICROS_USER_ADDRESS_SPACE_CORE_SOURCES",
                    "MICROS_USER_ADDRESS_SPACE_SOURCES",
                    "MICROS_USER_EXECUTION_SOURCES",
                ),
            ),
            "endpoint": (
                endpoint,
                (
                    "MICROS_USER_ADDRESS_SPACE_CORE_SOURCES",
                    "MICROS_USER_ADDRESS_SPACE_HOST_TEST_SOURCES",
                ),
            ),
            "address-test": (
                address_test,
                ("MICROS_USER_ADDRESS_SPACE_QEMU_TEST_SOURCES",),
            ),
            "execution-test": (
                execution_test,
                ("MICROS_USER_EXECUTION_QEMU_TEST_SOURCES",),
            ),
        }

        for consumer, (block, selected_sets) in expected.items():
            for source_set_name in SOURCE_SETS:
                with self.subTest(
                    consumer=consumer,
                    source_set=source_set_name,
                ):
                    self.assertEqual(
                        int(source_set_name in selected_sets),
                        block.count(f"${{{source_set_name}}}"),
                    )

        self.assertEqual(
            1,
            address_test.count(
                "arch/riscv64/user_address_space_test.S"
            ),
        )
        self.assertEqual(
            1,
            execution_test.count(
                "arch/riscv64/user_execution_test.S"
            ),
        )

    def test_private_headers_contain_declarations_only(self) -> None:
        definitions = {}

        for relative_path in PRIVATE_HEADERS:
            path = REPOSITORY_ROOT / relative_path
            if not path.is_file():
                continue
            selected = c_function_definitions(
                path.read_text(encoding="utf-8")
            )
            if selected:
                definitions[relative_path] = selected

        self.assertEqual({}, definitions)

    def test_production_sources_do_not_include_test_private_headers(
        self,
    ) -> None:
        violations = []

        for relative_path in production_sources():
            path = REPOSITORY_ROOT / relative_path
            if not path.is_file():
                continue
            text = path.read_text(encoding="utf-8")
            for private_path in TEST_PRIVATE_HEADERS:
                if (
                    private_path in text
                    or Path(private_path).name in text
                ):
                    violations.append((relative_path, private_path))

        self.assertEqual([], violations)

    def test_sources_do_not_textually_include_c_files(self) -> None:
        violations = []
        include = re.compile(
            r"(?m)^\s*#\s*include\s*[<\"][^>\"]+\.c[>\"]"
        )

        for relative_path in (
            *all_source_set_paths(),
            *PRIVATE_HEADERS,
        ):
            path = REPOSITORY_ROOT / relative_path
            if (
                path.is_file()
                and include.search(path.read_text(encoding="utf-8"))
            ):
                violations.append(relative_path)

        self.assertEqual([], violations)

    def test_symbol_owners_follow_exact_migration_map(self) -> None:
        initial = set(INITIAL_SYMBOL_OWNER_MIGRATION)
        migration = set(SYMBOL_OWNER_MIGRATION)
        by_symbol = {}
        definitions = {}

        self.assertEqual(
            len(SYMBOL_OWNER_MIGRATION),
            len(migration),
            "symbol-owner migration contains duplicates",
        )
        self.assertTrue(
            migration <= initial,
            "symbol-owner migration may only shrink",
        )

        for current, final, symbol in migration:
            self.assertEqual(FINAL_SYMBOL_OWNERS[symbol], final)
            self.assertNotEqual(current, final)
            self.assertNotIn(symbol, by_symbol)
            by_symbol[symbol] = current

        for relative_path in symbol_scan_sources():
            path = REPOSITORY_ROOT / relative_path
            if not path.is_file():
                continue
            for symbol in c_function_definitions(
                path.read_text(encoding="utf-8")
            ):
                definitions.setdefault(symbol, []).append(relative_path)

        for symbol, final_owner in FINAL_SYMBOL_OWNERS.items():
            with self.subTest(symbol=symbol):
                expected = by_symbol.get(symbol, final_owner)
                self.assertEqual(
                    [expected],
                    definitions.get(symbol, []),
                )

    def test_scratch_storage_has_one_configured_owner(self) -> None:
        patterns = {
            "reachable_bitmap": re.compile(
                r"(?m)^static\s+uint64_t\s+"
                r"(?:\n\s*)?reachable_bitmap\s*\["
            ),
            "release_bitmap": re.compile(
                r"(?m)^static\s+uint64_t\s+"
                r"(?:\n\s*)?release_bitmap\s*\["
            ),
            "scratch_busy": re.compile(
                r"(?m)^static\s+bool\s+scratch_busy\s*;"
            ),
        }

        self.assertIn(SCRATCH_OWNER, ACCEPTED_SCRATCH_OWNERS)
        for name, pattern in patterns.items():
            owners = []
            for relative_path in production_sources():
                path = REPOSITORY_ROOT / relative_path
                if (
                    path.is_file()
                    and pattern.search(path.read_text(encoding="utf-8"))
                ):
                    owners.append(relative_path)
            with self.subTest(storage=name):
                self.assertEqual([SCRATCH_OWNER], owners)

    def test_split_tests_do_not_duplicate_mutable_state(self) -> None:
        families = (
            (
                SOURCE_SETS[
                    "MICROS_USER_ADDRESS_SPACE_QEMU_TEST_SOURCES"
                ],
                ADDRESS_SPACE_TEST_STATE,
            ),
            (
                SOURCE_SETS[
                    "MICROS_USER_EXECUTION_QEMU_TEST_SOURCES"
                ],
                EXECUTION_TEST_STATE,
            ),
        )

        for sources, names in families:
            for name in names:
                with self.subTest(source=sources[0], state=name):
                    self.assertLessEqual(
                        len(static_definition_paths(sources, name)),
                        1,
                    )

        contexts = (
            (
                "micros_user_address_space_test_context",
                "kernel/user_address_space_test_fixture.c",
                SOURCE_SETS[
                    "MICROS_USER_ADDRESS_SPACE_QEMU_TEST_SOURCES"
                ],
            ),
            (
                "micros_user_execution_test_context",
                "kernel/user_execution_test_fixture.c",
                SOURCE_SETS[
                    "MICROS_USER_EXECUTION_QEMU_TEST_SOURCES"
                ],
            ),
        )
        for context_type, owner, sources in contexts:
            pattern = re.compile(
                rf"(?m)^static\s+struct\s+{context_type}\s+"
                r"[A-Za-z_][A-Za-z0-9_]*\s*;"
            )
            observed = []
            for relative_path in sources:
                path = REPOSITORY_ROOT / relative_path
                if (
                    path.is_file()
                    and pattern.search(path.read_text(encoding="utf-8"))
                ):
                    observed.append(relative_path)
            if observed:
                self.assertEqual([owner], observed)

    def test_evidence_identities_and_markers_remain_stable(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")
        runner = (
            REPOSITORY_ROOT / "tools/run_qemu_smoke.py"
        ).read_text(encoding="utf-8")
        address_marker = (
            "MICROS_USER_ADDRESS_SPACE_TEST_PASS "
            "roots=isolated reuse=zeroed active=guarded "
            "ownership=validated sum=cleared"
        )
        execution_marker = (
            "MICROS_USER_EXECUTION_TEST_PASS "
            "mode=entered faults=isolated context=preserved "
            "stack=owned return=resumed"
        )

        self.assertEqual(1, cmake.count("endpoint-profile"))
        for target in (
            "test-qemu-user-address-space",
            "test-qemu-address-space-handoff",
            "test-qemu-user-execution",
        ):
            self.assertEqual(1, cmake.count(target), target)
        self.assertEqual(1, cmake.count(address_marker))
        self.assertEqual(1, cmake.count(execution_marker))
        for token in address_marker.split():
            self.assertIn(token, runner)
        for token in execution_marker.split():
            self.assertIn(token, runner)

    def test_validation_plan_inherits_exact_path_classification(
        self,
    ) -> None:
        self.assertEqual(
            PATH_CLASSIFICATION_EQUIVALENTS,
            validation_plan.PATH_CLASSIFICATION_EQUIVALENTS,
        )

        for baseline, equivalents in PATH_CLASSIFICATION_EQUIVALENTS:
            for equivalent in equivalents:
                for tier in ("fast", "pr"):
                    with self.subTest(
                        baseline=baseline,
                        equivalent=equivalent,
                        tier=tier,
                    ):
                        self.assertEqual(
                            validation_plan.plan(
                                [baseline],
                                tier,
                                "main",
                            ),
                            validation_plan.plan(
                                [equivalent],
                                tier,
                                "main",
                            ),
                        )

        self.assertIn(
            STRUCTURAL_TEST_PATH,
            validation_plan.HOST_UNIT_ONLY_INPUTS,
        )
        self.assertEqual(
            [
                validation_plan.UNIT_FAST,
                *validation_plan.diff_commands("main"),
            ],
            validation_plan.plan(
                [STRUCTURAL_TEST_PATH],
                "fast",
                "main",
            ),
        )
        self.assertEqual(
            [],
            validation_plan.affected_workflows(
                [STRUCTURAL_TEST_PATH]
            ),
        )

    def test_long_function_migration_matches_review_signal(self) -> None:
        initial = set(INITIAL_LONG_FUNCTION_MIGRATION)
        migration = set(LONG_FUNCTION_MIGRATION)
        oversized = {}

        self.assertEqual(
            len(LONG_FUNCTION_MIGRATION),
            len(migration),
            "long-function migration contains duplicates",
        )
        self.assertTrue(
            migration <= initial,
            "long-function migration may only shrink",
        )

        for relative_path in all_source_set_paths():
            path = REPOSITORY_ROOT / relative_path
            if not path.is_file():
                continue
            for function_name, line_count in c_function_definitions(
                path.read_text(encoding="utf-8")
            ).items():
                if line_count > FUNCTION_REVIEW_LINE_LIMIT:
                    oversized[(relative_path, function_name)] = line_count

        self.assertEqual(migration, set(oversized), oversized)


if __name__ == "__main__":
    unittest.main()
