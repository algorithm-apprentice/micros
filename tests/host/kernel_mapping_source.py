"""Current native build, case and work contracts."""

import hashlib


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
        "lib/vm/vm_keys.c",
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


# Narrow fixed MMU source/assembly binding consumed by the stack proof.
SOURCE_PROJECTION = {'retained_source_sha256': {'arch/riscv64/mmu_test.S': 'a0fcba06e038eb6b538dc541d2601f200a6e8385ca40e3740edde98c390425ab',
                            'kernel/memory/address_space.c': 'fd774c3074e762bf3b40d03db8371175ed3f9560f6fcc0511364a39b9b8ac696',
                            'kernel/memory/address_space_internal.h': '6ed1e61254bee6483849588cceec092456327e6388200e5e24cb26807e8466b9',
                            'kernel/memory/address_space_validation.c': 'd6b47319a1545463a3dacd121eea54d518f74f13956b356323b740229b9d13cc',
                            'kernel/tests/memory/mmu_test.c': '5553190f5f6834b693b2c62fab057d8b2ec6ad7b34c0853d47293779877bb1e8'},
 'mmu_fixture_inputs': {'c_sha256': 'f7b551459ec1e04691e124f5ede4d26765999897f802cd87a64edb5f253ffca6',
                        'c_extension_sha256': '4f33081a19855faff785a883a650a5ea65e0922e1a7bf52b5b720e54ce8a7b84',
                        'assembly_sha256': '7f93ff92da25ba82d0996964f9ae5bef1414b46152c9b744243f34570b9dbce8',
                        'assembly_extension_sha256': '7dcc37987a4425c3fe35ba69934343f24a19d530b447ec30cb82cfb4e1b1ba79',
                        'load_instructions': ('00053503', '00008067'),
                        'frame_bytes': 0,
                        'c_includes': '#include "arch/riscv64/interrupt.h"\n'
                                      '#include "arch/riscv64/mmu.h"\n'
                                      '#include "arch/riscv64/platform.h"\n'
                                      '#include "kernel/memory/address_space_internal.h"\n'
                                      '#include "micros/bootstrap_memory.h"\n'
                                      '#include "micros/frame_ownership_runtime.h"\n'
                                      '#include "micros/panic.h"\n'
                                      '\n'
                                      'static enum micros_mmu_test_trap_result '
                                      'mapping_test_handle_trap(struct micros_trap_frame *frame);\n'
                                      'static bool mapping_test_run(void);\n',
                        'c_trap_hook': '    enum micros_mmu_test_trap_result mapping_result = '
                                       'mapping_test_handle_trap(frame);\n'
                                       '\n'
                                       '    if (mapping_result != MICROS_MMU_TEST_TRAP_INACTIVE) {\n'
                                       '        return mapping_result;\n'
                                       '    }\n',
                        'assembly_extension': '\n'
                                              '.section .text, "ax", @progbits\n'
                                              '.option push\n'
                                              '.option norvc\n'
                                              '.balign 4\n'
                                              '.globl micros_mmu_large_load_test_trigger\n'
                                              '.type micros_mmu_large_load_test_trigger, @function\n'
                                              'micros_mmu_large_load_test_trigger:\n'
                                              '.globl micros_mmu_large_load_fault\n'
                                              'micros_mmu_large_load_fault:\n'
                                              '    ld a0, 0(a0)\n'
                                              '.globl micros_mmu_large_load_resume\n'
                                              'micros_mmu_large_load_resume:\n'
                                              '    ret\n'
                                              '.size micros_mmu_large_load_test_trigger, . - '
                                              'micros_mmu_large_load_test_trigger\n'
                                              '.option pop\n'}}


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
