"""Closed MEM02 source, native/target recipe and work declarations."""

import ast
from functools import lru_cache
import hashlib
from pathlib import Path
import re
import subprocess

from tests.host.evidence_authority_source import literal_value, regular_source, single_assignment
from tests.host.evidence_authority_types import AuthorityClass, Fact, digest
from tests.host.work_count_types import Stage


FAMILY = "kernel-mapping-baseline"
SOURCE_PATH = "tests/host/kernel_mapping_source.py"
AUTHORITY_PATH = "tests/host/kernel_mapping_authority.py"

SOURCE_ROLES = {
    "base_membership_sha256": "858cca950f2968bf6f2791a4c0d2267510a714e40738fe2df3cf1bf205a8cd66",
    "candidate_paths": (
        "kernel/memory/address_space.c",
        "kernel/memory/address_space_validation.c",
        "kernel/memory/address_space_internal.h",
        "tests/host/kernel_layout_source.py",
        "tests/host/test_kernel_directory_layout.py",
    ),
    "immutable_additions": (
        "tests/host/kernel_mapping_test.c",
        "tests/host/kernel_mapping_test.h",
        "tests/host/kernel_mapping_source.py",
        "tests/host/kernel_mapping_evidence.py",
        "tests/host/kernel_mapping_authority.py",
        "tests/host/test_kernel_mapping.py",
        "tools/generate_kernel_mapping_fixture.py",
        "tools/check_kernel_mapping.py",
    ),
}

RECIPE_ADMISSION = {
    "native_target": "kernel_mapping_test",
    "native_case": "kernel-mapping",
    "native_tiers": ("test-unit-fast", "test-unit"),
    "native_definitions": ("MICROS_TEST_WORK_COUNTS=1", "MICROS_TEST_KERNEL_MAPPING_COUNTS=1"),
    "native_source_groups": (
        "MICROS_FRAME_OWNERSHIP_SOURCES",
        "MICROS_KERNEL_OBJECT_CORE_SOURCES",
        "MICROS_SCHEDULER_CORE_SOURCES",
    ),
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
        "kernel/memory/sv39.c",
        "kernel/memory/bootstrap_memory.c",
        "kernel/memory/address_space.c",
        "kernel/memory/address_space_validation.c",
    ),
    "fixture_sources": ("tests/host/kernel_mapping_test.c", "tests/work/work_counts.c"),
    "generated_subjects": (
        "kernel/memory/frame_allocator.c",
        "kernel/memory/frame_ownership.c",
        "kernel/memory/frame_ownership_bootstrap.c",
        "kernel/memory/frame_ownership_validation.c",
        "kernel/memory/bootstrap_memory.c",
        "kernel/memory/address_space.c",
        "kernel/memory/address_space_validation.c",
        "kernel/memory/sv39.c",
    ),
    "generator": "tools/generate_kernel_mapping_fixture.py",
    "generator_output": "build/host-debug/kernel-mapping",
    "native_standard": "c17",
    "native_sanitizers": ("address", "undefined"),
    "baseline_headers": "fixed-runtime-reference",
    "current_headers": "actual-current-source",
    "observations": "private-generated-native-translations-only",
    "ordinary_observations": "absent",
    "reference_cmake_sha256": "55e818c85969c2b7640f99cc26b91242e2fb94a84c811819995ba7e447e8a4e0",
    "qemu_workflow": "test-qemu-mmu",
    "qemu_memories": ("128M", "256M"),
    "qemu_timeout": 8,
    "qemu_fixture_paths": ("kernel/tests/memory/mmu_test.c", "arch/riscv64/mmu_test.S"),
    "stack_binding": "tools/stack_bound_assembly.py",
    "dependency_policy": "explicit-source-and-configure-enumerated-header-inputs;no-depfile",
}

STATE_WORK_CONTRACT = {
    "version": 1,
    "frame_size": 4096,
    "leaf_sizes": (4096, 2097152, 1073741824),
    "capacity": {"tables": 1024, "radix": 512, "managed_ranges": 96, "managed_frames": 262144},
    "default_linker_bounds": (0x80200000, 0x80201000, 0x80202000, 0x80203000),
    "device_pages": (0x10000000, 0x0C000000, 0x0C002000, 0x0C201000),
    "dense_frames": (9, 63, 64, 65, 511, 512, 513, 4096, 262143, 262144),
    "default_managed_base": 0xC0000000,
    "boundary_cases": ((0xC01FF000, 65), (0xBFFFF000, 513), (0xBFFFF000, 514)),
    "permission_split": {"writable_end": 0x80400000, "frames": 4096},
    "touching_rw": {"writable_end": 0x80401000, "managed_base": 0x80401000, "frames": 4096},
    "last_lower_canonical": {"low_frames": 16, "high_base": 0x3FFFFFF000, "high_frames": 1},
    "portable_split": {
        "memory_ranges": 16, "stride": 0x40000000, "pages": 11,
        "hole_pages": (1, 3, 5, 7, 9), "input_order": "reversed",
        "reserved_ranges": 80, "managed_ranges": 96, "fdt_case": "not-fdt",
    },
    "captures": ("pristine", "construct", "validate", "tree", "coverage"),
    "valid_cases": 17,
    "capture_count": 85,
    "exhaustion_frames": (1, 2, 3, 4, 5, 6, 7, 8),
    "metadata": {
        "table_range_indices": ("uint16_t", (1024,)),
        "level1_table_ids": ("uint16_t", (512,)),
        "level0_table_ids": ("uint16_t", (512, 512)),
        "child_id": "zero-absent;nonzero-slot-plus-one",
        "index_authority": "checked-actual-pa-range-and-pristine-allocation-ordinal;not-cursor",
        "persistent_bytes": 527360,
        "boot_clear_stores": 65920,
        "net_resident_saving_claim": "none",
    },
    "work": {
        "owner": "KERNEL_MEMORY", "owner_mask": 4, "dimensions": (27, 14),
        "producer_label": 0x4D454D3032000000, "capture_label": "case-major-1-through-85",
        "kinds": (
            "COMPARISON", "RECORD_VISIT", "INDEX_PROBE", "LINK_TRAVERSE",
            "BITMAP_WORD_LOAD", "PTE_VISIT", "COMPLETE_VALIDATOR_CALL",
            "CLEAR_UNIT", "CLEARED_BYTE", "RANGE_VISIT",
        ),
        "pristine_bitmap_loads": "p",
        "construction_index": "32*(L+t+g+1)",
        "construction_pte": "4*L+2*t",
        "construction_range": "t*(2*gmax+4*g)+8*L+2*g+32",
        "construction_record": "t*(2*gmax+4*g)+16*L+16*t+128",
        "construction_bitmap": "8*(L+t+1)",
        "construction_complete_calls": 0,
        "construction_clear_units": "512*t+512*m-current-only",
        "construction_cleared_bytes": "4096*t+1024*m-current-only",
        "validation_range": "4*gmax+4*t",
        "validation_complete_calls": 1,
        "tree_index": "16*(t+1)",
        "tree_pte": "512*t",
        "coverage_pte": "3*(Lreq+t+4)",
        "composition": "pristine+construct+validate;tree-and-coverage-not-added-twice",
    },
    "target": {
        "temporary_frames": 2, "owner": "KERNEL_TEMPORARY", "root_slots": (4, 5),
        "permissions": "supervisor-read-only-accessed",
        "probe": "micros_mmu_large_load_test_trigger",
        "fault": "micros_mmu_large_load_fault", "resume": "micros_mmu_large_load_resume",
        "fault_cause": 13, "faults": 2, "frame_bytes": 0,
        "mutation": "real-kernel-root-active-before-every-test-pte-write-and-release",
        "resource_proof": "original-kpt-prefix-counts-cursor-phase-ledger-root-and-cover",
        "observation_version": 1,
        "pass": "MICROS_KERNEL_MAPPING_TEST_PASS topology=largest aliases=2m,1g misaligned=rejected resources=preserved",
    },
}

SOURCE_PROJECTION = {
    "source_reference": "3f7e33026f92c8c1765ce00c2b03b5c2d6855197",
    "retirement_keys": (
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/address_space.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/address_space_validation.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/address_space_internal.h/2",
    ),
    "retained_source_sha256": {
        "arch/riscv64/mmu_test.S": "a0fcba06e038eb6b538dc541d2601f200a6e8385ca40e3740edde98c390425ab",
        "kernel/memory/address_space.c": "fd774c3074e762bf3b40d03db8371175ed3f9560f6fcc0511364a39b9b8ac696",
        "kernel/memory/address_space_internal.h": "6ed1e61254bee6483849588cceec092456327e6388200e5e24cb26807e8466b9",
        "kernel/memory/address_space_validation.c": "d6b47319a1545463a3dacd121eea54d518f74f13956b356323b740229b9d13cc",
        "kernel/tests/memory/mmu_test.c": "5553190f5f6834b693b2c62fab057d8b2ec6ad7b34c0853d47293779877bb1e8",
    },
    "retained_constructor": (
        "table_at", "linker_layout_is_valid", "map_kernel_image", "map_managed_memory",
        "micros_kernel_address_space_initialize", "micros_kernel_address_space_report",
    ),
    "retained_validator": (
        "table_at", "allocator_allocated_bit_count",
        "micros_kernel_address_space_allocator_is_pristine",
        "frame_owners_are_equal", "validation_authority_is_valid",
    ),
    "baseline_constructor_closure": (
        "linker_layout_is_valid", "table_frame_index", "allocate_table", "map_page",
        "map_identity_range", "map_kernel_image", "map_managed_memory",
        "micros_kernel_address_space_report",
    ),
    "baseline_validator_closure": (
        "allocated_table_set_is_exact", "allocator_allocated_bit_count",
        "allocator_frame_is_allocated", "candidate_table_index",
        "every_candidate_table_is_reachable", "expected_mappings_are_valid",
        "fixed_mappings_are_valid", "frame_owners_are_equal", "leaf_table_is_valid",
        "lookup_mapping", "managed_ranges_are_mapped", "mapping_is_absent",
        "mark_reachable_table", "micros_kernel_address_space_allocator_is_pristine",
        "micros_kernel_address_space_candidate_is_valid", "middle_table_is_valid",
        "range_matches", "table_at", "table_frames_are_mapped", "table_tree_is_valid",
        "tree_and_table_ownership_are_valid", "validation_authority_is_valid",
    ),
    "bootstrap_closure": (
        "micros_bootstrap_frame_allocator", "ownership_mutation_is_allowed",
        "micros_frame_ownership_runtime_ledger", "micros_frame_ownership_runtime_allocate",
        "micros_frame_ownership_runtime_release",
    ),
    "native_bindings": (
        "two-table_at-bodies-to-bounded-native-backing",
        "numeric-kernel-linker-boundaries", "real-bootstrap-state-and-readiness",
        "riscv_irq_is_enabled-operand-to-explicit-boolean-input",
    ),
    "private_header": "three-exact-arrays;declarations-only-selected-frame-helper;no-public-abi-change",
    "target_inverse": "exact-immutable-test-only-c-and-assembly-extensions;all-old-bytes-recovered",
    "mmu_fixture_inputs": {
        "c_sha256": "f7b551459ec1e04691e124f5ede4d26765999897f802cd87a64edb5f253ffca6",
        "c_extension_sha256": "4f33081a19855faff785a883a650a5ea65e0922e1a7bf52b5b720e54ce8a7b84",
        "assembly_sha256": "7f93ff92da25ba82d0996964f9ae5bef1414b46152c9b744243f34570b9dbce8",
        "assembly_extension_sha256": "7dcc37987a4425c3fe35ba69934343f24a19d530b447ec30cb82cfb4e1b1ba79",
        "load_instructions": ("00053503", "00008067"),
        "frame_bytes": 0,
        "c_includes": (
            '#include "arch/riscv64/interrupt.h"\n'
            '#include "arch/riscv64/mmu.h"\n'
            '#include "arch/riscv64/platform.h"\n'
            '#include "kernel/memory/address_space_internal.h"\n'
            '#include "micros/bootstrap_memory.h"\n'
            '#include "micros/frame_ownership_runtime.h"\n'
            '#include "micros/panic.h"\n\n'
            "static enum micros_mmu_test_trap_result mapping_test_handle_trap(struct micros_trap_frame *frame);\n"
            "static bool mapping_test_run(void);\n"
        ),
        "c_trap_hook": (
            "    enum micros_mmu_test_trap_result mapping_result = mapping_test_handle_trap(frame);\n\n"
            "    if (mapping_result != MICROS_MMU_TEST_TRAP_INACTIVE) {\n"
            "        return mapping_result;\n"
            "    }\n"
        ),
        "assembly_extension": (
            '\n.section .text, "ax", @progbits\n'
            ".option push\n.option norvc\n.balign 4\n"
            ".globl micros_mmu_large_load_test_trigger\n"
            ".type micros_mmu_large_load_test_trigger, @function\n"
            "micros_mmu_large_load_test_trigger:\n"
            ".globl micros_mmu_large_load_fault\n"
            "micros_mmu_large_load_fault:\n"
            "    ld a0, 0(a0)\n"
            ".globl micros_mmu_large_load_resume\n"
            "micros_mmu_large_load_resume:\n"
            "    ret\n"
            ".size micros_mmu_large_load_test_trigger, . - micros_mmu_large_load_test_trigger\n"
            ".option pop\n"
        ),
    },
    "current_bindings": {
        "constructor_added": (
            "allocated_table_range", "selected_table_is_valid", "child_identifier",
            "create_child_table", "select_child_table", "map_leaf", "leaf_level_for_range",
        ),
        "constructor_removed": ("table_frame_index", "map_page"),
        "validator_added": (
            "micros_kernel_address_space_table_frame_is_valid", "candidate_table_is_valid",
            "kernel_leaf_is_valid", "child_index_from_entry", "mark_child_table",
            "lookup_mapping_details", "range_leaf_level",
        ),
        "validator_removed": ("candidate_table_index", "allocator_frame_is_allocated"),
        "private_header": {
            "array_anchor": "    struct micros_kernel_address_space_report report;",
            "array_additions": (
                "    uint16_t table_range_indices[MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES];\n"
                "    uint16_t level1_table_ids[MICROS_SV39_TABLE_ENTRY_COUNT];\n"
                "    uint16_t level0_table_ids[MICROS_SV39_TABLE_ENTRY_COUNT][MICROS_SV39_TABLE_ENTRY_COUNT];\n"
            ),
            "forward": "struct micros_frame_ownership;",
            "helper": (
                "bool micros_kernel_address_space_table_frame_is_valid("
                "const struct micros_frame_allocator *allocator, "
                "const struct micros_frame_ownership *ownership, "
                "uint64_t frame_index, uint16_t range_index, uint64_t physical_address)"
            ),
        },
        "origin_sha256": "d598c89efecbcc50c270dd7970dc093b077c754a124ab4e51ebc573a03409713",
    },
    "admissions": (
        "exact-fixed-source-and-header-membership", "complete-retained-function-type-storage-register",
        "selected-frame-helper-owned-by-validation;constructor-depends-downward",
        "actual-primitive-site-and-compound-bindings;exact-passive-inverse",
        "one-native-target-case-and-two-native-tiers",
        "existing-mmu-workflow-two-memories-eight-seconds",
        "one-closed-zero-stack-load-probe-source-object-binding",
        "exact-three-key-assertion-policy-and-mem01-prior-delegation",
        "finite-before-older-gate-recipe-source-namespace-inverses",
    ),
}


def _paths(value, count):
    return (
        type(value) is tuple and len(value) == count
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
        result.update(
            (name, literal_value(single_assignment(tree, name).value))
            for name in names
        )
    validate_declarations(result)
    return result


def validate_declarations(value):
    if type(value) is not dict or set(value) != {
        "DESIGN_PINS", "ORACLE_RULES", "SOURCE_ROLES", "RECIPE_ADMISSION",
        "STATE_WORK_CONTRACT", "SOURCE_PROJECTION",
    } or any(type(value[name]) is not dict for name in value if name != "ORACLE_RULES"):
        raise ValueError("MEM02 has incomplete literal authority declarations")
    roles, projection = value["SOURCE_ROLES"], value["SOURCE_PROJECTION"]
    if (
        set(roles) != {"base_membership_sha256", "candidate_paths", "immutable_additions"}
        or type(roles["base_membership_sha256"]) is not str
        or re.fullmatch(r"[a-f0-9]{64}", roles["base_membership_sha256"]) is None
        or not _paths(roles["candidate_paths"], 5)
        or not _paths(roles["immutable_additions"], 8)
        or set(roles["candidate_paths"]) & set(roles["immutable_additions"])
        or not _paths(projection.get("retirement_keys"), 3)
        or projection.get("source_reference") != value["DESIGN_PINS"].get("runtime_reference")
    ):
        raise ValueError("MEM02 source scope or fixed reference is malformed")
    rules = value["ORACLE_RULES"]
    if type(rules) is not tuple or len(rules) != 3:
        raise ValueError("MEM02 requires three independently owned oracles")
    identifiers, tests, covered = set(), set(), set()
    for row in rules:
        if (
            type(row) is not tuple or len(row) != 6
            or any(type(item) is not str or not item for item in row[:5])
            or row[0] in identifiers or row[1] in tests
            or type(row[5]) is not tuple or not row[5]
            or len(set(row[5])) != len(row[5])
            or any(type(index) is not int or not 0 <= index < 3 for index in row[5])
        ):
            raise ValueError("MEM02 oracle coverage is malformed or duplicated")
        identifiers.add(row[0])
        tests.add(row[1])
        covered.update(row[5])
    if covered != {0, 1, 2}:
        raise ValueError("MEM02 has an uncovered retirement identity")


def declaration_facts(root, owner):
    if owner not in (SOURCE_PATH, AUTHORITY_PATH):
        raise ValueError("MEM02 has no declaration in this source owner")
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
            fact("design-anchor", "MEM02", AuthorityClass.TRANSPORT_IDENTITY, (), pins),
            *(fact("baseline-oracle", row[0], AuthorityClass.BASELINE_ORACLE,
                   roles["immutable_additions"], row) for row in values["ORACLE_RULES"]),
        )
    production = roles["candidate_paths"][:3]
    return (
        fact("source-membership", "MEM02", AuthorityClass.PERMANENT_INVARIANT,
             (*roles["candidate_paths"], *roles["immutable_additions"]), roles),
        fact("artifact-contract", "MEM02-recipes", AuthorityClass.ARTIFACT_CONTRACT,
             ("CMakeLists.txt", "CMakePresets.json", "include/micros/work_observation.h",
              "tests/work/work_counts.h", *recipe["fixture_sources"], *recipe["native_source_members"],
              recipe["generator"], *recipe["qemu_fixture_paths"], recipe["stack_binding"]), recipe),
        fact("state-work-contract", "MEM02", AuthorityClass.PERMANENT_INVARIANT,
             (*production, *roles["immutable_additions"][:2], "tests/host/kernel_mapping_evidence.py",
              "include/micros/work_observation.h", "tests/work/work_counts.h"), values["STATE_WORK_CONTRACT"]),
        fact("source-projection", "MEM02", AuthorityClass.TRANSPORT_IDENTITY,
             (*roles["candidate_paths"], "tests/host/c_source_records.py",
              "tests/host/test_kernel_address_space_source_layout.py", "tests/host/work_count_evidence.py",
              "tests/host/bootstrap_allocation_source.py", *recipe["qemu_fixture_paths"],
              recipe["stack_binding"], recipe["generator"]), projection),
    )


OBSERVATION_PREFIX = (
    '#include "micros/work_observation.h"\n'
    "#if !defined(MICROS_TEST_WORK_COUNTS) || MICROS_TEST_WORK_COUNTS != 1\n"
    '#error "MEM02 observations require the test-only work collector"\n'
    "#endif\n"
    "#if !defined(MICROS_TEST_KERNEL_MAPPING_COUNTS) || MICROS_TEST_KERNEL_MAPPING_COUNTS != 1\n"
    '#error "MEM02 observations require their exact dedicated native mode"\n'
    "#endif\n"
)

COMMON_OBSERVATIONS = {
    "kernel/memory/sv39.c": (
        ("micros_sv39_decode_pte", "decode-pte", "PTE_VISIT", (),
         "uint32_t permissions = 0;", "before", 1),
    ),
    "kernel/memory/frame_ownership.c": (
        ("micros_frame_ownership_clear_bytes", "owner-clear-unit", "CLEAR_UNIT",
         (("for (index = 0; index < size; ++index) {", 0),),
         "bytes[index] = 0;", "after", 1),
        ("micros_frame_ownership_clear_bytes", "owner-cleared-byte", "CLEARED_BYTE",
         (("for (index = 0; index < size; ++index) {", 0),),
         "bytes[index] = 0;", "after", 1),
    ),
    "kernel/memory/frame_ownership_bootstrap.c": (
        ("micros_frame_ownership_allocate", "allocate-owner-store", "RECORD_VISIT", (),
         "ownership->owners[frame_index] = owner;", "after", 1),
    ),
    "kernel/memory/frame_ownership_validation.c": (
        ("micros_frame_ownership_lookup", "lookup-owner-record", "RECORD_VISIT", (),
         "observed = ownership->owners[frame_index];", "before", 1),
        ("micros_frame_ownership_validate", "complete-owner-validation", "COMPLETE_VALIDATOR_CALL",
         (), "", "entry", 1),
        ("validate_active_frames", "active-bitmap-word", "BITMAP_WORD_LOAD",
         (("for (word_index = 0; frame_index < ownership->managed_frame_count; ++word_index) {", 0),),
         "uint64_t allocated_word = ownership->allocator->allocated_bitmap[word_index];", "before", 1),
        ("validate_active_frames", "active-owner-record", "RECORD_VISIT",
         (("for (word_index = 0; frame_index < ownership->managed_frame_count; ++word_index) {", 0),
          ("for (; frame_index < frame_limit; ++frame_index, bit <<= 1) {", 0)),
         "struct micros_frame_owner owner = ownership->owners[frame_index];", "before", 1),
        ("validate_active_frames", "active-target-record", "RECORD_VISIT",
         (("for (word_index = 0; frame_index < ownership->managed_frame_count; ++word_index) {", 0),
          ("for (; frame_index < frame_limit; ++frame_index, bit <<= 1) {", 0)),
         "uint8_t target = ownership->handoff_targets[frame_index];", "before", 1),
        ("inactive_state_is_zero", "inactive-owner-record", "RECORD_VISIT",
         (("for (frame_index = ownership->managed_frame_count; frame_index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES; ++frame_index) {", 0),),
         "struct micros_frame_owner owner = ownership->owners[frame_index];", "before", 1),
        ("inactive_state_is_zero", "inactive-boundary-word", "BITMAP_WORD_LOAD",
         (("if (boundary_bit != 0) {", 0),),
         "if ((ownership->allocator->allocated_bitmap[word_index] & inactive_mask) != 0)", "before", 1),
        ("inactive_state_is_zero", "inactive-tail-word", "BITMAP_WORD_LOAD",
         (("for (; word_index < MICROS_FRAME_ALLOCATOR_BITMAP_WORDS; ++word_index) {", 0),),
         "if (ownership->allocator->allocated_bitmap[word_index] != 0)", "before", 1),
        ("allocated_prefix_is_valid", "prefix-complete-word", "BITMAP_WORD_LOAD",
         (("for (index = 0; index < complete_words; ++index) {", 0),),
         "if (allocator->allocated_bitmap[index] != UINT64_MAX)", "before", 1),
        ("allocated_prefix_is_valid", "prefix-boundary-word", "BITMAP_WORD_LOAD",
         (("if (boundary != 0) {", 0),),
         "if ((allocator->allocated_bitmap[complete_words] & mask) != mask)", "before", 1),
    ),
}

BASELINE_OBSERVATIONS = {
    "kernel/memory/address_space.c": (
        ("table_frame_index", "constructor-table-candidate", "INDEX_PROBE",
         (("for (index = 0; index < (size_t)kernel_address_space.table_count; ++index) {", 0),),
         "if (kernel_address_space.table_frames[index] == physical_address)", "before", 1),
        ("allocate_table", "table-page-clear-unit", "CLEAR_UNIT",
         (("for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {", 0),),
         "table->entries[index] = 0;", "after", 1),
        ("allocate_table", "table-page-cleared-byte", "CLEARED_BYTE",
         (("for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {", 0),),
         "table->entries[index] = 0;", "after", 8),
        ("allocate_table", "table-record-publish", "RECORD_VISIT", (),
         "kernel_address_space.table_frames[kernel_address_space.table_count] = *physical_address;", "after", 1),
        ("map_page", "child-pte-store", "PTE_VISIT",
         (("for (level = MICROS_SV39_LEVEL_COUNT - 1; level > 0; --level) {", 0),),
         "table->entries[index] = child_pte;", "after", 1),
        ("map_page", "leaf-pte-store", "PTE_VISIT", (("{", 1),),
         "table->entries[index] = leaf_pte;", "after", 1),
        ("map_managed_memory", "mapped-managed-range", "RANGE_VISIT",
         (("for (index = 0; index < allocator->managed_range_count; ++index) {", 0),),
         "const struct micros_managed_frame_range *range = &allocator->managed_ranges[index];", "before", 1),
    ),
    "kernel/memory/address_space_validation.c": (
        ("candidate_table_index", "validator-table-candidate", "INDEX_PROBE",
         (("for (index = 0; index < (size_t)state->table_count; ++index) {", 0),),
         "if (state->table_frames[index] == physical_address)", "before", 1),
        ("allocator_frame_is_allocated", "table-allocation-range", "RANGE_VISIT",
         (("for (range_index = 0; range_index < allocator->managed_range_count; ++range_index) {", 0),),
         "const struct micros_managed_frame_range *range = &allocator->managed_ranges[range_index];", "before", 1),
        ("allocator_frame_is_allocated", "table-allocation-word", "BITMAP_WORD_LOAD",
         (("for (range_index = 0; range_index < allocator->managed_range_count; ++range_index) {", 0),),
         "return (allocator->allocated_bitmap[word_index] & bit) != 0;", "before", 1),
        ("allocator_allocated_bit_count", "retained-allocated-bit-word", "BITMAP_WORD_LOAD",
         (("for (bitmap_index = 0; bitmap_index < allocator->managed_frame_count; ++bitmap_index) {", 0),),
         "if ((allocator->allocated_bitmap[word_index] & bit) != 0)", "before", 1),
        ("micros_kernel_address_space_allocator_is_pristine", "retained-pristine-bit-word", "BITMAP_WORD_LOAD",
         (("for (bitmap_index = 0; bitmap_index < allocator->managed_frame_count; ++bitmap_index) {", 0),),
         "if ((allocator->allocated_bitmap[word_index] & bit) != 0)", "before", 1),
        ("allocated_table_set_is_exact", "allocated-table-set-is-exact", "INDEX_PROBE",
         (("for (table_index = 0; table_index < (size_t)state->table_count; ++table_index) {", 0),),
         "uint64_t physical_address = state->table_frames[table_index];", "before", 1),
        ("table_frames_are_mapped", "table-frames-are-mapped", "INDEX_PROBE",
         (("for (index = 0; index < (size_t)state->table_count; ++index) {", 0),),
         "uint64_t physical_address = state->table_frames[index];", "before", 1),
        ("managed_ranges_are_mapped", "expected-managed-range", "RANGE_VISIT",
         (("for (index = 0; index < allocator->managed_range_count; ++index) {", 0),),
         "const struct micros_managed_frame_range *range = &allocator->managed_ranges[index];", "before", 1),
        ("tree_and_table_ownership_are_valid", "reachable-clear-unit", "CLEAR_UNIT",
         (("for (table_index = 0; table_index < MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES; ++table_index) {", 0),),
         "reachable[table_index] = false;", "after", 1),
        ("tree_and_table_ownership_are_valid", "reachable-cleared-byte", "CLEARED_BYTE",
         (("for (table_index = 0; table_index < MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES; ++table_index) {", 0),),
         "reachable[table_index] = false;", "after", 1),
    ),
}

CURRENT_OBSERVATIONS = {
    "kernel/memory/address_space.c": (
        ("allocated_table_range", "new-table-range", "RANGE_VISIT",
         (("for (index = 0; index < allocator->managed_range_count; ++index) {", 0),),
         "const struct micros_managed_frame_range *range = &allocator->managed_ranges[index];", "before", 1),
        ("selected_table_is_valid", "selected-table-record", "INDEX_PROBE", (("{", 1),),
         "uint64_t physical_address = kernel_address_space.table_frames[index];", "before", 1),
        ("selected_table_is_valid", "selected-table-range-hint", "RECORD_VISIT", (("{", 1),),
         "uint16_t range_index = kernel_address_space.table_range_indices[index];", "before", 1),
        ("allocate_table", "table-range-publish", "RECORD_VISIT", (),
         "kernel_address_space.table_range_indices[kernel_address_space.table_count] = range_index;", "after", 1),
        ("create_child_table", "middle-row-clear-unit", "CLEAR_UNIT",
         (("if (level == 2) {", 0), ("for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {", 0)),
         "kernel_address_space.level0_table_ids[root_index][index] = 0;", "after", 1),
        ("create_child_table", "middle-row-cleared-byte", "CLEARED_BYTE",
         (("if (level == 2) {", 0), ("for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {", 0)),
         "kernel_address_space.level0_table_ids[root_index][index] = 0;", "after", 2),
        ("create_child_table", "child-pte-store", "PTE_VISIT", (),
         "parent->entries[entry_index] = pte;", "after", 1),
        ("create_child_table", "child-id-publish", "RECORD_VISIT", (),
         "*identifier = (uint16_t)(slot + 1);", "after", 1),
        ("select_child_table", "selected-child-id", "RECORD_VISIT", (("{", 3),),
         "uint16_t identifier = *child_identifier(level, root_index, entry_index);", "before", 1),
        ("select_child_table", "child-physical-record", "INDEX_PROBE", (),
         "if (kernel_address_space.table_frames[selected] != decoded.physical_address)", "before", 1),
        ("map_leaf", "mapping-parent-record", "INDEX_PROBE",
         (("for (level = 2; level >= (int)leaf_level; --level) {", 0), ("{", 1)),
         "struct micros_kernel_page_table *table = table_at(kernel_address_space.table_frames[table_index]);", "before", 1),
        ("map_leaf", "leaf-pte-store", "PTE_VISIT",
         (("for (level = 2; level >= (int)leaf_level; --level) {", 0), ("{", 1),
          ("if (level == (int)leaf_level) {", 0)),
         "table->entries[index] = leaf_pte;", "after", 1),
    ),
    "kernel/memory/address_space_validation.c": (
        ("micros_kernel_address_space_table_frame_is_valid", "selected-range", "RANGE_VISIT", (),
         "range = &allocator->managed_ranges[range_index];", "before", 1),
        ("micros_kernel_address_space_table_frame_is_valid", "selected-snapshot", "RECORD_VISIT", (),
         "snapshot = &ownership->managed_ranges[range_index];", "before", 1),
        ("micros_kernel_address_space_table_frame_is_valid", "selected-owner", "RECORD_VISIT", (),
         "owner = ownership->owners[frame_index];", "before", 1),
        ("micros_kernel_address_space_table_frame_is_valid", "selected-handoff-target", "RECORD_VISIT", (),
         "target = ownership->handoff_targets[frame_index];", "before", 1),
        ("micros_kernel_address_space_table_frame_is_valid", "selected-bitmap-word", "BITMAP_WORD_LOAD", (),
         "allocated_word = allocator->allocated_bitmap[frame_index / 64];", "before", 1),
        ("candidate_table_is_valid", "validated-table-record", "INDEX_PROBE", (),
         "physical_address = state->table_frames[index];", "before", 1),
        ("candidate_table_is_valid", "validated-range-hint", "RECORD_VISIT", (),
         "range_index = state->table_range_indices[index];", "before", 1),
        ("child_index_from_entry", "child-physical-record", "INDEX_PROBE", (),
         "if (state->table_frames[selected] != physical_address)", "before", 1),
        ("middle_table_is_valid", "middle-child-record", "RECORD_VISIT",
         (("for (middle_index = 0; middle_index < MICROS_SV39_TABLE_ENTRY_COUNT; ++middle_index) {", 0),),
         "uint16_t identifier = state->level0_table_ids[root_index][middle_index];", "before", 1),
        ("table_tree_is_valid", "root-physical-record", "INDEX_PROBE", (),
         "if (state->root_physical_address != state->table_frames[0])", "before", 1),
        ("table_tree_is_valid", "root-child-record", "RECORD_VISIT",
         (("for (root_index = 0; root_index < MICROS_SV39_TABLE_ENTRY_COUNT; ++root_index) {", 0),),
         "uint16_t identifier = state->level1_table_ids[root_index];", "before", 1),
        ("lookup_mapping_details", "lookup-table-record", "INDEX_PROBE",
         (("for (level = 2; level >= 0; --level) {", 0),),
         "physical = state->table_frames[table_index];", "before", 1),
        ("lookup_mapping_details", "lookup-child-record", "RECORD_VISIT",
         (("for (level = 2; level >= 0; --level) {", 0),),
         "identifier = level == 2 ? state->level1_table_ids[root_index] : state->level0_table_ids[root_index][index];", "before", 1),
    ),
}


@lru_cache(maxsize=256)
def baseline_source(root, path):
    allowed = {
        *RECIPE_ADMISSION["native_source_members"],
        *SOURCE_PROJECTION["retained_source_sha256"],
        "tests/host/bootstrap_allocation_source.py",
    }
    if path not in allowed:
        raise ValueError("MEM02 has no fixed source binding for this path")
    result = subprocess.run(
        ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + path),
        cwd=root, stdin=subprocess.DEVNULL, capture_output=True, check=False, timeout=30,
    )
    expected = SOURCE_PROJECTION["retained_source_sha256"].get(path)
    if result.returncode or expected is not None and hashlib.sha256(result.stdout).hexdigest() != expected:
        raise ValueError("MEM02 fixed source differs from its independent declaration")
    return result.stdout.decode("utf-8")


@lru_cache(maxsize=4)
def _allocator_observations(root):
    tree = ast.parse(baseline_source(root, "tests/host/bootstrap_allocation_source.py"))
    common = literal_value(single_assignment(tree, "COMMON_OBSERVATIONS").value)
    current = literal_value(single_assignment(tree, "GREEN_OBSERVATIONS").value)
    result = {}
    for path in ("kernel/memory/frame_allocator.c", "kernel/memory/frame_ownership.c"):
        selected = common[path]
        if path == "kernel/memory/frame_allocator.c":
            selected = (*selected, *current[path])
        result[path] = tuple(
            (function, site, kind, tuple((region, 0) for region in regions), primitive, "before", 1)
            for function, site, kind, regions, primitive in selected
        )
    return result


def current_function_names(root, path):
    from tests.host.c_source_records import function_records

    original = function_records(baseline_source(str(root), path))
    family = "constructor" if path.endswith("/address_space.c") else "validator"
    bindings = SOURCE_PROJECTION["current_bindings"]
    return tuple(sorted((set(original) - set(bindings[family + "_removed"])) | set(bindings[family + "_added"])))


def _current_calls():
    return {
        "allocated_table_range": {"micros_bootstrap_frame_allocator", "micros_frame_ownership_runtime_ledger",
                                  "micros_kernel_address_space_table_frame_is_valid"},
        "selected_table_is_valid": {"micros_bootstrap_frame_allocator", "micros_frame_ownership_runtime_ledger",
                                   "micros_kernel_address_space_table_frame_is_valid"},
        "child_identifier": set(),
        "create_child_table": {"child_identifier", "allocate_table", "micros_sv39_make_table_pte"},
        "select_child_table": {"child_identifier", "create_child_table"},
        "map_leaf": {"micros_sv39_vpn_index", "selected_table_is_valid", "table_at", "micros_sv39_decode_pte",
                     "child_identifier", "micros_sv39_make_leaf_pte", "select_child_table"},
        "leaf_level_for_range": set(),
        "allocate_table": {"micros_frame_owner_make_kernel", "micros_frame_ownership_runtime_allocate",
                           "micros_sv39_make_table_pte", "micros_frame_ownership_runtime_release",
                           "allocated_table_range", "table_at"},
        "map_identity_range": {"micros_sv39_virtual_address_is_canonical", "leaf_level_for_range", "map_leaf"},
        "micros_kernel_address_space_table_frame_is_valid": {
            "micros_bootstrap_frame_allocator", "micros_frame_ownership_runtime_ledger", "frame_owners_are_equal",
        },
        "candidate_table_is_valid": {"micros_kernel_address_space_table_frame_is_valid"},
        "kernel_leaf_is_valid": set(),
        "child_index_from_entry": set(),
        "mark_child_table": {"child_index_from_entry", "mark_reachable_table"},
        "lookup_mapping_details": {"micros_sv39_vpn_index", "table_at", "micros_sv39_decode_pte",
                                   "kernel_leaf_is_valid", "child_index_from_entry"},
        "range_leaf_level": set(),
        "allocated_table_set_is_exact": {"allocator_allocated_bit_count", "candidate_table_is_valid"},
        "mark_reachable_table": {"candidate_table_is_valid", "micros_bootstrap_frame_allocator",
                                 "micros_frame_ownership_runtime_ledger"},
        "leaf_table_is_valid": {"micros_sv39_decode_pte", "kernel_leaf_is_valid"},
        "middle_table_is_valid": {"micros_sv39_decode_pte", "kernel_leaf_is_valid", "mark_child_table",
                                  "table_at", "leaf_table_is_valid"},
        "table_tree_is_valid": {"mark_reachable_table", "table_at", "micros_sv39_decode_pte",
                                "kernel_leaf_is_valid", "mark_child_table", "middle_table_is_valid"},
        "every_candidate_table_is_reachable": set(),
        "tree_and_table_ownership_are_valid": {
            "micros_frame_ownership_runtime_ledger", "validation_authority_is_valid",
            "allocated_table_set_is_exact", "table_tree_is_valid", "every_candidate_table_is_reachable",
        },
        "lookup_mapping": {"lookup_mapping_details"},
        "range_matches": {"lookup_mapping_details", "range_leaf_level"},
        "mapping_is_absent": {"lookup_mapping"},
        "fixed_mappings_are_valid": {"range_matches", "mapping_is_absent"},
        "managed_ranges_are_mapped": {"range_matches"},
        "table_frames_are_mapped": {"lookup_mapping"},
        "expected_mappings_are_valid": {"fixed_mappings_are_valid", "managed_ranges_are_mapped", "table_frames_are_mapped"},
        "micros_kernel_address_space_candidate_is_valid": {"tree_and_table_ownership_are_valid", "expected_mappings_are_valid"},
    }


def _source_skeleton(text, records):
    from tests.host.c_source_records import compact
    from tests.host.test_frame_ownership_source_layout import c_lexical_text

    for record in records.values():
        fragment = record.signature + "{" + record.body + "}"
        if text.count(fragment) != 1:
            raise ValueError("MEM02 has ambiguous source definition boundaries")
        text = text.replace(fragment, "", 1)
    return compact(c_lexical_text(text))


def validate_retained_source(root, path, text, stage):
    from tests.host.bootstrap_allocation_source import _function_calls
    from tests.host.c_source_records import compact, function_records
    from tests.host.test_frame_ownership_source_layout import c_lexical_text

    if stage not in (Stage.RED, Stage.GREEN) or type(text) is not str:
        raise ValueError("MEM02 source admission has an unknown stage or representation")
    original = baseline_source(str(root), path)
    if stage is Stage.RED or path not in SOURCE_ROLES["candidate_paths"][:3]:
        if text != original:
            from tests.host.selected_ownership_source import native_assertion_admission
            if stage is Stage.GREEN and "MEM02/native-input/" + path in native_assertion_admission(
                root, "MEM02", path, text, stage,
            ):
                return
            raise ValueError("MEM02 fixed or noncandidate native source changed: " + path)
        return
    bindings = SOURCE_PROJECTION["current_bindings"]
    if path.endswith(".h"):
        policy = bindings["private_header"]
        expected = original.replace(policy["array_anchor"], policy["array_additions"] + policy["array_anchor"], 1)
        expected = expected.replace(
            "struct micros_frame_allocator;",
            "struct micros_frame_allocator;\n" + policy["forward"] + "\n\n" + policy["helper"] + ";\n", 1,
        )
        if compact(c_lexical_text(text)) != compact(c_lexical_text(expected)):
            raise ValueError("MEM02 private header changed beyond its three arrays and declared helper")
        return
    before, after = function_records(original), function_records(text)
    if set(after) != set(current_function_names(root, path)):
        raise ValueError("MEM02 current source added or omitted an unadmitted helper")
    family = "constructor" if path.endswith("/address_space.c") else "validator"
    retained = set(SOURCE_PROJECTION["retained_" + family])
    changed_signatures = {
        "mark_reachable_table", "middle_table_is_valid", "range_matches",
        "fixed_mappings_are_valid", "managed_ranges_are_mapped",
    }
    calls = _current_calls()
    for name, record in after.items():
        if record.guards:
            raise ValueError("MEM02 cannot hide a mapping helper behind a compile guard")
        if name in before and name not in changed_signatures and compact(record.signature) != compact(before[name].signature):
            raise ValueError("MEM02 changed an unrelated original signature: " + name)
        if name in retained:
            if compact(c_lexical_text(record.body)) != compact(c_lexical_text(before[name].body)):
                raise ValueError("MEM02 changed a retained scan/publication body: " + name)
        elif _function_calls(record.body) != calls[name]:
            raise ValueError("MEM02 mapping helper escaped its closed call set: " + name)
        if name in bindings[family + "_added"] and (
            name != "micros_kernel_address_space_table_frame_is_valid"
            and not compact(record.signature).startswith("static")
        ):
            raise ValueError("MEM02 exposed another private mapping helper")
    skeleton = text
    if family == "validator":
        addition = '#include "micros/bootstrap_memory.h"\n'
        if skeleton.count(addition) != 1:
            raise ValueError("MEM02 selected-frame helper lost its exact allocator getter include")
        skeleton = skeleton.replace(addition, "", 1)
    if _source_skeleton(skeleton, after) != _source_skeleton(original, before):
        raise ValueError("MEM02 changed unrelated include, type or storage ownership")


def observation_sites(root, path, stage):
    if stage not in (Stage.RED, Stage.GREEN) or path not in RECIPE_ADMISSION["generated_subjects"]:
        raise ValueError("MEM02 observations have an unknown source/stage")
    mapping = BASELINE_OBSERVATIONS.get(path, ())
    if stage is Stage.GREEN:
        retained = {
            "allocate_table", "map_managed_memory", "allocator_allocated_bit_count",
            "micros_kernel_address_space_allocator_is_pristine",
            "table_frames_are_mapped", "managed_ranges_are_mapped",
            "tree_and_table_ownership_are_valid",
        }
        mapping = (*tuple(row for row in mapping if row[0] in retained), *CURRENT_OBSERVATIONS.get(path, ()))
    result = (
        *_allocator_observations(str(root)).get(path, ()),
        *COMMON_OBSERVATIONS.get(path, ()),
        *mapping,
    )
    from tests.host.selected_ownership_source import legacy_observation_sites
    result = legacy_observation_sites(root, "MEM02", path, stage, result)
    keys = [(row[0], row[1]) for row in result]
    if len(keys) != len(set(keys)) or any(
        kind not in STATE_WORK_CONTRACT["work"]["kinds"]
        for _, _, kind, _, _, _, _ in result
    ):
        raise ValueError("MEM02 observation ownership is ambiguous")
    return result


def _observation_call(site):
    return (
        'MICROS_TEST_WORK_HIT("' + site[1] + '", MICROS_WORK_OWNER_KERNEL_MEMORY, '
        + "MICROS_WORK_KIND_" + site[2] + ", " + str(site[6]) + ");"
    )


def _observation_positions(root, path, text, stage):
    from tests.host.c_source_records import tokens
    from tests.host.work_count_lexical import compound_layout, function_spans, matches_at, positional_tokens
    from tests.host.work_count_projection import resolve_regions
    from tests.host.work_count_types import Region

    spans = function_spans(text)
    result = []
    for site in observation_sites(root, path, stage):
        function, identifier, kind, regions, primitive, placement, units = site
        if function not in spans:
            raise ValueError("MEM02 observation function is absent: " + function)
        if placement not in ("before", "after", "entry") or type(units) is not int or units < 1:
            raise ValueError("MEM02 observation has an unadmitted placement/unit")
        if units != 1 and (kind != "CLEARED_BYTE" or units not in (2, 8)):
            raise ValueError("MEM02 observation units are not an actual clear width")
        span = spans[function]
        if placement == "entry":
            if kind != "COMPLETE_VALIDATOR_CALL" or regions or primitive:
                raise ValueError("MEM02 function-entry observation has another primitive")
            result.append((site, span.body_start))
            continue
        body = positional_tokens(text[span.body_start:span.body_end])
        values = tuple(item.value for item in body)
        stacks, closings = compound_layout(values)
        start, end, enclosing = resolve_regions(
            values, stacks, closings, tuple(Region(*region) for region in regions),
        )
        fragment = tokens(primitive)
        selected = [
            index for index in matches_at(values, fragment, start, end)
            if stacks[index] == enclosing and (index == 0 or values[index - 1] in (";", "{", "}"))
        ]
        if len(selected) != 1:
            raise ValueError("MEM02 primitive occurrence is missing or ambiguous: " + identifier)
        token = body[selected[0] + len(fragment) - 1] if placement == "after" else body[selected[0]]
        if placement == "after" and fragment[-1] != ";":
            raise ValueError("MEM02 completed-store observation lacks its statement boundary")
        result.append((site, span.body_start + (token.end if placement == "after" else token.start)))
    return tuple(result)


def _render_observations(root, path, text, stage):
    edits = {}
    for site, position in _observation_positions(root, path, text, stage):
        edits.setdefault(position, []).append(_observation_call(site))
    for position in sorted(edits, reverse=True):
        text = text[:position] + "".join(edits[position]) + text[position:]
    return OBSERVATION_PREFIX + text


def observe_source(root, path, text, stage):
    validate_retained_source(root, path, text, stage)
    return _render_observations(root, path, text, stage)


def project_observations(root, path, text, stage):
    if not text.startswith(OBSERVATION_PREFIX):
        raise ValueError("MEM02 observation preamble is not its native-only declaration")
    original = text[len(OBSERVATION_PREFIX):]
    for site in observation_sites(root, path, stage):
        call = _observation_call(site)
        if original.count(call) != 1:
            raise ValueError("MEM02 observation is missing, changed or duplicated")
        original = original.replace(call, "", 1)
    expected = baseline_source(str(root), path) if stage is Stage.RED else regular_source(Path(root), path).read_text()
    if original != expected:
        raise ValueError("MEM02 observations changed the actual selected source")
    validate_retained_source(root, path, original, stage)
    if _render_observations(root, path, original, stage) != text:
        raise ValueError("MEM02 observation moved away from its actual primitive/compound")
    return original


def project_mmu_source(path, text):
    if path not in RECIPE_ADMISSION["qemu_fixture_paths"]:
        return text
    expected = SOURCE_PROJECTION["retained_source_sha256"][path]
    if hashlib.sha256(text.encode()).hexdigest() == expected:
        return text
    binding = SOURCE_PROJECTION["mmu_fixture_inputs"]
    kind = "assembly" if path.endswith(".S") else "c"
    if hashlib.sha256(text.encode()).hexdigest() != binding[kind + "_sha256"]:
        raise ValueError("MEM02 target fixture differs from its pre-production input declaration")
    if kind == "assembly":
        suffix = binding["assembly_extension"]
        if not text.endswith(suffix) or hashlib.sha256(suffix.encode()).hexdigest() != binding["assembly_extension_sha256"]:
            raise ValueError("MEM02 assembly fixture is not its exact load-probe extension")
        restored = text[:-len(suffix)]
    else:
        marker = "\nenum mapping_test_phase {"
        if text.count(marker) != 1:
            raise ValueError("MEM02 target fixture has an ambiguous extension boundary")
        prefix, suffix = text.split(marker)
        if hashlib.sha256((marker + suffix).encode()).hexdigest() != binding["c_extension_sha256"]:
            raise ValueError("MEM02 target fixture changed an independently frozen probe")
        for key in ("c_includes", "c_trap_hook"):
            if prefix.count(binding[key]) != 1:
                raise ValueError("MEM02 target fixture lost its finite original-scenario hook")
            prefix = prefix.replace(binding[key], "", 1)
        if prefix.count("    return mapping_test_run();\n") != 1:
            raise ValueError("MEM02 target fixture changed its original two-fault completion")
        restored = prefix.replace("    return mapping_test_run();\n", "    return true;\n", 1)
    if hashlib.sha256(restored.encode()).hexdigest() != expected:
        raise ValueError("MEM02 target fixture did not recover its complete original bytes")
    return restored


def project_mmu_source_bytes(path, data):
    if path not in RECIPE_ADMISSION["qemu_fixture_paths"]:
        return data
    return project_mmu_source(path, data.decode()).encode()


def extend_mmu_assembly(text):
    path = "arch/riscv64/mmu_test.S"
    if hashlib.sha256(text.encode()).hexdigest() != SOURCE_PROJECTION["retained_source_sha256"][path]:
        raise ValueError("MEM02 assembly reference is not the fixed original")
    result = text + SOURCE_PROJECTION["mmu_fixture_inputs"]["assembly_extension"]
    if project_mmu_source(path, result) != text:
        raise ValueError("MEM02 assembly reference extension is not reversible")
    return result


def _gate_changes(path):
    previous = "from tests.host.bootstrap_allocation_authority import identity_required as diagnostic_identity_required\n"
    current = "from tests.host.kernel_mapping_authority import identity_required as diagnostic_identity_required\n"
    if path == "tests/host/test_kernel_directory_layout.py":
        return ((previous, current),)
    if path != "tests/host/kernel_layout_source.py":
        return ()
    original = "    raw = project_terminal_source_bytes(KERNEL_LOGICAL_TO_PHYSICAL[logical], selected.read_bytes())\n"
    extended = original + "    raw = project_mmu_source_bytes(KERNEL_LOGICAL_TO_PHYSICAL[logical], raw)\n"
    return (
        (previous, current),
        ("", "from tests.host.kernel_mapping_source import project_mmu_source_bytes\n"),
        (original, extended),
    )


def project_gate_adapter(path, text):
    from tests.host.selected_ownership_source import project_legacy_adapter
    from tests.host.work_count_recipe import project_feedback_reader

    text = project_legacy_adapter(path, text)
    text = project_feedback_reader(path, text)
    changes = _gate_changes(path)
    present = tuple(text.count(extended) for _, extended in changes)
    if not any(present):
        return text
    if any(count != 1 for count in present):
        raise ValueError("MEM02 MMU assertion adapter is incomplete or duplicated")
    for original, extended in changes:
        text = text.replace(extended, original, 1)
    return text


def native_recipe():
    name = RECIPE_ADMISSION["native_target"]
    return (
        '    set(MEM02_NATIVE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/kernel-mapping")\n'
        "    set(MEM02_NATIVE_INPUTS\n"
        + "".join("        " + path + "\n" for path in RECIPE_ADMISSION["native_source_members"])
        + "    )\n"
        "    set(MEM02_NATIVE_OUTPUTS)\n"
        "    foreach(mem02_source IN LISTS MEM02_NATIVE_INPUTS)\n"
        '        list(APPEND MEM02_NATIVE_OUTPUTS "${MEM02_NATIVE_ROOT}/${mem02_source}")\n'
        "    endforeach()\n"
        "    file(GLOB_RECURSE MEM02_NATIVE_HEADERS\n"
        '        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}/arch/*.h"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}/kernel/*.h"\n'
        "    )\n"
        "    add_custom_command(\n"
        "        OUTPUT ${MEM02_NATIVE_OUTPUTS}\n"
        '        COMMAND "${MICROS_PYTHON}" ${MICROS_PYTHON_ARGS}\n'
        '            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_kernel_mapping_fixture.py"\n'
        "        DEPENDS ${MEM02_NATIVE_INPUTS} ${MEM02_NATIVE_HEADERS}\n"
        "            tools/generate_kernel_mapping_fixture.py\n"
        "            tests/host/kernel_mapping_source.py\n"
        "            tests/host/kernel_mapping_test.h\n"
        '        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"\n'
        "        VERBATIM\n"
        "    )\n"
        "    add_executable(" + name + " ${MEM02_NATIVE_OUTPUTS}\n"
        "        tests/host/kernel_mapping_test.c\n"
        "        tests/work/work_counts.c\n"
        "    )\n"
        "    target_include_directories(" + name + " PRIVATE\n"
        '        "${MEM02_NATIVE_ROOT}" "${MEM02_NATIVE_ROOT}/include"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include")\n'
        "    target_compile_features(" + name + " PRIVATE c_std_17)\n"
        "    target_compile_options(" + name
        + " PRIVATE -Wall -Wextra -Werror -Wpedantic -Wmissing-prototypes -Wstrict-prototypes -fno-builtin)\n"
        '    if(MICROS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")\n'
        "        target_compile_options(" + name + " PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)\n"
        "        target_link_options(" + name + " PRIVATE -fsanitize=address,undefined)\n"
        "    endif()\n"
        "    target_compile_definitions(" + name + " PRIVATE "
        + " ".join(RECIPE_ADMISSION["native_definitions"]) + ")\n"
        "    add_test(NAME kernel-mapping COMMAND " + name + ")\n"
        + "".join("    add_dependencies(" + tier + " " + name + ")\n" for tier in RECIPE_ADMISSION["native_tiers"])
        + "\n"
    )


def mmu_recipe_extension():
    patterns = (
        "MICROS_FRAME_OWNERSHIP_READY owned=0x[0-9a-f]{16} kernel-tables=0x[0-9a-f]{16} phase=bootstrap",
        "MICROS_KERNEL_MAPPING_OBSERVATION version=1 tables=0x[0-9a-f]{16} leaves-4k=0x[0-9a-f]{16} "
        "leaves-2m=0x[0-9a-f]{16} leaves-1g=0x[0-9a-f]{16} largest=1",
        "MICROS_KERNEL_MAPPING_GUARDS aliases=2m,1g misaligned=rejected resources=preserved",
        STATE_WORK_CONTRACT["target"]["pass"],
        "MICROS_MMU_TEST_PASS store-fault=text execute-fault=writable traps=0x0000000000000002",
        "MICROS_TTY_CONTROLLER_TEST_PASS mapping=exact priority=disabled enable=disabled context=supervisor seie=independent",
    )
    return (
        "            --memory\n            128M\n"
        "            --memory\n            256M\n"
        "            --marker\n"
        '            "' + STATE_WORK_CONTRACT["target"]["pass"] + '"\n'
        + "".join('            --ordered-pattern\n            "' + pattern + '"\n' for pattern in patterns)
    )


def extend_cmake(text):
    anchor = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
    mmu = "            --require-mmu-test-report\n"
    if hashlib.sha256(text.encode()).hexdigest() != RECIPE_ADMISSION["reference_cmake_sha256"]:
        raise ValueError("MEM02 recipe lacks its complete fixed predecessor")
    if text.count(anchor) != 1 or text.count(mmu) != 1:
        raise ValueError("MEM02 recipe has an ambiguous native/MMU anchor")
    return text.replace(anchor, native_recipe() + anchor, 1).replace(mmu, mmu + mmu_recipe_extension(), 1)


def project_cmake(text):
    from tests.host.selected_ownership_source import project_cmake as project_selected_cmake

    text = project_selected_cmake(text)
    from tests.host.work_count_recipe import project_host_test_commands

    text = project_host_test_commands(text)
    if not any(marker in text for marker in ("MEM02_NATIVE", "kernel_mapping", "MICROS_KERNEL_MAPPING")):
        return text
    addition = native_recipe()
    anchor = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
    mmu = "            --require-mmu-test-report\n"
    extension = mmu_recipe_extension()
    if text.count(addition + anchor) != 1 or text.count(mmu + extension) != 1:
        raise ValueError("MEM02 native/MMU recipe is changed, partial or relocated")
    original = text.replace(addition, "", 1).replace(mmu + extension, mmu, 1)
    if hashlib.sha256(original.encode()).hexdigest() != RECIPE_ADMISSION["reference_cmake_sha256"]:
        raise ValueError("MEM02 recipe changed unrelated predecessor bytes")
    return original


def validate_scaffold(root):
    from tests.host.ast_evidence import ast_dump

    root = Path(root)
    cmake = regular_source(root, "CMakeLists.txt").read_text()
    if project_cmake(cmake) == cmake:
        raise ValueError("MEM02 native/MMU recipe integration is absent")
    for path in SOURCE_ROLES["candidate_paths"][-2:]:
        current = regular_source(root, path).read_text()
        expected = subprocess.check_output(
            ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + path),
            cwd=root, text=True, timeout=30,
        )
        if current == expected or ast_dump(ast.parse(project_gate_adapter(path, current))) != ast_dump(ast.parse(expected)):
            raise ValueError("MEM02 assertion adapter is absent or altered unrelated policy")
    for path in RECIPE_ADMISSION["qemu_fixture_paths"]:
        text = regular_source(root, path).read_text()
        if project_mmu_source(path, text) == text:
            raise ValueError("MEM02 target fixture extension is absent")
