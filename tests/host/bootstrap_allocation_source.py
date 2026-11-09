"""Closed MEM01 source, recipe, layout and work declarations."""

import ast
from functools import lru_cache
import hashlib
from pathlib import Path
import re
import subprocess

from tests.host.evidence_authority_source import literal_value, regular_source, single_assignment
from tests.host.evidence_authority_types import AuthorityClass, Fact, digest
from tests.host.work_count_types import Stage


FAMILY = "bootstrap-allocation-baseline"
SOURCE_PATH = "tests/host/bootstrap_allocation_source.py"
AUTHORITY_PATH = "tests/host/bootstrap_allocation_authority.py"

SOURCE_ROLES = {
    "base_membership_sha256": "2c72aee3157201feed79190f2f57ed849ad83b236cecb5f78b5ef27ae9b0aa45",
    "candidate_paths": (
        "include/micros/frame_allocator.h",
        "kernel/memory/frame_allocator.c",
        "kernel/memory/bootstrap_memory.c",
        "kernel/memory/frame_ownership.c",
        "kernel/memory/frame_ownership_bootstrap.c",
        "kernel/memory/frame_ownership_validation.c",
        "tests/host/kernel_layout_source.py",
        "tests/host/test_kernel_directory_layout.py",
    ),
    "immutable_additions": (
        "tests/host/bootstrap_allocation_test.c",
        "tests/host/bootstrap_allocation_test.h",
        "tests/host/bootstrap_allocation_source.py",
        "tests/host/bootstrap_allocation_evidence.py",
        "tests/host/bootstrap_allocation_authority.py",
        "tests/host/test_bootstrap_allocation.py",
        "tools/generate_bootstrap_allocation_fixture.py",
        "tools/check_bootstrap_allocation.py",
    ),
}

RECIPE_ADMISSION = {
    "native_target": "bootstrap_allocation_test",
    "native_case": "bootstrap-allocation",
    "native_tiers": ("test-unit-fast", "test-unit"),
    "native_definitions": ("MICROS_TEST_WORK_COUNTS=1", "MICROS_TEST_BOOTSTRAP_MEMORY_COUNTS=1"),
    "native_source_groups": (
        "MICROS_FRAME_OWNERSHIP_SOURCES",
        "MICROS_KERNEL_OBJECT_CORE_SOURCES",
        "MICROS_SCHEDULER_CORE_SOURCES",
    ),
    "fixture_sources": ("tests/host/bootstrap_allocation_test.c", "tests/work/work_counts.c"),
    "generated_subjects": (
        "kernel/memory/frame_allocator.c",
        "kernel/memory/frame_ownership.c",
        "kernel/memory/frame_ownership_bootstrap.c",
        "kernel/memory/frame_ownership_validation.c",
        "kernel/memory/bootstrap_memory.c",
    ),
    "generator": "tools/generate_bootstrap_allocation_fixture.py",
    "generator_output": "build/host-debug/bootstrap-allocation",
    "native_standard": "c17",
    "native_sanitizers": ("address", "undefined"),
    "baseline_headers": "fixed-runtime-reference",
    "current_headers": "actual-current-source",
    "observations": "private-generated-native-translations-only",
    "ordinary_observations": "absent",
    "reference_cmake_sha256": "c000e9e10623c4aaf2938ca904e2e6f783b3a3e026987cde852634801d123837",
    "native_source_members": (
        "kernel/memory/frame_allocator.c",
        "kernel/memory/frame_ownership.c",
        "kernel/memory/frame_ownership_validation.c",
        "kernel/memory/frame_ownership_bootstrap.c",
        "kernel/memory/frame_ownership_handoff.c",
        "kernel/memory/frame_ownership_vm_claim.c",
        "kernel/memory/frame_ownership_vm_freeze.c",
        "kernel/memory/frame_ownership_vm_release.c",
        "kernel/objects/kernel_objects.c",
        "kernel/objects/kernel_object_state.c",
        "kernel/objects/kernel_object_identity.c",
        "kernel/objects/kernel_object_stack.c",
        "kernel/objects/kernel_object_process.c",
        "kernel/objects/kernel_object_thread.c",
        "kernel/objects/kernel_object_context.c",
        "kernel/objects/kernel_object_hart.c",
        "kernel/objects/kernel_object_validation.c",
        "kernel/scheduler/scheduler_core.c",
        "kernel/scheduler/scheduler_core_queue.c",
        "kernel/scheduler/scheduler_core_validate.c",
        "kernel/scheduler/scheduler_core_policy.c",
        "kernel/scheduler/scheduler_core_ipc.c",
        "kernel/scheduler/scheduler_core_ipc_transition.c",
        "kernel/scheduler/scheduler_core_return_plan.c",
        "kernel/scheduler/scheduler_core_return.c",
        "kernel/scheduler/scheduler_core_accounting.c",
    ),
}

STATE_WORK_CONTRACT = {
    "version": 1,
    "frame_size": 4096,
    "capacity": {
        "memory_ranges": 16, "reserved_ranges": 80, "managed_ranges": 96,
        "managed_frames": 262144, "bitmap_words": 4096,
    },
    "cursor": {
        "name": "allocation_cursor", "type": "uint64_t",
        "position": "immediately-before-allocated_bitmap", "persistent_bytes": 8,
        "invariant": "0<=cursor<=managed;every bit below cursor allocated;bitmap sole availability",
        "allocate": "ascending-range masked-word search;lowest free bit;commit cursor=index+1",
        "release": "cursor=min(cursor,released_index)",
        "typed_rollback": "restore exact saved cursor and complete original allocator/ledger/output",
        "bulk_and_vm_release": "existing shared allocator_mark_free lowers cursor",
        "complete_validation": "separate prefix helper;active/inactive bodies and cadence unchanged",
    },
    "dense_frames": (1, 63, 64, 65, 257, 4096, 262143, 262144),
    "dense_memory_base": 0x80000000,
    "kernel_start": 0x80200000,
    "kernel_end": 0x80400000,
    "live_prefix": 33,
    "allocation_prefix": 512,
    "fdt_split": {
        "ranges": 16, "stride_pages": 10, "pages": 9,
        "hole_pages": (1, 3, 5, 7), "reservation_source_split": (32, 32),
        "raw_order": "reversed", "managed_components": 80,
    },
    "portable_split": {
        "ranges": 16, "stride_pages": 12, "pages": 11,
        "hole_pages": (1, 3, 5, 7, 9), "reserved_ranges": 80,
        "managed_components": 96, "fdt_case": "not-fdt",
    },
    "mutation_guards": {
        "release_reuse": {
            "frames": 4096, "allocated": 512,
            "release_indices": (255, 63, 511, 0, 127, 64),
            "expected_indices": (0, 63, 64, 127, 255, 511),
        },
        "process_set": {"frames": 257, "allocated": 128, "process": (0, 1), "indices": (0, 64, 127)},
        "typed_rollback": {
            "frames": 257, "allocated": 65, "bad_owner_index": 65,
            "current_cursor": 64, "baseline_cursor": "absent",
        },
        "vm_mirror": "real claim/release and freeze/process-release with lower-index returns",
    },
    "work": {
        "owner": "KERNEL_MEMORY", "owner_mask": 4, "dimensions": (27, 14),
        "kinds": ("COMPARISON", "RECORD_VISIT", "INDEX_PROBE", "BITMAP_WORD_LOAD", "RANGE_VISIT"),
        "producer_label": 0x4D454D3031000000, "capture_label": "unique-case-index",
        "bootstrap_range_limit": "192+m*(m+g)+g*r",
        "bootstrap_word_limit": 4096,
        "prefix_word_limit": "2*a", "prefix_index_limit": "64*a",
        "portable_range_limit": "a*g", "typed_range_limit": "a*(192+2*g)",
        "arbitrary_release_amortized_claim": "none", "new_automatic_storage": "fixed scalar only",
    },
}

SOURCE_PROJECTION = {
    "source_reference": "bcace37522e5beb101ef5278889a2f87742f5721",
    "retirement_keys": (
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_allocator.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/bootstrap_memory.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership_bootstrap.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership_validation.c/2",
        "tests/host/kernel_layout_source.py:data-entry:BOUNDED_FRAME_VALIDATION_SHA256",
    ),
    "mutable_functions": {
        "kernel/memory/frame_allocator.c": (
            "commit_initialized_state", "micros_frame_allocator_allocate", "micros_frame_allocator_release",
        ),
        "kernel/memory/bootstrap_memory.c": ("bootstrap_state_is_valid",),
        "kernel/memory/frame_ownership.c": (
            "allocator_geometry_is_valid", "allocator_is_pristine",
            "micros_frame_ownership_allocator_mark_free",
        ),
        "kernel/memory/frame_ownership_bootstrap.c": (
            "micros_frame_ownership_allocate", "commit_process_release",
        ),
        "kernel/memory/frame_ownership_validation.c": ("micros_frame_ownership_validate",),
    },
    "new_helpers": {
        "kernel/memory/bootstrap_memory.c": (
            ("bootstrap_bitmap_word_count", "static uint64_t bootstrap_bitmap_word_count(uint64_t word)", ()),
            ("bootstrap_bitmap_is_valid", "static bool bootstrap_bitmap_is_valid(bool require_all_free)",
             ("bootstrap_bitmap_word_count",)),
            ("bootstrap_ranges_are_valid",
             "static bool bootstrap_ranges_are_valid(const struct micros_fdt_memory_map *memory_map)",
             ("range_end", "fdt_memory_union_covers", "intersects_fdt_reservations")),
        ),
        "kernel/memory/frame_ownership_validation.c": (
            ("allocated_prefix_is_valid",
             "static bool allocated_prefix_is_valid(const struct micros_frame_ownership *ownership)", ()),
        ),
    },
    "baseline_bootstrap_helpers": (
        "range_end", "ranges_intersect", "fdt_memory_union_covers",
        "intersects_fdt_reservations", "managed_frame_is_allowed", "bootstrap_state_is_valid",
    ),
    "selected_frame_guard": "MICROS_BUILD_FRAME_ALLOCATOR_TEST",
    "native_numeric_bindings": ("bootstrap_allocator", "__kernel_start", "__kernel_end"),
    "admissions": (
        "exact-fixed-source-and-header-membership",
        "complete-retained-function-signature-guard-data-register",
        "cursor-before-bitmap-only-header-change",
        "declared-file-local-helper-signatures-and-call-sets",
        "actual-primitive-site-observations-with-byte-exact-inverse",
        "generated-translation-units-and-executables-are-outputs",
        "one-native-target-case-and-two-native-tier-integration",
        "exact-six-key-assertion-policy-and-prior-delegation",
    ),
    "retained_source_sha256": {
        "include/micros/frame_allocator.h": "492f8f4ce861be018de8e696a8a503459e7514f77a93962d45b77de0edc539fa",
        "kernel/memory/frame_allocator.c": "2d875405cf0a432cb04e8b4a7ee15e6659042ecfbf7960ef0238a01d6cd023d3",
        "kernel/memory/bootstrap_memory.c": "a0a36dd71c35904bcf763dc95fd8e29f5f6f4d72a7243c575229fa1712a92302",
        "kernel/memory/frame_ownership.c": "f39ceaeb0c47b818403abe188787ab80a59c912fc981ac01f72451c217508ee1",
        "kernel/memory/frame_ownership_bootstrap.c": "23c8d097c644481a4567aa16079199a479f0b8f085a4e5f2c4124f9efce47466",
        "kernel/memory/frame_ownership_validation.c": "448f61074bb3c5d52631660b9537e45a8bb9cca266a645edd503457b6fa03694",
    },
}

COMMON_OBSERVATIONS = {
    "kernel/memory/frame_allocator.c": (
        ("micros_frame_allocator_allocate", "allocate-range", "RANGE_VISIT",
         ("for (range_index = 0; range_index < allocator->managed_range_count; ++range_index) {",),
         "const struct micros_managed_frame_range *range = &allocator->managed_ranges[range_index];"),
        ("micros_frame_allocator_release", "release-range", "RANGE_VISIT",
         ("for (range_index = 0; range_index < allocator->managed_range_count; ++range_index) {",),
         "const struct micros_managed_frame_range *range = &allocator->managed_ranges[range_index];"),
        ("micros_frame_allocator_release", "release-read", "BITMAP_WORD_LOAD",
         ("for (range_index = 0; range_index < allocator->managed_range_count; ++range_index) {",),
         "if ((allocator->allocated_bitmap[word_index] & bit) == 0)"),
        ("micros_frame_allocator_release", "release-commit", "BITMAP_WORD_LOAD",
         ("for (range_index = 0; range_index < allocator->managed_range_count; ++range_index) {",),
         "allocator->allocated_bitmap[word_index] &= ~bit;"),
    ),
    "kernel/memory/bootstrap_memory.c": (
        ("fdt_memory_union_covers", "fdt-memory-range", "RANGE_VISIT",
         ("while (cursor < end) {", "for (index = 0; index < memory_map->memory_range_count; ++index) {"),
         "const struct micros_fdt_range *range = &memory_map->memory_ranges[index];"),
        ("fdt_memory_union_covers", "fdt-progress-comparison", "COMPARISON",
         ("while (cursor < end) {",), "if (furthest == cursor)"),
        ("intersects_fdt_reservations", "fdt-reservation-range", "RANGE_VISIT",
         ("for (index = 0; index < memory_map->reservation_range_count; ++index) {",),
         "const struct micros_fdt_range *range = &memory_map->reservation_ranges[index];"),
        ("intersects_fdt_reservations", "fdt-reserved-memory-range", "RANGE_VISIT",
         ("for (index = 0; index < memory_map->reserved_memory_range_count; ++index) {",),
         "const struct micros_fdt_range *range = &memory_map->reserved_memory_ranges[index];"),
    ),
    "kernel/memory/frame_ownership.c": (
        ("allocator_geometry_is_valid", "owner-allocator-range", "RANGE_VISIT",
         ("for (index = 0; index < allocator->managed_range_count; ++index) {",),
         "const struct micros_managed_frame_range *range = &allocator->managed_ranges[index];"),
        ("allocator_geometry_is_valid", "owner-allocator-inactive", "RANGE_VISIT",
         ("for (index = allocator->managed_range_count; index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES; ++index) {",),
         "if (!managed_range_is_zero(&allocator->managed_ranges[index]))"),
        ("micros_frame_ownership_geometry_matches", "owner-snapshot-record", "RECORD_VISIT",
         ("for (index = 0; index < ownership->managed_range_count; ++index) {",),
         "const struct micros_frame_range_snapshot *snapshot = &ownership->managed_ranges[index];"),
        ("micros_frame_ownership_geometry_matches", "owner-current-range", "RANGE_VISIT",
         ("for (index = 0; index < ownership->managed_range_count; ++index) {",),
         "const struct micros_managed_frame_range *current = &allocator->managed_ranges[index];"),
        ("micros_frame_ownership_geometry_matches", "owner-inactive-record", "RECORD_VISIT",
         ("for (index = ownership->managed_range_count; index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES; ++index) {",),
         "if (!range_snapshot_is_zero(&ownership->managed_ranges[index]))"),
        ("micros_frame_ownership_physical_address_to_index", "owner-physical-record", "RECORD_VISIT",
         ("for (range_index = 0; range_index < ownership->managed_range_count; ++range_index) {",),
         "const struct micros_frame_range_snapshot *range = &ownership->managed_ranges[range_index];"),
    ),
}

RED_OBSERVATIONS = {
    "kernel/memory/frame_allocator.c": (
        ("micros_frame_allocator_allocate", "allocate-bit-read", "BITMAP_WORD_LOAD",
         ("for (range_index = 0; range_index < allocator->managed_range_count; ++range_index) {",
          "for (frame_index = 0; frame_index < range->frame_count; ++frame_index) {"),
         "if ((allocator->allocated_bitmap[word_index] & bit) == 0)"),
        ("micros_frame_allocator_allocate", "allocate-bit-probe", "INDEX_PROBE",
         ("for (range_index = 0; range_index < allocator->managed_range_count; ++range_index) {",
          "for (frame_index = 0; frame_index < range->frame_count; ++frame_index) {"),
         "if ((allocator->allocated_bitmap[word_index] & bit) == 0)"),
        ("micros_frame_allocator_allocate", "allocate-bit-comparison", "COMPARISON",
         ("for (range_index = 0; range_index < allocator->managed_range_count; ++range_index) {",
          "for (frame_index = 0; frame_index < range->frame_count; ++frame_index) {"),
         "if ((allocator->allocated_bitmap[word_index] & bit) == 0)"),
        ("micros_frame_allocator_allocate", "allocate-commit", "BITMAP_WORD_LOAD",
         ("for (range_index = 0; range_index < allocator->managed_range_count; ++range_index) {",
          "for (frame_index = 0; frame_index < range->frame_count; ++frame_index) {",
          "if ((allocator->allocated_bitmap[word_index] & bit) == 0) {"),
         "allocator->allocated_bitmap[word_index] |= bit;"),
    ),
    "kernel/memory/bootstrap_memory.c": (
        ("bootstrap_state_is_valid", "bootstrap-frame-read", "BITMAP_WORD_LOAD",
         ("for (bitmap_index = 0; bitmap_index < bootstrap_allocator.managed_frame_count; ++bitmap_index) {",),
         "if ((bootstrap_allocator.allocated_bitmap[word_index] & bit) != 0)"),
        ("bootstrap_state_is_valid", "bootstrap-frame-probe", "INDEX_PROBE",
         ("for (bitmap_index = 0; bitmap_index < bootstrap_allocator.managed_frame_count; ++bitmap_index) {",),
         "if ((bootstrap_allocator.allocated_bitmap[word_index] & bit) != 0)"),
        ("bootstrap_state_is_valid", "bootstrap-managed-range", "RANGE_VISIT",
         ("for (index = 0; index < bootstrap_allocator.managed_range_count; ++index) {",),
         "const struct micros_managed_frame_range *range = &bootstrap_allocator.managed_ranges[index];"),
    ),
}

GREEN_OBSERVATIONS = {
    "kernel/memory/frame_allocator.c": (
        ("micros_frame_allocator_allocate", "allocate-word-read", "BITMAP_WORD_LOAD",
         ("for (range_index = 0; range_index < allocator->managed_range_count; ++range_index) {",
          "for (word_index = (size_t)(search_index / 64); word_index < word_limit; ++word_index) {"),
         "uint64_t allocated_word = allocator->allocated_bitmap[word_index];"),
        ("micros_frame_allocator_allocate", "allocate-bit-probe", "INDEX_PROBE",
         ("for (range_index = 0; range_index < allocator->managed_range_count; ++range_index) {",
          "for (word_index = (size_t)(search_index / 64); word_index < word_limit; ++word_index) {",
          "for (bit_index = first_bit; bit_index < bit_limit; ++bit_index) {"),
         "if ((available & bit) == 0)"),
        ("micros_frame_allocator_allocate", "allocate-bit-comparison", "COMPARISON",
         ("for (range_index = 0; range_index < allocator->managed_range_count; ++range_index) {",
          "for (word_index = (size_t)(search_index / 64); word_index < word_limit; ++word_index) {",
          "for (bit_index = first_bit; bit_index < bit_limit; ++bit_index) {"),
         "if ((available & bit) == 0)"),
        ("micros_frame_allocator_allocate", "allocate-commit", "BITMAP_WORD_LOAD",
         ("for (range_index = 0; range_index < allocator->managed_range_count; ++range_index) {",
          "for (word_index = (size_t)(search_index / 64); word_index < word_limit; ++word_index) {",
          "for (bit_index = first_bit; bit_index < bit_limit; ++bit_index) {"),
         "allocator->allocated_bitmap[word_index] |= bit;"),
    ),
    "kernel/memory/bootstrap_memory.c": (
        ("bootstrap_bitmap_word_count", "bootstrap-word-bit", "INDEX_PROBE",
         ("while (word != 0) {",), "word &= word - UINT64_C(1);"),
        ("bootstrap_bitmap_is_valid", "bootstrap-word-read", "BITMAP_WORD_LOAD",
         ("for (word_index = 0; word_index < MICROS_FRAME_ALLOCATOR_BITMAP_WORDS; ++word_index) {",),
         "uint64_t word = bootstrap_allocator.allocated_bitmap[word_index];"),
        ("bootstrap_ranges_are_valid", "bootstrap-managed-range", "RANGE_VISIT",
         ("for (index = 0; index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES; ++index) {",),
         "const struct micros_managed_frame_range *range = &bootstrap_allocator.managed_ranges[index];"),
        ("bootstrap_ranges_are_valid", "bootstrap-unused-memory-range", "RANGE_VISIT",
         ("for (index = bootstrap_allocator.memory_range_count; index < MICROS_FRAME_ALLOCATOR_MAX_MEMORY_RANGES; ++index) {",),
         "const struct micros_physical_range *range = &bootstrap_allocator.memory_ranges[index];"),
        ("bootstrap_ranges_are_valid", "bootstrap-unused-reserved-range", "RANGE_VISIT",
         ("for (index = bootstrap_allocator.reserved_range_count; index < MICROS_FRAME_ALLOCATOR_MAX_RESERVED_RANGES; ++index) {",),
         "const struct micros_physical_range *range = &bootstrap_allocator.reserved_ranges[index];"),
    ),
}

OBSERVATION_PREFIX = (
    '#include "micros/work_observation.h"\n'
    "#if !defined(MICROS_TEST_WORK_COUNTS) || MICROS_TEST_WORK_COUNTS != 1\n"
    '#error "MEM01 observations require the test-only work collector"\n'
    "#endif\n"
    "#if !defined(MICROS_TEST_BOOTSTRAP_MEMORY_COUNTS) || MICROS_TEST_BOOTSTRAP_MEMORY_COUNTS != 1\n"
    '#error "MEM01 observations require their exact dedicated native mode"\n'
    "#endif\n"
)

MEASURED_FUNCTIONS = {
    "kernel/memory/frame_allocator.c": (
        "micros_frame_allocator_allocate", "micros_frame_allocator_release",
    ),
    "kernel/memory/bootstrap_memory.c": (
        "range_end", "ranges_intersect", "fdt_memory_union_covers", "intersects_fdt_reservations",
        "managed_frame_is_allowed", "bootstrap_state_is_valid", "bootstrap_bitmap_word_count",
        "bootstrap_bitmap_is_valid", "bootstrap_ranges_are_valid",
    ),
    "kernel/memory/frame_ownership.c": (
        "micros_frame_process_handle_is_valid", "micros_frame_owner_kind_is_process_bound",
        "micros_frame_owner_is_valid", "range_snapshot_is_zero", "managed_range_is_zero",
        "allocator_geometry_is_valid", "micros_frame_ownership_require_initialized",
        "micros_frame_ownership_geometry_matches", "micros_frame_ownership_physical_address_to_index",
    ),
    "kernel/memory/frame_ownership_bootstrap.c": (
        "owner_is_allocatable", "micros_frame_ownership_allocate",
    ),
}


def _paths(value, count=None):
    return (
        type(value) is tuple and (count is None or len(value) == count)
        and all(type(path) is str and path and not Path(path).is_absolute()
                and "\\" not in path and ".." not in Path(path).parts
                and Path(path).as_posix() == path for path in value)
        and len(set(value)) == len(value)
    )


def read_declarations(root):
    result = {}
    for path, names in (
        (AUTHORITY_PATH, ("DESIGN_PINS", "ORACLE_RULES")),
        (SOURCE_PATH, ("SOURCE_ROLES", "RECIPE_ADMISSION", "STATE_WORK_CONTRACT", "SOURCE_PROJECTION")),
    ):
        tree = ast.parse(regular_source(Path(root), path).read_text())
        result.update((name, literal_value(single_assignment(tree, name).value)) for name in names)
    validate_declarations(result)
    return result


def validate_declarations(value):
    if type(value) is not dict or set(value) != {
        "DESIGN_PINS", "ORACLE_RULES", "SOURCE_ROLES", "RECIPE_ADMISSION",
        "STATE_WORK_CONTRACT", "SOURCE_PROJECTION",
    } or any(type(value[name]) is not dict for name in value if name != "ORACLE_RULES"):
        raise ValueError("MEM01 has incomplete literal authority declarations")
    roles, projection = value["SOURCE_ROLES"], value["SOURCE_PROJECTION"]
    if (
        set(roles) != {"base_membership_sha256", "candidate_paths", "immutable_additions"}
        or type(roles["base_membership_sha256"]) is not str
        or re.fullmatch(r"[0-9a-f]{64}", roles["base_membership_sha256"]) is None
        or not _paths(roles["candidate_paths"], 8) or not _paths(roles["immutable_additions"], 8)
        or set(roles["candidate_paths"]) & set(roles["immutable_additions"])
        or not _paths(projection.get("retirement_keys"), 6)
        or projection.get("source_reference") != value["DESIGN_PINS"].get("runtime_reference")
    ):
        raise ValueError("MEM01 source membership or reference is malformed")
    rules = value["ORACLE_RULES"]
    if type(rules) is not tuple or len(rules) != 3:
        raise ValueError("MEM01 requires three independently owned work oracles")
    ids, tests, positions = set(), set(), set()
    for row in rules:
        if (
            type(row) is not tuple or len(row) != 6
            or any(type(item) is not str or not item for item in row[:5])
            or row[0] in ids or row[1] in tests or row[3] != "ADR-0100"
            or type(row[5]) is not tuple or not row[5] or len(set(row[5])) != len(row[5])
            or any(type(index) is not int or index < 0 or index >= 6 for index in row[5])
        ):
            raise ValueError("MEM01 oracle scope is malformed or duplicated")
        ids.add(row[0])
        tests.add(row[1])
        positions.update(row[5])
    if positions != set(range(6)):
        raise ValueError("MEM01 replacement oracles omit a retirement key")


def declaration_facts(root, owner):
    if owner not in (AUTHORITY_PATH, SOURCE_PATH):
        raise ValueError("MEM01 has no declaration in this source owner")
    values = read_declarations(root)
    pins, roles = values["DESIGN_PINS"], values["SOURCE_ROLES"]
    recipe, projection = values["RECIPE_ADMISSION"], values["SOURCE_PROJECTION"]
    references = tuple(pins[key] for key in ("design_document", "adr_document", "research_document"))
    consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)

    def fact(kind, scope, classification, payloads, value):
        return Fact(
            owner + ":" + kind + ":" + scope, FAMILY, owner, kind, scope, classification,
            "B02", consumers, tuple(sorted({owner, *references, *payloads})), digest(value),
        )

    if owner == AUTHORITY_PATH:
        return (
            fact("design-anchor", "MEM01", AuthorityClass.TRANSPORT_IDENTITY, (), pins),
            *(fact("baseline-oracle", row[0], AuthorityClass.BASELINE_ORACLE,
                   roles["immutable_additions"], row) for row in values["ORACLE_RULES"]),
        )
    return (
        fact("source-membership", "MEM01", AuthorityClass.PERMANENT_INVARIANT,
             (*roles["candidate_paths"], *roles["immutable_additions"]), roles),
        fact("artifact-contract", "MEM01-recipes", AuthorityClass.ARTIFACT_CONTRACT,
             ("CMakeLists.txt", "CMakePresets.json", "include/micros/work_observation.h",
              "tests/work/work_counts.h", *recipe["fixture_sources"], *recipe["native_source_members"],
              "kernel/memory/bootstrap_memory.c", recipe["generator"]), recipe),
        fact("state-work-contract", "MEM01", AuthorityClass.PERMANENT_INVARIANT,
             (*roles["candidate_paths"][:6], *roles["immutable_additions"][:2],
              "tests/host/bootstrap_allocation_evidence.py", "include/micros/work_observation.h",
              "tests/work/work_counts.h"), values["STATE_WORK_CONTRACT"]),
        fact("source-projection", "MEM01", AuthorityClass.TRANSPORT_IDENTITY,
             (*roles["candidate_paths"], "tests/host/c_source_records.py",
              "tests/host/test_frame_ownership_source_layout.py", "tests/host/test_vm_service_source_layout.py",
              "tests/host/work_count_evidence.py", "tools/generate_bootstrap_allocation_fixture.py"), projection),
    )


@lru_cache(maxsize=256)
def baseline_source(root, path):
    root = Path(root)
    if path not in SOURCE_PROJECTION["retained_source_sha256"]:
        raise ValueError("MEM01 has no baseline subject in this path")
    result = subprocess.run(
        ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + path),
        cwd=root, stdin=subprocess.DEVNULL, capture_output=True, check=False, timeout=30,
    )
    if result.returncode or hashlib.sha256(result.stdout).hexdigest() != SOURCE_PROJECTION["retained_source_sha256"][path]:
        raise ValueError("MEM01 fixed source differs from its independent declaration")
    return result.stdout.decode("utf-8")


def _function_fragment(record):
    return record.signature + "{" + record.body + "}"


def _function_calls(body):
    from tests.host.c_source_records import tokens
    from tests.host.test_frame_ownership_source_layout import c_lexical_text

    text = c_lexical_text(body)
    lexical = tokens(text)
    scalar_casts = {
        "size_t", "uintptr_t", "uint64_t", "uint32_t", "uint16_t", "uint8_t",
        "unsigned", "int", "bool", "void",
    }
    for index, (left, right) in enumerate(zip(lexical, lexical[1:])):
        if left == ")" and right == "(" and not (
            index >= 2 and lexical[index - 2] == "(" and lexical[index - 1] in scalar_casts
        ):
            raise ValueError("MEM01 does not admit an indirect call in its measured source")
    return set(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", text)) - {
        "if", "while", "for", "switch", "return", "sizeof", "_Alignof",
        "UINT64_C", "UINT32_C", "UINT16_C",
    }


def validate_retained_source(root, path, text, *, stage):
    from tests.host.c_source_records import compact, function_records
    from tests.host.test_frame_ownership_source_layout import c_lexical_text

    if stage not in (Stage.RED, Stage.GREEN) or type(text) is not str:
        raise ValueError("MEM01 source projection has an unknown stage or representation")
    from tests.host.selected_ownership_source import native_assertion_admission

    retired = native_assertion_admission(root, "MEM01", path, text, stage)
    original = baseline_source(Path(root), path)
    if stage is Stage.RED:
        if text != original:
            raise ValueError("MEM01 Red source is not the complete fixed original")
        return
    if path == "include/micros/frame_allocator.h":
        anchor = "    uint64_t allocated_bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];"
        field = "    uint64_t allocation_cursor;\n"
        if original.count(anchor) != 1 or compact(c_lexical_text(text)) != compact(c_lexical_text(
            original.replace(anchor, field + anchor, 1),
        )):
            raise ValueError("MEM01 allocator header changed more than the admitted cursor position")
        return
    before = function_records(original)
    observed = text
    if path == "kernel/memory/bootstrap_memory.c":
        records = function_records(observed)
        if "managed_frame_is_allowed" not in records:
            raise ValueError("MEM01 omitted the original selected-frame validator")
        fragment = _function_fragment(records["managed_frame_is_allowed"])
        guarded = "#if defined(MICROS_BUILD_FRAME_ALLOCATOR_TEST)\n" + fragment + "\n#endif"
        if observed.count(guarded) != 1:
            raise ValueError("MEM01 selected-frame guard differs from its exact admission")
        observed = observed.replace(guarded, fragment, 1)
    after = function_records(observed)
    additions = SOURCE_PROJECTION["new_helpers"].get(path, ())
    names = {row[0] for row in additions}
    if "MEM01/definitions/" + path not in retired and set(after) != set(before) | names:
        raise ValueError("MEM01 source added or omitted an unadmitted definition")
    mutable = SOURCE_PROJECTION["mutable_functions"][path]
    for name, old in before.items():
        if "MEM01/function/" + path + "/" + name in retired:
            continue
        current = after[name]
        if compact(current.signature) != compact(old.signature) or current.guards != old.guards:
            raise ValueError("MEM01 changed an original signature or CPP owner: " + name)
        if name not in mutable and compact(c_lexical_text(current.body)) != compact(c_lexical_text(old.body)):
            raise ValueError("MEM01 changed a retained original body: " + name)
        if name in mutable:
            calls = _function_calls(old.body)
            if name == "bootstrap_state_is_valid":
                calls = {"bootstrap_bitmap_is_valid", "bootstrap_ranges_are_valid"}
            elif name == "commit_process_release":
                calls.add("micros_frame_ownership_allocator_mark_free")
            elif name == "micros_frame_ownership_validate":
                calls.add("allocated_prefix_is_valid")
            if _function_calls(current.body) != calls:
                raise ValueError("MEM01 changed a mutation entry's admitted call set: " + name)
    for name, signature, calls in additions:
        current = after[name]
        observed_calls = _function_calls(current.body)
        if compact(current.signature) != compact(signature) or current.guards or observed_calls != set(calls):
            raise ValueError("MEM01 helper escaped its signature/call admission: " + name)

    def skeleton(source, records):
        for record in records.values():
            fragment = _function_fragment(record)
            if source.count(fragment) != 1:
                raise ValueError("MEM01 has ambiguous source definition boundaries")
            source = source.replace(fragment, "", 1)
        return compact(c_lexical_text(source))

    if skeleton(original, before) != skeleton(observed, after):
        raise ValueError("MEM01 changed an unrelated type, include, macro or storage owner")


def observation_sites(path, stage, root=None):
    if stage not in (Stage.RED, Stage.GREEN) or path not in RECIPE_ADMISSION["generated_subjects"]:
        raise ValueError("MEM01 observations have an unknown source/stage")
    selected = RED_OBSERVATIONS if stage is Stage.RED else GREEN_OBSERVATIONS
    result = (*COMMON_OBSERVATIONS.get(path, ()), *selected.get(path, ()))
    from tests.host.selected_ownership_source import legacy_observation_sites
    result = legacy_observation_sites(root, "MEM01", path, stage, result)
    keys = [(row[0], row[1]) for row in result]
    if len(keys) != len(set(keys)) or any(
        kind not in STATE_WORK_CONTRACT["work"]["kinds"] for _, _, kind, _, _ in result
    ):
        raise ValueError("MEM01 observation ownership is ambiguous")
    return result


def _observation_call(site):
    _, identifier, kind, _, _ = site
    return (
        'MICROS_TEST_WORK_HIT("' + identifier + '", MICROS_WORK_OWNER_KERNEL_MEMORY, '
        + "MICROS_WORK_KIND_" + kind + ", 1);"
    )


def _observation_spans(path, text, stage, root=None):
    from tests.host.c_source_records import tokens
    from tests.host.work_count_lexical import compound_layout, function_spans, matches_at, positional_tokens
    from tests.host.work_count_projection import resolve_regions
    from tests.host.work_count_types import Region
    from tests.host.selected_release_handoff_source import legacy_replaced_functions

    root = Path(__file__).resolve().parents[2] if root is None else Path(root)
    spans = function_spans(text)
    replaced = legacy_replaced_functions(root, "MEM01", path, text, stage)
    result = []
    for site in observation_sites(path, stage, root=root):
        function, _, _, regions, primitive = site
        if function in replaced:
            continue
        if function not in spans:
            raise ValueError("MEM01 observation function is absent: " + function)
        function_span = spans[function]
        body = positional_tokens(text[function_span.body_start:function_span.body_end])
        values = tuple(item.value for item in body)
        stacks, closings = compound_layout(values)
        start, end, enclosing = resolve_regions(
            values, stacks, closings, tuple(Region(region) for region in regions),
        )
        fragment = tokens(primitive)
        selected = [
            index for index in matches_at(values, fragment, start, end)
            if stacks[index] == enclosing and (index == 0 or values[index - 1] in (";", "{", "}"))
        ]
        if len(selected) != 1:
            raise ValueError("MEM01 primitive occurrence is missing or ambiguous: " + site[1])
        begin = function_span.body_start + body[selected[0]].start
        finish = function_span.body_start + body[selected[0] + len(fragment) - 1].end
        result.append((site, begin, finish))
    for function in (set(MEASURED_FUNCTIONS.get(path, ())) & set(spans)) - replaced:
        span = spans[function]
        for token in positional_tokens(text[span.body_start:span.body_end]):
            if token.value != "allocated_bitmap":
                continue
            location = span.body_start + token.start
            if not any(site[2] == "BITMAP_WORD_LOAD" and begin <= location < end
                       for site, begin, end in result if site[0] == function):
                raise ValueError("MEM01 bitmap input is outside a declared actual-load primitive")
    return tuple(result)


def _render_observations(path, text, stage, root=None):
    edits = {}
    for site, begin, _ in _observation_spans(path, text, stage, root=root):
        edits.setdefault(begin, []).append(_observation_call(site))
    for begin in sorted(edits, reverse=True):
        text = text[:begin] + "".join(edits[begin]) + text[begin:]
    return OBSERVATION_PREFIX + text


def observe_source(path, text, *, stage, root=None):
    from tests.host.selected_release_handoff_source import legacy_observe_source

    root = Path(__file__).resolve().parents[2] if root is None else Path(root)
    validate_retained_source(root, path, text, stage=stage)
    current = legacy_observe_source(root, "MEM01", path, text, stage)
    if current is not None:
        return current
    return _render_observations(path, text, stage, root=root)


def project_observations(path, text, *, stage, root=None):
    from tests.host.selected_release_handoff_source import project_legacy_observations

    root = Path(__file__).resolve().parents[2] if root is None else Path(root)
    current = project_legacy_observations(root, "MEM01", path, text, stage)
    if current is not None:
        return current
    if type(text) is not str or not text.startswith(OBSERVATION_PREFIX):
        raise ValueError("MEM01 observation preamble is not the fixed native-only declaration")
    original = text[len(OBSERVATION_PREFIX):]
    for site in observation_sites(path, stage, root=root):
        call = _observation_call(site)
        if original.count(call) != 1:
            raise ValueError("MEM01 observation is missing, changed or duplicated")
        original = original.replace(call, "", 1)
    expected = baseline_source(root, path) if stage is Stage.RED else regular_source(root, path).read_text()
    if original != expected:
        raise ValueError("MEM01 observations altered an actual selected source body")
    validate_retained_source(root, path, original, stage=stage)
    if _render_observations(path, original, stage, root=root) != text:
        raise ValueError("MEM01 observation moved away from its exact primitive/compound")
    return original


def native_recipe():
    name = RECIPE_ADMISSION["native_target"]
    members = (*RECIPE_ADMISSION["native_source_members"], "kernel/memory/bootstrap_memory.c")
    return (
        '    set(MEM01_NATIVE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-allocation")\n'
        "    set(MEM01_NATIVE_INPUTS\n"
        + "".join("        " + path + "\n" for path in members)
        + "    )\n"
        "    set(MEM01_NATIVE_OUTPUTS)\n"
        "    foreach(mem01_source IN LISTS MEM01_NATIVE_INPUTS)\n"
        '        list(APPEND MEM01_NATIVE_OUTPUTS "${MEM01_NATIVE_ROOT}/${mem01_source}")\n'
        "    endforeach()\n"
        "    file(GLOB_RECURSE MEM01_NATIVE_HEADERS\n"
        '        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}/arch/*.h"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}/kernel/*.h"\n'
        "    )\n"
        "    add_custom_command(\n"
        "        OUTPUT ${MEM01_NATIVE_OUTPUTS}\n"
        '        COMMAND "${MICROS_PYTHON}" ${MICROS_PYTHON_ARGS}\n'
        '            "${CMAKE_CURRENT_SOURCE_DIR}/' + RECIPE_ADMISSION["generator"] + '"\n'
        "        DEPENDS ${MEM01_NATIVE_INPUTS} ${MEM01_NATIVE_HEADERS}\n"
        "            " + RECIPE_ADMISSION["generator"] + "\n"
        "            tests/host/bootstrap_allocation_source.py\n"
        '        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"\n'
        "        VERBATIM\n"
        "    )\n"
        "    add_executable(" + name + " ${MEM01_NATIVE_OUTPUTS}\n"
        + "".join("        " + path + "\n" for path in RECIPE_ADMISSION["fixture_sources"])
        + "    )\n"
        "    target_include_directories(" + name + " PRIVATE\n"
        '        "${MEM01_NATIVE_ROOT}" "${MEM01_NATIVE_ROOT}/include"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include")\n'
        "    target_compile_features(" + name + " PRIVATE c_std_17)\n"
        "    target_compile_options(" + name
        + " PRIVATE -Wall -Wextra -Werror -Wpedantic -Wmissing-prototypes -Wstrict-prototypes)\n"
        '    if(MICROS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")\n'
        "        target_compile_options(" + name + " PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)\n"
        "        target_link_options(" + name + " PRIVATE -fsanitize=address,undefined)\n"
        "    endif()\n"
        "    target_compile_definitions(" + name + " PRIVATE "
        + " ".join(RECIPE_ADMISSION["native_definitions"]) + ")\n"
        "    add_test(NAME " + RECIPE_ADMISSION["native_case"] + " COMMAND " + name + ")\n"
        + "".join("    add_dependencies(" + tier + " " + name + ")\n" for tier in RECIPE_ADMISSION["native_tiers"])
        + "\n"
    )


def extend_cmake(text):
    anchor = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
    if hashlib.sha256(text.encode()).hexdigest() != RECIPE_ADMISSION["reference_cmake_sha256"] or text.count(anchor) != 1:
        raise ValueError("MEM01 native recipe lacks its complete fixed predecessor")
    return text.replace(anchor, native_recipe() + anchor, 1)


def project_cmake(text):
    from tests.host.kernel_mapping_source import project_cmake as project_mapping_cmake

    text = project_mapping_cmake(text)
    if not any(marker in text for marker in ("MEM01_NATIVE", "bootstrap_allocation", "bootstrap-allocation")):
        return text
    addition = native_recipe()
    anchor = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
    if text.count(addition) != 1 or text.count(addition + anchor) != 1:
        raise ValueError("MEM01 native recipe is changed, partial or relocated")
    original = text.replace(addition, "", 1)
    if hashlib.sha256(original.encode()).hexdigest() != RECIPE_ADMISSION["reference_cmake_sha256"]:
        raise ValueError("MEM01 native recipe changed unrelated predecessor bytes")
    return original


def project_bootstrap_family_members(members):
    observed = set(members)
    addition = set(SOURCE_ROLES["immutable_additions"][:2])
    if not addition <= observed:
        raise ValueError("MEM01 native fixture membership is incomplete")
    return observed - addition


def _bootstrap_discovery_change():
    original = (
        "def discovered_family_paths() -> set[str]:\n"
        "    patterns = (\n"
        '        "kernel/bootstrap*.[ch]", "kernel/boot_*.[ch]", "kernel/main.c",\n'
        '        "include/micros/bootstrap*.h", "include/micros/fdt.h",\n'
        '        "tests/host/bootstrap*.[ch]", "tests/host/fdt*.[ch]",\n'
        '        "tests/qemu/bootstrap_launcher*.[ch]", "tests/qemu/bootstrap_protocol.h",\n'
        "        GENERATOR_PATH,\n"
        "    )\n"
        "    return {\n"
        "        str(path.relative_to(REPOSITORY_ROOT))\n"
        "        for pattern in patterns for path in kernel_layout_source.glob_paths(REPOSITORY_ROOT, pattern)\n"
        "    }\n"
    )
    extended = original.replace(
        "def discovered_family_paths() -> set[str]:\n",
        "def discovered_family_paths() -> set[str]:\n"
        "    from tests.host.bootstrap_allocation_source import project_bootstrap_family_members\n\n",
    ).replace("    return {\n", "    return project_bootstrap_family_members({\n").replace(
        "    }\n", "    })\n",
    )
    return original, extended


def project_bootstrap_discovery(text):
    if "project_bootstrap_family_members" not in text:
        return text
    original, extended = _bootstrap_discovery_change()
    if text.count(extended) != 1 or text.count("project_bootstrap_family_members") != 2:
        raise ValueError("MEM01 bootstrap discovery extension is changed, partial or relocated")
    return text.replace(extended, original, 1)


def _gate_changes(path):
    previous = "from tests.host.diagnostic_history_authority import identity_required as diagnostic_identity_required\n"
    current = "from tests.host.bootstrap_allocation_authority import identity_required as diagnostic_identity_required\n"
    if path == "tests/host/test_kernel_directory_layout.py":
        return ((previous, current),)
    if path == "tests/host/kernel_layout_source.py":
        condition = '    if logical == "kernel/frame_ownership_validation.c":\n'
        guarded = (
            "    if (\n"
            '        logical == "kernel/frame_ownership_validation.c"\n'
            "        and diagnostic_identity_required(\n"
            '            root, "tests/host/kernel_layout_source.py:data-entry:BOUNDED_FRAME_VALIDATION_SHA256",\n'
            "        )\n"
            "    ):\n"
        )
        return ((previous, current), (condition, guarded))
    return ()


def extend_gate_adapter(path, text):
    changes = _gate_changes(path)
    if not changes:
        raise ValueError("MEM01 has no source-assertion adapter in this owner")
    for previous, current in changes:
        if text.count(previous) != 1 or current in text:
            raise ValueError("MEM01 source-assertion adapter lacks its exact original anchor")
        text = text.replace(previous, current, 1)
    return text


def project_gate_adapter(path, text):
    from tests.host.kernel_mapping_source import project_gate_adapter as project_mapping_adapter

    text = project_mapping_adapter(path, text)
    if path == "tests/host/test_kernel_bootstrap_source_layout.py":
        return project_bootstrap_discovery(text)
    changes = _gate_changes(path)
    present = [text.count(current) for _, current in changes]
    if not any(present):
        return text
    if any(count != 1 for count in present):
        raise ValueError("MEM01 source-assertion adapter is incomplete or duplicated")
    for previous, current in changes:
        text = text.replace(current, previous, 1)
    return text


def validate_scaffold(root):
    from tests.host.ast_evidence import ast_dump

    root = Path(root)
    cmake = regular_source(root, "CMakeLists.txt").read_text()
    if project_cmake(cmake) == cmake:
        raise ValueError("MEM01 native target/case integration is absent")
    for path in (
        *SOURCE_ROLES["candidate_paths"][-2:],
        "tests/host/test_kernel_bootstrap_source_layout.py",
    ):
        current = regular_source(root, path).read_text()
        expected = subprocess.check_output(
            ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + path),
            cwd=root, text=True, timeout=30,
        )
        if current == expected or ast_dump(ast.parse(project_gate_adapter(path, current))) != ast_dump(ast.parse(expected)):
            raise ValueError("MEM01 source-assertion adapter is absent or altered unrelated policy")
