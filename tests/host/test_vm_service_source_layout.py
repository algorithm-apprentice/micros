from __future__ import annotations

import ast
import hashlib
from pathlib import Path
import re
import unittest

from tools import validation_plan
from tests.host.test_frame_ownership_source_layout import (
    c_function_definitions,
    c_lexical_text,
    cmake_command,
    cmake_if_blocks,
    static_definition_paths,
)


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CMAKE_PATH = REPOSITORY_ROOT / "CMakeLists.txt"
FUNCTION_REVIEW_LINE_LIMIT = 120
STRUCTURAL_TEST_PATH = "tests/host/test_vm_service_source_layout.py"
GENERATOR_PATH = "tools/generate_vm_scratch_preemption_fixture.py"

SOURCE_SETS = {
    "MICROS_VM_BOOT_VALIDATION_SOURCES": (
        "lib/vm/vm_boot_validate.c",
    ),
    "MICROS_VM_IMAGE_SOURCES": (
        "servers/vm/vm_image.c",
        "servers/vm/vm_stack.c",
        "servers/vm/vm_elf.c",
    ),
    "MICROS_VM_CORE_SOURCES": (
        "servers/vm/vm_core.c",
        "servers/vm/vm_validation.c",
        "servers/vm/vm_frames.c",
        "servers/vm/vm_load.c",
        "servers/vm/vm_exit.c",
    ),
    "MICROS_VM_CORE_HOST_TEST_SOURCES": (
        "tests/host/vm_core_test.c",
        "tests/host/vm_core_test_fixture.c",
        "tests/host/vm_core_boot_test.c",
        "tests/host/vm_core_load_test.c",
        "tests/host/vm_core_abort_test.c",
        "tests/host/vm_core_validation_test.c",
    ),
    "MICROS_VM_SERVICE_QEMU_SOURCES": (
        "tests/qemu/vm_server.c",
        "tests/qemu/vm_server_fixture.c",
        "tests/qemu/vm_server_handoff.c",
        "tests/qemu/vm_server_probe.c",
        "tests/qemu/vm_server_tty.c",
        "tests/qemu/vm_server_scratch.c",
        "tests/qemu/vm_server_pm.c",
    ),
    "MICROS_VM_SCRATCH_PREEMPTION_QEMU_TEST_SOURCES": (
        "kernel/vm_scratch_preemption_test.c",
        "kernel/vm_scratch_preemption_test_fixture.c",
        "kernel/vm_scratch_preemption_test_runtime.c",
        "kernel/vm_scratch_preemption_test_report.c",
        "kernel/vm_scratch_preemption_test_trap.c",
    ),
}

PRIVATE_HEADERS = (
    "servers/vm/vm_internal.h",
    "servers/vm/vm_image_internal.h",
    "tests/host/vm_core_test_internal.h",
    "tests/qemu/vm_server_internal.h",
    "kernel/vm_scratch_preemption_test_internal.h",
)

TEST_PRIVATE_HEADERS = (
    "tests/host/vm_core_test_internal.h",
    "tests/qemu/vm_server_internal.h",
    "kernel/vm_scratch_preemption_test_internal.h",
)

STABLE_HEADER_HASHES = {
    "include/micros/vm_bootstrap.h":
        "a37f0a611a0e688478dfb5dfbee487b31a04fa9ff534538c4cb23c5c4967e866",
    "servers/vm/vm_core.h":
        "97c64900fcf40cb7dcfdba8a61ddabcad290feded7224710c1f2d29d99125bba",
    "servers/vm/vm_image.h":
        "aeed2227d2817ebde729302df12fe71a3f25dffc774058f2660304ebd0a1f1ae",
    "kernel/vm_scratch_preemption_test.h":
        "bd86b1b80ed1a9cfa5c8cba0809678c3c2b88d259d5a678f50c4f913c68e18bc",
    "kernel/vm_scratch_preemption_test_fixture.h":
        "d30f8be9e3a36721f7182f31f700f82fac9ffb945526381df446dc554b9bc02f",
}

VM_SERVICE_PATH_CLASSIFICATION_EQUIVALENTS = (
    (
        "servers/vm/vm_core.c",
        (
            "servers/vm/vm_validation.c",
            "servers/vm/vm_frames.c",
            "servers/vm/vm_load.c",
            "servers/vm/vm_exit.c",
            "servers/vm/vm_internal.h",
        ),
    ),
    (
        "servers/vm/vm_image.c",
        (
            "servers/vm/vm_stack.c",
            "servers/vm/vm_elf.c",
            "servers/vm/vm_image_internal.h",
        ),
    ),
    (
        "tests/host/vm_core_test.c",
        (
            "tests/host/vm_core_test_fixture.c",
            "tests/host/vm_core_boot_test.c",
            "tests/host/vm_core_load_test.c",
            "tests/host/vm_core_abort_test.c",
            "tests/host/vm_core_validation_test.c",
            "tests/host/vm_core_test_internal.h",
        ),
    ),
    (
        "tests/qemu/vm_server.c",
        (
            "tests/qemu/vm_server_fixture.c",
            "tests/qemu/vm_server_handoff.c",
            "tests/qemu/vm_server_probe.c",
            "tests/qemu/vm_server_tty.c",
            "tests/qemu/vm_server_scratch.c",
            "tests/qemu/vm_server_pm.c",
            "tests/qemu/vm_server_internal.h",
        ),
    ),
    (
        "kernel/vm_scratch_preemption_test.c",
        (
            "kernel/vm_scratch_preemption_test_fixture.c",
            "kernel/vm_scratch_preemption_test_runtime.c",
            "kernel/vm_scratch_preemption_test_report.c",
            "kernel/vm_scratch_preemption_test_trap.c",
            "kernel/vm_scratch_preemption_test_internal.h",
        ),
    ),
)

CORE_FINAL_OWNERS = {
    "micros_vm_state_initialize": "servers/vm/vm_core.c",
    "micros_vm_state_validate": "servers/vm/vm_validation.c",
    "micros_vm_load_prepare": "servers/vm/vm_load.c",
    "micros_vm_load_bind": "servers/vm/vm_load.c",
    "micros_vm_load_next_page": "servers/vm/vm_load.c",
    "micros_vm_load_scratch_mapped": "servers/vm/vm_load.c",
    "micros_vm_load_fill_scratch": "servers/vm/vm_load.c",
    "micros_vm_load_scratch_committed": "servers/vm/vm_load.c",
    "micros_vm_load_freeze_plan": "servers/vm/vm_load.c",
    "micros_vm_load_frozen": "servers/vm/vm_load.c",
    "micros_vm_load_aborted": "servers/vm/vm_load.c",
    "micros_vm_load_committed": "servers/vm/vm_load.c",
    "micros_vm_exit_release_begin": "servers/vm/vm_exit.c",
    "micros_vm_exit_release_complete": "servers/vm/vm_exit.c",
}

IMAGE_FINAL_OWNERS = {
    "micros_application_stack_plan": "servers/vm/vm_stack.c",
    "micros_application_stack_build": "servers/vm/vm_stack.c",
    "micros_vm_elf_plan": "servers/vm/vm_elf.c",
}

TEST_FINAL_OWNERS = {
    "test_record_abi": "tests/host/vm_core_boot_test.c",
    "test_boot_import": "tests/host/vm_core_boot_test.c",
    "test_load_lifecycle": "tests/host/vm_core_load_test.c",
    "test_abort_paths": "tests/host/vm_core_abort_test.c",
    "test_capacity_and_invariants":
        "tests/host/vm_core_validation_test.c",
    "test_staged_inputs_and_immutable_database":
        "tests/host/vm_core_validation_test.c",
}

FINAL_SYMBOL_OWNERS = {
    **CORE_FINAL_OWNERS,
    **IMAGE_FINAL_OWNERS,
    "micros_vm_scratch_preemption_test_handle_trap":
        "kernel/vm_scratch_preemption_test_trap.c",
    **TEST_FINAL_OWNERS,
}

CURRENT_SYMBOL_OWNERS = {
    **{
        symbol: "servers/vm/vm_core.c"
        for symbol in CORE_FINAL_OWNERS
    },
    **{
        symbol: "servers/vm/vm_image.c"
        for symbol in IMAGE_FINAL_OWNERS
    },
    "micros_vm_scratch_preemption_test_handle_trap":
        "kernel/vm_scratch_preemption_test.c",
    **{
        symbol: "tests/host/vm_core_test.c"
        for symbol in TEST_FINAL_OWNERS
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
    if migration[2] == "micros_vm_scratch_preemption_test_handle_trap"
)

INITIAL_LONG_FUNCTION_MIGRATION = (
    ("servers/vm/vm_core.c", "vm_core_validate_state"),
    ("servers/vm/vm_core.c", "micros_vm_load_prepare"),
    ("servers/vm/vm_core.c", "micros_vm_load_bind"),
    ("servers/vm/vm_image.c", "micros_vm_elf_plan"),
    ("lib/vm/vm_boot_validate.c", "micros_vm_boot_validate"),
    (
        "kernel/vm_scratch_preemption_test.c",
        "micros_vm_scratch_preemption_test_handle_trap",
    ),
    ("kernel/vm_scratch_preemption_test.c", "report_success"),
    ("tests/host/vm_core_test.c", "initialize_boot_info"),
    ("tests/host/vm_core_test.c", "test_load_lifecycle"),
    ("tests/qemu/vm_server.c", "pm_test_load_child"),
    (GENERATOR_PATH, "render_fixture"),
)

LONG_FUNCTION_MIGRATION = tuple(
    migration
    for migration in INITIAL_LONG_FUNCTION_MIGRATION
    if migration[0] in {
        "kernel/vm_scratch_preemption_test.c",
        GENERATOR_PATH,
    }
)

VM_CORE_CASE_ORDER = (
    "test_record_abi",
    "test_boot_import",
    "test_load_lifecycle",
    "test_abort_paths",
    "test_capacity_and_invariants",
    "test_staged_inputs_and_immutable_database",
)

VM_SYMBOLS = (
    "micros_vm_scratch_preemption_vm_hook_trap",
    "micros_vm_scratch_preemption_vm_hook_spin",
    "micros_vm_scratch_preemption_vm_hook_resume",
    "micros_vm_scratch_preemption_stale_load_fault",
    "micros_vm_scratch_preemption_stale_load_unexpected",
)

PEER_SYMBOLS = (
    "micros_vm_scratch_preemption_peer_hold_trap",
    "micros_vm_scratch_preemption_peer_hold_resume",
    "micros_vm_scratch_preemption_peer_run_trap",
    "micros_vm_scratch_preemption_peer_run_resume",
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
        *SOURCE_SETS["MICROS_VM_BOOT_VALIDATION_SOURCES"],
        *SOURCE_SETS["MICROS_VM_IMAGE_SOURCES"],
        *SOURCE_SETS["MICROS_VM_CORE_SOURCES"],
    )


def accepted_family_paths() -> tuple[str, ...]:
    return tuple(
        dict.fromkeys(
            (
                *all_source_set_paths(),
                *PRIVATE_HEADERS,
                *STABLE_HEADER_HASHES,
                GENERATOR_PATH,
            )
        )
    )


def discovered_family_paths() -> tuple[str, ...]:
    patterns = (
        "servers/vm/vm*.c",
        "servers/vm/vm*.h",
        "lib/vm/vm_boot*.c",
        "lib/vm/vm_boot*.h",
        "kernel/vm_scratch_preemption_test*.c",
        "kernel/vm_scratch_preemption_test*.h",
        "tests/host/vm_core*.c",
        "tests/host/vm_core*.h",
        "tests/qemu/vm_server*.c",
        "tests/qemu/vm_server*.h",
    )
    discovered = {
        str(path.relative_to(REPOSITORY_ROOT))
        for pattern in patterns
        for path in REPOSITORY_ROOT.glob(pattern)
    }
    discovered.add("include/micros/vm_bootstrap.h")
    discovered.add(GENERATOR_PATH)
    return tuple(sorted(discovered))


def resolve_local_include(source: Path, include: str) -> Path | None:
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
        if (REPOSITORY_ROOT / relative).suffix in {".c", ".h"}
        and (REPOSITORY_ROOT / relative).is_file()
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
                and REPOSITORY_ROOT in resolved.parents
            ):
                pending.append(resolved)

    return tuple(
        sorted(str(path.relative_to(REPOSITORY_ROOT)) for path in observed)
    )


def python_function_definitions(text: str) -> dict[str, int]:
    definitions = {}

    for node in ast.walk(ast.parse(text)):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            definitions[node.name] = node.end_lineno - node.lineno + 1
    return definitions


class VmServiceSourceLayoutTest(unittest.TestCase):
    def test_expected_paths_and_family_inventory_are_exact(self) -> None:
        missing = [
            relative
            for relative in (*all_source_set_paths(), *PRIVATE_HEADERS)
            if not (REPOSITORY_ROOT / relative).is_file()
        ]
        self.assertEqual([], missing)
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

    def test_cmake_source_sets_and_consumers_are_exact(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")

        for name, expected in SOURCE_SETS.items():
            with self.subTest(source_set=name):
                block = cmake_command(cmake, "set", name)
                listed = tuple(
                    re.findall(
                        r"(?:kernel|lib/vm|servers/vm|tests/host|"
                        r"tests/qemu)/[A-Za-z0-9_]+\.c",
                        block,
                    )
                )
                self.assertEqual(expected, listed)
        for source in all_source_set_paths():
            self.assertEqual(1, cmake.count(source), source)

        vm_bootstrap = cmake_command(
            cmake,
            "add_executable",
            "vm_bootstrap_test",
        )
        spawn_abi = cmake_command(
            cmake,
            "add_executable",
            "spawn_abi_test",
        )
        vm_core = cmake_command(cmake, "add_executable", "vm_core_test")
        micros = cmake_command(cmake, "add_executable", "micros")
        vm_server = cmake_command(
            cmake,
            "micros_add_bootstrap_user",
            "micros_vm_server",
        )
        scratch = "\n".join(
            cmake_if_blocks(
                cmake,
                "MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST",
            )
        )
        consumers = {
            "vm-bootstrap": (
                vm_bootstrap,
                ("MICROS_VM_BOOT_VALIDATION_SOURCES",),
            ),
            "spawn-abi": (
                spawn_abi,
                ("MICROS_VM_IMAGE_SOURCES",),
            ),
            "vm-core": (
                vm_core,
                (
                    "MICROS_VM_BOOT_VALIDATION_SOURCES",
                    "MICROS_VM_IMAGE_SOURCES",
                    "MICROS_VM_CORE_SOURCES",
                    "MICROS_VM_CORE_HOST_TEST_SOURCES",
                ),
            ),
            "vm-server": (
                vm_server,
                (
                    "MICROS_VM_BOOT_VALIDATION_SOURCES",
                    "MICROS_VM_IMAGE_SOURCES",
                    "MICROS_VM_CORE_SOURCES",
                    "MICROS_VM_SERVICE_QEMU_SOURCES",
                ),
            ),
            "micros": (
                micros,
                ("MICROS_VM_BOOT_VALIDATION_SOURCES",),
            ),
            "scratch": (
                scratch,
                ("MICROS_VM_SCRATCH_PREEMPTION_QEMU_TEST_SOURCES",),
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

    def test_private_headers_are_declarations_only(self) -> None:
        definitions = {}
        static = {}

        for relative in PRIVATE_HEADERS:
            path = REPOSITORY_ROOT / relative
            if not path.is_file():
                continue
            text = path.read_text(encoding="utf-8")
            selected = c_function_definitions(text)
            if selected:
                definitions[relative] = selected
            static_matches = re.findall(
                r"(?m)^\s*(?:_Alignas\([^\n]+\)\s*)?"
                r"static\b[^\n;]*(?:;|\{)",
                c_lexical_text(text),
            )
            if static_matches:
                static[relative] = tuple(static_matches)
        self.assertEqual({}, definitions)
        self.assertEqual({}, static)

    def test_sources_do_not_include_source_or_test_private_headers(self) -> None:
        source_include = re.compile(
            r"(?m)^\s*#\s*include\s*[<\"][^>\"]+\.(?:c|py)[>\"]"
        )
        source_violations = []
        private_violations = []

        for relative in (*all_source_set_paths(), *PRIVATE_HEADERS):
            path = REPOSITORY_ROOT / relative
            if not path.is_file():
                continue
            text = path.read_text(encoding="utf-8")
            if source_include.search(text):
                source_violations.append(relative)
            if relative in production_sources():
                for private in TEST_PRIVATE_HEADERS:
                    if Path(private).name in text:
                        private_violations.append((relative, private))
        self.assertEqual([], source_violations)
        self.assertEqual([], private_violations)

    def test_symbol_owners_follow_exact_migration_map(self) -> None:
        initial = set(INITIAL_SYMBOL_OWNER_MIGRATION)
        migration = set(SYMBOL_OWNER_MIGRATION)
        expected_current = {}
        definitions = {}

        self.assertEqual(23, len(INITIAL_SYMBOL_OWNER_MIGRATION))
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

    def test_current_mutable_state_owners_are_exact(self) -> None:
        sources = all_source_set_paths()
        expected = {
            "test_context": ["tests/host/vm_core_test_fixture.c"],
            "boot_info": [],
            "state": [],
            "executable": [],
            "request": [],
            "scratch_page": [],
            "vm_state": [],
            "test_phase": [
                "kernel/vm_scratch_preemption_test.c",
            ],
            "timer_observed": [
                "kernel/vm_scratch_preemption_test.c",
            ],
            "scratch_physical_address": [
                "kernel/vm_scratch_preemption_test.c",
            ],
            "scratch_load_token": [
                "kernel/vm_scratch_preemption_test.c",
            ],
            "scratch_process": [
                "kernel/vm_scratch_preemption_test.c",
            ],
        }

        for name, owners in expected.items():
            self.assertEqual(
                owners,
                static_definition_paths(sources, name),
                name,
            )
        contexts = []
        pattern = re.compile(
            r"\bstatic\s+struct\s+(vm_core_test_context|vm_server_context)\b"
        )
        for relative in compiled_source_header_closure():
            text = c_lexical_text(
                (REPOSITORY_ROOT / relative).read_text(encoding="utf-8")
            )
            contexts.extend(
                (relative, match.group(1))
                for match in pattern.finditer(text)
            )
        self.assertEqual(
            [
                ("tests/host/vm_core_test_fixture.c", "vm_core_test_context"),
                ("tests/qemu/vm_server_fixture.c", "vm_server_context"),
            ],
            contexts,
        )

    def test_evidence_identities_and_orders_remain_stable(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")
        host = (
            REPOSITORY_ROOT / "tests/host/vm_core_test.c"
        ).read_text(encoding="utf-8")
        service = (
            REPOSITORY_ROOT / "tests/qemu/vm_server.c"
        ).read_text(encoding="utf-8")
        generator = (
            REPOSITORY_ROOT / GENERATOR_PATH
        ).read_text(encoding="utf-8")
        scratch_marker = (
            "MICROS_VM_SCRATCH_PREEMPTION_TEST_PASS "
            "map=real timer=preempted roots=distinct "
            "inactive-validation=accepted reactivation=fenced "
            "sentinel=preserved commit=real "
            "stale-translation=faulted pte=absent "
            "ownership=transferable scratch=clear"
        )

        self.assertEqual(1, cmake.count("NAME vm-core"))
        self.assertEqual(1, cmake.count("NAME vm-bootstrap"))
        self.assertEqual(1, cmake.count("NAME spawn-abi"))
        for target in (
            "test-qemu-vm-handoff",
            "test-qemu-pm-service",
            "test-qemu-vm-scratch-preemption",
            "test-qemu-tty",
            "test-qemu-ramfs",
            "test-qemu-vfs",
            "test-qemu-vm-ready-early",
            "test-qemu-vm-self-fault",
            "test-qemu-vm-self-fault-sealed",
        ):
            cmake_command(cmake, "add_custom_target", target)
        scratch_target = cmake_command(
            cmake,
            "add_custom_target",
            "test-qemu-vm-scratch-preemption",
        )
        self.assertIn("--timeout\n            45", scratch_target)
        self.assertEqual(1, cmake.count(scratch_marker))
        self.assertIn("--resident-page-limit\n                160", cmake)

        main_body = host[host.index("int main"):]
        positions = [main_body.index(name) for name in VM_CORE_CASE_ORDER]
        self.assertEqual(sorted(positions), positions)

        service_body = service[service.index("void micros_service_main"):]
        validate_position = service_body.index(
            "vm_server_validate_configuration"
        )
        initialize_position = service_body.index(
            "micros_vm_state_initialize"
        )
        first_ready = service_body.index("vm_server_send_ready")
        handoff_position = service_body.index(
            "vm_server_complete_handoff"
        )
        second_ready = service_body.index(
            "vm_server_send_ready",
            first_ready + 1,
        )
        self.assertEqual(2, service_body.count("vm_server_send_ready"))
        self.assertTrue(
            validate_position
            < initialize_position
            < first_ready
            < handoff_position
            < second_ready
        )
        for symbol in (*VM_SYMBOLS, *PEER_SYMBOLS):
            self.assertEqual(1, generator.count(f'"{symbol}"'), symbol)

    def test_validation_plan_inherits_path_classification(self) -> None:
        baselines = {
            baseline
            for baseline, _ in VM_SERVICE_PATH_CLASSIFICATION_EQUIVALENTS
        }
        observed = tuple(
            item
            for item in validation_plan.PATH_CLASSIFICATION_EQUIVALENTS
            if item[0] in baselines
        )
        self.assertEqual(
            VM_SERVICE_PATH_CLASSIFICATION_EQUIVALENTS,
            observed,
        )
        for baseline, equivalents in VM_SERVICE_PATH_CLASSIFICATION_EQUIVALENTS:
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

        self.assertEqual(11, len(INITIAL_LONG_FUNCTION_MIGRATION))
        self.assertEqual(len(migration), len(LONG_FUNCTION_MIGRATION))
        self.assertTrue(migration <= initial)
        for relative in compiled_source_header_closure():
            path = REPOSITORY_ROOT / relative
            for name, line_count in c_function_definitions(
                path.read_text(encoding="utf-8")
            ).items():
                if line_count > FUNCTION_REVIEW_LINE_LIMIT:
                    oversized[(relative, name)] = line_count
        generator = (REPOSITORY_ROOT / GENERATOR_PATH).read_text(
            encoding="utf-8"
        )
        for name, line_count in python_function_definitions(
            generator
        ).items():
            if line_count > FUNCTION_REVIEW_LINE_LIMIT:
                oversized[(GENERATOR_PATH, name)] = line_count
        self.assertEqual(migration, set(oversized), oversized)


if __name__ == "__main__":
    unittest.main()
