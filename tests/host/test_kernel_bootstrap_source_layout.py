"""Current bootstrap module, state, reference and build boundaries."""

import ast
import hashlib
from pathlib import Path
import re
import unittest
from unittest.mock import patch

from tests.host.build_source_records import cmake_command, cmake_commands
from tests.host.c_source_records import c_declaration_text, c_function_body, c_function_definitions, c_lexical_text, file_scope_objects, function_records
from tests.host.source_snapshot import source_bytes
from tests.host import test_vm_service_source_layout as vm_layout
from tests.host.test_kernel_pm_control_source_layout import macro_definition_text
from tests.host.test_pm_service_source_layout import static_local_storage
from tools import validation_plan


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]

STRUCTURAL_TEST_PATH = "tests/host/test_kernel_bootstrap_source_layout.py"

SOURCE_SETS = {
    "MICROS_BOOTSTRAP_MANIFEST_SOURCES": (
        'kernel/boot/bootstrap_manifest.c',
        'kernel/boot/bootstrap_manifest_common.c',
        'kernel/boot/bootstrap_manifest_profiles.c',
        'kernel/boot/bootstrap_transitions.c',
    ),
    "MICROS_BOOTSTRAP_IMAGE_SOURCES": (
        'kernel/boot/bootstrap_image.c',
    ),
    "MICROS_BOOTSTRAP_RUNTIME_SOURCES": (
        'kernel/boot/bootstrap_runtime.c',
        'kernel/boot/bootstrap_runtime_state.c',
        'kernel/boot/bootstrap_preparation.c',
        'kernel/boot/bootstrap_preparation_image.c',
        'kernel/boot/bootstrap_preparation_rollback.c',
        'kernel/boot/bootstrap_runtime_validation.c',
        'kernel/boot/bootstrap_diagnostics.c',
    ),
    "MICROS_BOOTSTRAP_SYSCALL_SOURCES": (
        'kernel/boot/bootstrap_syscall.c',
        'kernel/boot/bootstrap_syscall_common.c',
        'kernel/boot/bootstrap_syscall_release.c',
        'kernel/boot/bootstrap_syscall_ready.c',
        'kernel/boot/bootstrap_syscall_complete.c',
        'kernel/boot/bootstrap_syscall_failure.c',
    ),
    "MICROS_BOOT_FOUNDATION_SOURCES": (
        'kernel/boot/main.c',
        'kernel/boot/boot_foundation.c',
        'kernel/boot/boot_component_tests.c',
    ),
    "MICROS_BOOTSTRAP_LAUNCHER_SOURCES": (
        "tests/qemu/bootstrap_launcher.c",
        "tests/qemu/bootstrap_launcher_common.c",
        "tests/qemu/bootstrap_launcher_manifest.c",
        "tests/qemu/bootstrap_launcher_protocol.c",
        "tests/qemu/bootstrap_launcher_control.c",
    ),
    "MICROS_BOOTSTRAP_MANIFEST_HOST_TEST_SOURCES": (
        "tests/host/bootstrap_manifest_test.c",
        "tests/host/bootstrap_manifest_fixture.c",
        "tests/host/bootstrap_manifest_acceptance_test.c",
        "tests/host/bootstrap_manifest_topology_test.c",
        "tests/host/bootstrap_manifest_tty_test.c",
        "tests/host/bootstrap_runtime_test.c",
        "tests/host/bootstrap_runtime_model.c",
        "tests/host/bootstrap_runtime_reference.c",
    ),
    "MICROS_BOOTSTRAP_CONTROL_HOST_TEST_SOURCES": (
        "tests/host/bootstrap_control_test.c",
        "tests/host/bootstrap_control_fixture.c",
        "tests/host/bootstrap_control_abi_test.c",
        "tests/host/bootstrap_control_lifecycle_test.c",
        "tests/host/bootstrap_control_completion_test.c",
        "tests/host/bootstrap_control_image_catalog_test.c",
    ),
    "MICROS_FDT_HOST_TEST_SOURCES": (
        "tests/host/fdt_test.c",
        "tests/host/fdt_test_fixture.c",
        "tests/host/fdt_header_test.c",
        "tests/host/fdt_memory_test.c",
    ),
}

FAMILY_HEADERS = {
    "MICROS_BOOTSTRAP_MANIFEST_SOURCES": (
        'kernel/boot/bootstrap_manifest_internal.h',
        'kernel/boot/bootstrap_transitions_internal.h',
    ),
    "MICROS_BOOTSTRAP_IMAGE_SOURCES": (),
    "MICROS_BOOTSTRAP_RUNTIME_SOURCES": (
        'kernel/boot/bootstrap_runtime_internal.h',
    ),
    "MICROS_BOOTSTRAP_SYSCALL_SOURCES": (
        'kernel/boot/bootstrap_syscall_internal.h',
    ),
    "MICROS_BOOT_FOUNDATION_SOURCES": ('kernel/boot/boot_internal.h',),
    "MICROS_BOOTSTRAP_LAUNCHER_SOURCES": (
        "tests/qemu/bootstrap_launcher_internal.h",
    ),
    "MICROS_BOOTSTRAP_MANIFEST_HOST_TEST_SOURCES": (
        "tests/host/bootstrap_manifest_test_internal.h",
        "tests/host/bootstrap_runtime_model_internal.h",
    ),
    "MICROS_BOOTSTRAP_CONTROL_HOST_TEST_SOURCES": (
        "tests/host/bootstrap_control_test_internal.h",
    ),
    "MICROS_FDT_HOST_TEST_SOURCES": ("tests/host/fdt_test_internal.h",),
}

PRIVATE_HEADERS = tuple(path for paths in FAMILY_HEADERS.values() for path in paths)

PUBLIC_PATHS = ('include/micros/bootstrap.h',
 'include/micros/bootstrap_control.h',
 'include/micros/fdt.h',
 'kernel/boot/bootstrap_runtime.h',
 'kernel/boot/bootstrap_image.h',
 'kernel/boot/bootstrap_syscall.h',
 'kernel/boot/bootstrap_control_internal.h',
 'kernel/tests/boot/bootstrap_test.h',
 'kernel/tests/boot/bootstrap_test_fixture.h',
 'tests/qemu/bootstrap_protocol.h',
 'tests/qemu/bootstrap_launcher_control.h',
 'kernel/boot/bootstrap_control.c',
 'kernel/boot/bootstrap_control_core.c')

EXISTING_INLINE_HEADERS = (
    "arch/riscv64/interrupt.h", 'kernel/boot/bootstrap_control_internal.h',
)

RETAINED_NEIGHBORS = (
    "include/micros/bootstrap_memory.h",
    'kernel/memory/bootstrap_memory.c',
    'kernel/tests/boot/bootstrap_test.c',
)

MANIFEST_CASE_ORDER = (
    "test_manifest_contract",
    "test_manifest_validation",
    "test_manifest_rejections",
    "test_vm_mapping_capacity",
    "test_three_service_topology",
    "test_tty_service_contract",
    "test_runtime_transitions",
    "test_retained_runtime_deadline",
    "test_replayable_runtime_model",
)

CONTROL_CASE_ORDER = (
    "test_valid_commands",
    "test_malformed_commands",
    "test_binding_lookup_rejects_oversized_count",
    "test_control_transitions",
    "test_pm_completion_rejections_are_atomic",
    "test_completion_rejects_launcher_ipc_dependencies",
    "test_pm_completion_queues_sealed_event_once",
    "test_pm_completion_wakes_only_any_receiver",
    "test_pm_completion_preserves_specific_receiver",
    "test_image_catalog",
)

FDT_CASES = (
    ("parses memory and reservations", "test_parses_memory_and_reservations"),
    ("rejects null arguments", "test_rejects_null_arguments"),
    ("accepts nonzero property padding", "test_accepts_nonzero_property_padding"),
    ("rejects truncated header", "test_rejects_truncated_header"),
    ("rejects bad magic", "test_rejects_bad_magic"),
    ("rejects incompatible version", "test_rejects_incompatible_version"),
    ("rejects total size beyond buffer", "test_rejects_total_size_beyond_buffer"),
    ("rejects overlapping blocks", "test_rejects_overlapping_blocks"),
    ("rejects unterminated reservation map", "test_rejects_unterminated_reservation_map"),
    ("rejects unknown structure token", "test_rejects_unknown_structure_token"),
    ("rejects bad property name offset", "test_rejects_bad_property_name_offset"),
    ("rejects oversized property name", "test_rejects_oversized_property_name"),
    ("rejects unsupported root cells", "test_rejects_unsupported_root_cells"),
    ("rejects partial reg tuple", "test_rejects_partial_reg_tuple"),
    ("rejects conflicting memory device_type", "test_rejects_memory_name_with_conflicting_type"),
    ("skips disabled named memory", "test_skips_disabled_named_memory"),
    ("skips disabled device_type memory", "test_skips_disabled_device_type_memory"),
    ("rejects range overflow", "test_rejects_range_overflow"),
    ("rejects memory capacity overflow", "test_rejects_memory_range_capacity_overflow"),
    ("disabled memory preserves capacity", "test_disabled_memory_does_not_consume_capacity"),
    ("rejects excessive depth", "test_rejects_excessive_tree_depth"),
    ("rejects reserved-memory cell mismatch", "test_rejects_reserved_memory_cell_mismatch"),
    ("rejects nonempty reserved-memory ranges", "test_rejects_nonempty_reserved_memory_ranges"),
    ("rejects reserved-memory as usable memory", "test_rejects_reserved_memory_as_usable_memory"),
    ("rejects duplicate device_type", "test_rejects_duplicate_device_type"),
    ("rejects dynamic reserved memory", "test_rejects_dynamic_reserved_memory"),
    ("requires memory range", "test_requires_at_least_one_memory_range"),
)

FINAL_OWNER_GROUPS = {
    'kernel/boot/bootstrap_transitions.c': (
        "micros_bootstrap_runtime_initialize",
        "micros_bootstrap_runtime_release",
        "micros_bootstrap_runtime_release_retaining_deadline",
        "micros_bootstrap_runtime_accept_ready",
        "micros_bootstrap_runtime_complete",
        "micros_bootstrap_classify_ready",
    ),
    'kernel/boot/bootstrap_runtime_state.c': (
        "micros_bootstrap_runtime_state",
        "micros_bootstrap_runtime_authoritative_state",
        "micros_bootstrap_runtime_is_active_controller",
    ),
    'kernel/boot/bootstrap_runtime_validation.c': (
        "micros_bootstrap_runtime_validate",
        "micros_bootstrap_runtime_validate_vm_prepared",
    ),
    'kernel/boot/bootstrap_diagnostics.c': (
        "micros_bootstrap_runtime_record_failure",
        "micros_bootstrap_runtime_fail",
        "micros_bootstrap_runtime_check_deadline",
    ),
    "tests/host/bootstrap_manifest_acceptance_test.c": (
        "test_manifest_contract", "test_manifest_validation", "test_manifest_rejections",
    ),
    "tests/host/bootstrap_manifest_topology_test.c": (
        "test_vm_mapping_capacity", "test_three_service_topology",
    ),
    "tests/host/bootstrap_manifest_tty_test.c": ("test_tty_service_contract",),
    "tests/host/bootstrap_runtime_test.c": (
        "test_runtime_transitions", "test_retained_runtime_deadline",
    ),
    "tests/host/bootstrap_runtime_model.c": ("test_replayable_runtime_model",),
    "tests/host/bootstrap_control_abi_test.c": (
        "test_valid_commands", "test_malformed_commands",
    ),
    "tests/host/bootstrap_control_lifecycle_test.c": (
        "test_binding_lookup_rejects_oversized_count", "test_control_transitions",
    ),
    "tests/host/bootstrap_control_completion_test.c": (
        "test_pm_completion_rejections_are_atomic",
        "test_completion_rejects_launcher_ipc_dependencies",
        "test_pm_completion_queues_sealed_event_once",
        "test_pm_completion_wakes_only_any_receiver",
        "test_pm_completion_preserves_specific_receiver",
    ),
    "tests/host/bootstrap_control_image_catalog_test.c": ("test_image_catalog",),
    "tests/host/fdt_header_test.c": (
        "test_rejects_null_arguments", "test_accepts_nonzero_property_padding",
        "test_rejects_truncated_header", "test_rejects_bad_magic",
        "test_rejects_incompatible_version", "test_rejects_total_size_beyond_buffer",
        "test_rejects_overlapping_blocks", "test_rejects_unterminated_reservation_map",
        "test_rejects_unknown_structure_token", "test_rejects_bad_property_name_offset",
        "test_rejects_oversized_property_name", "test_rejects_excessive_tree_depth",
    ),
    "tests/host/fdt_memory_test.c": (
        "test_parses_memory_and_reservations", "test_rejects_unsupported_root_cells",
        "test_rejects_partial_reg_tuple", "test_rejects_memory_name_with_conflicting_type",
        "test_skips_disabled_named_memory", "test_skips_disabled_device_type_memory",
        "test_rejects_range_overflow", "test_rejects_memory_range_capacity_overflow",
        "test_disabled_memory_does_not_consume_capacity",
        "test_rejects_reserved_memory_cell_mismatch",
        "test_rejects_nonempty_reserved_memory_ranges",
        "test_rejects_reserved_memory_as_usable_memory", "test_rejects_duplicate_device_type",
        "test_rejects_dynamic_reserved_memory", "test_requires_at_least_one_memory_range",
    ),
}

FINAL_SYMBOL_OWNERS = {
    name: owner for owner, names in FINAL_OWNER_GROUPS.items() for name in names
}

RUNTIME_FIELDS = (
    "struct micros_bootstrap_control_state bootstrap_state;",
    "struct micros_bootstrap_manifest_plan validation_plan;",
    "struct micros_bootstrap_image_info image_infos[MICROS_BOOTSTRAP_SERVICE_CAPACITY];",
    "struct micros_bootstrap_binding prepared_bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];",
    "struct preparation_record preparation_records[MICROS_BOOTSTRAP_SERVICE_CAPACITY];",
    "uint16_t preparation_order[MICROS_BOOTSTRAP_SERVICE_CAPACITY];",
    "struct preparation_baseline preparation_baseline;",
    "struct micros_bootstrap_diagnostic preparation_diagnostic;",
    "struct micros_vm_snapshot_result prepared_vm_snapshot;",
    "bool failure_record_emitted;",
)

LAUNCHER_OBJECTS = (
    "volatile struct micros_bootstrap_service_config micros_bootstrap_service_config;",
    "const volatile uint64_t micros_bootstrap_launcher_rodata = UINT64_C(0x4c41554e43484552);",
    "volatile uint64_t micros_bootstrap_launcher_data = UINT64_C(0x4253544c41554e43);",
)

TYPE_LAYOUT_HASHES = {
    "preparation_record": "0ef5ed0095fc266a1ec1d17f3c939b7b2f698f18deef8f3c4f06765a247a9e6d",
    "preparation_baseline": "4612fcb968fb12e88a0d6fe106635ac3e6b56515d8b4f0a4f2bb03b20f2be71a",
    "property_location": "61fcf56b85b3f310715d4661846fbcc07ba8f6da204f8b9dc0dbc6454a22aae2",
    "fdt_fixture": "a81acd3b07135ab1a187433648c02caf7af34fd30e256b2cf659f64696ac0c9c",
    "fixture_options": "4194db221562b66e5fc2e9c6ef53d3b2a59b2f4e7fb6524c3cc752af74b4d102",
    "structure_builder": "3149b4dad8b1ce56e73083be35d8073c44a22624abf476fdc2f8a55f02528ee9",
    "reference_runtime_entry": "b4187c645d6802359cfc3877115d4ea2376fddb49dc944551e5876548ee5a886",
    "reference_runtime": "85006e3d9ea88f7cb35a600c9f5be4b9b07f4b867da239a034fcf38327ea0073",
    "model_trace_entry": "a27da1ed6c0dafb005eff5ff2705fee47ed1eed48f554117f5d36badafe44bd1",
    "model_operation": "da855d20243b8930048657ab43cea3c5e41ae0da92414733d08045eae6da495f",
    "launcher_service_state": "a12ed51b4b894cad1d6254f3cc94e5726ee6cb3d5f57aec59d0f3db7063be424",
}

MANIFEST_COMMON = 'kernel/boot/bootstrap_manifest_common.c'

MANIFEST_PROFILES = 'kernel/boot/bootstrap_manifest_profiles.c'

TRANSITIONS = 'kernel/boot/bootstrap_transitions.c'

RUNTIME_STATE = 'kernel/boot/bootstrap_runtime_state.c'

PREPARATION = 'kernel/boot/bootstrap_preparation.c'

PREPARATION_IMAGE = 'kernel/boot/bootstrap_preparation_image.c'

ROLLBACK = 'kernel/boot/bootstrap_preparation_rollback.c'

RUNTIME_VALIDATION = 'kernel/boot/bootstrap_runtime_validation.c'

DIAGNOSTICS = 'kernel/boot/bootstrap_diagnostics.c'

SYSCALL_COMMON = 'kernel/boot/bootstrap_syscall_common.c'

SYSCALL_COMMANDS = SOURCE_SETS["MICROS_BOOTSTRAP_SYSCALL_SOURCES"][2:]

LAUNCHER_COMMON = "tests/qemu/bootstrap_launcher_common.c"

MODEL = "tests/host/bootstrap_runtime_model.c"

REFERENCE = "tests/host/bootstrap_runtime_reference.c"

PERMITTED_OWNER_DEPENDENCIES = {
    MANIFEST_COMMON: (),
    MANIFEST_PROFILES: (MANIFEST_COMMON,),
    TRANSITIONS: (MANIFEST_COMMON,),
    'kernel/boot/bootstrap_manifest.c': (MANIFEST_COMMON, MANIFEST_PROFILES),
    'kernel/boot/bootstrap_image.c': (),
    RUNTIME_STATE: (),
    PREPARATION_IMAGE: (RUNTIME_STATE,),
    ROLLBACK: (RUNTIME_STATE,),
    RUNTIME_VALIDATION: (RUNTIME_STATE, PREPARATION_IMAGE),
    DIAGNOSTICS: (RUNTIME_STATE, TRANSITIONS),
    PREPARATION: (RUNTIME_STATE, PREPARATION_IMAGE, ROLLBACK,
                  'kernel/boot/bootstrap_manifest.c', 'kernel/boot/bootstrap_image.c'),
    'kernel/boot/bootstrap_runtime.c': (
        RUNTIME_STATE, PREPARATION, PREPARATION_IMAGE, ROLLBACK, RUNTIME_VALIDATION,
        DIAGNOSTICS, 'kernel/boot/bootstrap_manifest.c', 'kernel/boot/bootstrap_image.c', TRANSITIONS,
    ),
    SYSCALL_COMMON: (RUNTIME_STATE, RUNTIME_VALIDATION, DIAGNOSTICS),
    **{path: (SYSCALL_COMMON, RUNTIME_STATE, RUNTIME_VALIDATION, DIAGNOSTICS,
              *((TRANSITIONS,) if path == 'kernel/boot/bootstrap_syscall_release.c' else ()))
       for path in SYSCALL_COMMANDS},
    'kernel/boot/bootstrap_syscall.c': (
        SYSCALL_COMMON, *SYSCALL_COMMANDS, RUNTIME_STATE, RUNTIME_VALIDATION, DIAGNOSTICS,
    ),
    'kernel/boot/main.c': ('kernel/boot/boot_foundation.c', 'kernel/boot/boot_component_tests.c'),
    'kernel/boot/boot_foundation.c': (),
    'kernel/boot/boot_component_tests.c': ('kernel/boot/boot_foundation.c',),
    LAUNCHER_COMMON: (),
    "tests/qemu/bootstrap_launcher_manifest.c": (LAUNCHER_COMMON,),
    "tests/qemu/bootstrap_launcher_protocol.c": (
        LAUNCHER_COMMON, "tests/qemu/bootstrap_launcher_control.c",
    ),
    "tests/qemu/bootstrap_launcher_control.c": (),
    "tests/qemu/bootstrap_launcher.c": SOURCE_SETS["MICROS_BOOTSTRAP_LAUNCHER_SOURCES"][1:],
    "tests/host/bootstrap_manifest_fixture.c": ('kernel/boot/bootstrap_manifest.c',),
    REFERENCE: (),
    MODEL: ("tests/host/bootstrap_manifest_fixture.c", REFERENCE, TRANSITIONS),
}

for family, fixture, production in (
    ("MICROS_BOOTSTRAP_MANIFEST_HOST_TEST_SOURCES", "tests/host/bootstrap_manifest_fixture.c",
     ('kernel/boot/bootstrap_manifest.c', TRANSITIONS)),
    ("MICROS_BOOTSTRAP_CONTROL_HOST_TEST_SOURCES", "tests/host/bootstrap_control_fixture.c",
     ('kernel/boot/bootstrap_manifest.c', TRANSITIONS, 'kernel/boot/bootstrap_image.c')),
    ("MICROS_FDT_HOST_TEST_SOURCES", "tests/host/fdt_test_fixture.c", ()),
):
    paths = SOURCE_SETS[family]
    PERMITTED_OWNER_DEPENDENCIES[paths[0]] = (*paths[1:], *production)
    for path in paths[1:]:
        PERMITTED_OWNER_DEPENDENCIES.setdefault(
            path, production if path == fixture else (fixture, *production),
        )

def read_source(relative: str) -> str:
    return source_bytes(REPOSITORY_ROOT, REPOSITORY_ROOT / relative).decode()

def compact(text: str) -> str:
    return "".join(text.split())

def compact_hash(text: str) -> str:
    return hashlib.sha256(compact(text).encode()).hexdigest()

def all_sources() -> tuple[str, ...]:
    return tuple(path for paths in SOURCE_SETS.values() for path in paths)

def local_include_closure(relative: str) -> tuple[str, ...]:
    pending = [relative]
    observed = set()
    while pending:
        for dependency in vm_layout.local_include_closure(pending.pop()):
            if dependency in observed:
                continue
            observed.add(dependency)
            for include in re.findall(r"(?m)^\s*#\s*include\s*<([^>]+)>", read_source(dependency)):
                resolved = vm_layout.resolve_local_include(REPOSITORY_ROOT / dependency, include)
                if resolved is not None and REPOSITORY_ROOT in resolved.parents:
                    pending.append(str(resolved.relative_to(REPOSITORY_ROOT)))
    return tuple(sorted(observed))

def header_executable_text(relative: str) -> str:
    selected = []
    for dependency in local_include_closure(relative):
        if dependency == relative:
            continue
        text = read_source(dependency)
        selected.append(macro_definition_text(text))
        selected.extend(c_function_body(text, name) for name in c_function_definitions(text))
    return "\n".join(selected)

def has_function_reference(text: str, name: str) -> bool:
    if name.startswith(("micros_", "bootstrap_", "boot_", "fdt_test_", "test_")):
        return re.search(rf"\b{re.escape(name)}\b", text) is not None
    if re.search(rf"\b{re.escape(name)}\s*\(", text):
        return True
    local_object = re.search(
        rf"\b(?:const\s+)?(?:struct\s+[A-Za-z_][A-Za-z0-9_]*|[A-Za-z_][A-Za-z0-9_]*)"
        rf"\s+[*\s]*\b{re.escape(name)}\s*(?:;|=|\[)", text,
    )
    if local_object is not None:
        return False
    return re.search(
        rf"(?<!&)&(?!&)\s*{re.escape(name)}\b(?!\s*(?:->|\.))"
        rf"|^\s*#\s*define\b[^\n]*\b{re.escape(name)}\b",
        text, re.MULTILINE,
    ) is not None

def is_immutable(declaration: str) -> bool:
    return re.match(r"(?:static\s+)?const\b|static\s+_Alignas\([^)]*\)\s+const\b", declaration) is not None

def scalar_context(declaration: str) -> tuple[str, str]:
    match = re.fullmatch(
        r"static\s+struct\s+([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;",
        declaration,
    )
    if match is None:
        raise AssertionError(f"expected one scalar, static-lifetime context: {declaration}")
    return match.group(1), match.group(2)

def type_bodies(text: str, name: str) -> tuple[str, ...]:
    return tuple(re.findall(
        rf"\b(?:struct|enum)\s+{re.escape(name)}\s*\{{([^{{}}]*)\}}",
        c_declaration_text(text),
    ))

def string_records(text: str) -> tuple[str, ...]:
    string = r'"(?:\\.|[^"\\])*"'
    return tuple(
        "".join(ast.literal_eval(literal) for literal in re.findall(string, group))
        for group in re.findall(string + rf"(?:\s*{string})*", text)
    )

class KernelBootstrapSourceLayoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.texts = {
            path: read_source(path) for path in all_sources()
            if (REPOSITORY_ROOT / path).is_file()
        }
        cls.definitions = {
            path: c_function_definitions(text) for path, text in cls.texts.items()
        }

    def expanded_calls(
        self, path: str, name: str, paths: tuple[str, ...], leaves: set[str],
        active: tuple[tuple[str, str], ...] = (),
    ) -> tuple[str, ...]:
        self.assertNotIn((path, name), active, "phase helpers must not recurse")
        calls = []
        body = c_lexical_text(c_function_body(self.texts[path], name))
        for called in re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", body):
            canonical = called
            providers = [
                (source, called) for source in paths
                if called in self.definitions.get(source, {})
            ]
            if (path, called) in providers:
                providers = [(path, called)]
            if providers and canonical not in leaves:
                self.assertEqual(1, len(providers), (path, called, providers))
                calls.extend(self.expanded_calls(
                    *providers[0], paths, leaves, (*active, (path, name)),
                ))
            else:
                calls.append(canonical)
        return tuple(calls)

    def test_source_sets_have_exact_order_and_no_duplicate_raw_split_sources(self) -> None:
        cmake = read_source("CMakeLists.txt")
        observed_sets = re.findall(
            r"\bset\s*\(\s*(MICROS_(?:BOOTSTRAP_[A-Z_]+|BOOT_FOUNDATION|FDT_HOST_TEST)_SOURCES)\b",
            cmake,
        )
        self.assertEqual(set(SOURCE_SETS), set(observed_sets))
        self.assertEqual(9, len(observed_sets))
        for name, expected in SOURCE_SETS.items():
            with self.subTest(source_set=name):
                blocks = cmake_commands(cmake, "set", name)
                self.assertEqual(1, len(blocks), name)
                self.assertEqual(
                    expected,
                    tuple(re.findall(r"(?:kernel|tests/host|tests/qemu)/[A-Za-z0-9_/]+\.c", blocks[0])),
                )

    def test_all_current_consumers_expand_only_their_required_sets_once(self) -> None:
        cmake = read_source("CMakeLists.txt")
        consumers = {
            "micros": ("add_executable", (
                "MICROS_BOOTSTRAP_MANIFEST_SOURCES", "MICROS_BOOTSTRAP_IMAGE_SOURCES",
                "MICROS_BOOTSTRAP_RUNTIME_SOURCES", "MICROS_BOOTSTRAP_SYSCALL_SOURCES",
                "MICROS_BOOT_FOUNDATION_SOURCES",
            )),
            "bootstrap_test": ("add_executable", (
                "MICROS_BOOTSTRAP_MANIFEST_SOURCES", "MICROS_BOOTSTRAP_MANIFEST_HOST_TEST_SOURCES",
            )),
            "bootstrap_control_test": ("add_executable", (
                "MICROS_BOOTSTRAP_MANIFEST_SOURCES", "MICROS_BOOTSTRAP_IMAGE_SOURCES",
                "MICROS_BOOTSTRAP_CONTROL_HOST_TEST_SOURCES",
            )),
            "fdt_test": ("add_executable", ("MICROS_FDT_HOST_TEST_SOURCES",)),
            "micros_bootstrap_launcher": (
                "micros_add_bootstrap_user", ("MICROS_BOOTSTRAP_LAUNCHER_SOURCES",),
            ),
        }
        for target, (command, required) in consumers.items():
            with self.subTest(consumer=target):
                blocks = cmake_commands(cmake, command, target)
                self.assertEqual(1, len(blocks))
                observed = {
                    name: blocks[0].count("${" + name + "}") for name in SOURCE_SETS
                }
                self.assertEqual({name: int(name in required) for name in SOURCE_SETS}, observed)
                self.assertFalse(set(re.findall(
                    r"(?:kernel|tests/host|tests/qemu)/[A-Za-z0-9_/]+\.c", blocks[0],
                )) & set(all_sources()), target)
        for name in SOURCE_SETS:
            self.assertEqual(
                sum(name in required for _, required in consumers.values()),
                cmake.count("${" + name + "}"), name,
            )
        for target in ("micros", "bootstrap_control_test"):
            block = cmake_command(cmake, "add_executable", target)
            for source in ('kernel/boot/bootstrap_control.c', 'kernel/boot/bootstrap_control_core.c'):
                self.assertEqual(1, block.count(source), (target, source))
        native_control = cmake_command(cmake, "add_executable", "bootstrap_control_test")
        self.assertEqual(1, native_control.count("${MICROS_KERNEL_OBJECT_CORE_SOURCES}"))
        object_set = cmake_command(cmake, "set", "MICROS_KERNEL_OBJECT_CORE_SOURCES")
        self.assertEqual(
            (
                'kernel/objects/kernel_objects.c', 'kernel/objects/kernel_object_state.c',
                'kernel/objects/kernel_object_identity.c', 'kernel/objects/kernel_object_stack.c',
                'kernel/objects/kernel_object_process.c', 'kernel/objects/kernel_object_thread.c',
                'kernel/objects/kernel_object_context.c', 'kernel/objects/kernel_object_hart.c',
                'kernel/objects/kernel_object_validation.c',
            ),
            tuple(re.findall(r"kernel/[A-Za-z0-9_/]+\.c", object_set)),
        )
        self.assertEqual(1, native_control.count("${MICROS_ENDPOINT_CORE_SOURCES}"))
        endpoint_set = cmake_command(cmake, "set", "MICROS_ENDPOINT_CORE_SOURCES")
        self.assertEqual(
            (
                'kernel/endpoint/endpoint.c', 'kernel/endpoint/endpoint_codec.c', 'kernel/endpoint/endpoint_identity.c',
                'kernel/endpoint/endpoint_registry.c', 'kernel/endpoint/endpoint_privilege.c',
                'kernel/endpoint/endpoint_completion_validation.c', 'kernel/endpoint/endpoint_queue_validation.c',
                'kernel/endpoint/endpoint_object_validation.c', 'kernel/endpoint/endpoint_publication.c',
                'kernel/endpoint/endpoint_close.c',
            ),
            tuple(re.findall(r"kernel/[A-Za-z0-9_/]+\.c", endpoint_set)),
        )
        self.assertEqual(1, native_control.count("${MICROS_SCHEDULER_CORE_SOURCES}"))
        self.assertEqual(1, native_control.count("${MICROS_IPC_CORE_SOURCES}"))
        self.assertEqual(
            1, cmake_command(cmake, "add_executable", "fdt_test").count('kernel/platform/fdt.c'),
        )
        runtime = cmake_command(cmake, "add_executable", "runtime_test")
        self.assertEqual(1, runtime.count("tests/qemu/bootstrap_launcher_control.c"))
        self.assertFalse(any("${" + name + "}" in runtime for name in SOURCE_SETS))

    def test_new_private_interfaces_are_declarations_only_and_have_one_declared_owner(self) -> None:
        declared = set()
        allowed_names = set(FINAL_SYMBOL_OWNERS)
        prefixes = (
            "bootstrap_manifest_", "bootstrap_transition_", "bootstrap_runtime_",
            "bootstrap_preparation_", "bootstrap_diagnostics_", "bootstrap_syscall_",
            "boot_", "bootstrap_launcher_", "bootstrap_manifest_test_",
            "bootstrap_control_test_", "fdt_test_",
        )
        for path in PRIVATE_HEADERS:
            if not (REPOSITORY_ROOT / path).is_file():
                continue
            text = read_source(path)
            with self.subTest(header=path):
                self.assertEqual({}, c_function_definitions(text))
                self.assertEqual((), file_scope_objects(text))
                self.assertNotRegex(c_declaration_text(text), r"\b(?:static|inline)\b")
                self.assertNotRegex(c_lexical_text(macro_definition_text(text)), r"\bstatic\b")
                for name in re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", c_declaration_text(text)):
                    if name in {"_Static_assert", "_Alignas", "_Alignof", "sizeof"}:
                        continue
                    self.assertTrue(name in allowed_names or name.startswith(prefixes), (path, name))
                    owners = [source for source, names in self.definitions.items() if name in names]
                    self.assertEqual(1, len(owners), (path, name, owners))
                    declared.add(name)
        stable_declarations = "\n".join(read_source(path) for path in PUBLIC_PATHS if path.endswith(".h"))
        for path, names in self.definitions.items():
            lexical = c_declaration_text(self.texts[path])
            for name in names:
                static = re.search(
                    rf"(?ms)^static\b[^;{{}}]*\b{re.escape(name)}\s*\(", lexical,
                )
                if static is not None or name in {"main", "kernel_main", "micros_service_main"}:
                    continue
                self.assertTrue(
                    name in declared or re.search(rf"\b{re.escape(name)}\s*\(", stable_declarations),
                    (path, name, "cross-file functions require a private or preserved declaration"),
                )

    def test_recursive_local_includes_cannot_hide_code_storage_or_textual_sources(self) -> None:
        closure = {
            dependency for path in self.texts for dependency in local_include_closure(path)
        }
        for path in sorted(closure):
            text = read_source(path)
            with self.subTest(path=path):
                self.assertNotRegex(
                    text, r'(?m)^\s*#\s*include\s*[<"][^">]+\.(?:c|s|S|py)[">]',
                )
                self.assertNotRegex(text, r'(?m)^\s*#\s*include\s+(?![<"\s])')
                self.assertNotRegex(c_lexical_text(macro_definition_text(text)), r"\bstatic\b")
                if not path.endswith(".h"):
                    continue
                self.assertEqual((), file_scope_objects(text))
                definitions = c_function_definitions(text)
                if definitions:
                    self.assertIn(path, EXISTING_INLINE_HEADERS)

    def test_parser_checks_reject_alias_array_and_hidden_macro_state_without_files(self) -> None:
        good = "static struct bootstrap_runtime_context runtime_context;"
        self.assertEqual(("bootstrap_runtime_context", "runtime_context"), scalar_context(good))
        for declaration in (
            "static struct bootstrap_runtime_context runtime_context[2];",
            "static context_alias runtime_context;",
            "extern struct bootstrap_runtime_context runtime_context = {0};",
        ):
            with self.subTest(declaration=declaration):
                self.assertEqual(1, len(file_scope_objects(declaration)))
                with self.assertRaises(AssertionError):
                    scalar_context(declaration)
        for declaration in (
            "static struct bootstrap_runtime_context one, two;",
            "static struct bootstrap_runtime_context one = {0}, two = {0};",
        ):
            with self.assertRaisesRegex(AssertionError, "multiple objects"):
                file_scope_objects(declaration)
        macro = "#define HIDDEN_STATE static struct bootstrap_runtime_context hidden;\n"
        self.assertRegex(c_lexical_text(macro_definition_text(macro)), r"\bstatic\b")
        with patch(f"{__name__}.local_include_closure", return_value=("owner.c", "bridge.h", "hidden.h")), \
                patch(f"{__name__}.read_source", side_effect={
                    "owner.c": "", "bridge.h": "",
                    "hidden.h": "#define HIDDEN_CALL micros_bootstrap_runtime_prepare\n"
                                "static inline void hidden(void) { micros_bootstrap_runtime_fail(0); }\n",
                }.__getitem__):
            executable = header_executable_text("owner.c")
        self.assertIn("micros_bootstrap_runtime_prepare", executable)
        self.assertIn("micros_bootstrap_runtime_fail", executable)
        self.assertFalse(has_function_reference("profile->id == 1", "profile"))
        self.assertFalse(has_function_reference("valid && profile->id == 1", "profile"))
        self.assertFalse(has_function_reference("field = &profile->id", "profile"))
        self.assertFalse(has_function_reference(
            "const struct micros_privilege_profile *profile; resolve(&profile);", "profile",
        ))
        self.assertTrue(has_function_reference("profile(1, 0)", "profile"))
        self.assertTrue(has_function_reference("callback = &profile;", "profile"))
        self.assertTrue(has_function_reference("#define CALLBACK profile", "profile"))
        self.assertTrue(has_function_reference(
            "callback = bootstrap_manifest_copy_bytes;", "bootstrap_manifest_copy_bytes",
        ))

    def test_target_storage_is_exactly_the_original_ten_or_one_complete_context(self) -> None:
        mutable = {
            path: tuple(declaration for declaration in file_scope_objects(self.texts[path])
                        if not is_immutable(declaration))
            for path in SOURCE_SETS["MICROS_BOOTSTRAP_RUNTIME_SOURCES"] if path in self.texts
        }
        mutable = {path: declarations for path, declarations in mutable.items() if declarations}


        self.assertEqual({RUNTIME_STATE}, set(mutable))
        self.assertEqual(1, len(mutable[RUNTIME_STATE]))
        context_type, object_name = scalar_context(mutable[RUNTIME_STATE][0])
        definitions = [
            body for path in (*self.texts, *PRIVATE_HEADERS) if (REPOSITORY_ROOT / path).is_file()
            for body in type_bodies(read_source(path), context_type)
        ]
        self.assertEqual(1, len(definitions), context_type)
        self.assertEqual(tuple(map(compact, RUNTIME_FIELDS)), tuple(
            compact(field + ";") for field in definitions[0].split(";") if field.strip()
        ))
        self.assertTrue(any(
            re.search(rf"\breturn\s+&{re.escape(object_name)}\s*;", c_lexical_text(
                c_function_body(self.texts[RUNTIME_STATE], name),
            )) for name in self.definitions[RUNTIME_STATE]
        ), "the state owner must expose one private borrow, not a copied runtime")
        for symbol in ("micros_bootstrap_runtime_state", "micros_bootstrap_runtime_authoritative_state"):
            path = self.symbol_owner(symbol)
            body = compact(c_function_body(self.texts[path], symbol))
            self.assertIn("MICROS_BOOTSTRAP_PHASE_UNINITIALIZED?NULL:", body)
            self.assertRegex(body, r":&[A-Za-z_][A-Za-z0-9_]*(?:->|\.)?bootstrap_state;|:&bootstrap_state;")

    def test_launcher_exported_objects_keep_types_initializers_and_one_owner(self) -> None:
        observed = {}
        names = ("micros_bootstrap_service_config", "micros_bootstrap_launcher_rodata",
                 "micros_bootstrap_launcher_data")
        for path in SOURCE_SETS["MICROS_BOOTSTRAP_LAUNCHER_SOURCES"]:
            if path not in self.texts:
                continue
            objects = tuple(
                declaration for declaration in file_scope_objects(self.texts[path])
                if any(re.search(rf"\b{name}\b", declaration) for name in names)
                or not is_immutable(declaration)
            )
            if objects:
                observed[path] = tuple(map(compact, objects))
        expected = tuple(map(compact, LAUNCHER_OBJECTS))
        self.assertEqual({LAUNCHER_COMMON: expected}, observed)

    def test_native_mutable_storage_preserves_automatic_lifetimes_and_three_catalog_pages(self) -> None:
        host_paths = tuple(path for path in self.texts if path.startswith("tests/host/"))
        file_objects = {
            path: tuple(declaration for declaration in file_scope_objects(self.texts[path])
                        if not is_immutable(declaration))
            for path in host_paths
        }
        file_objects = {path: declarations for path, declarations in file_objects.items() if declarations}
        local_objects = {
            path: tuple(declaration for declaration in static_local_storage(self.texts[path])
                        if not is_immutable(declaration))
            for path in host_paths
        }
        local_objects = {path: declarations for path, declarations in local_objects.items() if declarations}
        owner = "tests/host/bootstrap_control_image_catalog_test.c"
        self.assertEqual({owner}, set(file_objects))
        self.assertEqual(1, len(file_objects[owner]))
        context_type, _ = scalar_context(file_objects[owner][0])
        bodies = [
            body for path in (*host_paths, *PRIVATE_HEADERS) if (REPOSITORY_ROOT / path).is_file()
            for body in type_bodies(read_source(path), context_type)
        ]
        self.assertEqual(1, len(bodies))
        self.assertEqual(
            compact("unsigned char text[MICROS_SV39_PAGE_SIZE];"
                    "unsigned char rodata[MICROS_SV39_PAGE_SIZE];"
                    "unsigned char data[MICROS_SV39_PAGE_SIZE];"),
            compact(bodies[0]),
        )
        self.assertEqual({}, local_objects)

    def test_no_other_file_scope_or_static_local_mutable_state_is_added(self) -> None:
        exceptional = {
            *SOURCE_SETS["MICROS_BOOTSTRAP_RUNTIME_SOURCES"],
            *SOURCE_SETS["MICROS_BOOTSTRAP_LAUNCHER_SOURCES"],
            "tests/host/bootstrap_control_image_catalog_test.c",
        }
        for path, text in self.texts.items():
            with self.subTest(path=path):
                if path not in exceptional:
                    self.assertEqual(
                        (), tuple(item for item in file_scope_objects(text) if not is_immutable(item)),
                    )
                if path != "tests/host/bootstrap_control_test.c":
                    self.assertEqual(
                        (), tuple(item for item in static_local_storage(text) if not is_immutable(item)),
                    )

    def test_existing_record_fixture_reference_and_trace_field_layouts_are_preserved(self) -> None:
        texts = [
            read_source(path) for path in (*self.texts, *PRIVATE_HEADERS)
            if (REPOSITORY_ROOT / path).is_file()
        ]
        for name, expected in TYPE_LAYOUT_HASHES.items():
            with self.subTest(type=name):
                bodies = [body for text in texts for body in type_bodies(text, name)]
                self.assertEqual(1, len(bodies))
                self.assertEqual(expected, compact_hash(bodies[0]))

    def test_dependency_direction_is_checked_per_owner_through_recursive_includes(self) -> None:
        self.assertEqual(set(all_sources()), set(PERMITTED_OWNER_DEPENDENCIES))
        providers = {}
        for path, definitions in self.definitions.items():
            for name in definitions:
                providers.setdefault(name, []).append(path)
        for path, definitions in self.definitions.items():
            executable_headers = header_executable_text(path)
            macros = macro_definition_text(self.texts[path])
            records = function_records(self.texts[path])
            for name in definitions:
                owner = path
                allowed = {owner, *PERMITTED_OWNER_DEPENDENCIES[owner]}
                body = c_lexical_text(
                    records[name].body + "\n" + macros + "\n" + executable_headers,
                )
                body = re.sub(r"\b(?:struct|union|enum)\s+[A-Za-z_][A-Za-z0-9_]*", "", body)
                violations = {}
                for called in set(re.findall(r"\b[A-Za-z_]\w*\b", body)) & providers.keys():
                    if not has_function_reference(body, called):
                        continue
                    sources = providers[called]
                    selected = [path] if called in definitions else sources
                    owners = set(selected)
                    if not owners & allowed:
                        violations[called] = sorted(owners)
                with self.subTest(source=path, function=name, owner=owner):
                    self.assertEqual({}, violations)

    def test_dependency_direction_rejects_hidden_calls_and_macro_backedges(self):
        probe = type(self)("test_dependency_direction_is_checked_per_owner_through_recursive_includes")
        for hidden in (
            "micros_bootstrap_runtime_prepare(0);",
            "#define HIDDEN_CALLBACK micros_bootstrap_runtime_prepare",
        ):
            with self.subTest(hidden=hidden), patch(
                f"{__name__}.header_executable_text", return_value=hidden,
            ):
                with self.assertRaisesRegex(AssertionError, "micros_bootstrap_runtime_prepare"):
                    probe.test_dependency_direction_is_checked_per_owner_through_recursive_includes()

    def test_private_include_direction_preserves_portable_launcher_and_oracle_boundaries(self) -> None:
        host_headers = {path for path in PRIVATE_HEADERS if path.startswith("tests/host/")}
        portable = {
            *SOURCE_SETS["MICROS_BOOTSTRAP_MANIFEST_SOURCES"],
            *SOURCE_SETS["MICROS_BOOTSTRAP_IMAGE_SOURCES"],
        }
        target_headers = {
            'kernel/boot/bootstrap_runtime_internal.h', 'kernel/boot/bootstrap_syscall_internal.h',
            'kernel/boot/boot_internal.h',
        }
        launcher = set(SOURCE_SETS["MICROS_BOOTSTRAP_LAUNCHER_SOURCES"])
        for path in self.texts:
            closure = set(local_include_closure(path))
            if path.startswith("kernel/"):
                self.assertFalse(closure & host_headers, path)
                self.assertNotIn("tests/qemu/bootstrap_launcher_internal.h", closure, path)
            if path in portable:
                self.assertFalse(closure & target_headers, path)
                self.assertFalse(closure & host_headers, path)
            if path in launcher:
                self.assertFalse({item for item in closure if item.startswith("kernel/")}, path)
                self.assertFalse(closure & host_headers, path)
            if path == REFERENCE:
                self.assertFalse({item for item in closure if item.startswith("kernel/")}, path)
                self.assertFalse(closure & (host_headers - {
                    "tests/host/bootstrap_runtime_model_internal.h",
                }), path)

    def assert_independent_reference(self, text: str) -> None:
        self.assertNotRegex(
            c_lexical_text(text),
            r"\b(?:micros_[A-Za-z0-9_]+|bootstrap_manifest_[A-Za-z0-9_]+"
            r"|bootstrap_transition_[A-Za-z0-9_]+|bootstrap_preparation_[A-Za-z0-9_]+"
            r"|bootstrap_runtime_(?!reference_)[A-Za-z0-9_]+)\s*\(",
        )

    def test_reference_transitions_classification_and_projection_remain_independent(self) -> None:

        if REFERENCE in self.texts:
            self.assert_independent_reference(
                "\n".join(c_function_body(self.texts[REFERENCE], name)
                          for name in self.definitions[REFERENCE]) + header_executable_text(REFERENCE),
            )
        for call in (
            "micros_bootstrap_runtime_release(0, 0, 0);",
            "bootstrap_manifest_copy_bytes(0, 0, 0);",
            "bootstrap_transition_runtime_entry(0, 0);",
        ):
            with self.subTest(hidden_call=call):
                with self.assertRaises(AssertionError):
                    self.assert_independent_reference(call)

    def test_native_case_registries_order_labels_short_circuit_and_status_are_unchanged(self) -> None:

        for path, expected in (
            ("tests/host/bootstrap_manifest_test.c", MANIFEST_CASE_ORDER),
            ("tests/host/bootstrap_control_test.c", CONTROL_CASE_ORDER),
        ):
            self.assertEqual(
                expected, tuple(re.findall(r"\b(test_[A-Za-z0-9_]+)\s*\(", c_function_body(self.texts[path], "main"))),
            )
        self.assertEqual(
            FDT_CASES,
            tuple(re.findall(
                r'\{\s*"([^"\n]+)"\s*,\s*(test_[A-Za-z0-9_]+)\s*\}',
                c_function_body(self.texts["tests/host/fdt_test.c"], "main"),
            )),
        )
        self.assertEqual(
            (
                "manifest contract failed\n", "manifest validation failed\n",
                "manifest rejections failed\n", "VM mapping capacity failed\n",
                "three-service topology failed\n", "TTY service contract failed\n",
                "runtime transitions failed\n", "retained runtime deadline failed\n",
                "runtime model failed\n",
            ),
            string_records(c_function_body(self.texts["tests/host/bootstrap_manifest_test.c"], "main")),
        )
        fdt_strings = string_records(c_function_body(self.texts["tests/host/fdt_test.c"], "main"))
        self.assertEqual(
            ("TAP version 13\n", "ok %zu - %s\n", "not ok %zu - %s\n"),
            fdt_strings[len(FDT_CASES):],
        )

    def symbol_owner(self, name):
        return FINAL_SYMBOL_OWNERS[name]

    def test_supported_entries_and_current_paths_have_complete_owners(self):
        for path in (*all_sources(), *PRIVATE_HEADERS, *PUBLIC_PATHS, *RETAINED_NEIGHBORS):
            self.assertTrue((REPOSITORY_ROOT / path).is_file(), path)
        for name, owner in FINAL_SYMBOL_OWNERS.items():
            self.assertEqual([owner], [path for path, names in self.definitions.items() if name in names], name)
        for paths in SOURCE_SETS.values():
            for path in paths[1:]:
                for tier in ("fast", "pr"):
                    self.assertEqual(
                        validation_plan.plan([paths[0]], tier, "main"),
                        validation_plan.plan([path], tier, "main"), path,
                    )
        self.assertIn(STRUCTURAL_TEST_PATH, validation_plan.HOST_UNIT_ONLY_INPUTS)

    def test_current_validation_and_rollback_keep_readonly_direction(self):
        readonly_calls = {
            "micros_kernel_object_runtime_authoritative_registry", "micros_ipc_runtime_authoritative_registry",
            "micros_tty_handoff_runtime_state", "micros_bootstrap_control_validate",
            "micros_frame_ownership_runtime_validate", "micros_frame_ownership_runtime_validate_metadata",
            "micros_grant_runtime_validate", "micros_tty_handoff_runtime_validate",
            "micros_user_address_space_validate", "micros_user_address_space_translate", "micros_user_execution_inspect",
            "micros_user_execution_validate_context", "micros_vm_handoff_runtime_state",
            "micros_vm_handoff_runtime_validate", "micros_vm_boot_validate", "micros_vm_snapshot_validate_current",
        }
        for name in FINAL_OWNER_GROUPS[RUNTIME_VALIDATION]:
            calls = self.expanded_calls(
                self.symbol_owner(name), name, SOURCE_SETS["MICROS_BOOTSTRAP_RUNTIME_SOURCES"], readonly_calls,
            )
            self.assertLessEqual({call for call in calls if call.startswith("micros_")}, readonly_calls)
        rollback = c_function_body(self.texts[ROLLBACK], "bootstrap_preparation_rollback_preparation")
        self.assertLess(rollback.index("--count"), rollback.index("rollback_record("))
        for name in self.definitions[ROLLBACK]:
            self.assertNotRegex(
                c_lexical_text(c_function_body(self.texts[ROLLBACK], name)),
                r"\bmicros_[A-Za-z0-9_]*(?:_allocate|_create|_reserve|_publish|_admit)(?:_[A-Za-z0-9_]+)?\s*\(",
            )

    def test_current_model_keeps_seed_transition_budget_and_trace(self):
        text = read_source(MODEL)
        self.assertIn("UINT64_C(0x42535452504c4159)", text)
        self.assertRegex(text, r"(?:MODEL_STEPS\s*=\s*4096|step\s*<\s*4096)")
        self.assertIn("trace[16]", text + read_source("tests/host/bootstrap_runtime_model_internal.h"))


if __name__ == "__main__":
    unittest.main()
