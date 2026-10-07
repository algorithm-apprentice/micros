#include "kernel/address_space_handoff_test.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/grant_runtime_internal.h"
#include "kernel/ipc_runtime_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_copy.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_buffer.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

enum {
    HANDOFF_GRANTOR = 0,
    HANDOFF_GRANTEE,
    HANDOFF_PROCESS_COUNT,
    HANDOFF_PAGE_COUNT = 5,
};

enum handoff_test_state {
    HANDOFF_TEST_IDLE = 0,
    HANDOFF_TEST_USER_RUNNING,
    HANDOFF_TEST_SUPERVISOR,
};

static const uint64_t TEST_CODE_ADDRESS =
    MICROS_USER_VIRTUAL_BASE;
static const uint64_t TEST_STACK_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + MICROS_SV39_PAGE_SIZE;
static const uint64_t TEST_DATA_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x2000);
static const uint64_t TEST_DATA_SECOND_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x3000);
static const uint64_t TEST_READ_ONLY_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x4000);
static const uint64_t TEST_UNMAPPED_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x6000);

static struct micros_frame_ownership ownership_snapshot;
static struct micros_frame_allocator allocator_snapshot;
static struct micros_kernel_objects objects_snapshot;
static struct micros_endpoint_registry endpoint_snapshot;
static struct micros_grant_registry grant_snapshot;
static unsigned char
    kernel_stack_snapshot[MICROS_THREAD_KERNEL_STACK_SIZE];
static unsigned char page_table_snapshot[
    HANDOFF_PROCESS_COUNT
][3][MICROS_SV39_PAGE_SIZE];
static unsigned char page_table_observed[
    HANDOFF_PROCESS_COUNT
][3][MICROS_SV39_PAGE_SIZE];
static unsigned char user_bytes_snapshot[
    HANDOFF_PROCESS_COUNT
][HANDOFF_PAGE_COUNT][MICROS_SV39_PAGE_SIZE];
static unsigned char user_bytes_observed[
    HANDOFF_PROCESS_COUNT
][HANDOFF_PAGE_COUNT][MICROS_SV39_PAGE_SIZE];
static uint64_t user_physical[HANDOFF_PROCESS_COUNT][HANDOFF_PAGE_COUNT];
static struct micros_process_handle processes[HANDOFF_PROCESS_COUNT];
static struct micros_thread_handle threads[HANDOFF_PROCESS_COUNT];
static micros_endpoint_t endpoints[HANDOFF_PROCESS_COUNT];
static volatile enum handoff_test_state test_state;
static uint64_t failure_stage;

extern const unsigned char micros_address_space_handoff_payload_start[];
extern const unsigned char micros_address_space_handoff_payload_ecall[];
extern const unsigned char micros_address_space_handoff_payload_end[];
extern const unsigned char
    micros_address_space_handoff_test_supervisor_resume[];
extern const uint64_t micros_address_space_handoff_test_saved_state[17];
extern const uint64_t micros_address_space_handoff_test_restored_state[17];

void micros_address_space_handoff_test_enter(
    uint64_t thread_slot,
    uint64_t thread_generation
);

_Noreturn void micros_address_space_handoff_test_enter_production(
    uint64_t thread_slot,
    uint64_t thread_generation
);

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

static bool bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

static void fill_bytes(void *storage, unsigned char value, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = value;
    }
}

static uint64_t physical_for(
    size_t process_index,
    uint64_t virtual_address
)
{
    size_t page_index;
    uint64_t offset;

    if (
        process_index >= HANDOFF_PROCESS_COUNT
        || virtual_address < TEST_CODE_ADDRESS
        || virtual_address
            >= TEST_CODE_ADDRESS
                + HANDOFF_PAGE_COUNT * MICROS_SV39_PAGE_SIZE
    ) {
        return 0;
    }
    page_index = (size_t)(
        (virtual_address - TEST_CODE_ADDRESS)
        / MICROS_SV39_PAGE_SIZE
    );
    offset = virtual_address % MICROS_SV39_PAGE_SIZE;
    return user_physical[process_index][page_index] + offset;
}

static void write_pattern(
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char seed
)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        *(unsigned char *)(uintptr_t)physical_for(
            process_index,
            virtual_address + index
        ) = (unsigned char)(seed + index);
    }
}

static bool pattern_matches(
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char seed
)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        if (
            *(const unsigned char *)(uintptr_t)physical_for(
                process_index,
                virtual_address + index
            ) != (unsigned char)(seed + index)
        ) {
            return false;
        }
    }
    return true;
}

static void write_virtual(
    size_t process_index,
    uint64_t virtual_address,
    const void *source,
    size_t size
)
{
    const unsigned char *bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        *(unsigned char *)(uintptr_t)physical_for(
            process_index,
            virtual_address + index
        ) = bytes[index];
    }
}

static bool virtual_matches(
    size_t process_index,
    uint64_t virtual_address,
    const void *expected,
    size_t size
)
{
    const unsigned char *bytes = expected;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (
            *(const unsigned char *)(uintptr_t)physical_for(
                process_index,
                virtual_address + index
            ) != bytes[index]
        ) {
            return false;
        }
    }
    return true;
}

static bool snapshot_page_tables(
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
            &objects->processes[processes[process_index].slot];
        struct page_table *root =
            (struct page_table *)(uintptr_t)record->address_space_root;
        struct micros_sv39_decoded_pte root_entry;
        struct micros_sv39_decoded_pte middle_entry;
        struct page_table *middle;
        struct page_table *leaf;

        if (
            record->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || record->generation
                != processes[process_index].generation
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
        copy_bytes(
            page_table_snapshot[process_index][0],
            root,
            MICROS_SV39_PAGE_SIZE
        );
        copy_bytes(
            page_table_snapshot[process_index][1],
            middle,
            MICROS_SV39_PAGE_SIZE
        );
        copy_bytes(
            page_table_snapshot[process_index][2],
            leaf,
            MICROS_SV39_PAGE_SIZE
        );
    }
    return true;
}

static bool page_tables_match(
    const struct micros_kernel_objects *objects
)
{
    copy_bytes(
        page_table_observed,
        page_table_snapshot,
        sizeof(page_table_observed)
    );
    if (!snapshot_page_tables(objects)) {
        return false;
    }
    if (!bytes_equal(
        page_table_observed,
        page_table_snapshot,
        sizeof(page_table_observed)
    )) {
        return false;
    }
    copy_bytes(
        page_table_snapshot,
        page_table_observed,
        sizeof(page_table_snapshot)
    );
    return true;
}

static void snapshot_user_bytes(void)
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
            copy_bytes(
                user_bytes_snapshot[process_index][page_index],
                (const void *)(uintptr_t)
                    user_physical[process_index][page_index],
                MICROS_SV39_PAGE_SIZE
            );
        }
    }
}

static bool user_bytes_match(void)
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
            copy_bytes(
                user_bytes_observed[process_index][page_index],
                (const void *)(uintptr_t)
                    user_physical[process_index][page_index],
                MICROS_SV39_PAGE_SIZE
            );
        }
    }
    return bytes_equal(
        user_bytes_snapshot,
        user_bytes_observed,
        sizeof(user_bytes_snapshot)
    );
}

static bool snapshot_state(
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
    copy_bytes(&ownership_snapshot, ledger, sizeof(ownership_snapshot));
    copy_bytes(
        &allocator_snapshot,
        ledger->allocator,
        sizeof(allocator_snapshot)
    );
    copy_bytes(&objects_snapshot, objects, sizeof(objects_snapshot));
    copy_bytes(
        &endpoint_snapshot,
        endpoint_registry,
        sizeof(endpoint_snapshot)
    );
    copy_bytes(
        &grant_snapshot,
        grant_registry,
        sizeof(grant_snapshot)
    );
    if (!snapshot_page_tables(objects)) {
        return false;
    }
    snapshot_user_bytes();
    return true;
}

static bool state_matches(
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_grant_registry *grant_registry
)
{
    return (
        ledger != NULL
        && ledger->allocator != NULL
        && bytes_equal(
            &ownership_snapshot,
            ledger,
            sizeof(ownership_snapshot)
        )
        && bytes_equal(
            &allocator_snapshot,
            ledger->allocator,
            sizeof(allocator_snapshot)
        )
        && bytes_equal(
            &objects_snapshot,
            objects,
            sizeof(objects_snapshot)
        )
        && bytes_equal(
            &endpoint_snapshot,
            endpoint_registry,
            sizeof(endpoint_snapshot)
        )
        && bytes_equal(
            &grant_snapshot,
            grant_registry,
            sizeof(grant_snapshot)
        )
        && page_tables_match(objects)
        && user_bytes_match()
    );
}

static bool frame_index_for_physical(
    const struct micros_frame_ownership *ledger,
    uint64_t physical_address,
    uint64_t *frame_index
)
{
    size_t range_index;

    for (
        range_index = 0;
        range_index < ledger->managed_range_count;
        ++range_index
    ) {
        const struct micros_frame_range_snapshot *range =
            &ledger->managed_ranges[range_index];
        uint64_t offset;

        if (physical_address < range->base) {
            continue;
        }
        offset = physical_address - range->base;
        if (
            offset / MICROS_SV39_PAGE_SIZE
                < range->frame_count
            && offset % MICROS_SV39_PAGE_SIZE == 0
        ) {
            *frame_index = range->bitmap_offset
                + offset / MICROS_SV39_PAGE_SIZE;
            return true;
        }
    }
    return false;
}

static uint64_t *leaf_pte_for(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint64_t virtual_address
)
{
    struct page_table {
        uint64_t entries[MICROS_SV39_TABLE_ENTRY_COUNT];
    };
    const struct micros_process *record =
        &objects->processes[process.slot];
    struct page_table *root =
        (struct page_table *)(uintptr_t)record->address_space_root;
    struct micros_sv39_decoded_pte root_entry;
    struct micros_sv39_decoded_pte middle_entry;
    struct page_table *middle;
    struct page_table *leaf;
    uint16_t root_index;
    uint16_t middle_index;
    uint16_t leaf_index;

    if (
        record->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || record->generation != process.generation
        || micros_sv39_vpn_index(virtual_address, 2, &root_index)
            != MICROS_SV39_OK
        || micros_sv39_vpn_index(virtual_address, 1, &middle_index)
            != MICROS_SV39_OK
        || micros_sv39_vpn_index(virtual_address, 0, &leaf_index)
            != MICROS_SV39_OK
        || micros_sv39_decode_pte(
            root->entries[root_index],
            &root_entry
        ) != MICROS_SV39_OK
        || root_entry.kind != MICROS_SV39_PTE_TABLE
    ) {
        return NULL;
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
        return NULL;
    }
    leaf = (struct page_table *)(uintptr_t)
        middle_entry.physical_address;
    return &leaf->entries[leaf_index];
}

static void prepare_context(
    struct micros_user_context *context,
    size_t process_index
)
{
    fill_bytes(context, 0, sizeof(*context));
    context->sepc = TEST_CODE_ADDRESS;
    context->sp = TEST_STACK_ADDRESS + MICROS_SV39_PAGE_SIZE;
    context->a0 = UINT64_C(0x1000) + process_index;
}

static bool stage_wired_pages(void)
{
    size_t process_index;
    size_t page_index;

    for (
        process_index = 0;
        process_index < HANDOFF_PROCESS_COUNT;
        ++process_index
    ) {
        struct micros_frame_owner owner;

        if (
            micros_frame_owner_make_process(
                MICROS_FRAME_OWNER_PROCESS_USER,
                processes[process_index],
                &owner
            ) != MICROS_FRAME_OWNERSHIP_OK
        ) {
            return false;
        }
        for (
            page_index = 0;
            page_index < HANDOFF_PAGE_COUNT;
            ++page_index
        ) {
            if (
                micros_frame_ownership_runtime_prepare_handoff(
                    user_physical[process_index][page_index],
                    owner,
                    MICROS_FRAME_HANDOFF_VM_WIRED
                ) != MICROS_FRAME_OWNERSHIP_OK
            ) {
                return false;
            }
        }
    }
    return true;
}

_Noreturn void micros_address_space_handoff_test_enter_production(
    uint64_t thread_slot,
    uint64_t thread_generation
)
{
    micros_scheduler_test_enter_without_timer(
        (struct micros_thread_handle){
            .slot = (uint16_t)thread_slot,
            .generation = (uint32_t)thread_generation,
        }
    );
}

enum micros_address_space_handoff_test_trap_result
micros_address_space_handoff_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    const uint64_t control_mask =
        MICROS_RISCV_SSTATUS_SIE
        | MICROS_RISCV_SSTATUS_SPIE
        | MICROS_RISCV_SSTATUS_SPP
        | MICROS_RISCV_SSTATUS_SUM
        | (UINT64_C(1) << 6)
        | (UINT64_C(3) << 9)
        | (UINT64_C(3) << 13)
        | (UINT64_C(3) << 15)
        | (UINT64_C(1) << 19);

    if (test_state == HANDOFF_TEST_IDLE) {
        return MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_INACTIVE;
    }
    if (
        test_state != HANDOFF_TEST_USER_RUNNING
        || hart == NULL
        || frame == NULL
        || (frame->scause >> 63) != 0
        || (frame->scause & (UINT64_C(1) << 63) - 1) != 8
        || frame->sepc
            != TEST_CODE_ADDRESS
                + (
                    (uintptr_t)micros_address_space_handoff_payload_ecall
                    - (uintptr_t)micros_address_space_handoff_payload_start
                )
        || frame->a0 != UINT64_C(0x4a5)
        || frame->a7 != UINT64_C(0x40)
        || micros_scheduler_test_prepare_supervisor_return(
            hart,
            frame
        ) != MICROS_SCHEDULER_OK
    ) {
        return MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_MISMATCH;
    }
    frame->sp = micros_address_space_handoff_test_saved_state[1];
    frame->sepc =
        (uintptr_t)micros_address_space_handoff_test_supervisor_resume;
    frame->sstatus &= ~control_mask;
    frame->sstatus |= MICROS_RISCV_SSTATUS_SPP;
    test_state = HANDOFF_TEST_SUPERVISOR;
    return MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_SUPERVISOR_RETURN;
}

bool micros_address_space_handoff_runtime_run_self_test(void)
{
    static const struct micros_privilege_profile profile = {
        .id = 1,
        .name = "HANDOFF_TEST",
        .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
    };
    const uint32_t page_permissions[HANDOFF_PAGE_COUNT] = {
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE,
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE,
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE,
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE,
        MICROS_SV39_PERMISSION_READ,
    };
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    struct micros_endpoint_registry *endpoint_registry;
    struct micros_grant_registry *grant_registry;
    struct micros_process_handle empty_process;
    struct micros_user_context contexts[HANDOFF_PROCESS_COUNT];
    struct micros_user_context observed_context;
    struct micros_thread thread_snapshot;
    struct micros_ipc_message message;
    struct micros_ipc_message observed_message;
    struct micros_frame_owner process_user_owner;
    struct micros_frame_owner saved_owner;
    micros_grant_t read_grant;
    micros_grant_t write_grant;
    uint64_t *corrupt_pte;
    uint64_t saved_pte;
    uint64_t frame_index;
    uint64_t physical_address;
    uint32_t permissions;
    size_t contiguous;
    uintptr_t kernel_stack_bottom;
    uintptr_t kernel_stack_top;
    uintptr_t saved_status;
    size_t payload_size;
    size_t process_index;
    size_t page_index;
    size_t payload_index;
    bool passed = false;

    saved_status = riscv_irq_save();
    fill_bytes(&message, 0, sizeof(message));
    failure_stage = 1;
    objects = micros_kernel_object_runtime_test_registry();
    ledger = micros_frame_ownership_runtime_ledger();
    if (
        objects == NULL
        || ledger == NULL
        || micros_ipc_runtime_registry() != NULL
        || micros_grant_runtime_registry() != NULL
        || micros_ipc_runtime_initialize(&profile, 1)
            != MICROS_ENDPOINT_OK
        || micros_grant_runtime_initialize() != MICROS_GRANT_OK
        || (
            endpoint_registry =
                micros_ipc_runtime_authoritative_registry()
        ) == NULL
        || (
            grant_registry =
                micros_grant_runtime_authoritative_registry()
        ) == NULL
    ) {
        goto done;
    }

    failure_stage = 2;
    for (
        process_index = 0;
        process_index < HANDOFF_PROCESS_COUNT;
        ++process_index
    ) {
        if (
            micros_process_create(
                objects,
                &processes[process_index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_user_address_space_create(
                processes[process_index]
            ) != MICROS_USER_ADDRESS_SPACE_OK
        ) {
            goto done;
        }
        for (
            page_index = 0;
            page_index < HANDOFF_PAGE_COUNT;
            ++page_index
        ) {
            if (
                micros_user_address_space_allocate_page(
                    processes[process_index],
                    TEST_CODE_ADDRESS
                        + page_index * MICROS_SV39_PAGE_SIZE,
                    page_permissions[page_index],
                    &user_physical[process_index][page_index]
                ) != MICROS_USER_ADDRESS_SPACE_OK
            ) {
                goto done;
            }
            fill_bytes(
                (void *)(uintptr_t)
                    user_physical[process_index][page_index],
                (unsigned char)(UINT8_C(0xb0) + process_index),
                MICROS_SV39_PAGE_SIZE
            );
        }
        if (
            micros_thread_create(
                objects,
                processes[process_index],
                &threads[process_index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                endpoint_registry,
                objects,
                processes[process_index],
                &endpoints[process_index]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                endpoint_registry,
                objects,
                processes[process_index],
                1
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                endpoint_registry,
                objects,
                endpoints[process_index]
            ) != MICROS_ENDPOINT_OK
        ) {
            goto done;
        }
    }
    if (
        micros_process_create(objects, &empty_process)
            != MICROS_KERNEL_OBJECT_OK
        || objects->processes[empty_process.slot].address_space_root != 0
        || objects->processes[empty_process.slot].live_thread_count != 0
        || objects->processes[empty_process.slot].primary_endpoint
            != MICROS_PROCESS_ENDPOINT_NONE
    ) {
        goto done;
    }

    failure_stage = 3;
    payload_size =
        (uintptr_t)micros_address_space_handoff_payload_end
        - (uintptr_t)micros_address_space_handoff_payload_start;
    if (payload_size == 0 || payload_size > MICROS_SV39_PAGE_SIZE) {
        goto done;
    }
    for (
        process_index = 0;
        process_index < HANDOFF_PROCESS_COUNT;
        ++process_index
    ) {
        prepare_context(&contexts[process_index], process_index);
        copy_bytes(
            (void *)(uintptr_t)user_physical[process_index][0],
            micros_address_space_handoff_payload_start,
            payload_size
        );
        if (
            micros_user_execution_prepare(
                threads[process_index],
                &contexts[process_index]
            ) != MICROS_USER_EXECUTION_OK
        ) {
            goto done;
        }
    }
    __asm__ volatile("fence.i" : : : "memory");

    if (
        micros_grant_create(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTOR],
            endpoints[HANDOFF_GRANTEE],
            TEST_DATA_SECOND_ADDRESS - 64,
            256,
            MICROS_GRANT_PERMISSION_READ,
            &read_grant
        ) != MICROS_GRANT_OK
        || micros_grant_create(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTOR],
            endpoints[HANDOFF_GRANTEE],
            TEST_DATA_SECOND_ADDRESS - 64,
            256,
            MICROS_GRANT_PERMISSION_WRITE,
            &write_grant
        ) != MICROS_GRANT_OK
    ) {
        goto done;
    }

    failure_stage = 4;
    for (payload_index = 0; payload_index < sizeof(message.payload); ++payload_index) {
        message.payload[payload_index] =
            (unsigned char)(UINT8_C(0x30) + payload_index);
    }
    message.type = UINT32_C(0x12345678);
    message.source = UINT32_C(0xabcdef01);
    message.reply_token = UINT64_C(0x1122334455667788);
    write_virtual(
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 32,
        &message,
        sizeof(message)
    );
    if (
        micros_user_address_space_validate(processes[HANDOFF_GRANTOR])
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_translate(
            processes[HANDOFF_GRANTOR],
            TEST_DATA_SECOND_ADDRESS - 32,
            &physical_address,
            &permissions,
            &contiguous
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || physical_address
            != physical_for(
                HANDOFF_GRANTOR,
                TEST_DATA_SECOND_ADDRESS - 32
            )
        || contiguous != 32
        || micros_ipc_buffer_snapshot(
            processes[HANDOFF_GRANTOR],
            TEST_DATA_SECOND_ADDRESS - 32,
            MICROS_IPC_BUFFER_READ,
            &observed_message
        ) != MICROS_IPC_BUFFER_OK
        || !bytes_equal(
            &observed_message,
            &message,
            sizeof(message)
        )
    ) {
        goto done;
    }
    write_pattern(
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 64,
        128,
        UINT8_C(0x50)
    );
    if (
        micros_grant_copy_from(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTEE],
            endpoints[HANDOFF_GRANTOR],
            read_grant,
            0,
            TEST_DATA_SECOND_ADDRESS - 32,
            128
        ) != MICROS_GRANT_OK
        || !pattern_matches(
            HANDOFF_GRANTEE,
            TEST_DATA_SECOND_ADDRESS - 32,
            128,
            UINT8_C(0x50)
        )
    ) {
        goto done;
    }

    failure_stage = 5;
    if (!stage_wired_pages()) {
        goto done;
    }
    if (
        micros_user_execution_detach(threads[HANDOFF_GRANTEE])
            != MICROS_USER_EXECUTION_OK
        || !snapshot_state(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
        || micros_user_address_space_complete_wired_handoff()
            != MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        || !state_matches(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
        || ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        || micros_user_execution_prepare(
            threads[HANDOFF_GRANTEE],
            &contexts[HANDOFF_GRANTEE]
        ) != MICROS_USER_EXECUTION_OK
    ) {
        goto done;
    }
    if (
        micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_USER,
            processes[HANDOFF_GRANTEE],
            &process_user_owner
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_prepare_handoff(
            user_physical[HANDOFF_GRANTEE][2],
            process_user_owner,
            MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        ) != MICROS_FRAME_OWNERSHIP_OK
        || !snapshot_state(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
        || micros_user_address_space_complete_wired_handoff()
            != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        || !state_matches(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
        || ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        || micros_frame_ownership_runtime_prepare_handoff(
            user_physical[HANDOFF_GRANTEE][2],
            process_user_owner,
            MICROS_FRAME_HANDOFF_VM_WIRED
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_user_address_space_complete_wired_handoff()
            != MICROS_USER_ADDRESS_SPACE_OK
        || ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    ) {
        goto done;
    }

    failure_stage = 6;
    for (
        process_index = 0;
        process_index < HANDOFF_PROCESS_COUNT;
        ++process_index
    ) {
        if (
            micros_user_address_space_validate(
                processes[process_index]
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || micros_user_address_space_lookup(
                processes[process_index],
                TEST_DATA_ADDRESS,
                &physical_address,
                &permissions
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || physical_address
                != user_physical[process_index][2]
            || micros_user_address_space_translate(
                processes[process_index],
                TEST_DATA_ADDRESS + 7,
                &physical_address,
                &permissions,
                &contiguous
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || physical_address
                != user_physical[process_index][2] + 7
            || contiguous != MICROS_SV39_PAGE_SIZE - 7
            || (
                permissions
                & (
                    MICROS_SV39_PERMISSION_READ
                    | MICROS_SV39_PERMISSION_WRITE
                )
            ) != (
                MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
            )
            || micros_user_address_space_activate(
                processes[process_index]
            ) != MICROS_USER_ADDRESS_SPACE_OK
        ) {
            goto done;
        }
    }
    if (
        micros_user_address_space_activate_kernel()
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        goto done;
    }
    write_virtual(
        HANDOFF_GRANTOR,
        TEST_DATA_ADDRESS + UINT64_C(0x100),
        &message,
        sizeof(message)
    );
    if (
        micros_ipc_buffer_snapshot(
            processes[HANDOFF_GRANTOR],
            TEST_DATA_ADDRESS + UINT64_C(0x100),
            MICROS_IPC_BUFFER_READ,
            &observed_message
        ) != MICROS_IPC_BUFFER_OK
        || !bytes_equal(
            &observed_message,
            &message,
            sizeof(message)
        )
        || micros_ipc_buffer_write(
            processes[HANDOFF_GRANTEE],
            TEST_DATA_ADDRESS + UINT64_C(0x180),
            &message
        ) != MICROS_IPC_BUFFER_OK
        || !virtual_matches(
            HANDOFF_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x180),
            &message,
            sizeof(message)
        )
    ) {
        goto done;
    }
    write_virtual(
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 32,
        &message,
        sizeof(message)
    );
    if (
        micros_ipc_buffer_snapshot(
            processes[HANDOFF_GRANTOR],
            TEST_DATA_SECOND_ADDRESS - 32,
            MICROS_IPC_BUFFER_READ,
            &observed_message
        ) != MICROS_IPC_BUFFER_OK
        || !bytes_equal(
            &observed_message,
            &message,
            sizeof(message)
        )
        || micros_ipc_buffer_write(
            processes[HANDOFF_GRANTEE],
            TEST_DATA_SECOND_ADDRESS - 32,
            &message
        ) != MICROS_IPC_BUFFER_OK
        || !virtual_matches(
            HANDOFF_GRANTEE,
            TEST_DATA_SECOND_ADDRESS - 32,
            &message,
            sizeof(message)
        )
    ) {
        goto done;
    }

    write_pattern(
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 64,
        128,
        UINT8_C(0x70)
    );
    if (
        micros_grant_copy_from(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTEE],
            endpoints[HANDOFF_GRANTOR],
            read_grant,
            0,
            TEST_DATA_SECOND_ADDRESS - 32,
            128
        ) != MICROS_GRANT_OK
        || !pattern_matches(
            HANDOFF_GRANTEE,
            TEST_DATA_SECOND_ADDRESS - 32,
            128,
            UINT8_C(0x70)
        )
    ) {
        goto done;
    }
    write_pattern(
        HANDOFF_GRANTEE,
        TEST_DATA_SECOND_ADDRESS - 48,
        96,
        UINT8_C(0x90)
    );
    if (
        micros_grant_copy_to(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTEE],
            endpoints[HANDOFF_GRANTOR],
            write_grant,
            16,
            TEST_DATA_SECOND_ADDRESS - 48,
            96
        ) != MICROS_GRANT_OK
        || !pattern_matches(
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 48,
            96,
            UINT8_C(0x90)
        )
    ) {
        goto done;
    }
    write_pattern(
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS,
        32,
        UINT8_C(0x11)
    );
    *(unsigned char *)(uintptr_t)physical_for(
        HANDOFF_GRANTEE,
        TEST_DATA_ADDRESS + UINT64_C(0x2ff)
    ) = UINT8_C(0x5a);
    *(unsigned char *)(uintptr_t)physical_for(
        HANDOFF_GRANTEE,
        TEST_DATA_ADDRESS + UINT64_C(0x320)
    ) = UINT8_C(0xa5);
    if (
        micros_grant_copy_from(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTEE],
            endpoints[HANDOFF_GRANTOR],
            read_grant,
            64,
            TEST_DATA_ADDRESS + UINT64_C(0x300),
            32
        ) != MICROS_GRANT_OK
        || !pattern_matches(
            HANDOFF_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x300),
            32,
            UINT8_C(0x11)
        )
        || *(const unsigned char *)(uintptr_t)physical_for(
            HANDOFF_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x2ff)
        ) != UINT8_C(0x5a)
        || *(const unsigned char *)(uintptr_t)physical_for(
            HANDOFF_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x320)
        ) != UINT8_C(0xa5)
    ) {
        goto done;
    }
    write_pattern(
        HANDOFF_GRANTEE,
        TEST_DATA_ADDRESS + UINT64_C(0x380),
        32,
        UINT8_C(0x22)
    );
    *(unsigned char *)(uintptr_t)physical_for(
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS + UINT64_C(0x3f)
    ) = UINT8_C(0x6b);
    *(unsigned char *)(uintptr_t)physical_for(
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS + UINT64_C(0x60)
    ) = UINT8_C(0xb6);
    if (
        micros_grant_copy_to(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTEE],
            endpoints[HANDOFF_GRANTOR],
            write_grant,
            128,
            TEST_DATA_ADDRESS + UINT64_C(0x380),
            32
        ) != MICROS_GRANT_OK
        || !pattern_matches(
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS + UINT64_C(0x40),
            32,
            UINT8_C(0x22)
        )
        || *(const unsigned char *)(uintptr_t)physical_for(
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS + UINT64_C(0x3f)
        ) != UINT8_C(0x6b)
        || *(const unsigned char *)(uintptr_t)physical_for(
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS + UINT64_C(0x60)
        ) != UINT8_C(0xb6)
    ) {
        goto done;
    }
    if (
        micros_grant_copy_from(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTEE],
            endpoints[HANDOFF_GRANTOR],
            read_grant,
            0,
            0,
            0
        ) != MICROS_GRANT_OK
        || micros_grant_copy_to(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTEE],
            endpoints[HANDOFF_GRANTOR],
            write_grant,
            0,
            0,
            0
        ) != MICROS_GRANT_OK
    ) {
        goto done;
    }

    failure_stage = 7;
    write_pattern(
        HANDOFF_GRANTEE,
        TEST_READ_ONLY_ADDRESS,
        64,
        UINT8_C(0xa0)
    );
    write_pattern(
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 64,
        64,
        UINT8_C(0xb0)
    );
    if (
        !snapshot_state(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
        || micros_ipc_buffer_write(
            processes[HANDOFF_GRANTEE],
            TEST_READ_ONLY_ADDRESS,
            &message
        ) != MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT
        || micros_grant_copy_from(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTEE],
            endpoints[HANDOFF_GRANTOR],
            read_grant,
            0,
            TEST_READ_ONLY_ADDRESS,
            64
        ) != MICROS_GRANT_ERROR_FAULT
        || micros_grant_copy_from(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTEE],
            endpoints[HANDOFF_GRANTOR],
            read_grant,
            0,
            TEST_UNMAPPED_ADDRESS,
            64
        ) != MICROS_GRANT_ERROR_FAULT
        || !pattern_matches(
            HANDOFF_GRANTEE,
            TEST_READ_ONLY_ADDRESS,
            64,
            UINT8_C(0xa0)
        )
        || !pattern_matches(
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 64,
            64,
            UINT8_C(0xb0)
        )
        || !state_matches(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
    ) {
        goto done;
    }

    failure_stage = 8;
    if (
        !frame_index_for_physical(
            ledger,
            user_physical[HANDOFF_GRANTEE][2],
            &frame_index
        )
    ) {
        goto done;
    }
    saved_owner = ledger->owners[frame_index];
    write_pattern(
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 64,
        64,
        UINT8_C(0xc0)
    );
    write_pattern(
        HANDOFF_GRANTEE,
        TEST_DATA_ADDRESS,
        64,
        UINT8_C(0xd0)
    );
    ((struct micros_frame_ownership *)(uintptr_t)ledger)
        ->owners[frame_index] =
        (struct micros_frame_owner){
            .generation = processes[HANDOFF_GRANTOR].generation,
            .slot = processes[HANDOFF_GRANTOR].slot,
            .kind = MICROS_FRAME_OWNER_VM_WIRED,
            .reserved = 0,
        };
    if (
        !snapshot_state(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
        || micros_user_address_space_validate(
            processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        || micros_grant_copy_from(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTEE],
            endpoints[HANDOFF_GRANTOR],
            read_grant,
            0,
            TEST_DATA_ADDRESS,
            64
        ) != MICROS_GRANT_ERROR_INVARIANT
        || !pattern_matches(
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 64,
            64,
            UINT8_C(0xc0)
        )
        || !pattern_matches(
            HANDOFF_GRANTEE,
            TEST_DATA_ADDRESS,
            64,
            UINT8_C(0xd0)
        )
        || !state_matches(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
    ) {
        goto done;
    }
    ((struct micros_frame_ownership *)(uintptr_t)ledger)
        ->owners[frame_index] = saved_owner;

    ((struct micros_frame_ownership *)(uintptr_t)ledger)
        ->owners[frame_index] =
        (struct micros_frame_owner){
            .generation = 0,
            .slot = 0,
            .kind = MICROS_FRAME_OWNER_VM_TRANSFERABLE,
            .reserved = 0,
        };
    --((struct micros_frame_ownership *)(uintptr_t)ledger)
        ->owner_counts[MICROS_FRAME_OWNER_VM_WIRED];
    ++((struct micros_frame_ownership *)(uintptr_t)ledger)
        ->owner_counts[MICROS_FRAME_OWNER_VM_TRANSFERABLE];
    if (
        micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_user_address_space_validate(
            processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
    ) {
        goto done;
    }
    ((struct micros_frame_ownership *)(uintptr_t)ledger)
        ->owners[frame_index] = saved_owner;
    ++((struct micros_frame_ownership *)(uintptr_t)ledger)
        ->owner_counts[MICROS_FRAME_OWNER_VM_WIRED];
    --((struct micros_frame_ownership *)(uintptr_t)ledger)
        ->owner_counts[MICROS_FRAME_OWNER_VM_TRANSFERABLE];

    corrupt_pte = leaf_pte_for(
        objects,
        processes[HANDOFF_GRANTEE],
        TEST_DATA_ADDRESS
    );
    if (corrupt_pte == NULL) {
        goto done;
    }
    saved_pte = *corrupt_pte;
    *corrupt_pte = UINT64_C(1);
    physical_address = UINT64_C(0xfeedfacefeedface);
    permissions = UINT32_MAX;
    contiguous = SIZE_MAX;
    if (
        micros_user_address_space_translate(
            processes[HANDOFF_GRANTEE],
            TEST_UNMAPPED_ADDRESS,
            &physical_address,
            &permissions,
            &contiguous
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PTE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || permissions != UINT32_MAX
        || contiguous != SIZE_MAX
    ) {
        goto done;
    }
    *corrupt_pte = saved_pte;

    failure_stage = 9;
    physical_address = UINT64_C(0xfeedfacefeedface);
    if (
        !snapshot_state(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
        || micros_user_address_space_create(
            processes[HANDOFF_GRANTOR]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || micros_user_address_space_allocate_page(
            processes[HANDOFF_GRANTOR],
            TEST_UNMAPPED_ADDRESS,
            MICROS_SV39_PERMISSION_READ,
            &physical_address
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || physical_address != UINT64_C(0xfeedfacefeedface)
    ) {
        goto done;
    }
    physical_address = UINT64_C(0xfacefeedfacefeed);
    if (
        micros_user_address_space_release_page(
            processes[HANDOFF_GRANTOR],
            TEST_DATA_ADDRESS,
            &physical_address
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || physical_address != UINT64_C(0xfacefeedfacefeed)
        || micros_user_address_space_destroy(
            processes[HANDOFF_GRANTOR]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || !state_matches(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
    ) {
        goto done;
    }

    if (
        !micros_user_execution_test_stack_bounds(
            threads[HANDOFF_GRANTOR],
            &kernel_stack_bottom,
            &kernel_stack_top
        )
        || kernel_stack_top - kernel_stack_bottom
            != MICROS_THREAD_KERNEL_STACK_SIZE
    ) {
        goto done;
    }
    copy_bytes(
        &thread_snapshot,
        &objects->threads[threads[HANDOFF_GRANTOR].slot],
        sizeof(thread_snapshot)
    );
    copy_bytes(
        kernel_stack_snapshot,
        (const void *)kernel_stack_bottom,
        sizeof(kernel_stack_snapshot)
    );
    copy_bytes(
        &observed_context,
        &contexts[HANDOFF_GRANTOR],
        sizeof(observed_context)
    );
    if (
        !snapshot_state(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
        || micros_user_execution_prepare(
            threads[HANDOFF_GRANTOR],
            &observed_context
        ) != MICROS_USER_EXECUTION_ERROR_PHASE
        || !bytes_equal(
            &observed_context,
            &contexts[HANDOFF_GRANTOR],
            sizeof(observed_context)
        )
        || !bytes_equal(
            &thread_snapshot,
            &objects->threads[threads[HANDOFF_GRANTOR].slot],
            sizeof(thread_snapshot)
        )
        || !bytes_equal(
            kernel_stack_snapshot,
            (const void *)kernel_stack_bottom,
            sizeof(kernel_stack_snapshot)
        )
        || !state_matches(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
    ) {
        goto done;
    }

    failure_stage = 10;
    test_state = HANDOFF_TEST_USER_RUNNING;
    micros_address_space_handoff_test_enter(
        threads[HANDOFF_GRANTOR].slot,
        threads[HANDOFF_GRANTOR].generation
    );
    if (
        test_state != HANDOFF_TEST_SUPERVISOR
        || !bytes_equal(
            micros_address_space_handoff_test_saved_state,
            micros_address_space_handoff_test_restored_state,
            sizeof(uint64_t) * 17
        )
        || micros_user_address_space_validate(
            processes[HANDOFF_GRANTOR]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_validate(
            processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || objects->processes[empty_process.slot].slot_state
            != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || objects->processes[empty_process.slot].generation
            != empty_process.generation
        || objects->processes[empty_process.slot].address_space_root != 0
        || objects->processes[empty_process.slot].live_thread_count != 0
        || objects->processes[empty_process.slot].primary_endpoint
            != MICROS_PROCESS_ENDPOINT_NONE
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
    ) {
        goto done;
    }
    passed = true;

done:
    if (!passed) {
        uart_write("MICROS_TEST_FAILURE handoff-stage=");
        uart_write_hex64(failure_stage);
        uart_write("\n");
        uart_flush();
    }
    test_state = HANDOFF_TEST_IDLE;
    riscv_irq_restore(saved_status);
    return passed;
}
