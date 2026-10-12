from __future__ import annotations

import hashlib
from pathlib import Path
import re
import unittest

from tools import validation_plan
from tests.host.test_frame_ownership_source_layout import (
    c_function_definitions,
    cmake_command,
    static_definition_paths,
)


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CMAKE_PATH = REPOSITORY_ROOT / "CMakeLists.txt"
FUNCTION_REVIEW_LINE_LIMIT = 120
STRUCTURAL_TEST_PATH = (
    "tests/host/test_kernel_pm_control_source_layout.py"
)

SOURCE_SETS = {
    "MICROS_PM_CONTROL_CORE_SOURCES": (
        "kernel/pm_control_core.c",
        "kernel/pm_control_abi.c",
        "kernel/pm_control_validation.c",
        "kernel/pm_control_reservation.c",
        "kernel/pm_control_execution.c",
        "kernel/pm_control_activation.c",
        "kernel/pm_control_exit.c",
        "kernel/pm_control_runtime_core.c",
        "kernel/pm_control_syscall_core.c",
    ),
    "MICROS_PM_CONTROL_TARGET_SOURCES": (
        "kernel/pm_control_runtime.c",
        "kernel/pm_control_syscall.c",
        "kernel/pm_control_syscall_runtime.c",
        "kernel/pm_control_syscall_reservation.c",
        "kernel/pm_control_syscall_execution.c",
        "kernel/pm_control_syscall_activation.c",
        "kernel/pm_control_syscall_exit.c",
    ),
    "MICROS_PM_CONTROL_HOST_TEST_SOURCES": (
        "tests/host/pm_control_test.c",
        "tests/host/pm_control_test_fixture.c",
        "tests/host/pm_control_abi_test.c",
        "tests/host/pm_control_syscall_test.c",
        "tests/host/pm_control_reservation_test.c",
        "tests/host/pm_control_runtime_test.c",
    ),
    "MICROS_PM_CONTROL_EXECUTION_HOST_TEST_SOURCES": (
        "tests/host/pm_control_execution_test.c",
        "tests/host/pm_control_execution_test_fixture.c",
        "tests/host/pm_control_execution_prepare_test.c",
        "tests/host/pm_control_execution_token_test.c",
        "tests/host/pm_control_execution_exit_test.c",
    ),
}

PRIVATE_HEADERS = (
    "kernel/pm_control_internal.h",
    "kernel/pm_control_syscall_internal.h",
    "tests/host/pm_control_test_internal.h",
    "tests/host/pm_control_execution_test_internal.h",
)

TEST_PRIVATE_HEADERS = (
    "tests/host/pm_control_test_internal.h",
    "tests/host/pm_control_execution_test_internal.h",
)

STABLE_HEADER_HASHES = {
    "include/micros/pm.h":
        "03d4c85565fa859046fcc48140331affc8da4ca453e7f6972d448287b037240c",
    "kernel/pm_control_core.h":
        "a9a402c6b68fc1cdb43ac2eb4964f9e2e9a92855334774c7d863fc1e4b870a9e",
    "kernel/pm_control_runtime.h":
        "24fe6f840c55c5bbe6bf2d6fca216ad780fe5ffb054f6d50548c178490e444c0",
    "kernel/pm_control_runtime_core.h":
        "5ea8d15732689c728f21774161caa21f90e8d3c529066e3b4f2a33daefdb30a6",
    "kernel/pm_control_syscall.h":
        "e2f8bf689879dfc15887e796542e7ec2d9898c3f8809518052e9567d07af820f",
    "kernel/pm_control_syscall_core.h":
        "0f12e8eece367052bba8da14bcce990261cb91619114567388f1adfb647aef82",
}

PM_PATH_CLASSIFICATION_EQUIVALENTS = (
    (
        "kernel/pm_control_core.c",
        (
            "kernel/pm_control_abi.c",
            "kernel/pm_control_validation.c",
            "kernel/pm_control_reservation.c",
            "kernel/pm_control_execution.c",
            "kernel/pm_control_activation.c",
            "kernel/pm_control_exit.c",
            "kernel/pm_control_internal.h",
        ),
    ),
    (
        "kernel/pm_control_syscall.c",
        (
            "kernel/pm_control_syscall_runtime.c",
            "kernel/pm_control_syscall_reservation.c",
            "kernel/pm_control_syscall_execution.c",
            "kernel/pm_control_syscall_activation.c",
            "kernel/pm_control_syscall_exit.c",
            "kernel/pm_control_syscall_internal.h",
        ),
    ),
    (
        "tests/host/pm_control_test.c",
        (
            "tests/host/pm_control_test_fixture.c",
            "tests/host/pm_control_abi_test.c",
            "tests/host/pm_control_syscall_test.c",
            "tests/host/pm_control_reservation_test.c",
            "tests/host/pm_control_runtime_test.c",
            "tests/host/pm_control_test_internal.h",
        ),
    ),
    (
        "tests/host/pm_control_execution_test.c",
        (
            "tests/host/pm_control_execution_test_fixture.c",
            "tests/host/pm_control_execution_prepare_test.c",
            "tests/host/pm_control_execution_token_test.c",
            "tests/host/pm_control_execution_exit_test.c",
            "tests/host/pm_control_execution_test_internal.h",
        ),
    ),
)

PORTABLE_FINAL_OWNERS = {
    "micros_pm_control_state_prepare":
        "kernel/pm_control_core.c",
    "micros_pm_control_decode":
        "kernel/pm_control_abi.c",
    "micros_pm_control_state_validate":
        "kernel/pm_control_validation.c",
    "micros_pm_control_state_validate_objects":
        "kernel/pm_control_validation.c",
    "micros_pm_control_reserve_preflight":
        "kernel/pm_control_reservation.c",
    "micros_pm_control_reserve_commit_prevalidated":
        "kernel/pm_control_reservation.c",
    "micros_pm_control_abort_preflight":
        "kernel/pm_control_reservation.c",
    "micros_pm_control_abort_commit_prevalidated":
        "kernel/pm_control_reservation.c",
    "micros_pm_control_prepare_preflight":
        "kernel/pm_control_execution.c",
    "micros_pm_control_prepare_commit_prevalidated":
        "kernel/pm_control_execution.c",
    "micros_pm_control_abort_prepared_preflight":
        "kernel/pm_control_execution.c",
    "micros_pm_control_abort_prepared_commit_prevalidated":
        "kernel/pm_control_execution.c",
    "micros_pm_control_activate_preflight":
        "kernel/pm_control_activation.c",
    "micros_pm_control_activate_commit_prevalidated":
        "kernel/pm_control_activation.c",
    "micros_pm_control_exit_begin_preflight":
        "kernel/pm_control_exit.c",
    "micros_pm_control_exit_begin_commit_prevalidated":
        "kernel/pm_control_exit.c",
    "micros_pm_control_exit_detach_preflight":
        "kernel/pm_control_exit.c",
    "micros_pm_control_exit_detach_commit_prevalidated":
        "kernel/pm_control_exit.c",
    "micros_pm_control_release_exited_preflight":
        "kernel/pm_control_exit.c",
    "micros_pm_control_release_exited_commit_prevalidated":
        "kernel/pm_control_exit.c",
}

TEST_FINAL_OWNERS = {
    "micros_pm_control_test_run":
        "tests/host/pm_control_test.c",
    "test_contract_and_decode":
        "tests/host/pm_control_abi_test.c",
    "test_syscall_authority":
        "tests/host/pm_control_syscall_test.c",
    "test_syscall_authority_resolution":
        "tests/host/pm_control_syscall_test.c",
    "test_syscall_phase_and_retained_output":
        "tests/host/pm_control_syscall_test.c",
    "test_state_preparation":
        "tests/host/pm_control_reservation_test.c",
    "test_reserve_abort_lifecycle":
        "tests/host/pm_control_reservation_test.c",
    "test_capacity_and_quarantine":
        "tests/host/pm_control_reservation_test.c",
    "test_invariant_rejection":
        "tests/host/pm_control_reservation_test.c",
    "test_runtime_accepts_exact_frozen_vm_reservation":
        "tests/host/pm_control_runtime_test.c",
    "micros_pm_control_execution_test_run":
        "tests/host/pm_control_execution_test.c",
    "test_prepare_abort_activate":
        "tests/host/pm_control_execution_prepare_test.c",
    "test_prepared_corruption_is_invariant":
        "tests/host/pm_control_execution_prepare_test.c",
    "test_token_exhaustion_is_invariant":
        "tests/host/pm_control_execution_token_test.c",
    "test_exit_begin_detach_release":
        "tests/host/pm_control_execution_exit_test.c",
}

FINAL_SYMBOL_OWNERS = {
    **PORTABLE_FINAL_OWNERS,
    "micros_pm_control_handle_captured_user_ecall":
        "kernel/pm_control_syscall.c",
    **TEST_FINAL_OWNERS,
}

CURRENT_SYMBOL_OWNERS = {
    **{
        symbol: "kernel/pm_control_core.c"
        for symbol in PORTABLE_FINAL_OWNERS
    },
    "micros_pm_control_handle_captured_user_ecall":
        "kernel/pm_control_syscall.c",
    **{
        symbol: "tests/host/pm_control_test.c"
        for symbol in (
            "micros_pm_control_test_run",
            "test_contract_and_decode",
            "test_syscall_authority",
            "test_syscall_authority_resolution",
            "test_syscall_phase_and_retained_output",
            "test_state_preparation",
            "test_reserve_abort_lifecycle",
            "test_capacity_and_quarantine",
            "test_invariant_rejection",
            "test_runtime_accepts_exact_frozen_vm_reservation",
        )
    },
    **{
        symbol: "tests/host/pm_control_execution_test.c"
        for symbol in (
            "micros_pm_control_execution_test_run",
            "test_prepare_abort_activate",
            "test_prepared_corruption_is_invariant",
            "test_token_exhaustion_is_invariant",
            "test_exit_begin_detach_release",
        )
    },
}

INITIAL_SYMBOL_OWNER_MIGRATION = tuple(
    (
        CURRENT_SYMBOL_OWNERS[symbol],
        final_owner,
        symbol,
    )
    for symbol, final_owner in FINAL_SYMBOL_OWNERS.items()
    if CURRENT_SYMBOL_OWNERS[symbol] != final_owner
)

SYMBOL_OWNER_MIGRATION = tuple(
    migration
    for migration in INITIAL_SYMBOL_OWNER_MIGRATION
    if migration[1] not in {
        "kernel/pm_control_abi.c",
        "kernel/pm_control_activation.c",
        "kernel/pm_control_execution.c",
        "kernel/pm_control_validation.c",
        "kernel/pm_control_reservation.c",
        "tests/host/pm_control_abi_test.c",
        "tests/host/pm_control_execution_exit_test.c",
        "tests/host/pm_control_execution_prepare_test.c",
        "tests/host/pm_control_execution_token_test.c",
        "tests/host/pm_control_syscall_test.c",
        "tests/host/pm_control_reservation_test.c",
        "tests/host/pm_control_runtime_test.c",
    }
)

INITIAL_LONG_FUNCTION_MIGRATION = (
    (
        "kernel/pm_control_core.c",
        "micros_pm_control_state_validate_objects",
    ),
    ("kernel/pm_control_core.c", "micros_pm_control_decode"),
    (
        "kernel/pm_control_core.c",
        "micros_pm_control_prepare_preflight",
    ),
    ("kernel/pm_control_core.c", "running_application_resolve"),
    (
        "kernel/pm_control_syscall.c",
        "micros_pm_control_handle_captured_user_ecall",
    ),
    ("tests/host/pm_control_test.c", "test_contract_and_decode"),
    ("tests/host/pm_control_test.c", "test_syscall_authority"),
    (
        "tests/host/pm_control_test.c",
        "test_syscall_authority_resolution",
    ),
    (
        "tests/host/pm_control_test.c",
        "test_syscall_phase_and_retained_output",
    ),
    (
        "tests/host/pm_control_test.c",
        "test_reserve_abort_lifecycle",
    ),
    (
        "tests/host/pm_control_test.c",
        "test_runtime_accepts_exact_frozen_vm_reservation",
    ),
    ("tests/host/pm_control_execution_test.c", "freeze_child"),
    (
        "tests/host/pm_control_execution_test.c",
        "initialize_fixture",
    ),
    (
        "tests/host/pm_control_execution_test.c",
        "test_prepare_abort_activate",
    ),
    (
        "tests/host/pm_control_execution_test.c",
        "test_exit_begin_detach_release",
    ),
)

LONG_FUNCTION_MIGRATION = tuple(
    migration
    for migration in INITIAL_LONG_FUNCTION_MIGRATION
    if migration not in {
        (
            "kernel/pm_control_core.c",
            "micros_pm_control_state_validate_objects",
        ),
        ("kernel/pm_control_core.c", "micros_pm_control_decode"),
        (
            "tests/host/pm_control_test.c",
            "test_contract_and_decode",
        ),
        (
            "tests/host/pm_control_test.c",
            "test_syscall_authority",
        ),
        (
            "tests/host/pm_control_test.c",
            "test_syscall_authority_resolution",
        ),
        (
            "tests/host/pm_control_test.c",
            "test_syscall_phase_and_retained_output",
        ),
        (
            "tests/host/pm_control_test.c",
            "test_reserve_abort_lifecycle",
        ),
        (
            "tests/host/pm_control_test.c",
            "test_runtime_accepts_exact_frozen_vm_reservation",
        ),
        (
            "kernel/pm_control_core.c",
            "micros_pm_control_prepare_preflight",
        ),
        (
            "tests/host/pm_control_execution_test.c",
            "freeze_child",
        ),
        (
            "tests/host/pm_control_execution_test.c",
            "initialize_fixture",
        ),
        (
            "tests/host/pm_control_execution_test.c",
            "test_prepare_abort_activate",
        ),
        (
            "tests/host/pm_control_execution_test.c",
            "test_exit_begin_detach_release",
        ),
    }
)

PM_CONTROL_CASE_ORDER = (
    "test_contract_and_decode",
    "test_syscall_authority",
    "test_syscall_authority_resolution",
    "test_syscall_phase_and_retained_output",
    "test_state_preparation",
    "test_reserve_abort_lifecycle",
    "test_capacity_and_quarantine",
    "test_invariant_rejection",
    "test_runtime_accepts_exact_frozen_vm_reservation",
)

PM_CONTROL_EXECUTION_CASE_ORDER = (
    "test_prepare_abort_activate",
    "test_prepared_corruption_is_invariant",
    "test_token_exhaustion_is_invariant",
    "test_exit_begin_detach_release",
)


def all_source_set_paths() -> tuple[str, ...]:
    return tuple(
        dict.fromkeys(
            path
            for sources in SOURCE_SETS.values()
            for path in sources
        )
    )


def production_sources() -> tuple[str, ...]:
    return (
        *SOURCE_SETS["MICROS_PM_CONTROL_CORE_SOURCES"],
        *SOURCE_SETS["MICROS_PM_CONTROL_TARGET_SOURCES"],
    )


def accepted_family_paths() -> tuple[str, ...]:
    return tuple(
        dict.fromkeys(
            (
                *all_source_set_paths(),
                *PRIVATE_HEADERS,
                *STABLE_HEADER_HASHES,
            )
        )
    )


def discovered_family_paths() -> tuple[str, ...]:
    patterns = (
        "kernel/pm_control*.c",
        "kernel/pm_control*.h",
        "tests/host/pm_control*.c",
        "tests/host/pm_control*.h",
    )
    discovered = {
        str(path.relative_to(REPOSITORY_ROOT))
        for pattern in patterns
        for path in REPOSITORY_ROOT.glob(pattern)
    }
    discovered.add("include/micros/pm.h")
    return tuple(sorted(discovered))


def resolve_local_include(
    source: Path,
    include: str,
) -> Path | None:
    candidates = (
        source.parent / include,
        REPOSITORY_ROOT / include,
        REPOSITORY_ROOT / "include" / include,
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    return None


def compiled_source_header_closure() -> tuple[str, ...]:
    pending = [
        (REPOSITORY_ROOT / relative).resolve()
        for relative in accepted_family_paths()
        if (REPOSITORY_ROOT / relative).is_file()
    ]
    observed = set()
    include_pattern = re.compile(
        r'(?m)^\s*#\s*include\s*"([^"]+)"'
    )

    while pending:
        path = pending.pop()
        if path in observed:
            continue
        observed.add(path)
        for include in include_pattern.findall(
            path.read_text(encoding="utf-8")
        ):
            resolved = resolve_local_include(path, include)
            if (
                resolved is not None
                and resolved.suffix == ".h"
                and REPOSITORY_ROOT in resolved.parents
            ):
                pending.append(resolved)

    return tuple(
        sorted(str(path.relative_to(REPOSITORY_ROOT)) for path in observed)
    )


class KernelPmControlSourceLayoutTest(unittest.TestCase):
    def test_expected_sources_and_private_headers_exist(self) -> None:
        missing = [
            path
            for path in (*all_source_set_paths(), *PRIVATE_HEADERS)
            if not (REPOSITORY_ROOT / path).is_file()
        ]
        self.assertEqual([], missing)

    def test_family_path_inventory_is_exact(self) -> None:
        self.assertEqual(
            tuple(sorted(accepted_family_paths())),
            discovered_family_paths(),
        )

    def test_stable_headers_are_byte_exact(self) -> None:
        observed = {
            path: hashlib.sha256(
                (REPOSITORY_ROOT / path).read_bytes()
            ).hexdigest()
            for path in STABLE_HEADER_HASHES
        }
        self.assertEqual(STABLE_HEADER_HASHES, observed)

    def test_cmake_source_sets_have_exact_ordered_membership(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")

        for name, expected in SOURCE_SETS.items():
            with self.subTest(source_set=name):
                source_set = cmake_command(cmake, "set", name)
                listed = tuple(
                    re.findall(
                        r"(?:kernel|tests/host)/"
                        r"[A-Za-z0-9_]+\.c",
                        source_set,
                    )
                )
                self.assertEqual(expected, listed)
        for source in all_source_set_paths():
            self.assertEqual(1, cmake.count(source), source)

    def test_consumers_expand_exact_source_sets_once(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")
        consumers = {
            "pm-test": (
                cmake_command(cmake, "add_executable", "pm_test"),
                (
                    "MICROS_PM_CONTROL_CORE_SOURCES",
                    "MICROS_PM_CONTROL_HOST_TEST_SOURCES",
                    "MICROS_PM_CONTROL_EXECUTION_HOST_TEST_SOURCES",
                ),
            ),
            "micros": (
                cmake_command(cmake, "add_executable", "micros"),
                (
                    "MICROS_PM_CONTROL_CORE_SOURCES",
                    "MICROS_PM_CONTROL_TARGET_SOURCES",
                ),
            ),
        }

        for consumer, (block, selected) in consumers.items():
            for source_set in SOURCE_SETS:
                with self.subTest(
                    consumer=consumer,
                    source_set=source_set,
                ):
                    self.assertEqual(
                        int(source_set in selected),
                        block.count(f"${{{source_set}}}"),
                    )
                    for relative in SOURCE_SETS[source_set]:
                        self.assertNotIn(relative, block)

    def test_private_headers_contain_declarations_only(self) -> None:
        definitions = {}

        for relative in PRIVATE_HEADERS:
            path = REPOSITORY_ROOT / relative
            if not path.is_file():
                continue
            selected = c_function_definitions(
                path.read_text(encoding="utf-8")
            )
            if selected:
                definitions[relative] = selected
        self.assertEqual({}, definitions)

    def test_sources_do_not_include_c_or_test_private_headers(self) -> None:
        c_include = re.compile(
            r"(?m)^\s*#\s*include\s*[<\"][^>\"]+\.c[>\"]"
        )
        c_violations = []
        private_violations = []

        for relative in (*all_source_set_paths(), *PRIVATE_HEADERS):
            path = REPOSITORY_ROOT / relative
            if not path.is_file():
                continue
            text = path.read_text(encoding="utf-8")
            if c_include.search(text):
                c_violations.append(relative)
            if relative in production_sources():
                for private in TEST_PRIVATE_HEADERS:
                    if Path(private).name in text:
                        private_violations.append((relative, private))
        self.assertEqual([], c_violations)
        self.assertEqual([], private_violations)

    def test_symbol_owners_follow_exact_migration_map(self) -> None:
        initial = set(INITIAL_SYMBOL_OWNER_MIGRATION)
        migration = set(SYMBOL_OWNER_MIGRATION)
        expected_current = {}
        definitions = {}

        self.assertEqual(32, len(INITIAL_SYMBOL_OWNER_MIGRATION))
        self.assertEqual(len(migration), len(SYMBOL_OWNER_MIGRATION))
        self.assertTrue(migration <= initial)
        for current, final, symbol in migration:
            self.assertEqual(FINAL_SYMBOL_OWNERS[symbol], final)
            expected_current[symbol] = current

        for relative in all_source_set_paths():
            path = REPOSITORY_ROOT / relative
            if not path.is_file():
                continue
            for symbol in c_function_definitions(
                path.read_text(encoding="utf-8")
            ):
                definitions.setdefault(symbol, []).append(relative)

        for symbol, final in FINAL_SYMBOL_OWNERS.items():
            with self.subTest(symbol=symbol):
                self.assertEqual(
                    [expected_current.get(symbol, final)],
                    definitions.get(symbol, []),
                )

    def test_mutable_state_owners_are_exact(self) -> None:
        sources = all_source_set_paths()

        self.assertEqual(
            ["kernel/pm_control_runtime.c"],
            static_definition_paths(sources, "control_state"),
        )
        for name in (
            "runtime_allocator",
            "runtime_ownership",
            "runtime_vm",
        ):
            self.assertEqual([], static_definition_paths(sources, name))
        for name in ("fixture", "fixture_snapshot"):
            self.assertEqual([], static_definition_paths(sources, name))

        context_definitions = []
        context_pattern = re.compile(
            r"(?m)^\s*static\s+struct\s+"
            r"([A-Za-z_][A-Za-z0-9_]*test_context)\b"
        )
        for relative in compiled_source_header_closure():
            path = REPOSITORY_ROOT / relative
            for context_type in context_pattern.findall(
                path.read_text(encoding="utf-8")
            ):
                context_definitions.append((relative, context_type))
        self.assertEqual(
            sorted(
                (
                    (
                        "tests/host/pm_control_test_fixture.c",
                        "pm_control_test_context",
                    ),
                    (
                        "tests/host/"
                        "pm_control_execution_test_fixture.c",
                        "pm_control_execution_test_context",
                    ),
                )
            ),
            sorted(context_definitions),
        )

    def test_evidence_identities_and_case_order_remain_stable(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")
        state_entry = (
            REPOSITORY_ROOT / "tests/host/pm_control_test.c"
        ).read_text(encoding="utf-8")
        execution_entry = (
            REPOSITORY_ROOT / "tests/host/pm_control_execution_test.c"
        ).read_text(encoding="utf-8")
        pm_entry = (
            REPOSITORY_ROOT / "tests/host/pm_test.c"
        ).read_text(encoding="utf-8")
        qemu_entry = (
            REPOSITORY_ROOT / "kernel/pm_service_test.c"
        ).read_text(encoding="utf-8")
        marker = (
            "MICROS_PM_SERVICE_TEST_PASS "
            "handoff=complete readiness=acknowledged protocol=stable "
            "sealed=received prepare=aborted-reprepared "
            "activate=committed exit=detached-released "
            "resources=clean process-generation=advanced "
            "thread-generation=advanced tokens=advanced"
        )

        self.assertEqual(1, cmake.count("NAME pm-lifecycle"))
        for target in (
            "test-qemu-pm-service",
            "test-qemu-vm-scratch-preemption",
            "test-qemu-tty",
            "test-qemu-ramfs",
            "test-qemu-vfs",
        ):
            cmake_command(cmake, "add_custom_target", target)
        pm_target = cmake_command(
            cmake,
            "add_custom_target",
            "test-qemu-pm-service",
        )
        self.assertIn("--timeout\n            45", pm_target)
        self.assertEqual(1, cmake.count(marker))
        self.assertEqual(
            1,
            qemu_entry.count("MICROS_PM_SERVICE_TEST_PASS "),
        )
        for field in (
            "handoff=complete",
            "prepare=aborted-reprepared",
            "exit=detached-released",
            "tokens=advanced",
        ):
            self.assertEqual(1, qemu_entry.count(field), field)

        state_body = state_entry[
            state_entry.index("bool micros_pm_control_test_run") :
        ]
        state_positions = [
            state_body.index(name)
            for name in PM_CONTROL_CASE_ORDER
        ]
        self.assertEqual(sorted(state_positions), state_positions)

        execution_body = execution_entry[
            execution_entry.index(
                "bool micros_pm_control_execution_test_run"
            ) :
        ]
        execution_positions = [
            execution_body.index(name)
            for name in PM_CONTROL_EXECUTION_CASE_ORDER
        ]
        self.assertEqual(
            sorted(execution_positions),
            execution_positions,
        )

        main_body = pm_entry[pm_entry.index("int main") :]
        self.assertLess(
            main_body.index("micros_pm_control_test_run"),
            main_body.index("micros_pm_control_execution_test_run"),
        )

    def test_validation_plan_inherits_path_classification(self) -> None:
        baselines = {
            baseline
            for baseline, _ in PM_PATH_CLASSIFICATION_EQUIVALENTS
        }
        observed = tuple(
            item
            for item in validation_plan.PATH_CLASSIFICATION_EQUIVALENTS
            if item[0] in baselines
        )
        self.assertEqual(PM_PATH_CLASSIFICATION_EQUIVALENTS, observed)
        for baseline, equivalents in PM_PATH_CLASSIFICATION_EQUIVALENTS:
            for equivalent in equivalents:
                for tier in ("fast", "pr"):
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
                        equivalent,
                    )
        self.assertIn(
            STRUCTURAL_TEST_PATH,
            validation_plan.HOST_UNIT_ONLY_INPUTS,
        )

    def test_long_function_migration_matches_review_signal(self) -> None:
        initial = set(INITIAL_LONG_FUNCTION_MIGRATION)
        migration = set(LONG_FUNCTION_MIGRATION)
        oversized = {}

        self.assertEqual(15, len(INITIAL_LONG_FUNCTION_MIGRATION))
        self.assertEqual(len(migration), len(LONG_FUNCTION_MIGRATION))
        self.assertTrue(migration <= initial)
        for relative in compiled_source_header_closure():
            path = REPOSITORY_ROOT / relative
            for name, line_count in c_function_definitions(
                path.read_text(encoding="utf-8")
            ).items():
                if line_count > FUNCTION_REVIEW_LINE_LIMIT:
                    oversized[(relative, name)] = line_count
        self.assertEqual(migration, set(oversized), oversized)


if __name__ == "__main__":
    unittest.main()
