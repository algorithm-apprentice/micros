"""Closed MEM04 source, native binding, recipe and primitive declarations."""


from tests.host.c_source_records import function_records
from tests.host.work_count_types import Stage


RECIPE_ADMISSION = {
    "native_source_members": (
    "kernel/memory/frame_allocator.c",
    "kernel/memory/bootstrap_memory.c",
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
    "kernel/objects/kernel_object_runtime.c",
    "kernel/scheduler/scheduler_core.c",
    "kernel/scheduler/scheduler_core_queue.c",
    "kernel/scheduler/scheduler_core_validate.c",
    "kernel/scheduler/scheduler_core_policy.c",
    "kernel/scheduler/scheduler_core_ipc.c",
    "kernel/scheduler/scheduler_core_ipc_transition.c",
    "kernel/scheduler/scheduler_core_return_plan.c",
    "kernel/scheduler/scheduler_core_return.c",
    "kernel/scheduler/scheduler_core_accounting.c",
    "kernel/scheduler/scheduler_context.c",
    "kernel/endpoint/endpoint.c",
    "kernel/endpoint/endpoint_codec.c",
    "kernel/endpoint/endpoint_identity.c",
    "kernel/endpoint/endpoint_registry.c",
    "kernel/endpoint/endpoint_publication.c",
    "kernel/endpoint/endpoint_privilege.c",
    "kernel/endpoint/endpoint_completion_validation.c",
    "kernel/endpoint/endpoint_queue_validation.c",
    "kernel/endpoint/endpoint_object_validation.c",
    "kernel/ipc/ipc_runtime.c",
    "kernel/memory/sv39.c",
    "kernel/memory/address_space.c",
    "kernel/memory/user_address_space.c",
    "kernel/memory/user_address_space_core.c",
    "kernel/memory/user_address_space_table.c",
    "kernel/memory/user_address_space_runtime.c",
    "kernel/memory/user_address_space_mapping.c",
    "kernel/memory/user_address_space_validation.c",
    "kernel/memory/user_address_space_mutation.c",
    "kernel/memory/user_address_space_activation.c",
    "kernel/vm/vm_handoff_core.c",
    "kernel/vm/vm_handoff_boot.c",
    "kernel/vm/vm_handoff_runtime.c",
    "kernel/tty/tty_handoff_core.c",
    "kernel/tty/tty_handoff_runtime_state.c",
    "kernel/tty/tty_handoff_runtime_validate.c",
    "kernel/boot/bootstrap_control_core.c",
    ),
    "base_cmake_sha256": "9dfda2f310dfeb7cc9fd97f16b50935b56b2a802a83a89cfa3644a47ae811feb",
    "legacy_cmake_sha256": "528fd82f7df2fd8d49188db24362def4793e94d84f096e38bb30fe0d0dcc2746",
    "native_targets": (
        "selected_address_space_aggregate_test", "selected_address_space_path_test",
        "selected_address_space_context_test", "selected_address_space_ordinary_test",
    ),
    "native_case": "selected-address-space",
    "native_tiers": ("test-unit",),
    "native_label": "slow",
    "native_standard": "c17",
    "native_sanitizers": ("address", "undefined"),
    "source_pool": "mem03_source_proof",
    "generator": "tools/generate_selected_address_space_fixture.py",
    "generator_output": "build/host-debug/selected-address-space",
    "driver": "tools/check_selected_address_space.py",
    "fixture_header": "tests/host/selected_address_space_test.h",
    "fixture_sources": ("tests/host/selected_address_space_test.c", "tests/work/work_counts.c"),
    "baseline_headers": "fixed-runtime-reference",
    "current_headers": "actual-current-source",
    "ordinary_observations": "absent",
}
NATIVE_SOURCE_MEMBERS = RECIPE_ADMISSION["native_source_members"]
OBSERVED_HEADERS = ("kernel/boot/bootstrap_control_internal.h",)


STATE_WORK_CONTRACT = {
    "version": 1,
    "dimensions": (27, 14),
    "owner_mask": 11388,
    "channels": ("aggregate", "path", "context"),
    "producer_base": 5567941383739670528,
    "current_producer_offset": 256,
    "red_queries": 523,
    "green_queries": 529,
    "current_binding_guards": 30,
    "query_inputs_sha256": "2370689a52246a161868f3c78bbb8c276eb837186fcbca9073e71352d393dbae",
    "point_pte_reads": 3,
    "point_owner_reads": 4,
    "root_pte_reads": "1024+3*s",
    "root_owner_reads": "3+4*s",
    "controller_slots": 2,
    "binding_inspections": 1,
    "scratch_stores": 0,
    "complete_audits": 0,
    "owner_kinds": 8,
    "owner_geometry": "(proofs+1)*(8*g+2*k)",
    "owner_index_work": "(proofs+1)*(64*g+32*k)",
    "root_index_allowance": "512+3*s",
    "vm_header_work": 128,
    "tty_header_work": 64,
    "path_bytes": 128,
    "binding_bytes": 128,
    "rollback_cursor": 64,
    "first_free_frame": 128,
    "allocation_prefix_frames": (3, 2, 1),
    "target_workflows": ("test-qemu-user-address-space", "test-qemu-address-space-handoff"),
    "preservation": (
        "full-diagnostic-bodies-and-20-retained-caller-rows",
        "selected-owner-before-dereference-and-scalar-ancestry",
        "exact-allocator-ledger-object-pte-scratch-controller-and-output-state",
        "irq-restoration-and-full-same-root-hardware-commit",
        "VM-TTY-command-authority-and-prepared-address-space-seals",
        "actual-prior-memory-evidence-and-unchanged-collector-ABI",
    ),
}


BOOTSTRAP_FUNCTIONS = (
    "micros_bootstrap_frame_allocator", "ownership_mutation_is_allowed",
    "owner_is_process_bound", "validate_process_owners",
    "micros_frame_ownership_runtime_initialize", "micros_frame_ownership_runtime_ledger",
    "micros_frame_ownership_runtime_allocate", "micros_frame_ownership_runtime_release",
    "micros_frame_ownership_runtime_release_process_set",
    "micros_frame_ownership_runtime_validate", "micros_frame_ownership_runtime_release_process",
    "micros_frame_ownership_runtime_complete_handoff",
    "micros_frame_ownership_runtime_validate_metadata", "validate_active_process_owners",
)

NATIVE_PREFIX = '#include "tests/host/selected_address_space_test.h"\n'
CHANNELS = ("aggregate", "path", "context", "ordinary")
OWNER_MASK = 11388


def definitions(text, names):
    records = function_records(text)
    if len(set(names)) != len(names) or not set(names) <= set(records):
        raise ValueError("MEM04 native closure is missing a real source definition")
    return (
        "".join(records[name].signature + ";\n" for name in names)
        + "".join(records[name].signature + "{" + records[name].body + "}\n" for name in names)
    )


def replace_body(text, name, body):
    record = function_records(text)[name]
    original = record.signature + "{" + record.body + "}"
    if text.count(original) != 1:
        raise ValueError("MEM04 native binding has ambiguous source boundaries")
    return text.replace(original, record.signature + "{\n" + body + "\n}", 1)


def bind_native(path, text, stage=Stage.GREEN):
    if stage is not Stage.GREEN:
        raise ValueError("address-space native bindings require current source")
    prefix = NATIVE_PREFIX
    if path == "arch/riscv64/interrupt.h":
        for name, body in (
            ("riscv_irq_save", "    return micros_as_native_irq_save();"),
            ("riscv_irq_restore", "    micros_as_native_irq_restore(status);"),
            ("riscv_irq_is_enabled", "    return micros_as_native_irq_enabled();"),
        ):
            text = replace_body(text, name, body)
        return prefix + text
    if path == "kernel/memory/bootstrap_memory.c":
        return (
            prefix + '#include "micros/kernel_object_runtime.h"\n#include <string.h>\n'
            "#define riscv_irq_is_enabled micros_as_native_irq_enabled\n"
            "static struct micros_frame_allocator bootstrap_allocator;\n"
            "static struct micros_frame_ownership bootstrap_ownership;\n"
            "static bool ownership_runtime_ready;\n"
            + definitions(text, BOOTSTRAP_FUNCTIONS)
            + "\nstruct micros_frame_allocator *micros_as_native_allocator(void)\n"
            "{ return &bootstrap_allocator; }\n"
            "struct micros_frame_ownership *micros_as_native_ownership(void)\n"
            "{ return &bootstrap_ownership; }\n"
            "void micros_as_native_bootstrap_reset(void)\n{\n"
            "    memset(&bootstrap_allocator, 0, sizeof(bootstrap_allocator));\n"
            "    memset(&bootstrap_ownership, 0, sizeof(bootstrap_ownership));\n"
            "    ownership_runtime_ready = false;\n}\n"
        )
    if path == "kernel/objects/kernel_object_runtime.c":
        return prefix + "#include <string.h>\n" + text + (
            "\nvoid micros_as_native_objects_reset(void)\n{\n"
            "    memset(&kernel_objects, 0, sizeof(kernel_objects));\n"
            "    memset(&boot_hart_handle, 0, sizeof(boot_hart_handle));\n"
            "    runtime_ready = false;\n}\n"
        )
    if path == "kernel/memory/address_space.c":
        return (
            prefix + '#include "kernel/memory/address_space_internal.h"\n#include <string.h>\n'
            "static struct micros_kernel_address_space_private_state kernel_address_space;\n"
            + definitions(text, ("micros_kernel_address_space_report",))
            + "\nvoid micros_as_native_kernel_reset(void)\n"
            "{ memset(&kernel_address_space, 0, sizeof(kernel_address_space)); }\n"
            "void micros_as_native_kernel_bind(const struct micros_kernel_address_space_report *report)\n"
            "{ kernel_address_space.report = *report; kernel_address_space.initialized = true; }\n"
        )
    if path == "kernel/ipc/ipc_runtime.c":
        return (
            prefix + '#include "micros/ipc_runtime.h"\n#include "kernel/ipc/ipc_runtime_internal.h"\n'
            "#include <string.h>\n"
            "static struct micros_endpoint_registry authoritative_registry;\n"
            "static bool runtime_ready;\n"
            + definitions(text, ("micros_ipc_runtime_registry", "micros_ipc_runtime_authoritative_registry"))
            + "\nvoid micros_as_native_ipc_reset(void)\n"
            "{ memset(&authoritative_registry, 0, sizeof(authoritative_registry)); runtime_ready = false; }\n"
            "struct micros_endpoint_registry *micros_as_native_ipc_input(void)\n"
            "{ return &authoritative_registry; }\n"
            "void micros_as_native_ipc_publish(void)\n"
            "{ runtime_ready = true; }\n"
        )
    if path == "kernel/vm/vm_handoff_runtime.c":
        return (
            prefix + '#include "kernel/vm/vm_handoff_runtime.h"\n#include <string.h>\n'
            "static struct micros_vm_handoff_state handoff_state;\n"
            + definitions(text, (
                "micros_vm_handoff_runtime_state", "micros_vm_handoff_runtime_authoritative_state",
                "micros_vm_handoff_runtime_scratch_authority",
                "micros_vm_handoff_runtime_inspect_scratch_binding",
            ))
            + "\nvoid micros_as_native_vm_reset(void)\n"
            "{ memset(&handoff_state, 0, sizeof(handoff_state)); }\n"
            "struct micros_vm_handoff_state *micros_as_native_vm_input(void)\n"
            "{ return &handoff_state; }\n"
        )
    if path == "kernel/scheduler/scheduler_context.c":
        return (
            prefix + '#include "kernel/scheduler/scheduler_runtime_internal.h"\n'
            '#include "micros/user_address_space.h"\n'
            + definitions(text, ("scheduler_runtime_validate_selected_thread",))
        )
    if path == "kernel/boot/bootstrap_control_core.c":
        return (
            prefix + '#include "kernel/boot/bootstrap_control_internal.h"\n'
            + definitions(text, ("micros_bootstrap_control_find_binding",))
        )
    if path == "kernel/memory/user_address_space_runtime.c":
        return prefix + replace_body(text, "read_satp", "    return micros_as_native_read_satp();") + (
            "\nconst uint64_t *micros_as_native_scratch_bitmap(bool release)\n"
            "{ return release ? release_selection.bitmap : reachable_selection.bitmap; }\n"
            "bool micros_as_native_scratch_busy(void)\n"
            "{ return scratch_mode != MICROS_USER_ADDRESS_SPACE_SCRATCH_IDLE; }\n"
            "const struct micros_frame_selection *micros_as_native_scratch_selection(bool release)\n"
            "{ return release ? &release_selection : &reachable_selection; }\n"
            "uint8_t micros_as_native_scratch_mode(void)\n"
            "{ return scratch_mode; }\n"
        )
    return text
