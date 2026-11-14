from __future__ import annotations

from pathlib import Path
import re
import unittest

from tools import validation_plan
from tests.host.test_frame_ownership_source_layout import (
    c_lexical_text,
    c_function_definitions,
    cmake_command,
    static_definition_paths,
)
from tests.host.source_snapshot import isolated_source_view, source_bytes


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CMAKE_PATH = REPOSITORY_ROOT / "CMakeLists.txt"


def current_text(path, encoding="utf-8"):
    return source_bytes(REPOSITORY_ROOT, path).decode(encoding)


STRUCTURAL_TEST_PATH = (
    "tests/host/test_kernel_pm_control_source_layout.py"
)

SOURCE_SETS = {
    "MICROS_PM_CONTROL_CORE_SOURCES": (
        'kernel/pm/pm_control_core.c',
        'kernel/pm/pm_control_abi.c',
        'kernel/pm/pm_control_validation.c',
        'kernel/pm/pm_control_reservation.c',
        'kernel/pm/pm_control_execution.c',
        'kernel/pm/pm_control_activation.c',
        'kernel/pm/pm_control_exit.c',
        'kernel/pm/pm_control_runtime_core.c',
        'kernel/pm/pm_control_syscall_core.c',
    ),
    "MICROS_PM_CONTROL_TARGET_SOURCES": (
        'kernel/pm/pm_control_runtime.c',
        'kernel/pm/pm_control_syscall.c',
        'kernel/pm/pm_control_syscall_runtime.c',
        'kernel/pm/pm_control_syscall_reservation.c',
        'kernel/pm/pm_control_syscall_execution.c',
        'kernel/pm/pm_control_syscall_activation.c',
        'kernel/pm/pm_control_syscall_exit.c',
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
    'kernel/pm/pm_control_internal.h',
    'kernel/pm/pm_control_syscall_internal.h',
    "tests/host/pm_control_test_internal.h",
    "tests/host/pm_control_execution_test_internal.h",
)

TEST_PRIVATE_HEADERS = (
    "tests/host/pm_control_test_internal.h",
    "tests/host/pm_control_execution_test_internal.h",
)

PUBLIC_HEADERS = ('include/micros/pm.h',
 'kernel/pm/pm_control_core.h',
 'kernel/pm/pm_control_runtime.h',
 'kernel/pm/pm_control_runtime_core.h',
 'kernel/pm/pm_control_syscall.h',
 'kernel/pm/pm_control_syscall_core.h')


PORTABLE_FINAL_OWNERS = {
    "micros_pm_control_state_prepare":
        'kernel/pm/pm_control_core.c',
    "micros_pm_control_decode":
        'kernel/pm/pm_control_abi.c',
    "micros_pm_control_state_validate":
        'kernel/pm/pm_control_validation.c',
    "micros_pm_control_state_validate_objects":
        'kernel/pm/pm_control_validation.c',
    "micros_pm_control_reserve_preflight":
        'kernel/pm/pm_control_reservation.c',
    "micros_pm_control_reserve_commit_prevalidated":
        'kernel/pm/pm_control_reservation.c',
    "micros_pm_control_abort_preflight":
        'kernel/pm/pm_control_reservation.c',
    "micros_pm_control_abort_commit_prevalidated":
        'kernel/pm/pm_control_reservation.c',
    "micros_pm_control_prepare_preflight":
        'kernel/pm/pm_control_execution.c',
    "micros_pm_control_prepare_commit_prevalidated":
        'kernel/pm/pm_control_execution.c',
    "micros_pm_control_abort_prepared_preflight":
        'kernel/pm/pm_control_execution.c',
    "micros_pm_control_abort_prepared_commit_prevalidated":
        'kernel/pm/pm_control_execution.c',
    "micros_pm_control_activate_preflight":
        'kernel/pm/pm_control_activation.c',
    "micros_pm_control_activate_commit_prevalidated":
        'kernel/pm/pm_control_activation.c',
    "micros_pm_control_exit_begin_preflight":
        'kernel/pm/pm_control_exit.c',
    "micros_pm_control_exit_begin_commit_prevalidated":
        'kernel/pm/pm_control_exit.c',
    "micros_pm_control_exit_detach_preflight":
        'kernel/pm/pm_control_exit.c',
    "micros_pm_control_exit_detach_commit_prevalidated":
        'kernel/pm/pm_control_exit.c',
    "micros_pm_control_release_exited_preflight":
        'kernel/pm/pm_control_exit.c',
    "micros_pm_control_release_exited_commit_prevalidated":
        'kernel/pm/pm_control_exit.c',
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
        'kernel/pm/pm_control_syscall.c',
    **TEST_FINAL_OWNERS,
}

PORTABLE_TRANSITION_OWNERS = (
    'kernel/pm/pm_control_reservation.c',
    'kernel/pm/pm_control_execution.c',
    'kernel/pm/pm_control_activation.c',
    'kernel/pm/pm_control_exit.c',
)

PORTABLE_TRANSITION_SYMBOLS = {
    owner: tuple(
        symbol
        for symbol, final_owner in PORTABLE_FINAL_OWNERS.items()
        if final_owner == owner
    )
    for owner in PORTABLE_TRANSITION_OWNERS
}

SYSCALL_ADAPTER_ENTRY_OWNERS = {
    "micros_pm_control_syscall_handle_reservation":
        'kernel/pm/pm_control_syscall_reservation.c',
    "micros_pm_control_syscall_handle_execution":
        'kernel/pm/pm_control_syscall_execution.c',
    "micros_pm_control_syscall_handle_activation":
        'kernel/pm/pm_control_syscall_activation.c',
    "micros_pm_control_syscall_handle_exit":
        'kernel/pm/pm_control_syscall_exit.c',
}

SYSCALL_PORTABLE_OWNER = {
    'kernel/pm/pm_control_syscall_reservation.c':
        'kernel/pm/pm_control_reservation.c',
    'kernel/pm/pm_control_syscall_execution.c':
        'kernel/pm/pm_control_execution.c',
    'kernel/pm/pm_control_syscall_activation.c':
        'kernel/pm/pm_control_activation.c',
    'kernel/pm/pm_control_syscall_exit.c':
        'kernel/pm/pm_control_exit.c',
}

EXPECTED_PM_INCLUDES = {
    'kernel/pm/pm_control_internal.h': (
        'kernel/pm/pm_control_core.h',
    ),
    'kernel/pm/pm_control_syscall_internal.h': (
        'kernel/pm/pm_control_runtime.h',
        'kernel/pm/pm_control_syscall.h',
        'kernel/pm/pm_control_syscall_core.h',
    ),
    "tests/host/pm_control_test_internal.h": (
        'kernel/pm/pm_control_core.h',
        'kernel/pm/pm_control_syscall_core.h',
    ),
    "tests/host/pm_control_execution_test_internal.h": (
        'kernel/pm/pm_control_core.h',
    ),
    'kernel/pm/pm_control_core.c': (
        'kernel/pm/pm_control_internal.h',
    ),
    'kernel/pm/pm_control_abi.c': (
        'kernel/pm/pm_control_internal.h',
    ),
    'kernel/pm/pm_control_validation.c': (
        'kernel/pm/pm_control_internal.h',
    ),
    'kernel/pm/pm_control_reservation.c': (
        'kernel/pm/pm_control_internal.h',
    ),
    'kernel/pm/pm_control_execution.c': (
        'kernel/pm/pm_control_internal.h',
    ),
    'kernel/pm/pm_control_activation.c': (
        'kernel/pm/pm_control_internal.h',
    ),
    'kernel/pm/pm_control_exit.c': (
        'kernel/pm/pm_control_internal.h',
    ),
    'kernel/pm/pm_control_runtime_core.c': (
        'kernel/pm/pm_control_runtime_core.h',
        'kernel/pm/pm_control_internal.h',
    ),
    'kernel/pm/pm_control_syscall_core.c': (
        'kernel/pm/pm_control_syscall_core.h',
    ),
    'kernel/pm/pm_control_runtime.c': (
        'kernel/pm/pm_control_runtime.h',
        'kernel/pm/pm_control_runtime_core.h',
        'kernel/pm/pm_control_internal.h',
    ),
    'kernel/pm/pm_control_syscall.c': (
        'kernel/pm/pm_control_syscall.h',
        'kernel/pm/pm_control_runtime.h',
        'kernel/pm/pm_control_syscall_internal.h',
    ),
    'kernel/pm/pm_control_syscall_runtime.c': (
        'kernel/pm/pm_control_syscall_internal.h',
    ),
    'kernel/pm/pm_control_syscall_reservation.c': (
        'kernel/pm/pm_control_syscall_internal.h',
        'kernel/pm/pm_control_runtime_core.h',
    ),
    'kernel/pm/pm_control_syscall_execution.c': (
        'kernel/pm/pm_control_syscall_internal.h',
        'kernel/pm/pm_control_internal.h',
    ),
    'kernel/pm/pm_control_syscall_activation.c': (
        'kernel/pm/pm_control_syscall_internal.h',
    ),
    'kernel/pm/pm_control_syscall_exit.c': (
        'kernel/pm/pm_control_syscall_internal.h',
    ),
}


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
                *PUBLIC_HEADERS,
            )
        )
    )


def discovered_family_paths() -> tuple[str, ...]:
    patterns = (
        'kernel/pm/pm_control*.c',
        'kernel/pm/pm_control*.h',
        "tests/host/pm_control*.c",
        "tests/host/pm_control*.h",
    )
    discovered = {
        str(path.relative_to(REPOSITORY_ROOT))
        for pattern in patterns
        for path in (REPOSITORY_ROOT).glob(pattern)
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
        if (candidate).is_file():
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
            current_text(path, encoding="utf-8")
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


def local_include_closure(relative: str) -> tuple[str, ...]:
    pending = [(REPOSITORY_ROOT / relative).resolve()]
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
            current_text(path, encoding="utf-8")
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


def macro_definition_text(text: str) -> str:
    lines = text.splitlines()
    definitions = []
    index = 0

    while index < len(lines):
        line = lines[index]
        if re.match(r"^\s*#\s*define\b", line):
            selected = [line]
            while selected[-1].rstrip().endswith("\\"):
                index += 1
                if index >= len(lines):
                    break
                selected.append(lines[index])
            definitions.extend(selected)
        index += 1
    return "\n".join(definitions)


def owner_dependency_text(relative: str) -> str:
    declaration_only = {
        'kernel/pm/pm_control_core.h',
        'kernel/pm/pm_control_syscall_internal.h',
    }
    selected = []

    for dependency in local_include_closure(relative):
        text = current_text(REPOSITORY_ROOT / dependency,
            encoding="utf-8"
        )
        if dependency in declaration_only and dependency != relative:
            selected.append(c_lexical_text(macro_definition_text(text)))
        else:
            selected.append(c_lexical_text(text))
    return "\n".join(selected)


def private_header_object_definitions(text: str) -> tuple[str, ...]:
    lexical = c_lexical_text(text)
    lexical = re.sub(r"(?m)^\s*#.*$", "", lexical)
    definitions = []
    depth = 0
    segment_start = 0

    for index, selected in enumerate(lexical):
        if selected == "{":
            depth += 1
        elif selected == "}":
            depth -= 1
            if depth < 0:
                raise AssertionError("unexpected closing brace")
        elif selected == ";" and depth == 0:
            declaration = lexical[segment_start:index + 1].strip()
            segment_start = index + 1
            if (
                not declaration
                or declaration.startswith("typedef ")
                or declaration.startswith("_Static_assert")
                or (
                    declaration.startswith("extern ")
                    and "=" not in declaration
                    and "{" not in declaration
                )
                or re.fullmatch(
                    r"[A-Za-z_][A-Za-z0-9_\s*]*"
                    r"\b[A-Za-z_][A-Za-z0-9_]*\s*"
                    r"\([^;]*\)\s*;",
                    declaration,
                    re.DOTALL,
                )
                or re.match(
                    r"^(?:struct|union|enum)\b[^{;]*\{",
                    declaration,
                    re.DOTALL,
                )
                or re.fullmatch(
                    r"(?:struct|union|enum)\s+"
                    r"[A-Za-z_][A-Za-z0-9_]*\s*;",
                    declaration,
                )
            ):
                continue
            definitions.append(declaration)

    return tuple(definitions)


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


    def test_cmake_source_sets_have_exact_ordered_membership(self) -> None:
        cmake = current_text(CMAKE_PATH, encoding="utf-8")

        for name, expected in SOURCE_SETS.items():
            with self.subTest(source_set=name):
                source_set = cmake_command(cmake, "set", name)
                listed = tuple(
                    re.findall(
                        r"(?:kernel|tests/host)/"
                        r"[A-Za-z0-9_/]+\.c",
                        source_set,
                    )
                )
                self.assertEqual(expected, listed)

    def test_consumers_expand_exact_source_sets_once(self) -> None:
        cmake = current_text(CMAKE_PATH, encoding="utf-8")
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
        function_definitions = {}
        object_definitions = {}
        static_declarations = {}

        for relative in PRIVATE_HEADERS:
            path = REPOSITORY_ROOT / relative
            if not (path).is_file():
                continue
            text = current_text(path, encoding="utf-8")
            selected = c_function_definitions(text)
            objects = private_header_object_definitions(text)
            static = re.findall(
                r"(?m)^\s*(?:_Alignas\([^\n]+\)\s*)?"
                r"static\b[^\n;]*(?:;|\{)",
                c_lexical_text(text),
            )
            if selected:
                function_definitions[relative] = selected
            if objects:
                object_definitions[relative] = objects
            if static:
                static_declarations[relative] = tuple(static)
        self.assertEqual({}, function_definitions)
        self.assertEqual({}, object_definitions)
        self.assertEqual({}, static_declarations)

    def test_sources_do_not_include_c_or_test_private_headers(self) -> None:
        c_include = re.compile(
            r"(?m)^\s*#\s*include\s*[<\"][^>\"]+\.c[>\"]"
        )
        c_violations = []
        private_violations = []

        for relative in (*all_source_set_paths(), *PRIVATE_HEADERS):
            path = REPOSITORY_ROOT / relative
            if not (path).is_file():
                continue
            text = current_text(path, encoding="utf-8")
            if c_include.search(text):
                c_violations.append(relative)
            if relative in production_sources():
                for private in TEST_PRIVATE_HEADERS:
                    if Path(private).name in text:
                        private_violations.append((relative, private))
        self.assertEqual([], c_violations)
        self.assertEqual([], private_violations)

    def test_private_dependency_direction_is_exact(self) -> None:
        include_pattern = re.compile(
            r'(?m)^\s*#\s*include\s*"([^"]*pm_control[^"]*)"'
        )
        transition_symbols = {
            symbol
            for symbols in PORTABLE_TRANSITION_SYMBOLS.values()
            for symbol in symbols
        }
        adapter_symbols = set(SYSCALL_ADAPTER_ENTRY_OWNERS)

        for relative, expected in EXPECTED_PM_INCLUDES.items():
            text = current_text(REPOSITORY_ROOT / relative,
                encoding="utf-8"
            )
            with self.subTest(source=relative, dependency="include"):
                self.assertEqual(
                    expected,
                    tuple(include_pattern.findall(text)),
                )

        for relative in SOURCE_SETS["MICROS_PM_CONTROL_CORE_SOURCES"]:
            text = owner_dependency_text(relative)
            allowed = set(
                PORTABLE_TRANSITION_SYMBOLS.get(relative, ())
            )
            observed = {
                symbol
                for symbol in transition_symbols
                if re.search(rf"\b{re.escape(symbol)}\b", text)
            }
            with self.subTest(source=relative, dependency="portable"):
                self.assertEqual(allowed, observed)

        for relative in SOURCE_SETS["MICROS_PM_CONTROL_TARGET_SOURCES"]:
            text = owner_dependency_text(relative)
            allowed_portable_owner = SYSCALL_PORTABLE_OWNER.get(
                relative
            )
            allowed_portable = set(
                PORTABLE_TRANSITION_SYMBOLS.get(
                    allowed_portable_owner,
                    (),
                )
            )
            observed_portable = {
                symbol
                for symbol in transition_symbols
                if re.search(rf"\b{re.escape(symbol)}\b", text)
            }
            allowed_adapters = (
                adapter_symbols
                if relative == 'kernel/pm/pm_control_syscall.c'
                else {
                    symbol
                    for symbol, owner
                    in SYSCALL_ADAPTER_ENTRY_OWNERS.items()
                    if owner == relative
                }
            )
            observed_adapters = {
                symbol
                for symbol in adapter_symbols
                if re.search(rf"\b{re.escape(symbol)}\b", text)
            }
            with self.subTest(source=relative, dependency="adapter"):
                self.assertEqual(
                    allowed_portable,
                    observed_portable,
                )
                self.assertEqual(
                    allowed_adapters,
                    observed_adapters,
                )

    def test_supported_entries_have_one_responsible_owner(self):
        definitions = {}
        for path in all_source_set_paths():
            for name in c_function_definitions(current_text(REPOSITORY_ROOT / path)):
                definitions.setdefault(name, []).append(path)
        for name, owner in FINAL_SYMBOL_OWNERS.items():
            self.assertEqual([owner], definitions.get(name), name)

    def test_mutable_state_owners_are_exact(self) -> None:
        sources = all_source_set_paths()

        state_definitions = []
        static_state = re.compile(
            r"(?m)^\s*(?:_Alignas\([^\n]+\)\s*)?"
            r"static\s+struct\s+micros_pm_control_state\b"
        )
        nonstatic_global_state = re.compile(
            r"(?m)^(?:_Alignas\([^\n]+\)\s*)?"
            r"struct\s+micros_pm_control_state\s+"
            r"[A-Za-z_][A-Za-z0-9_]*\b"
        )
        state_alias = re.compile(
            r"\btypedef\s+struct\s+micros_pm_control_state\b"
        )
        aliases = []
        nonstatic_globals = []

        for relative in compiled_source_header_closure():
            path = REPOSITORY_ROOT / relative
            lexical = c_lexical_text(
                current_text(path, encoding="utf-8")
            )
            for _ in static_state.finditer(lexical):
                state_definitions.append(relative)
            for _ in nonstatic_global_state.finditer(lexical):
                nonstatic_globals.append(relative)
            for _ in state_alias.finditer(lexical):
                aliases.append(relative)
        self.assertEqual(
            ['kernel/pm/pm_control_runtime.c'],
            state_definitions,
        )
        self.assertEqual([], nonstatic_globals)
        self.assertEqual([], aliases)
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
                current_text(path, encoding="utf-8")
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
        cmake = current_text(CMAKE_PATH, encoding="utf-8")
        state_entry = current_text(REPOSITORY_ROOT / "tests/host/pm_control_test.c", encoding="utf-8")
        execution_entry = current_text(REPOSITORY_ROOT / "tests/host/pm_control_execution_test.c", encoding="utf-8")
        pm_entry = current_text(REPOSITORY_ROOT / "tests/host/pm_test.c", encoding="utf-8")
        qemu_entry = current_text(REPOSITORY_ROOT / 'kernel/tests/pm/pm_service_test_report.c', encoding="utf-8")
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

    def test_current_paths_keep_broad_validation_classification(self):
        for sources in SOURCE_SETS.values():
            for path in sources[1:]:
                for tier in ("fast", "pr"):
                    with self.subTest(path=path, tier=tier):
                        baseline = (
                            "unclassified-input"
                            if path == "kernel/pm/pm_control_runtime_core.c" else sources[0]
                        )
                        self.assertEqual(
                            validation_plan.plan([baseline], tier, "main"),
                            validation_plan.plan([path], tier, "main"),
                        )
        self.assertIn(STRUCTURAL_TEST_PATH, validation_plan.HOST_UNIT_ONLY_INPUTS)


if __name__ == "__main__":
    unittest.main()
