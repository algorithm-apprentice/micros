#include "kernel/address_space_handoff_test.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/grant_runtime_internal.h"
#include "kernel/grant_syscall.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/user_address_space_internal.h"
#include "kernel/vm_snapshot.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_copy.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_buffer.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/scheduler_core.h"
#include "micros/sv39.h"
#include "micros/syscall_abi.h"
#include "micros/tty.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

enum {
    HANDOFF_GRANTOR = 0,
    HANDOFF_GRANTEE,
    HANDOFF_PROCESS_COUNT,
    HANDOFF_PAGE_COUNT = 5,
    HANDOFF_RESERVED_PROCESS_COUNT = 2,
    HANDOFF_GRANTOR_MAPPING_COUNT = HANDOFF_PAGE_COUNT,
    HANDOFF_GRANTEE_MAPPING_COUNT = HANDOFF_PAGE_COUNT + 2,
    HANDOFF_TOTAL_MAPPING_COUNT =
        HANDOFF_GRANTOR_MAPPING_COUNT
        + HANDOFF_GRANTEE_MAPPING_COUNT,
};

enum handoff_test_state {
    HANDOFF_TEST_IDLE = 0,
    HANDOFF_TEST_USER_RUNNING,
    HANDOFF_TEST_SUPERVISOR,
};

enum handoff_syscall_command {
    HANDOFF_SYSCALL_NONE = 0,
    HANDOFF_SYSCALL_CREATE_READ,
    HANDOFF_SYSCALL_CREATE_WRITE,
    HANDOFF_SYSCALL_COPY_FROM_LOCAL,
    HANDOFF_SYSCALL_COPY_FROM_CROSS,
    HANDOFF_SYSCALL_COPY_TO_LOCAL,
    HANDOFF_SYSCALL_COPY_TO_CROSS,
    HANDOFF_SYSCALL_COPY_FROM_ZERO,
    HANDOFF_SYSCALL_COPY_TO_ZERO,
    HANDOFF_SYSCALL_WRONG_DIRECTION,
    HANDOFF_SYSCALL_RANGE,
    HANDOFF_SYSCALL_UNMAPPED,
    HANDOFF_SYSCALL_PERMISSION,
    HANDOFF_SYSCALL_REVOKE_READ,
    HANDOFF_SYSCALL_REVOKE_WRITE,
    HANDOFF_SYSCALL_STALE_READ,
    HANDOFF_SYSCALL_STALE_WRITE,
};

enum handoff_syscall_control_action {
    HANDOFF_SYSCALL_CONTROL_NONE = 0,
    HANDOFF_SYSCALL_CONTROL_SWITCH,
    HANDOFF_SYSCALL_CONTROL_FINISH,
};

struct handoff_syscall_script {
    bool armed;
    bool awaiting;
    bool token_result;
    bool preserve_state;
    enum handoff_syscall_command command;
    uint64_t expected_result;
    struct micros_user_context captured;
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
static const uint64_t TEST_TTY_SPARE_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x5000);
static const uint64_t TEST_TTY_STACK_ADDRESS =
    MICROS_TTY_UART_VIRTUAL_BASE + MICROS_SV39_PAGE_SIZE;
static const uint64_t TEST_SYSCALL_CONTROL_MAGIC =
    UINT64_C(0x48414e444f464653);

static struct micros_frame_ownership ownership_snapshot;
static struct micros_frame_allocator allocator_snapshot;
static struct micros_kernel_objects objects_snapshot;
static struct micros_kernel_objects objects_observed;
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
static uint64_t tty_spare_physical;
static uint64_t tty_stack_physical;
static struct micros_process_handle
    reserved_processes[HANDOFF_RESERVED_PROCESS_COUNT];
static struct micros_process_handle processes[HANDOFF_PROCESS_COUNT];
static struct micros_thread_handle threads[HANDOFF_PROCESS_COUNT];
static micros_endpoint_t endpoints[HANDOFF_PROCESS_COUNT];
static const struct micros_vm_boot_info *handoff_snapshot_info;
static struct handoff_syscall_script
    syscall_scripts[HANDOFF_PROCESS_COUNT];
static micros_grant_t syscall_read_grant;
static micros_grant_t syscall_write_grant;
static enum handoff_syscall_control_action syscall_control_action;
static size_t syscall_control_next;
static volatile enum handoff_test_state test_state;
static uint64_t failure_stage;

struct managed_inventory_context {
    size_t count;
    bool saw_uart;
};

extern const unsigned char micros_address_space_handoff_payload_start[];
extern const unsigned char micros_address_space_handoff_payload_ecall[];
extern const unsigned char
    micros_address_space_handoff_payload_after_ecall[];
extern const unsigned char micros_address_space_handoff_payload_spin[];
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

static bool record_managed_mapping(
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

static void normalize_syscall_objects(
    struct micros_kernel_objects *objects
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        fill_bytes(
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

static bool snapshot_syscall_state(
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
    fill_bytes(
        &ownership_snapshot,
        0,
        sizeof(ownership_snapshot)
    );
    copy_bytes(
        &ownership_snapshot,
        ledger,
        offsetof(struct micros_frame_ownership, owners)
    );
    copy_bytes(
        ownership_snapshot.owners,
        ledger->owners,
        (size_t)ledger->managed_frame_count
            * sizeof(ledger->owners[0])
    );
    copy_bytes(
        ownership_snapshot.handoff_targets,
        ledger->handoff_targets,
        (size_t)ledger->managed_frame_count
    );
    fill_bytes(&allocator_snapshot, 0, sizeof(allocator_snapshot));
    copy_bytes(
        &allocator_snapshot,
        ledger->allocator,
        offsetof(struct micros_frame_allocator, allocated_bitmap)
    );
    copy_bytes(
        allocator_snapshot.allocated_bitmap,
        ledger->allocator->allocated_bitmap,
        bitmap_words
            * sizeof(ledger->allocator->allocated_bitmap[0])
    );
    copy_bytes(&objects_snapshot, objects, sizeof(objects_snapshot));
    normalize_syscall_objects(&objects_snapshot);
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

static bool syscall_state_matches(
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
            != ownership_snapshot.managed_frame_count
        || ledger->managed_frame_count
            > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
    ) {
        return false;
    }
    bitmap_words = (size_t)(
        (ledger->managed_frame_count + 63) / 64
    );
    copy_bytes(&objects_observed, objects, sizeof(objects_observed));
    normalize_syscall_objects(&objects_observed);
    return (
        bytes_equal(
            &ownership_snapshot,
            ledger,
            offsetof(struct micros_frame_ownership, owners)
        )
        && bytes_equal(
            ownership_snapshot.owners,
            ledger->owners,
            (size_t)ledger->managed_frame_count
                * sizeof(ledger->owners[0])
        )
        && bytes_equal(
            ownership_snapshot.handoff_targets,
            ledger->handoff_targets,
            (size_t)ledger->managed_frame_count
        )
        && bytes_equal(
            &allocator_snapshot,
            ledger->allocator,
            offsetof(
                struct micros_frame_allocator,
                allocated_bitmap
            )
        )
        && bytes_equal(
            allocator_snapshot.allocated_bitmap,
            ledger->allocator->allocated_bitmap,
            bitmap_words
                * sizeof(
                    ledger->allocator->allocated_bitmap[0]
                )
        )
        && bytes_equal(
            &objects_snapshot,
            &objects_observed,
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
    uint64_t words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    for (index = 0; index < sizeof(words) / sizeof(words[0]); ++index) {
        words[index] =
            UINT64_C(0x1000)
            + process_index * UINT64_C(0x1000)
            + index;
    }
    copy_bytes(context, words, sizeof(*context));
    context->sepc = TEST_CODE_ADDRESS
        + (
            (uintptr_t)micros_address_space_handoff_payload_spin
            - (uintptr_t)micros_address_space_handoff_payload_start
        );
    context->sp = TEST_STACK_ADDRESS + MICROS_SV39_PAGE_SIZE;
    context->sstatus = 0;
}

static uint64_t handoff_read_satp(void)
{
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}

static uint64_t handoff_user_address_of(
    const unsigned char *symbol
)
{
    return TEST_CODE_ADDRESS
        + (
            (uintptr_t)symbol
            - (uintptr_t)micros_address_space_handoff_payload_start
        );
}

static uint64_t handoff_expected_satp(uint64_t root)
{
    return MICROS_RISCV_SATP_MODE_SV39 | (root >> 12);
}

static uint64_t handoff_abi_result(int64_t result)
{
    return (uint64_t)result;
}

static size_t handoff_current_actor(const struct micros_hart *hart)
{
    size_t process_index;

    if (hart == NULL) {
        return HANDOFF_PROCESS_COUNT;
    }
    for (
        process_index = 0;
        process_index < HANDOFF_PROCESS_COUNT;
        ++process_index
    ) {
        if (
            hart->current_thread.slot == threads[process_index].slot
            && hart->current_thread.generation
                == threads[process_index].generation
        ) {
            return process_index;
        }
    }
    return HANDOFF_PROCESS_COUNT;
}

static bool handoff_scheduler_state_valid(
    const struct micros_hart *hart,
    size_t actor,
    bool after_return
)
{
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_registry();
    const struct micros_thread *thread;
    const struct micros_process *process;

    if (
        objects == NULL
        || hart == NULL
        || actor >= HANDOFF_PROCESS_COUNT
    ) {
        return false;
    }
    thread = &objects->threads[threads[actor].slot];
    process = &objects->processes[processes[actor].slot];
    return (
        thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
        && thread->generation == threads[actor].generation
        && process->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
        && process->generation == processes[actor].generation
        && hart->current_thread.slot == threads[actor].slot
        && hart->current_thread.generation
            == threads[actor].generation
        && thread->runtime_flags == 0
        && thread->scheduler_assigned
        && thread->ready_linked
        && hart->ready_head[thread->scheduler_priority].slot
            == threads[actor].slot
        && hart->ready_head[thread->scheduler_priority].generation
            == threads[actor].generation
        && hart->trap.primary_stack_bottom
            == thread->kernel_stack_bottom
        && hart->trap.primary_stack_top == thread->kernel_stack_top
        && handoff_read_satp()
            == handoff_expected_satp(process->address_space_root)
        && (
            !after_return
            || (
                hart->accounting_owner
                    == MICROS_SCHEDULER_ACCOUNTING_THREAD
                && hart->accounted_thread.slot
                    == threads[actor].slot
                && hart->accounted_thread.generation
                    == threads[actor].generation
            )
        )
    );
}

static bool handoff_syscall_mismatch(
    uint64_t stage,
    size_t actor,
    enum handoff_syscall_command command,
    const struct micros_trap_frame *frame
)
{
    uart_write("MICROS_TEST_FAILURE handoff-syscall-stage=");
    uart_write_hex64(stage);
    uart_write(" actor=");
    uart_write_hex64(actor);
    uart_write(" command=");
    uart_write_hex64((uint64_t)command);
    if (frame != NULL) {
        uart_write(" a0=");
        uart_write_hex64(frame->a0);
        uart_write(" a7=");
        uart_write_hex64(frame->a7);
        uart_write(" sepc=");
        uart_write_hex64(frame->sepc);
    }
    uart_write("\n");
    uart_flush();
    return false;
}

static void arm_handoff_syscall_raw(
    size_t actor,
    struct micros_user_context *context,
    enum handoff_syscall_command command,
    uint64_t operation,
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t expected_result,
    bool token_result,
    bool preserve_state
)
{
    struct handoff_syscall_script *script =
        &syscall_scripts[actor];

    script->armed = true;
    script->awaiting = false;
    script->token_result = token_result;
    script->preserve_state = preserve_state;
    script->command = command;
    script->expected_result = expected_result;
    context->a0 = a0;
    context->a1 = a1;
    context->a2 = a2;
    context->a3 = a3;
    context->a4 = a4;
    context->a5 = 0;
    context->a6 = 0;
    context->a7 = operation;
    context->sepc = handoff_user_address_of(
        micros_address_space_handoff_payload_ecall
    );
}

static bool arm_handoff_syscall(
    size_t actor,
    struct micros_user_context *context,
    enum handoff_syscall_command command
)
{
    switch (command) {
    case HANDOFF_SYSCALL_CREATE_READ:
    case HANDOFF_SYSCALL_CREATE_WRITE:
        arm_handoff_syscall_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_CREATE,
            endpoints[HANDOFF_GRANTEE],
            TEST_DATA_SECOND_ADDRESS - 64,
            256,
            command == HANDOFF_SYSCALL_CREATE_READ
                ? MICROS_GRANT_PERMISSION_READ
                : MICROS_GRANT_PERMISSION_WRITE,
            0,
            0,
            true,
            false
        );
        return true;
    case HANDOFF_SYSCALL_COPY_FROM_LOCAL:
        write_pattern(
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS,
            32,
            UINT8_C(0x31)
        );
        fill_bytes(
            (void *)(uintptr_t)physical_for(
                HANDOFF_GRANTEE,
                TEST_DATA_ADDRESS + UINT64_C(0x0ff)
            ),
            UINT8_C(0xcc),
            34
        );
        arm_handoff_syscall_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[HANDOFF_GRANTOR],
            syscall_read_grant,
            64,
            TEST_DATA_ADDRESS + UINT64_C(0x100),
            32,
            MICROS_SYSCALL_ABI_OK,
            false,
            false
        );
        return true;
    case HANDOFF_SYSCALL_COPY_FROM_CROSS:
        write_pattern(
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 64,
            128,
            UINT8_C(0x41)
        );
        fill_bytes(
            (void *)(uintptr_t)user_physical[HANDOFF_GRANTEE][2],
            UINT8_C(0xcc),
            MICROS_SV39_PAGE_SIZE
        );
        fill_bytes(
            (void *)(uintptr_t)user_physical[HANDOFF_GRANTEE][3],
            UINT8_C(0xcc),
            MICROS_SV39_PAGE_SIZE
        );
        arm_handoff_syscall_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[HANDOFF_GRANTOR],
            syscall_read_grant,
            0,
            TEST_DATA_SECOND_ADDRESS - 32,
            128,
            MICROS_SYSCALL_ABI_OK,
            false,
            false
        );
        return true;
    case HANDOFF_SYSCALL_COPY_TO_LOCAL:
        write_pattern(
            HANDOFF_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x180),
            32,
            UINT8_C(0x51)
        );
        fill_bytes(
            (void *)(uintptr_t)physical_for(
                HANDOFF_GRANTOR,
                TEST_DATA_SECOND_ADDRESS + UINT64_C(0x3f)
            ),
            UINT8_C(0xdd),
            34
        );
        arm_handoff_syscall_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_TO,
            endpoints[HANDOFF_GRANTOR],
            syscall_write_grant,
            128,
            TEST_DATA_ADDRESS + UINT64_C(0x180),
            32,
            MICROS_SYSCALL_ABI_OK,
            false,
            false
        );
        return true;
    case HANDOFF_SYSCALL_COPY_TO_CROSS:
        write_pattern(
            HANDOFF_GRANTEE,
            TEST_DATA_SECOND_ADDRESS - 48,
            96,
            UINT8_C(0x61)
        );
        fill_bytes(
            (void *)(uintptr_t)user_physical[HANDOFF_GRANTOR][2],
            UINT8_C(0xee),
            MICROS_SV39_PAGE_SIZE
        );
        fill_bytes(
            (void *)(uintptr_t)user_physical[HANDOFF_GRANTOR][3],
            UINT8_C(0xee),
            MICROS_SV39_PAGE_SIZE
        );
        arm_handoff_syscall_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_TO,
            endpoints[HANDOFF_GRANTOR],
            syscall_write_grant,
            16,
            TEST_DATA_SECOND_ADDRESS - 48,
            96,
            MICROS_SYSCALL_ABI_OK,
            false,
            false
        );
        return true;
    case HANDOFF_SYSCALL_COPY_FROM_ZERO:
    case HANDOFF_SYSCALL_COPY_TO_ZERO:
        arm_handoff_syscall_raw(
            actor,
            context,
            command,
            command == HANDOFF_SYSCALL_COPY_FROM_ZERO
                ? MICROS_SYSCALL_ABI_GRANT_COPY_FROM
                : MICROS_SYSCALL_ABI_GRANT_COPY_TO,
            endpoints[HANDOFF_GRANTOR],
            command == HANDOFF_SYSCALL_COPY_FROM_ZERO
                ? syscall_read_grant
                : syscall_write_grant,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_OK,
            false,
            true
        );
        return true;
    case HANDOFF_SYSCALL_WRONG_DIRECTION:
        arm_handoff_syscall_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[HANDOFF_GRANTOR],
            syscall_write_grant,
            0,
            TEST_DATA_ADDRESS,
            1,
            handoff_abi_result(MICROS_SYSCALL_ABI_UNAUTHORIZED),
            false,
            true
        );
        return true;
    case HANDOFF_SYSCALL_RANGE:
        arm_handoff_syscall_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[HANDOFF_GRANTOR],
            syscall_read_grant,
            257,
            TEST_DATA_ADDRESS,
            1,
            handoff_abi_result(MICROS_SYSCALL_ABI_RANGE),
            false,
            true
        );
        return true;
    case HANDOFF_SYSCALL_UNMAPPED:
        arm_handoff_syscall_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[HANDOFF_GRANTOR],
            syscall_read_grant,
            0,
            TEST_UNMAPPED_ADDRESS,
            1,
            handoff_abi_result(
                MICROS_SYSCALL_ABI_MEMORY_FAULT
            ),
            false,
            true
        );
        return true;
    case HANDOFF_SYSCALL_PERMISSION:
        arm_handoff_syscall_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[HANDOFF_GRANTOR],
            syscall_read_grant,
            0,
            TEST_READ_ONLY_ADDRESS,
            1,
            handoff_abi_result(
                MICROS_SYSCALL_ABI_MEMORY_FAULT
            ),
            false,
            true
        );
        return true;
    case HANDOFF_SYSCALL_REVOKE_READ:
    case HANDOFF_SYSCALL_REVOKE_WRITE:
        arm_handoff_syscall_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_REVOKE,
            command == HANDOFF_SYSCALL_REVOKE_READ
                ? syscall_read_grant
                : syscall_write_grant,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_OK,
            false,
            false
        );
        return true;
    case HANDOFF_SYSCALL_STALE_READ:
    case HANDOFF_SYSCALL_STALE_WRITE:
        arm_handoff_syscall_raw(
            actor,
            context,
            command,
            command == HANDOFF_SYSCALL_STALE_READ
                ? MICROS_SYSCALL_ABI_GRANT_COPY_FROM
                : MICROS_SYSCALL_ABI_GRANT_COPY_TO,
            endpoints[HANDOFF_GRANTOR],
            command == HANDOFF_SYSCALL_STALE_READ
                ? syscall_read_grant
                : syscall_write_grant,
            0,
            TEST_DATA_ADDRESS,
            1,
            handoff_abi_result(
                MICROS_SYSCALL_ABI_STALE_GRANT
            ),
            false,
            true
        );
        return true;
    case HANDOFF_SYSCALL_NONE:
        return false;
    }
    return false;
}

static void arm_handoff_control(
    struct micros_user_context *context
)
{
    context->a0 = TEST_SYSCALL_CONTROL_MAGIC;
    context->a7 = UINT64_MAX;
    context->sepc = handoff_user_address_of(
        micros_address_space_handoff_payload_ecall
    );
}

static bool schedule_handoff_syscall(
    size_t current,
    size_t next,
    enum handoff_syscall_command command,
    struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_user_context *context;

    if (
        objects == NULL
        || next >= HANDOFF_PROCESS_COUNT
    ) {
        return false;
    }
    context = current == next
        ? (struct micros_user_context *)frame
        : &objects->threads[threads[next].slot].user_context;
    if (!arm_handoff_syscall(next, context, command)) {
        return false;
    }
    if (current == next) {
        return true;
    }
    syscall_control_action = HANDOFF_SYSCALL_CONTROL_SWITCH;
    syscall_control_next = next;
    arm_handoff_control((struct micros_user_context *)frame);
    return true;
}

static bool finish_handoff_syscalls(
    struct micros_trap_frame *frame
)
{
    syscall_control_action = HANDOFF_SYSCALL_CONTROL_FINISH;
    syscall_control_next = HANDOFF_PROCESS_COUNT;
    arm_handoff_control((struct micros_user_context *)frame);
    return true;
}

static bool transition_handoff_syscall(
    size_t actor,
    enum handoff_syscall_command command,
    struct micros_trap_frame *frame
)
{
    switch (command) {
    case HANDOFF_SYSCALL_CREATE_READ:
        return schedule_handoff_syscall(
            actor,
            actor,
            HANDOFF_SYSCALL_CREATE_WRITE,
            frame
        );
    case HANDOFF_SYSCALL_CREATE_WRITE:
        return schedule_handoff_syscall(
            actor,
            HANDOFF_GRANTEE,
            HANDOFF_SYSCALL_COPY_FROM_LOCAL,
            frame
        );
    case HANDOFF_SYSCALL_COPY_FROM_LOCAL:
        return schedule_handoff_syscall(
            actor,
            actor,
            HANDOFF_SYSCALL_COPY_FROM_CROSS,
            frame
        );
    case HANDOFF_SYSCALL_COPY_FROM_CROSS:
        return schedule_handoff_syscall(
            actor,
            actor,
            HANDOFF_SYSCALL_COPY_TO_LOCAL,
            frame
        );
    case HANDOFF_SYSCALL_COPY_TO_LOCAL:
        return schedule_handoff_syscall(
            actor,
            actor,
            HANDOFF_SYSCALL_COPY_TO_CROSS,
            frame
        );
    case HANDOFF_SYSCALL_COPY_TO_CROSS:
        return schedule_handoff_syscall(
            actor,
            actor,
            HANDOFF_SYSCALL_COPY_FROM_ZERO,
            frame
        );
    case HANDOFF_SYSCALL_COPY_FROM_ZERO:
        return schedule_handoff_syscall(
            actor,
            actor,
            HANDOFF_SYSCALL_COPY_TO_ZERO,
            frame
        );
    case HANDOFF_SYSCALL_COPY_TO_ZERO:
        return schedule_handoff_syscall(
            actor,
            actor,
            HANDOFF_SYSCALL_WRONG_DIRECTION,
            frame
        );
    case HANDOFF_SYSCALL_WRONG_DIRECTION:
        return schedule_handoff_syscall(
            actor,
            actor,
            HANDOFF_SYSCALL_RANGE,
            frame
        );
    case HANDOFF_SYSCALL_RANGE:
        return schedule_handoff_syscall(
            actor,
            actor,
            HANDOFF_SYSCALL_UNMAPPED,
            frame
        );
    case HANDOFF_SYSCALL_UNMAPPED:
        return schedule_handoff_syscall(
            actor,
            actor,
            HANDOFF_SYSCALL_PERMISSION,
            frame
        );
    case HANDOFF_SYSCALL_PERMISSION:
        return schedule_handoff_syscall(
            actor,
            HANDOFF_GRANTOR,
            HANDOFF_SYSCALL_REVOKE_READ,
            frame
        );
    case HANDOFF_SYSCALL_REVOKE_READ:
        return schedule_handoff_syscall(
            actor,
            actor,
            HANDOFF_SYSCALL_REVOKE_WRITE,
            frame
        );
    case HANDOFF_SYSCALL_REVOKE_WRITE:
        return schedule_handoff_syscall(
            actor,
            HANDOFF_GRANTEE,
            HANDOFF_SYSCALL_STALE_READ,
            frame
        );
    case HANDOFF_SYSCALL_STALE_READ:
        return schedule_handoff_syscall(
            actor,
            actor,
            HANDOFF_SYSCALL_STALE_WRITE,
            frame
        );
    case HANDOFF_SYSCALL_STALE_WRITE:
        return finish_handoff_syscalls(frame);
    case HANDOFF_SYSCALL_NONE:
        return false;
    }
    return false;
}

static bool validate_handoff_syscall_effect(
    enum handoff_syscall_command command,
    const struct micros_trap_frame *frame
)
{
    switch (command) {
    case HANDOFF_SYSCALL_CREATE_READ:
        syscall_read_grant = (micros_grant_t)frame->a0;
        return true;
    case HANDOFF_SYSCALL_CREATE_WRITE:
        syscall_write_grant = (micros_grant_t)frame->a0;
        return syscall_write_grant != syscall_read_grant;
    case HANDOFF_SYSCALL_COPY_FROM_LOCAL:
        return (
            pattern_matches(
                HANDOFF_GRANTEE,
                TEST_DATA_ADDRESS + UINT64_C(0x100),
                32,
                UINT8_C(0x31)
            )
            && *(const unsigned char *)(uintptr_t)physical_for(
                HANDOFF_GRANTEE,
                TEST_DATA_ADDRESS + UINT64_C(0x0ff)
            ) == UINT8_C(0xcc)
            && *(const unsigned char *)(uintptr_t)physical_for(
                HANDOFF_GRANTEE,
                TEST_DATA_ADDRESS + UINT64_C(0x120)
            ) == UINT8_C(0xcc)
        );
    case HANDOFF_SYSCALL_COPY_FROM_CROSS:
        return pattern_matches(
            HANDOFF_GRANTEE,
            TEST_DATA_SECOND_ADDRESS - 32,
            128,
            UINT8_C(0x41)
        );
    case HANDOFF_SYSCALL_COPY_TO_LOCAL:
        return (
            pattern_matches(
                HANDOFF_GRANTOR,
                TEST_DATA_SECOND_ADDRESS + UINT64_C(0x40),
                32,
                UINT8_C(0x51)
            )
            && *(const unsigned char *)(uintptr_t)physical_for(
                HANDOFF_GRANTOR,
                TEST_DATA_SECOND_ADDRESS + UINT64_C(0x3f)
            ) == UINT8_C(0xdd)
            && *(const unsigned char *)(uintptr_t)physical_for(
                HANDOFF_GRANTOR,
                TEST_DATA_SECOND_ADDRESS + UINT64_C(0x60)
            ) == UINT8_C(0xdd)
        );
    case HANDOFF_SYSCALL_COPY_TO_CROSS:
        return pattern_matches(
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 48,
            96,
            UINT8_C(0x61)
        );
    default:
        return true;
    }
}

bool micros_address_space_handoff_test_before_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_endpoint_registry *endpoint_registry =
        micros_ipc_runtime_authoritative_registry();
    struct micros_grant_registry *grant_registry =
        micros_grant_runtime_authoritative_registry();
    size_t actor = handoff_current_actor(hart);
    struct handoff_syscall_script *script;

    if (
        test_state != HANDOFF_TEST_USER_RUNNING
        || frame == NULL
        || actor >= HANDOFF_PROCESS_COUNT
        || !handoff_scheduler_state_valid(hart, actor, false)
    ) {
        return handoff_syscall_mismatch(
            1,
            actor,
            HANDOFF_SYSCALL_NONE,
            frame
        );
    }
    script = &syscall_scripts[actor];
    if (
        !script->armed
        || script->awaiting
        || frame->sepc
            != handoff_user_address_of(
                micros_address_space_handoff_payload_ecall
            )
    ) {
        return handoff_syscall_mismatch(
            2,
            actor,
            script->command,
            frame
        );
    }
    if (
        script->preserve_state
        && !snapshot_syscall_state(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
    ) {
        return handoff_syscall_mismatch(
            UINT64_C(0x20),
            actor,
            script->command,
            frame
        );
    }
    copy_bytes(
        &script->captured,
        (const struct micros_user_context *)frame,
        sizeof(script->captured)
    );
    script->armed = false;
    script->awaiting = true;
    return true;
}

bool micros_address_space_handoff_test_after_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
)
{
    size_t actor = handoff_current_actor(hart);

    return (
        frame != NULL
        && actor < HANDOFF_PROCESS_COUNT
        && syscall_scripts[actor].awaiting
        && syscall_return == MICROS_SYSCALL_RETURN_NORMAL
    );
}

bool micros_address_space_handoff_test_after_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_endpoint_registry *endpoint_registry =
        micros_ipc_runtime_authoritative_registry();
    struct micros_grant_registry *grant_registry =
        micros_grant_runtime_authoritative_registry();
    size_t actor = handoff_current_actor(hart);
    struct handoff_syscall_script *script;
    struct micros_user_context expected;
    enum handoff_syscall_command completed;

    if (
        frame == NULL
        || actor >= HANDOFF_PROCESS_COUNT
        || !handoff_scheduler_state_valid(hart, actor, true)
    ) {
        return handoff_syscall_mismatch(
            3,
            actor,
            HANDOFF_SYSCALL_NONE,
            frame
        );
    }
    script = &syscall_scripts[actor];
    if (!script->awaiting) {
        return handoff_syscall_mismatch(
            4,
            actor,
            script->command,
            frame
        );
    }
    copy_bytes(&expected, &script->captured, sizeof(expected));
    if (script->token_result) {
        if (
            frame->a0 >= MICROS_GRANT_NONE
            || (int64_t)frame->a0 < 0
        ) {
            return handoff_syscall_mismatch(
                UINT64_C(0x41),
                actor,
                script->command,
                frame
            );
        }
        expected.a0 = frame->a0;
    } else {
        expected.a0 = script->expected_result;
    }
    expected.sepc = handoff_user_address_of(
        micros_address_space_handoff_payload_after_ecall
    );
    if (!bytes_equal(frame, &expected, sizeof(expected))) {
        return handoff_syscall_mismatch(
            5,
            actor,
            script->command,
            frame
        );
    }
    if (
        script->preserve_state
        && !syscall_state_matches(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
    ) {
        return handoff_syscall_mismatch(
            UINT64_C(0x50),
            actor,
            script->command,
            frame
        );
    }
    completed = script->command;
    if (!validate_handoff_syscall_effect(completed, frame)) {
        return handoff_syscall_mismatch(
            UINT64_C(0x60),
            actor,
            completed,
            frame
        );
    }
    script->awaiting = false;
    script->command = HANDOFF_SYSCALL_NONE;
    if (!transition_handoff_syscall(actor, completed, frame)) {
        return handoff_syscall_mismatch(
            6,
            actor,
            completed,
            frame
        );
    }
    return true;
}

static bool stage_wired_pages(void)
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
    fill_bytes(&manifest, 0, sizeof(manifest));
    fill_bytes(bindings, 0, sizeof(bindings));
    fill_bytes(&result, 0, sizeof(result));
    manifest.header.entry_count = HANDOFF_PROCESS_COUNT;
    for (index = 0; index < HANDOFF_PROCESS_COUNT; ++index) {
        manifest.entries[index].service_id = (uint32_t)index + 1;
        manifest.entries[index].process_slot = processes[index].slot;
        manifest.entries[index].role_flags = (
            index == HANDOFF_GRANTEE
        )
            ? MICROS_BOOTSTRAP_ROLE_VM
            : 0;
        bindings[index] = (struct micros_bootstrap_binding){
            .manifest_index = (uint16_t)index,
            .service_id = (uint32_t)index + 1,
            .process = processes[index],
            .thread = threads[index],
            .root = objects->processes[
                processes[index].slot
            ].address_space_root,
            .endpoint = endpoints[index],
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
        handoff_snapshot_info = result.info;
        return true;
    }
    return false;
}

static bool validate_wired_snapshot(void)
{
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_registry();
    struct micros_bootstrap_manifest manifest;
    struct micros_bootstrap_binding
        bindings[HANDOFF_PROCESS_COUNT];
    size_t index;

    if (objects == NULL || handoff_snapshot_info == NULL) {
        return false;
    }
    fill_bytes(&manifest, 0, sizeof(manifest));
    fill_bytes(bindings, 0, sizeof(bindings));
    manifest.header.entry_count = HANDOFF_PROCESS_COUNT;
    for (index = 0; index < HANDOFF_PROCESS_COUNT; ++index) {
        manifest.entries[index].service_id = (uint32_t)index + 1;
        manifest.entries[index].process_slot = processes[index].slot;
        manifest.entries[index].role_flags = (
            index == HANDOFF_GRANTEE
        )
            ? MICROS_BOOTSTRAP_ROLE_VM
            : 0;
        bindings[index] = (struct micros_bootstrap_binding){
            .manifest_index = (uint16_t)index,
            .service_id = (uint32_t)index + 1,
            .process = processes[index],
            .thread = threads[index],
            .root = objects->processes[
                processes[index].slot
            ].address_space_root,
            .endpoint = endpoints[index],
        };
    }
    return micros_vm_snapshot_validate_current(
        &manifest,
        bindings,
        HANDOFF_PROCESS_COUNT,
        handoff_snapshot_info
    ) == MICROS_BOOTSTRAP_OK;
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
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t actor = handoff_current_actor(hart);
    struct micros_user_context expected;
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
        || objects == NULL
        || hart == NULL
        || frame == NULL
        || (frame->scause >> 63) != 0
        || (frame->scause & (UINT64_C(1) << 63) - 1) != 8
        || actor >= HANDOFF_PROCESS_COUNT
        || frame->sepc
            != handoff_user_address_of(
                micros_address_space_handoff_payload_ecall
            )
        || frame->a0 != TEST_SYSCALL_CONTROL_MAGIC
        || frame->a7 != UINT64_MAX
        || syscall_control_action
            == HANDOFF_SYSCALL_CONTROL_NONE
    ) {
        return MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_MISMATCH;
    }
    if (
        syscall_control_action
            == HANDOFF_SYSCALL_CONTROL_SWITCH
    ) {
        const struct micros_thread *next =
            &objects->threads[threads[syscall_control_next].slot];

        copy_bytes(&expected, &next->user_context, sizeof(expected));
        if (
            (
                next->scheduler_assigned
                ? micros_thread_runtime_flags_unset(
                    objects,
                    threads[syscall_control_next],
                    MICROS_THREAD_RTS_INACTIVE
                )
                : micros_thread_scheduler_admit(
                    objects,
                    micros_kernel_object_runtime_boot_hart_handle(),
                    threads[syscall_control_next],
                    MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
                    UINT64_MAX,
                    true
                )
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_hold(
                objects,
                threads[actor]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_scheduler_select_user_return(hart, frame)
                != MICROS_SCHEDULER_OK
            || handoff_current_actor(hart)
                != syscall_control_next
            || !bytes_equal(frame, &expected, sizeof(expected))
            || !handoff_scheduler_state_valid(
                hart,
                syscall_control_next,
                true
            )
        ) {
            return MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_MISMATCH;
        }
        syscall_control_action = HANDOFF_SYSCALL_CONTROL_NONE;
        return MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_USER_RETURN;
    }
    if (
        syscall_control_action
            != HANDOFF_SYSCALL_CONTROL_FINISH
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
    syscall_control_action = HANDOFF_SYSCALL_CONTROL_NONE;
    test_state = HANDOFF_TEST_SUPERVISOR;
    return MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_SUPERVISOR_RETURN;
}

bool micros_address_space_handoff_runtime_run_self_test(void)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "HANDOFF_TEST",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        },
        {
            .id = MICROS_PRIVILEGE_PROFILE_TTY,
            .name = "TTY",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
            .kernel_operations = MICROS_KERNEL_OPERATION_TTY_CONTROL,
        },
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
    struct micros_bootstrap_control_state tty_bootstrap;
    struct micros_bootstrap_binding tty_binding;
    struct micros_bootstrap_manifest_entry tty_entry;
    struct micros_process_handle empty_process;
    struct micros_user_context contexts[HANDOFF_PROCESS_COUNT];
    struct micros_user_context observed_context;
    struct micros_syscall_arguments grant_arguments;
    struct micros_syscall_context grant_context;
    struct micros_trap_frame grant_frame;
    struct micros_hart grant_hart;
    struct micros_thread thread_snapshot;
    struct micros_ipc_message message;
    struct micros_ipc_message observed_message;
    struct micros_frame_owner process_user_owner;
    struct micros_frame_owner retained_owner;
    struct micros_frame_owner saved_owner;
    struct micros_grant_record grant_record;
    struct micros_user_address_space_tty_uart_plan tty_map_plan;
    struct micros_user_address_space_tty_uart_plan tty_map_sentinel;
    struct micros_tty_handoff mapped_handoff;
    struct micros_tty_handoff mapped_handoff_sentinel;
    struct micros_tty_handoff_runtime_state *tty_runtime;
    struct managed_inventory_context inventory;
    micros_grant_t read_grant;
    micros_grant_t write_grant;
    micros_grant_t device_copy_grant;
    uint64_t *tty_spare_pte;
    uint64_t *tty_uart_pte;
    uint64_t *corrupt_pte;
    uint64_t saved_spare_pte;
    uint64_t saved_uart_pte;
    uint64_t saved_pte;
    uint64_t frame_index;
    uint64_t inventory_root;
    uint64_t tty_root;
    uint64_t orphan_physical;
    uint64_t physical_address;
    uint64_t stale_snapshot_frame;
    uint32_t permissions;
    size_t contiguous;
    size_t inventory_count;
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
    fill_bytes(&tty_bootstrap, 0, sizeof(tty_bootstrap));
    failure_stage = 1;
    objects = micros_kernel_object_runtime_test_registry();
    ledger = micros_frame_ownership_runtime_ledger();
    if (
        objects == NULL
        || ledger == NULL
        || micros_ipc_runtime_registry() != NULL
        || micros_grant_runtime_registry() != NULL
        || micros_ipc_runtime_initialize(
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        )
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
        process_index < HANDOFF_RESERVED_PROCESS_COUNT;
        ++process_index
    ) {
        if (
            micros_process_create(
                objects,
                &reserved_processes[process_index]
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            goto done;
        }
    }
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
        processes[HANDOFF_GRANTEE].slot != MICROS_TTY_PROCESS_SLOT
        || micros_user_address_space_allocate_page(
            processes[HANDOFF_GRANTEE],
            TEST_TTY_SPARE_ADDRESS,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE,
            &tty_spare_physical
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            processes[HANDOFF_GRANTEE],
            TEST_TTY_STACK_ADDRESS,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE,
            &tty_stack_physical
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        goto done;
    }
    fill_bytes(
        (void *)(uintptr_t)tty_spare_physical,
        UINT8_C(0xc3),
        MICROS_SV39_PAGE_SIZE
    );
    fill_bytes(
        (void *)(uintptr_t)tty_stack_physical,
        UINT8_C(0x3c),
        MICROS_SV39_PAGE_SIZE
    );
    fill_bytes(&tty_binding, 0, sizeof(tty_binding));
    fill_bytes(&tty_entry, 0, sizeof(tty_entry));
    tty_binding.service_id = MICROS_TTY_SERVICE_ID;
    tty_binding.process = processes[HANDOFF_GRANTEE];
    tty_binding.thread = threads[HANDOFF_GRANTEE];
    tty_binding.root = objects->processes[
        processes[HANDOFF_GRANTEE].slot
    ].address_space_root;
    tty_binding.endpoint = endpoints[HANDOFF_GRANTEE];
    tty_binding.scheduler_priority =
        MICROS_SCHEDULER_PRIORITY_DEFAULT_USER;
    tty_binding.scheduler_preemptible = true;
    tty_binding.scheduler_quantum_counter_ticks = UINT64_MAX;
    tty_entry.service_id = MICROS_TTY_SERVICE_ID;
    tty_entry.image_id = 104;
    tty_entry.process_slot = MICROS_TTY_PROCESS_SLOT;
    tty_entry.profile_id = MICROS_PRIVILEGE_PROFILE_TTY;
    tty_entry.role_flags = MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER;
    tty_entry.irq_source = MICROS_TTY_UART_IRQ_SOURCE;
    tty_entry.device_base = MICROS_TTY_UART_PHYSICAL_BASE;
    tty_entry.device_length = MICROS_TTY_UART_MAPPED_LENGTH;
    tty_bootstrap.entry_count = 1;
    tty_bootstrap.plan.console_service_id = MICROS_TTY_SERVICE_ID;
    copy_bytes(
        &tty_bootstrap.bindings[0],
        &tty_binding,
        sizeof(tty_binding)
    );
    copy_bytes(
        &tty_bootstrap.manifest.entries[0],
        &tty_entry,
        sizeof(tty_entry)
    );
    tty_bootstrap.transitions.entry_count = 1;
    tty_bootstrap.transitions.entries[0] =
        (struct micros_bootstrap_runtime_entry){
            .service_id = MICROS_TTY_SERVICE_ID,
            .state = MICROS_BOOTSTRAP_SERVICE_PREPARED,
            .endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_RESERVED,
        };
    if (
        micros_tty_handoff_runtime_reset()
            != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_runtime_prepare(
            &tty_binding,
            &tty_entry
        ) != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_runtime_bind_bootstrap(
            &tty_bootstrap
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        goto done;
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
    if (
        micros_user_execution_detach(threads[HANDOFF_GRANTEE])
            != MICROS_USER_EXECUTION_OK
        || !snapshot_state(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
        || micros_user_address_space_prepare_wired_handoff()
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
        || micros_frame_ownership_runtime_allocate(
            process_user_owner,
            &orphan_physical
        ) != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto done;
    }
    if (
        !snapshot_state(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
    ) {
        goto done;
    }
    if (
        micros_user_address_space_prepare_wired_handoff()
            != MICROS_USER_ADDRESS_SPACE_ERROR_STATE
    ) {
        goto done;
    }
    if (
        !state_matches(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
    ) {
        goto done;
    }
    if (
        micros_frame_ownership_runtime_release(
            process_user_owner,
            orphan_physical
        ) != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto done;
    }
    if (
        !stage_wired_pages()
        || !validate_wired_snapshot()
        || micros_frame_owner_make_kernel(
            MICROS_FRAME_OWNER_KERNEL_RETAINED,
            &retained_owner
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_allocate(
            retained_owner,
            &stale_snapshot_frame
        ) != MICROS_FRAME_OWNERSHIP_OK
        || validate_wired_snapshot()
        || micros_frame_ownership_runtime_release(
            retained_owner,
            stale_snapshot_frame
        ) != MICROS_FRAME_OWNERSHIP_OK
        || !validate_wired_snapshot()
    ) {
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
        || micros_user_execution_prepare(
            threads[HANDOFF_GRANTEE],
            &contexts[HANDOFF_GRANTEE]
        ) != MICROS_USER_EXECUTION_OK
    ) {
        goto done;
    }
    if (
        micros_frame_ownership_runtime_prepare_handoff(
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
    if (
        !arm_handoff_syscall(
            HANDOFF_GRANTOR,
            &objects->threads[
                threads[HANDOFF_GRANTOR].slot
            ].user_context,
            HANDOFF_SYSCALL_CREATE_READ
        )
    ) {
        goto done;
    }
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
        || syscall_scripts[HANDOFF_GRANTOR].armed
        || syscall_scripts[HANDOFF_GRANTOR].awaiting
        || syscall_scripts[HANDOFF_GRANTEE].armed
        || syscall_scripts[HANDOFF_GRANTEE].awaiting
        || micros_grant_inspect(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTOR],
            syscall_read_grant,
            &grant_record
        ) != MICROS_GRANT_ERROR_STALE_GRANT
        || micros_grant_inspect(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTOR],
            syscall_write_grant,
            &grant_record
        ) != MICROS_GRANT_ERROR_STALE_GRANT
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

    if (
        (
            objects->threads[
                threads[HANDOFF_GRANTEE].slot
            ].scheduler_assigned
            && micros_thread_scheduler_remove(
                objects,
                threads[HANDOFF_GRANTEE]
            ) != MICROS_KERNEL_OBJECT_OK
        )
        || objects->threads[
            threads[HANDOFF_GRANTEE].slot
        ].runtime_flags != MICROS_THREAD_RTS_INACTIVE
    ) {
        goto done;
    }
    objects->processes[
        processes[HANDOFF_GRANTEE].slot
    ].privilege_profile = 0;
    endpoint_registry->endpoints[
        processes[HANDOFF_GRANTEE].slot
    ].state = MICROS_ENDPOINT_STATE_RESERVED;
    if (
        micros_tty_handoff_runtime_validate(
            &tty_bootstrap,
            endpoint_registry,
            objects
        ) != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_runtime_begin(100, 100)
            != MICROS_TTY_HANDOFF_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
    ) {
        goto done;
    }

    failure_stage = 11;
    failure_stage = UINT64_C(0x1101);
    tty_root = objects->processes[
        processes[HANDOFF_GRANTEE].slot
    ].address_space_root;
    fill_bytes(
        &tty_map_sentinel,
        UINT8_C(0xa5),
        sizeof(tty_map_sentinel)
    );
    tty_map_plan = tty_map_sentinel;
    if (
        micros_user_address_space_prepare_tty_uart_mapping(
            processes[HANDOFF_GRANTOR],
            objects->processes[
                processes[HANDOFF_GRANTOR].slot
            ].address_space_root,
            &tty_map_plan
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED
        || !bytes_equal(
            &tty_map_plan,
            &tty_map_sentinel,
            sizeof(tty_map_plan)
        )
    ) {
        goto done;
    }
    tty_spare_pte = leaf_pte_for(
        objects,
        processes[HANDOFF_GRANTEE],
        TEST_TTY_SPARE_ADDRESS
    );
    tty_uart_pte = leaf_pte_for(
        objects,
        processes[HANDOFF_GRANTEE],
        MICROS_TTY_UART_VIRTUAL_BASE
    );
    if (
        tty_spare_pte == NULL
        || tty_uart_pte == NULL
        || *tty_spare_pte == 0
        || *tty_uart_pte != 0
    ) {
        goto done;
    }
    saved_spare_pte = *tty_spare_pte;
    failure_stage = UINT64_C(0x1102);
    *tty_spare_pte = 0;
    *tty_uart_pte = saved_spare_pte;
    tty_map_plan = tty_map_sentinel;
    if (
        micros_user_address_space_validate(
            processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_prepare_tty_uart_mapping(
            processes[HANDOFF_GRANTEE],
            tty_root,
            &tty_map_plan
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT
        || !bytes_equal(
            &tty_map_plan,
            &tty_map_sentinel,
            sizeof(tty_map_plan)
        )
    ) {
        goto done;
    }
    *tty_uart_pte = 0;
    *tty_spare_pte = saved_spare_pte;
    failure_stage = UINT64_C(0x1103);
    if (
        micros_user_address_space_activate(
            processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        goto done;
    }
    tty_map_plan = tty_map_sentinel;
    if (
        micros_user_address_space_prepare_tty_uart_mapping(
            processes[HANDOFF_GRANTEE],
            tty_root,
            &tty_map_plan
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        || !bytes_equal(
            &tty_map_plan,
            &tty_map_sentinel,
            sizeof(tty_map_plan)
        )
        || micros_user_address_space_activate_kernel()
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_prepare_tty_uart_mapping(
            processes[HANDOFF_GRANTEE],
            tty_root,
            &tty_map_plan
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_tty_handoff_runtime_prepare_mapped(
            &mapped_handoff
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        goto done;
    }
    micros_user_address_space_commit_tty_uart_mapping(
        &tty_map_plan
    );
    micros_tty_handoff_runtime_commit_mapped_prevalidated(
        &mapped_handoff
    );
    fill_bytes(
        &mapped_handoff_sentinel,
        UINT8_C(0xa5),
        sizeof(mapped_handoff_sentinel)
    );
    mapped_handoff = mapped_handoff_sentinel;
    failure_stage = UINT64_C(0x1104);
    if (
        micros_tty_handoff_runtime_prepare_mapped(
            &mapped_handoff
        ) != MICROS_TTY_HANDOFF_ERROR_STATE
        || !bytes_equal(
            &mapped_handoff,
            &mapped_handoff_sentinel,
            sizeof(mapped_handoff)
        )
        || micros_user_address_space_validate(
            processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_validate_tty_uart_mapping(
            processes[HANDOFF_GRANTEE],
            tty_root
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        goto done;
    }

    physical_address = UINT64_C(0xfeedfacefeedface);
    failure_stage = UINT64_C(0x1105);
    permissions = UINT32_MAX;
    if (
        micros_user_address_space_lookup(
            processes[HANDOFF_GRANTEE],
            MICROS_TTY_UART_VIRTUAL_BASE,
            &physical_address,
            &permissions
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || permissions != UINT32_MAX
    ) {
        goto done;
    }
    physical_address = UINT64_C(0xfacefeedfacefeed);
    failure_stage = UINT64_C(0x1106);
    permissions = UINT32_MAX;
    contiguous = SIZE_MAX;
    if (
        micros_user_address_space_translate(
            processes[HANDOFF_GRANTEE],
            MICROS_TTY_UART_VIRTUAL_BASE + 17,
            &physical_address,
            &permissions,
            &contiguous
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        || physical_address != UINT64_C(0xfacefeedfacefeed)
        || permissions != UINT32_MAX
        || contiguous != SIZE_MAX
    ) {
        goto done;
    }
    inventory = (struct managed_inventory_context){0};
    failure_stage = UINT64_C(0x1107);
    inventory_root = 0;
    inventory_count = 0;
    if (
        micros_user_address_space_inventory(
            processes[HANDOFF_GRANTEE],
            record_managed_mapping,
            &inventory,
            &inventory_root,
            &inventory_count
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || inventory_root != tty_root
        || inventory_count != HANDOFF_GRANTEE_MAPPING_COUNT
        || inventory.count != inventory_count
        || inventory.saw_uart
    ) {
        goto done;
    }
    fill_bytes(
        &observed_message,
        UINT8_C(0xa5),
        sizeof(observed_message)
    );
    message = observed_message;
    failure_stage = UINT64_C(0x1108);
    if (
        micros_ipc_buffer_snapshot(
            processes[HANDOFF_GRANTEE],
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_IPC_BUFFER_READ,
            &observed_message
        ) != MICROS_IPC_BUFFER_ERROR_INVARIANT
        || !bytes_equal(
            &observed_message,
            &message,
            sizeof(observed_message)
        )
    ) {
        goto done;
    }

    saved_uart_pte = *tty_uart_pte;
    failure_stage = UINT64_C(0x1109);
    if (
        micros_sv39_make_leaf_pte(
            MICROS_TTY_UART_PHYSICAL_BASE,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE
                | MICROS_SV39_PERMISSION_USER,
            &saved_pte
        ) != MICROS_SV39_OK
    ) {
        goto done;
    }
    *tty_uart_pte = saved_pte;
    if (
        micros_user_address_space_validate(
            processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
    ) {
        goto done;
    }
    if (
        micros_sv39_make_leaf_pte(
            MICROS_TTY_UART_PHYSICAL_BASE
                + MICROS_SV39_PAGE_SIZE,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_USER,
            &saved_pte
        ) != MICROS_SV39_OK
    ) {
        goto done;
    }
    *tty_uart_pte = saved_pte;
    if (
        micros_user_address_space_validate(
            processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
    ) {
        goto done;
    }
    *tty_uart_pte = 0;
    if (
        micros_user_address_space_validate(
            processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
    ) {
        goto done;
    }
    *tty_uart_pte = saved_uart_pte;
    saved_spare_pte = *tty_spare_pte;
    *tty_spare_pte = saved_uart_pte;
    if (
        micros_user_address_space_validate(
            processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
    ) {
        goto done;
    }
    *tty_spare_pte = saved_spare_pte;
    physical_address = UINT64_C(0x1122334455667788);
    if (
        micros_user_address_space_release_page(
            processes[HANDOFF_GRANTEE],
            MICROS_TTY_UART_VIRTUAL_BASE,
            &physical_address
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || physical_address != UINT64_C(0x1122334455667788)
        || micros_user_address_space_destroy(
            processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || *tty_uart_pte != saved_uart_pte
        || micros_user_address_space_validate_tty_uart_mapping(
            processes[HANDOFF_GRANTEE],
            tty_root
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        goto done;
    }

    failure_stage = UINT64_C(0x1110);
    tty_runtime =
        micros_tty_handoff_runtime_authoritative_state();
    if (
        tty_runtime == NULL
        || micros_endpoint_install_profile(
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTEE],
            MICROS_PRIVILEGE_PROFILE_TTY
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            endpoint_registry,
            objects,
            endpoints[HANDOFF_GRANTEE]
        ) != MICROS_ENDPOINT_OK
        || micros_thread_scheduler_admit(
            objects,
            micros_kernel_object_runtime_boot_hart_handle(),
            threads[HANDOFF_GRANTEE],
            tty_binding.scheduler_priority,
            tty_binding.scheduler_quantum_counter_ticks,
            tty_binding.scheduler_preemptible
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_tty_handoff_release(&tty_runtime->handoff)
            != MICROS_TTY_HANDOFF_OK
    ) {
        goto done;
    }
    tty_bootstrap.transitions.entries[0].state =
        MICROS_BOOTSTRAP_SERVICE_STARTING;
    tty_bootstrap.transitions.entries[0].endpoint_state =
        MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
    tty_bootstrap.transitions.entries[0].profile_installed = true;
    tty_bootstrap.transitions.entries[0].scheduler_assigned = true;
    tty_bootstrap.transitions.entries[0].ready_deadline =
        tty_runtime->handoff.deadline;
    if (
        micros_tty_handoff_runtime_validate(
            &tty_bootstrap,
            endpoint_registry,
            objects
        ) != MICROS_TTY_HANDOFF_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
    ) {
        goto done;
    }

    fill_bytes(&grant_hart, 0, sizeof(grant_hart));
    grant_hart.hardware_id = 0;
    fill_bytes(&grant_context, 0, sizeof(grant_context));
    grant_context.objects = objects;
    grant_context.process = processes[HANDOFF_GRANTEE];
    grant_context.current = threads[HANDOFF_GRANTEE];
    fill_bytes(&grant_frame, 0, sizeof(grant_frame));
    grant_frame.a0 = UINT64_C(0x1122334455667788);
    fill_bytes(&grant_arguments, 0, sizeof(grant_arguments));
    grant_arguments.a0 = endpoints[HANDOFF_GRANTOR];
    grant_arguments.a1 = MICROS_TTY_UART_VIRTUAL_BASE;
    grant_arguments.a2 = MICROS_TTY_UART_MAPPED_LENGTH;
    grant_arguments.a3 = MICROS_GRANT_PERMISSION_READ;
    grant_arguments.a7 = MICROS_SYSCALL_ABI_GRANT_CREATE;
    if (
        !snapshot_state(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
        || micros_grant_handle_captured_user_ecall(
            &grant_hart,
            &grant_frame,
            &grant_context,
            &grant_arguments
        ) != MICROS_SYSCALL_RETURN_NORMAL
        || grant_frame.a0
            != (uint64_t)(int64_t)
                MICROS_SYSCALL_ABI_MEMORY_FAULT
        || !state_matches(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
    ) {
        goto done;
    }

    failure_stage = UINT64_C(0x1111);
    if (
        micros_grant_create(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTOR],
            endpoints[HANDOFF_GRANTEE],
            TEST_DATA_SECOND_ADDRESS,
            32,
            MICROS_GRANT_PERMISSION_READ,
            &device_copy_grant
        ) != MICROS_GRANT_OK
    ) {
        goto done;
    }
    *tty_uart_pte = 0;
    if (
        micros_grant_copy_from(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTEE],
            endpoints[HANDOFF_GRANTOR],
            device_copy_grant,
            0,
            MICROS_TTY_UART_VIRTUAL_BASE,
            16
        ) != MICROS_GRANT_ERROR_INVARIANT
    ) {
        goto done;
    }
    *tty_uart_pte = saved_uart_pte;
    corrupt_pte = leaf_pte_for(
        objects,
        processes[HANDOFF_GRANTOR],
        TEST_DATA_SECOND_ADDRESS
    );
    if (corrupt_pte == NULL) {
        goto done;
    }
    saved_pte = *corrupt_pte;
    *corrupt_pte = UINT64_C(1);
    if (
        micros_grant_copy_from(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTEE],
            endpoints[HANDOFF_GRANTOR],
            device_copy_grant,
            0,
            MICROS_TTY_UART_VIRTUAL_BASE,
            16
        ) != MICROS_GRANT_ERROR_INVARIANT
    ) {
        goto done;
    }
    *corrupt_pte = saved_pte;
    if (
        !snapshot_state(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
    ) {
        goto done;
    }
    fill_bytes(&grant_frame, 0, sizeof(grant_frame));
    grant_frame.a0 = UINT64_C(0x8877665544332211);
    fill_bytes(&grant_arguments, 0, sizeof(grant_arguments));
    grant_arguments.a0 = endpoints[HANDOFF_GRANTOR];
    grant_arguments.a1 = device_copy_grant;
    grant_arguments.a3 = MICROS_TTY_UART_VIRTUAL_BASE;
    grant_arguments.a4 = 16;
    grant_arguments.a7 = MICROS_SYSCALL_ABI_GRANT_COPY_FROM;
    if (
        micros_grant_handle_captured_user_ecall(
            &grant_hart,
            &grant_frame,
            &grant_context,
            &grant_arguments
        ) != MICROS_SYSCALL_RETURN_NORMAL
        || grant_frame.a0
            != (uint64_t)(int64_t)
                MICROS_SYSCALL_ABI_MEMORY_FAULT
        || !state_matches(
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
        || micros_grant_revoke(
            grant_registry,
            endpoint_registry,
            objects,
            processes[HANDOFF_GRANTOR],
            device_copy_grant
        ) != MICROS_GRANT_OK
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
