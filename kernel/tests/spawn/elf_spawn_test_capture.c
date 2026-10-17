#include "kernel/tests/spawn/elf_spawn_test_internal.h"

#include "kernel/pm/pm_control_runtime.h"
#include "kernel/vm/vm_handoff_runtime.h"
#include "micros/bootstrap_memory.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"

static bool elf_spawn_frame_address(
    const struct micros_frame_ownership *ledger, uint64_t frame, uint64_t *address
)
{
    size_t index;

    for (index = 0; index < ledger->managed_range_count; ++index) {
        const struct micros_frame_range_snapshot *range = &ledger->managed_ranges[index];

        if (frame >= range->bitmap_offset
            && frame - range->bitmap_offset < range->frame_count) {
            *address = range->base + (frame - range->bitmap_offset) * MICROS_FRAME_SIZE;
            return true;
        }
    }
    return false;
}

static bool elf_spawn_capture_page_tables(struct micros_elf_spawn_kernel_resources *snapshot)
{
    const struct micros_frame_ownership *ledger = &snapshot->ownership;
    uint64_t frame;

    snapshot->table_count = 0;
    snapshot->reserved = 0;
    for (frame = 0; frame < ledger->managed_frame_count; ++frame) {
        if (ledger->owners[frame].kind == MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE) {
            struct micros_elf_spawn_page_table *table;
            uint64_t address;

            if (
                snapshot->table_count == MICROS_ELF_SPAWN_TABLE_CAPACITY
                || !elf_spawn_frame_address(ledger, frame, &address)
            ) {
                return false;
            }
            table = &snapshot->tables[snapshot->table_count++];
            table->physical_address = address;
            micros_elf_spawn_resource_copy(table->entries, (const void *)(uintptr_t)address, MICROS_FRAME_SIZE);
        }
    }
    return true;
}

bool elf_spawn_test_capture_kernel(struct micros_elf_spawn_kernel_resources *snapshot)
{
    const struct micros_kernel_objects *objects = micros_kernel_object_runtime_registry();
    const struct micros_endpoint_registry *endpoints = micros_ipc_runtime_registry();
    const struct micros_grant_registry *grants = micros_grant_runtime_registry();
    const struct micros_frame_allocator *allocator = micros_bootstrap_frame_allocator();
    const struct micros_frame_ownership *ownership = micros_frame_ownership_runtime_ledger();
    const struct micros_vm_handoff_state *vm = micros_vm_handoff_runtime_state();
    const struct micros_pm_control_state *pm = micros_pm_control_runtime_state();

    if (objects == NULL || endpoints == NULL || grants == NULL || allocator == NULL
        || ownership == NULL || vm == NULL || pm == NULL) {
        return false;
    }
    micros_elf_spawn_resource_copy(&snapshot->objects, objects, sizeof(*objects));
    micros_elf_spawn_resource_copy(&snapshot->endpoints, endpoints, sizeof(*endpoints));
    micros_elf_spawn_resource_copy(&snapshot->grants, grants, sizeof(*grants));
    micros_elf_spawn_resource_copy(&snapshot->allocator, allocator, sizeof(*allocator));
    micros_elf_spawn_resource_copy(&snapshot->ownership, ownership, sizeof(*ownership));
    micros_elf_spawn_resource_copy(&snapshot->vm, vm, sizeof(*vm));
    micros_elf_spawn_resource_copy(&snapshot->pm, pm, sizeof(*pm));
    return elf_spawn_capture_page_tables(snapshot);
}
