#include "kernel/address_space_handoff_test_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "kernel/grant/grant_runtime_internal.h"
#include "kernel/grant/grant_syscall.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/memory/user_address_space_internal.h"
#include "kernel/vm_snapshot.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_copy.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_buffer.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/sv39.h"
#include "micros/syscall_abi.h"
#include "micros/tty.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

bool handoff_test_record_managed_mapping(
    void *context,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint32_t permissions
)
{
    struct managed_inventory_context *inventory = context;

    (void)physical_address;
    (void)permissions;
    if (virtual_address == MICROS_TTY_UART_VIRTUAL_BASE) {
        inventory->saw_uart = true;
    }
    ++inventory->count;
    return true;
}

static bool handoff_test_snapshot_page_tables(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_kernel_objects *objects
)
{
    struct page_table {
        uint64_t entries[MICROS_SV39_TABLE_ENTRY_COUNT];
    };
    uint16_t root_index;
    uint16_t middle_index;
    size_t process_index;

    if (
        micros_sv39_vpn_index(
            TEST_CODE_ADDRESS,
            2,
            &root_index
        ) != MICROS_SV39_OK
        || micros_sv39_vpn_index(
            TEST_CODE_ADDRESS,
            1,
            &middle_index
        ) != MICROS_SV39_OK
    ) {
        return false;
    }
    for (
        process_index = 0;
        process_index < HANDOFF_PROCESS_COUNT;
        ++process_index
    ) {
        const struct micros_process *record =
            &objects->processes[fixture->processes[process_index].slot];
        struct page_table *root =
            (struct page_table *)(uintptr_t)record->address_space_root;
        struct micros_sv39_decoded_pte root_entry;
        struct micros_sv39_decoded_pte middle_entry;
        struct page_table *middle;
        struct page_table *leaf;

        if (
            record->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || record->generation
                != fixture->processes[process_index].generation
            || record->address_space_root == 0
            || micros_sv39_decode_pte(
                root->entries[root_index],
                &root_entry
            ) != MICROS_SV39_OK
            || root_entry.kind != MICROS_SV39_PTE_TABLE
        ) {
            return false;
        }
        middle = (struct page_table *)(uintptr_t)
            root_entry.physical_address;
        if (
            micros_sv39_decode_pte(
                middle->entries[middle_index],
                &middle_entry
            ) != MICROS_SV39_OK
            || middle_entry.kind != MICROS_SV39_PTE_TABLE
        ) {
            return false;
        }
        leaf = (struct page_table *)(uintptr_t)
            middle_entry.physical_address;
        handoff_test_copy_bytes(fixture,
            fixture->page_table_snapshot[process_index][0],
            root,
            MICROS_SV39_PAGE_SIZE
        );
        handoff_test_copy_bytes(fixture,
            fixture->page_table_snapshot[process_index][1],
            middle,
            MICROS_SV39_PAGE_SIZE
        );
        handoff_test_copy_bytes(fixture,
            fixture->page_table_snapshot[process_index][2],
            leaf,
            MICROS_SV39_PAGE_SIZE
        );
    }
    return true;
}

static bool handoff_test_page_tables_match(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_kernel_objects *objects
)
{
    handoff_test_copy_bytes(fixture,
        fixture->page_table_observed,
        fixture->page_table_snapshot,
        sizeof(fixture->page_table_observed)
    );
    if (!handoff_test_snapshot_page_tables(fixture, objects)) {
        return false;
    }
    if (!handoff_test_bytes_equal(fixture,
        fixture->page_table_observed,
        fixture->page_table_snapshot,
        sizeof(fixture->page_table_observed)
    )) {
        return false;
    }
    handoff_test_copy_bytes(fixture,
        fixture->page_table_snapshot,
        fixture->page_table_observed,
        sizeof(fixture->page_table_snapshot)
    );
    return true;
}

static void handoff_test_snapshot_user_bytes(
    struct micros_address_space_handoff_test_context *fixture
)
{
    size_t process_index;
    size_t page_index;

    for (
        process_index = 0;
        process_index < HANDOFF_PROCESS_COUNT;
        ++process_index
    ) {
        for (
            page_index = 0;
            page_index < HANDOFF_PAGE_COUNT;
            ++page_index
        ) {
            handoff_test_copy_bytes(fixture,
                fixture->user_bytes_snapshot[process_index][page_index],
                (const void *)(uintptr_t)
                    fixture->user_physical[process_index][page_index],
                MICROS_SV39_PAGE_SIZE
            );
        }
    }
}

static bool handoff_test_user_bytes_match(
    struct micros_address_space_handoff_test_context *fixture
)
{
    size_t process_index;
    size_t page_index;

    for (
        process_index = 0;
        process_index < HANDOFF_PROCESS_COUNT;
        ++process_index
    ) {
        for (
            page_index = 0;
            page_index < HANDOFF_PAGE_COUNT;
            ++page_index
        ) {
            handoff_test_copy_bytes(fixture,
                fixture->user_bytes_observed[process_index][page_index],
                (const void *)(uintptr_t)
                    fixture->user_physical[process_index][page_index],
                MICROS_SV39_PAGE_SIZE
            );
        }
    }
    return handoff_test_bytes_equal(fixture,
        fixture->user_bytes_snapshot,
        fixture->user_bytes_observed,
        sizeof(fixture->user_bytes_snapshot)
    );
}

bool handoff_test_snapshot_state(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_grant_registry *grant_registry
)
{
    if (
        ledger == NULL
        || ledger->allocator == NULL
        || objects == NULL
        || endpoint_registry == NULL
        || grant_registry == NULL
    ) {
        return false;
    }
    handoff_test_copy_bytes(fixture, &fixture->ownership_snapshot, ledger, sizeof(fixture->ownership_snapshot));
    handoff_test_copy_bytes(fixture,
        &fixture->allocator_snapshot,
        ledger->allocator,
        sizeof(fixture->allocator_snapshot)
    );
    handoff_test_copy_bytes(fixture, &fixture->objects_snapshot, objects, sizeof(fixture->objects_snapshot));
    handoff_test_copy_bytes(fixture,
        &fixture->endpoint_snapshot,
        endpoint_registry,
        sizeof(fixture->endpoint_snapshot)
    );
    handoff_test_copy_bytes(fixture,
        &fixture->grant_snapshot,
        grant_registry,
        sizeof(fixture->grant_snapshot)
    );
    if (!handoff_test_snapshot_page_tables(fixture, objects)) {
        return false;
    }
    handoff_test_snapshot_user_bytes(fixture);
    return true;
}

bool handoff_test_state_matches(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_grant_registry *grant_registry
)
{
    return (
        ledger != NULL
        && ledger->allocator != NULL
        && handoff_test_bytes_equal(fixture,
            &fixture->ownership_snapshot,
            ledger,
            sizeof(fixture->ownership_snapshot)
        )
        && handoff_test_bytes_equal(fixture,
            &fixture->allocator_snapshot,
            ledger->allocator,
            sizeof(fixture->allocator_snapshot)
        )
        && handoff_test_bytes_equal(fixture,
            &fixture->objects_snapshot,
            objects,
            sizeof(fixture->objects_snapshot)
        )
        && handoff_test_bytes_equal(fixture,
            &fixture->endpoint_snapshot,
            endpoint_registry,
            sizeof(fixture->endpoint_snapshot)
        )
        && handoff_test_bytes_equal(fixture,
            &fixture->grant_snapshot,
            grant_registry,
            sizeof(fixture->grant_snapshot)
        )
        && handoff_test_page_tables_match(fixture, objects)
        && handoff_test_user_bytes_match(fixture)
    );
}

static void handoff_test_normalize_syscall_objects(
    struct micros_address_space_handoff_test_context *fixture,
    struct micros_kernel_objects *objects
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        handoff_test_fill_bytes(fixture,
            &objects->threads[index].user_context,
            0,
            sizeof(objects->threads[index].user_context)
        );
        objects->threads[index].remaining_counter_ticks = 0;
    }
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        objects->harts[index].trap.entry_t0 = 0;
        objects->harts[index].trap.entry_t1 = 0;
        objects->harts[index].trap.entry_t2 = 0;
        objects->harts[index].accounting_owner =
            MICROS_SCHEDULER_ACCOUNTING_NONE;
        objects->harts[index].accounting_started_at = 0;
        objects->harts[index].accounted_thread =
            (struct micros_thread_handle){0};
        objects->harts[index].kernel_counter_ticks = 0;
        objects->harts[index].idle_counter_ticks = 0;
    }
}

bool handoff_test_snapshot_syscall_state(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_grant_registry *grant_registry
)
{
    size_t bitmap_words;

    if (
        ledger == NULL
        || ledger->allocator == NULL
        || objects == NULL
        || endpoint_registry == NULL
        || grant_registry == NULL
        || ledger->managed_frame_count
            > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
    ) {
        return false;
    }
    bitmap_words = (size_t)(
        (ledger->managed_frame_count + 63) / 64
    );
    handoff_test_fill_bytes(fixture,
        &fixture->ownership_snapshot,
        0,
        sizeof(fixture->ownership_snapshot)
    );
    handoff_test_copy_bytes(fixture,
        &fixture->ownership_snapshot,
        ledger,
        offsetof(struct micros_frame_ownership, owners)
    );
    handoff_test_copy_bytes(fixture,
        fixture->ownership_snapshot.owners,
        ledger->owners,
        (size_t)ledger->managed_frame_count
            * sizeof(ledger->owners[0])
    );
    handoff_test_copy_bytes(fixture,
        fixture->ownership_snapshot.handoff_targets,
        ledger->handoff_targets,
        (size_t)ledger->managed_frame_count
    );
    handoff_test_fill_bytes(fixture, &fixture->allocator_snapshot, 0, sizeof(fixture->allocator_snapshot));
    handoff_test_copy_bytes(fixture,
        &fixture->allocator_snapshot,
        ledger->allocator,
        offsetof(struct micros_frame_allocator, allocated_bitmap)
    );
    handoff_test_copy_bytes(fixture,
        fixture->allocator_snapshot.allocated_bitmap,
        ledger->allocator->allocated_bitmap,
        bitmap_words
            * sizeof(ledger->allocator->allocated_bitmap[0])
    );
    handoff_test_copy_bytes(fixture, &fixture->objects_snapshot, objects, sizeof(fixture->objects_snapshot));
    handoff_test_normalize_syscall_objects(fixture, &fixture->objects_snapshot);
    handoff_test_copy_bytes(fixture,
        &fixture->endpoint_snapshot,
        endpoint_registry,
        sizeof(fixture->endpoint_snapshot)
    );
    handoff_test_copy_bytes(fixture,
        &fixture->grant_snapshot,
        grant_registry,
        sizeof(fixture->grant_snapshot)
    );
    if (!handoff_test_snapshot_page_tables(fixture, objects)) {
        return false;
    }
    handoff_test_snapshot_user_bytes(fixture);
    return true;
}

bool handoff_test_syscall_state_matches(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_grant_registry *grant_registry
)
{
    size_t bitmap_words;

    if (
        ledger == NULL
        || ledger->allocator == NULL
        || objects == NULL
        || endpoint_registry == NULL
        || grant_registry == NULL
        || ledger->managed_frame_count
            != fixture->ownership_snapshot.managed_frame_count
        || ledger->managed_frame_count
            > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
    ) {
        return false;
    }
    bitmap_words = (size_t)(
        (ledger->managed_frame_count + 63) / 64
    );
    handoff_test_copy_bytes(fixture, &fixture->objects_observed, objects, sizeof(fixture->objects_observed));
    handoff_test_normalize_syscall_objects(fixture, &fixture->objects_observed);
    return (
        handoff_test_bytes_equal(fixture,
            &fixture->ownership_snapshot,
            ledger,
            offsetof(struct micros_frame_ownership, owners)
        )
        && handoff_test_bytes_equal(fixture,
            fixture->ownership_snapshot.owners,
            ledger->owners,
            (size_t)ledger->managed_frame_count
                * sizeof(ledger->owners[0])
        )
        && handoff_test_bytes_equal(fixture,
            fixture->ownership_snapshot.handoff_targets,
            ledger->handoff_targets,
            (size_t)ledger->managed_frame_count
        )
        && handoff_test_bytes_equal(fixture,
            &fixture->allocator_snapshot,
            ledger->allocator,
            offsetof(
                struct micros_frame_allocator,
                allocated_bitmap
            )
        )
        && handoff_test_bytes_equal(fixture,
            fixture->allocator_snapshot.allocated_bitmap,
            ledger->allocator->allocated_bitmap,
            bitmap_words
                * sizeof(
                    ledger->allocator->allocated_bitmap[0]
                )
        )
        && handoff_test_bytes_equal(fixture,
            &fixture->objects_snapshot,
            &fixture->objects_observed,
            sizeof(fixture->objects_snapshot)
        )
        && handoff_test_bytes_equal(fixture,
            &fixture->endpoint_snapshot,
            endpoint_registry,
            sizeof(fixture->endpoint_snapshot)
        )
        && handoff_test_bytes_equal(fixture,
            &fixture->grant_snapshot,
            grant_registry,
            sizeof(fixture->grant_snapshot)
        )
        && handoff_test_page_tables_match(fixture, objects)
        && handoff_test_user_bytes_match(fixture)
    );
}

bool handoff_test_stage_wired_pages(
    struct micros_address_space_handoff_test_context *fixture
)
{
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_registry();
    struct micros_bootstrap_manifest manifest;
    struct micros_bootstrap_binding
        bindings[HANDOFF_PROCESS_COUNT];
    struct micros_vm_snapshot_result result;
    size_t index;

    if (objects == NULL) {
        return false;
    }
    handoff_test_fill_bytes(fixture, &manifest, 0, sizeof(manifest));
    handoff_test_fill_bytes(fixture, bindings, 0, sizeof(bindings));
    handoff_test_fill_bytes(fixture, &result, 0, sizeof(result));
    manifest.header.entry_count = HANDOFF_PROCESS_COUNT;
    for (index = 0; index < HANDOFF_PROCESS_COUNT; ++index) {
        manifest.entries[index].service_id = (uint32_t)index + 1;
        manifest.entries[index].process_slot = fixture->processes[index].slot;
        manifest.entries[index].role_flags = (
            index == HANDOFF_GRANTEE
        )
            ? MICROS_BOOTSTRAP_ROLE_VM
            : 0;
        bindings[index] = (struct micros_bootstrap_binding){
            .manifest_index = (uint16_t)index,
            .service_id = (uint32_t)index + 1,
            .process = fixture->processes[index],
            .thread = fixture->threads[index],
            .root = objects->processes[
                fixture->processes[index].slot
            ].address_space_root,
            .endpoint = fixture->endpoints[index],
        };
    }
    if (
        micros_vm_snapshot_prepare(
            &manifest,
            bindings,
            HANDOFF_PROCESS_COUNT,
            &result
        ) == MICROS_BOOTSTRAP_OK
        && result.info != NULL
        && result.vm_binding_index == HANDOFF_GRANTEE
        && result.summary.address_space_count
            == HANDOFF_PROCESS_COUNT
        && result.summary.mapping_count
            == HANDOFF_TOTAL_MAPPING_COUNT
        && result.summary.vm_self_wired_frame_count
            == HANDOFF_GRANTEE_MAPPING_COUNT
        && result.info->mappings[0].role
            == MICROS_VM_MAPPING_SERVICE_WIRED
        && result.info->mappings[
            HANDOFF_GRANTOR_MAPPING_COUNT
        ].role
            == MICROS_VM_MAPPING_SELF_WIRED
        && micros_vm_boot_validate(
            result.info,
            &result.summary
        ) == MICROS_VM_BOOT_OK
    ) {
        fixture->handoff_snapshot_info = result.info;
        return true;
    }
    return false;
}

bool handoff_test_validate_wired_snapshot(
    struct micros_address_space_handoff_test_context *fixture
)
{
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_registry();
    struct micros_bootstrap_manifest manifest;
    struct micros_bootstrap_binding
        bindings[HANDOFF_PROCESS_COUNT];
    size_t index;

    if (objects == NULL || fixture->handoff_snapshot_info == NULL) {
        return false;
    }
    handoff_test_fill_bytes(fixture, &manifest, 0, sizeof(manifest));
    handoff_test_fill_bytes(fixture, bindings, 0, sizeof(bindings));
    manifest.header.entry_count = HANDOFF_PROCESS_COUNT;
    for (index = 0; index < HANDOFF_PROCESS_COUNT; ++index) {
        manifest.entries[index].service_id = (uint32_t)index + 1;
        manifest.entries[index].process_slot = fixture->processes[index].slot;
        manifest.entries[index].role_flags = (
            index == HANDOFF_GRANTEE
        )
            ? MICROS_BOOTSTRAP_ROLE_VM
            : 0;
        bindings[index] = (struct micros_bootstrap_binding){
            .manifest_index = (uint16_t)index,
            .service_id = (uint32_t)index + 1,
            .process = fixture->processes[index],
            .thread = fixture->threads[index],
            .root = objects->processes[
                fixture->processes[index].slot
            ].address_space_root,
            .endpoint = fixture->endpoints[index],
        };
    }
    return micros_vm_snapshot_validate_current(
        &manifest,
        bindings,
        HANDOFF_PROCESS_COUNT,
        fixture->handoff_snapshot_info
    ) == MICROS_BOOTSTRAP_OK;
}
