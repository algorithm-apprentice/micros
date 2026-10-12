from __future__ import annotations

import ast
import hashlib
from pathlib import Path
import re
import unittest

from tests.host.test_frame_ownership_source_layout import (
    c_function_definitions,
    c_lexical_text,
    cmake_command,
)
from tests.host.test_vm_service_source_layout import (
    c_declaration_text,
    file_scope_objects,
)
from tools import validation_plan


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
FUNCTION_REVIEW_LINE_LIMIT = 120
GENERATOR_PATH = "tools/generate_pm_service_fixture.py"

SOURCE_SETS = {
    "MICROS_PM_CORE_SOURCES": (
        "servers/pm/pm_common.c",
        "servers/pm/pm_core.c",
        "servers/pm/pm_protocol.c",
        "servers/pm/pm_validation.c",
        "servers/pm/pm_spawn.c",
        "servers/pm/pm_rollback.c",
        "servers/pm/pm_exit.c",
        "servers/pm/pm_wait.c",
    ),
    "MICROS_SPAWN_REQUEST_SOURCES": (
        "lib/spawn/spawn_request.c",
        "lib/spawn/spawn_stack_layout.c",
    ),
    "MICROS_PM_SERVICE_SOURCES": (
        "servers/pm/pm_service.c",
        "servers/pm/pm_service_fixture.c",
        "servers/pm/pm_service_bootstrap.c",
        "servers/pm/pm_service_requests.c",
        "servers/pm/pm_service_self_test.c",
        "servers/pm/pm_service_scratch.c",
    ),
    "MICROS_PM_CORE_HOST_TEST_SOURCES": (
        "tests/host/pm_test.c",
        "tests/host/pm_test_fixture.c",
        "tests/host/pm_protocol_test.c",
        "tests/host/pm_spawn_test.c",
        "tests/host/pm_validation_test.c",
        "tests/host/pm_exit_wait_test.c",
        "tests/host/pm_model_test.c",
        "tests/host/pm_model_reference.c",
        "tests/host/pm_model_operations.c",
    ),
    "MICROS_SPAWN_ABI_HOST_TEST_SOURCES": (
        "tests/host/spawn_abi_test.c",
        "tests/host/spawn_abi_test_fixture.c",
        "tests/host/spawn_request_test.c",
        "tests/host/application_stack_test.c",
        "tests/host/vm_elf_test.c",
    ),
    "MICROS_PM_SERVICE_QEMU_TEST_SOURCES": (
        "kernel/pm_service_test.c",
        "kernel/pm_service_test_fixture.c",
        "kernel/pm_service_test_report.c",
    ),
}

PRIVATE_HEADERS = (
    "servers/pm/pm_internal.h",
    "servers/pm/pm_service_internal.h",
    "tests/host/pm_test_internal.h",
    "tests/host/pm_model_test_internal.h",
    "tests/host/spawn_abi_test_internal.h",
    "kernel/pm_service_test_internal.h",
)

STABLE_HEADER_HASHES = {
    "include/micros/pm.h":
        "03d4c85565fa859046fcc48140331affc8da4ca453e7f6972d448287b037240c",
    "include/micros/spawn.h":
        "d78e2c03f02ae06a24fdfc3fd1f2908cb184068f298a428b9be1a7789459f5f0",
    "lib/spawn/spawn_request.h":
        "98d4cd7beec17b7d7ef00070e31b41c6a4cdf10a856dea1e1b545bd69c8966cf",
    "servers/pm/pm_core.h":
        "96d660e2dcd7d7cc62030dea77d08bd373d03e34d1e571b501abb77af5254696",
    "servers/pm/pm_control.h":
        "8ea7047976c776e75d5e4bd46e36fb5de2232b4f025e337673cde3d0820fb6ef",
    "kernel/pm_service_test.h":
        "2977f85b83c1fca182ccdecc594529fc3e8dbf4fb05a2f35e9244ee7213af443",
    "kernel/pm_service_test_fixture.h":
        "90645c9d069a09d15840676c79d81a86d7922363d227d5e83ef59e873a29861e",
    "tests/qemu/pm_service_protocol.h":
        "b04b32c0bf1617d7b774d00e9f74146d5f25c7381ce3f615f11449275bdf00d1",
}

FINAL_OWNER_GROUPS = {
    "servers/pm/pm_protocol.c": (
        "micros_pm_decode_request", "micros_pm_build_result",
    ),
    "servers/pm/pm_validation.c": ("micros_pm_table_validate",),
    "servers/pm/pm_spawn.c": (
        "micros_pm_begin_init", "micros_pm_begin_spawn",
        "micros_pm_spawn_reserve_kernel", "micros_pm_spawn_prepare_image",
        "micros_pm_spawn_freeze_mappings", "micros_pm_spawn_prepare_descriptors",
        "micros_pm_spawn_prepare_execution", "micros_pm_spawn_commit_hidden",
        "micros_pm_spawn_activate",
    ),
    "servers/pm/pm_rollback.c": (
        "micros_pm_spawn_rollback_begin", "micros_pm_spawn_rollback_ack",
    ),
    "servers/pm/pm_exit.c": (
        "micros_pm_exit_begin", "micros_pm_exit_detach",
        "micros_pm_exit_release_vm", "micros_pm_exit_release_process",
    ),
    "servers/pm/pm_wait.c": (
        "micros_pm_wait_begin", "micros_pm_wait_resume",
        "micros_pm_wait_reply_complete",
    ),
    "lib/spawn/spawn_stack_layout.c": ("micros_spawn_stack_layout_plan",),
    "kernel/pm_service_test_report.c": ("micros_pm_service_test_handle_trap",),
    "tests/host/pm_protocol_test.c": ("test_protocol_contract",),
    "tests/host/pm_spawn_test.c": (
        "test_runtime_gate_and_spawn", "test_reverse_rollback_and_identity",
        "test_rollback_acknowledgment_identity",
        "test_generation_and_pid_exhaustion", "test_failed_init_is_terminal",
    ),
    "tests/host/pm_validation_test.c": (
        "test_endpoint_lookup_and_failure_preservation",
        "test_orphaned_lifecycle_is_invalid",
    ),
    "tests/host/pm_exit_wait_test.c": (
        "test_exit_wait_and_reply_restore", "test_wait_selection",
        "test_parent_exit_reparents_and_aborts",
        "test_nested_parent_exit_stages_independent_waits",
    ),
    "tests/host/pm_model_test.c": ("test_replayable_transition_model",),
    "tests/host/spawn_request_test.c": (
        "test_spawn_request_contract", "test_spawn_request_boundaries",
        "test_spawn_request_rejections",
    ),
    "tests/host/application_stack_test.c": (
        "test_application_stack", "test_application_stack_boundaries",
    ),
    "tests/host/vm_elf_test.c": (
        "test_elf_plan", "test_elf_header_rejections",
        "test_elf_segment_rejections",
    ),
}


def original_owner(final_owner: str) -> str:
    if final_owner.startswith("servers/pm/"):
        return "servers/pm/pm_core.c"
    if final_owner.startswith("lib/spawn/"):
        return "lib/spawn/spawn_request.c"
    if final_owner.startswith("kernel/"):
        return "kernel/pm_service_test.c"
    if final_owner in {
        "tests/host/spawn_request_test.c",
        "tests/host/application_stack_test.c",
        "tests/host/vm_elf_test.c",
    }:
        return "tests/host/spawn_abi_test.c"
    return "tests/host/pm_test.c"


INITIAL_SYMBOL_OWNER_MIGRATION = tuple(
    (original_owner(owner), owner, name)
    for owner, names in FINAL_OWNER_GROUPS.items()
    for name in names
)
SYMBOL_OWNER_MIGRATION = tuple(
    migration for migration in INITIAL_SYMBOL_OWNER_MIGRATION
    if not migration[1].startswith("servers/pm/")
    and migration[2] != "micros_spawn_stack_layout_plan"
    and migration[2] not in {
        "test_protocol_contract",
        "test_rollback_acknowledgment_identity",
        "test_endpoint_lookup_and_failure_preservation",
        "test_exit_wait_and_reply_restore",
        "test_nested_parent_exit_stages_independent_waits",
        "test_parent_exit_reparents_and_aborts",
        "test_replayable_transition_model",
        "test_runtime_gate_and_spawn", "test_reverse_rollback_and_identity",
        "test_generation_and_pid_exhaustion", "test_failed_init_is_terminal",
        "test_orphaned_lifecycle_is_invalid", "test_wait_selection",
    }
)

INITIAL_LONG_FUNCTION_MIGRATION = (
    ("servers/pm/pm_core.c", "micros_pm_table_validate"),
    ("servers/pm/pm_service.c", "run_self_test"),
    ("lib/spawn/spawn_request.c", "micros_spawn_request_decode"),
    ("tests/host/pm_test.c", "test_protocol_contract"),
    ("tests/host/pm_test.c", "test_rollback_acknowledgment_identity"),
    ("tests/host/pm_test.c", "test_endpoint_lookup_and_failure_preservation"),
    ("tests/host/pm_test.c", "test_exit_wait_and_reply_restore"),
    ("tests/host/pm_test.c", "test_parent_exit_reparents_and_aborts"),
    ("tests/host/pm_test.c", "test_nested_parent_exit_stages_independent_waits"),
    ("tests/host/pm_test.c", "test_replayable_transition_model"),
    ("tests/host/spawn_abi_test.c", "test_spawn_request_rejections"),
    ("tests/host/spawn_abi_test.c", "test_application_stack"),
    ("tests/host/spawn_abi_test.c", "test_elf_header_rejections"),
    ("tests/host/spawn_abi_test.c", "test_elf_segment_rejections"),
    ("kernel/pm_service_test.c", "micros_pm_service_test_handle_trap"),
    (GENERATOR_PATH, "render_fixture"),
)
LONG_FUNCTION_MIGRATION = tuple(
    migration for migration in INITIAL_LONG_FUNCTION_MIGRATION
    if migration[1] not in {
        "micros_spawn_request_decode",
        "micros_pm_table_validate", "test_protocol_contract",
        "test_rollback_acknowledgment_identity",
        "test_endpoint_lookup_and_failure_preservation",
        "test_exit_wait_and_reply_restore",
        "test_nested_parent_exit_stages_independent_waits",
        "test_parent_exit_reparents_and_aborts",
        "test_replayable_transition_model",
    }
)


def all_sources() -> tuple[str, ...]:
    return tuple(path for sources in SOURCE_SETS.values() for path in sources)


class PmServiceSourceLayoutTest(unittest.TestCase):
    def test_all_accepted_sources_and_private_headers_exist(self) -> None:
        self.assertEqual(
            [],
            [
                path for path in (*all_sources(), *PRIVATE_HEADERS)
                if not (REPOSITORY_ROOT / path).is_file()
            ],
        )

    def test_stable_headers_are_byte_exact(self) -> None:
        self.assertEqual(
            STABLE_HEADER_HASHES,
            {
                path: hashlib.sha256((REPOSITORY_ROOT / path).read_bytes()).hexdigest()
                for path in STABLE_HEADER_HASHES
            },
        )

    def test_source_sets_and_current_consumers_are_exact(self) -> None:
        cmake = (REPOSITORY_ROOT / "CMakeLists.txt").read_text()
        for name, sources in SOURCE_SETS.items():
            with self.subTest(source_set=name):
                block = cmake_command(cmake, "set", name)
                self.assertEqual(
                    sources,
                    tuple(re.findall(
                        r"(?:servers/pm|lib/spawn|kernel|tests/host)/"
                        r"[A-Za-z0-9_]+\.c", block,
                    )),
                )
                for source in sources:
                    self.assertEqual(1, cmake.count(source), source)
        for target, required in {
            "pm_test": ("MICROS_PM_CORE_SOURCES", "MICROS_PM_CORE_HOST_TEST_SOURCES"),
            "spawn_abi_test": (
                "MICROS_SPAWN_REQUEST_SOURCES", "MICROS_SPAWN_ABI_HOST_TEST_SOURCES",
            ),
            "vm_core_test": ("MICROS_SPAWN_REQUEST_SOURCES",),
            "vfs_test": ("MICROS_SPAWN_REQUEST_SOURCES",),
            "vfs_model_test": ("MICROS_SPAWN_REQUEST_SOURCES",),
            "vfs_service_test": ("MICROS_SPAWN_REQUEST_SOURCES",),
        }.items():
            block = cmake_command(cmake, "add_executable", target)
            for name in SOURCE_SETS:
                self.assertEqual(int(name in required), block.count(f"${{{name}}}"))
            for source in all_sources():
                self.assertNotIn(source, block)
        self.assertEqual(5, cmake.count("${MICROS_PM_SERVICE_SOURCES}"))
        self.assertEqual(6, cmake.count("${MICROS_PM_CORE_SOURCES}"))
        self.assertEqual(7, cmake.count("${MICROS_SPAWN_REQUEST_SOURCES}"))
        self.assertEqual(1, cmake.count("${MICROS_PM_SERVICE_QEMU_TEST_SOURCES}"))

    def test_public_and_case_owners_follow_exact_migration(self) -> None:
        self.assertEqual(44, len(INITIAL_SYMBOL_OWNER_MIGRATION))
        migration = set(SYMBOL_OWNER_MIGRATION)
        self.assertEqual(len(migration), len(SYMBOL_OWNER_MIGRATION))
        self.assertLessEqual(migration, set(INITIAL_SYMBOL_OWNER_MIGRATION))
        current = {name: owner for owner, _, name in migration}
        definitions = {}
        for path in all_sources():
            if not (REPOSITORY_ROOT / path).is_file():
                continue
            for name in c_function_definitions((REPOSITORY_ROOT / path).read_text()):
                definitions.setdefault(name, []).append(path)
        for owner, names in FINAL_OWNER_GROUPS.items():
            for name in names:
                self.assertEqual([current.get(name, owner)], definitions.get(name, []), name)

    def test_private_headers_are_declarations_only(self) -> None:
        for path in PRIVATE_HEADERS:
            if not (REPOSITORY_ROOT / path).is_file():
                continue
            text = (REPOSITORY_ROOT / path).read_text()
            self.assertEqual({}, c_function_definitions(text), path)
            self.assertEqual((), file_scope_objects(text), path)
            self.assertNotRegex(c_declaration_text(text), r"\bstatic\b")

    def test_current_static_state_has_exact_owners(self) -> None:
        expected = {
            "process_table": ["servers/pm/pm_service.c"],
            "pm_service_data": ["servers/pm/pm_service.c"],
            "initial_table": ["servers/pm/pm_service.c"],
            "request_bytes": ["tests/host/spawn_abi_test.c"],
            "elf_bytes": ["tests/host/spawn_abi_test.c"],
            "path_storage": ["tests/host/spawn_abi_test.c"],
            "argument_storage": ["tests/host/spawn_abi_test.c"] * 2,
            "environment_storage": ["tests/host/spawn_abi_test.c"] * 2,
        }
        for name, owners in expected.items():
            observed = []
            pattern = re.compile(
                r"\bstatic\s+(?:volatile\s+)?"
                r"(?:struct\s+[A-Za-z_][A-Za-z0-9_]*|uint8_t|uint64_t)\s+"
                rf"{re.escape(name)}\b"
            )
            for path in all_sources():
                text = c_lexical_text((REPOSITORY_ROOT / path).read_text())
                observed.extend(path for _ in pattern.finditer(text))
            self.assertEqual(owners, observed, name)
        for path in (*SOURCE_SETS["MICROS_PM_CORE_HOST_TEST_SOURCES"],
                     *SOURCE_SETS["MICROS_PM_SERVICE_QEMU_TEST_SOURCES"]):
            text = c_lexical_text((REPOSITORY_ROOT / path).read_text())
            self.assertNotRegex(
                text, r"\bstatic\s+(?:volatile\s+)?struct\s+micros_pm_table\b",
            )

    def test_new_paths_inherit_exact_original_classification(self) -> None:
        baselines = {
            "servers/pm/pm_core.c",
            "servers/pm/pm_service.c",
            "lib/spawn/spawn_request.c",
            "tests/host/pm_test.c",
            "tests/host/spawn_abi_test.c",
            "kernel/pm_service_test.c",
        }
        observed = [
            entry for entry in validation_plan.PATH_CLASSIFICATION_EQUIVALENTS
            if entry[0] in baselines
        ]
        self.assertEqual(baselines, {baseline for baseline, _ in observed})
        expected = set(all_sources()) | set(PRIVATE_HEADERS)
        inherited = set(baselines)
        for baseline, paths in observed:
            inherited.update(paths)
            for path in paths:
                for tier in ("fast", "pr"):
                    self.assertEqual(
                        validation_plan.plan([baseline], tier, "main"),
                        validation_plan.plan([path], tier, "main"),
                        path,
                    )
        self.assertEqual(expected, inherited)
        self.assertIn(
            "tests/host/test_pm_service_source_layout.py",
            validation_plan.HOST_UNIT_ONLY_INPUTS,
        )

    def test_long_function_migration_matches_real_signal(self) -> None:
        self.assertEqual(16, len(INITIAL_LONG_FUNCTION_MIGRATION))
        oversized = set()
        for path in all_sources():
            if not (REPOSITORY_ROOT / path).is_file():
                continue
            for name, count in c_function_definitions(
                (REPOSITORY_ROOT / path).read_text()
            ).items():
                if count > FUNCTION_REVIEW_LINE_LIMIT:
                    oversized.add((path, name))
        for node in ast.walk(ast.parse((REPOSITORY_ROOT / GENERATOR_PATH).read_text())):
            if (
                isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
                and node.end_lineno - node.lineno + 1 > FUNCTION_REVIEW_LINE_LIMIT
            ):
                oversized.add((GENERATOR_PATH, node.name))
        self.assertLessEqual(
            set(LONG_FUNCTION_MIGRATION), set(INITIAL_LONG_FUNCTION_MIGRATION),
        )
        self.assertEqual(set(LONG_FUNCTION_MIGRATION), oversized)


if __name__ == "__main__":
    unittest.main()
