#include "kernel/grant_syscall_test.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/grant_runtime_internal.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/kernel_object_runtime_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_copy.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_abi.h"
#include "micros/ipc_core.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/scheduler_core.h"
#include "micros/sv39.h"
#include "micros/syscall_abi.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

#define TEST_SSTATUS_UBE (UINT64_C(1) << 6)
#define TEST_SSTATUS_VS (UINT64_C(3) << 9)
#define TEST_SSTATUS_FS (UINT64_C(3) << 13)
#define TEST_SSTATUS_XS (UINT64_C(3) << 15)
#define TEST_SSTATUS_MXR (UINT64_C(1) << 19)
#define TEST_SSTATUS_SD (UINT64_C(1) << 63)

enum {
    GRANT_SYSCALL_GRANTOR = 0,
    GRANT_SYSCALL_GRANTEE,
    GRANT_SYSCALL_WRONG,
    GRANT_SYSCALL_PROCESS_COUNT,
    GRANT_SYSCALL_PAGE_COUNT = 5,
};

enum grant_syscall_command {
    GRANT_COMMAND_NONE = 0,
    GRANT_COMMAND_IPC_NOTIFY,
    GRANT_COMMAND_CREATE_READ,
    GRANT_COMMAND_CREATE_WRITE,
    GRANT_COMMAND_CREATE_REMOTE_READ_ONLY,
    GRANT_COMMAND_CREATE_UNMAPPED,
    GRANT_COMMAND_CREATE_STALE,
    GRANT_COMMAND_CREATE_UPPER_ENDPOINT,
    GRANT_COMMAND_CREATE_UPPER_PERMISSION,
    GRANT_COMMAND_CREATE_UNUSED,
    GRANT_COMMAND_REVOKE_STALE,
    GRANT_COMMAND_REVOKE_UPPER_TOKEN,
    GRANT_COMMAND_REVOKE_UNUSED,
    GRANT_COMMAND_COPY_FROM_CROSS,
    GRANT_COMMAND_COPY_TO_CROSS,
    GRANT_COMMAND_COPY_FROM_LOCAL,
    GRANT_COMMAND_COPY_TO_LOCAL,
    GRANT_COMMAND_COPY_FROM_ZERO,
    GRANT_COMMAND_COPY_TO_ZERO,
    GRANT_COMMAND_COPY_FROM_WRONG_DIRECTION,
    GRANT_COMMAND_COPY_TO_WRONG_DIRECTION,
    GRANT_COMMAND_COPY_WRONG_GRANTOR,
    GRANT_COMMAND_COPY_STALE_ENDPOINT,
    GRANT_COMMAND_COPY_STALE_TOKEN,
    GRANT_COMMAND_COPY_RANGE,
    GRANT_COMMAND_COPY_OVERFLOW,
    GRANT_COMMAND_COPY_OVERSIZE,
    GRANT_COMMAND_COPY_LOCAL_UNMAPPED,
    GRANT_COMMAND_COPY_REMOTE_UNMAPPED,
    GRANT_COMMAND_COPY_LOCAL_PERMISSION,
    GRANT_COMMAND_COPY_REMOTE_PERMISSION,
    GRANT_COMMAND_COPY_UPPER_ENDPOINT,
    GRANT_COMMAND_COPY_UPPER_TOKEN,
    GRANT_COMMAND_COPY_UNUSED,
    GRANT_COMMAND_UNKNOWN_OPERATION,
    GRANT_COMMAND_COPY_WRONG_GRANTEE,
    GRANT_COMMAND_REVOKE_READ,
    GRANT_COMMAND_REVOKE_WRITE,
    GRANT_COMMAND_COPY_REVOKED,
    GRANT_COMMAND_COPY_REUSED_ENDPOINT,
};

enum grant_syscall_control_action {
    GRANT_CONTROL_NONE = 0,
    GRANT_CONTROL_SWITCH,
    GRANT_CONTROL_REUSE_GRANTOR,
    GRANT_CONTROL_FINISH,
};

struct grant_syscall_script {
    bool armed;
    bool awaiting;
    bool token_result;
    bool preserve_state;
    enum grant_syscall_command command;
    enum micros_syscall_return expected_return;
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
static const uint64_t TEST_CONTROL_MAGIC =
    UINT64_C(0x4752414e54535953);

extern const unsigned char micros_grant_syscall_payload_start[];
extern const unsigned char micros_grant_syscall_payload_ecall[];
extern const unsigned char micros_grant_syscall_payload_after_ecall[];
extern const unsigned char micros_grant_syscall_payload_spin[];
extern const unsigned char micros_grant_syscall_payload_end[];
extern const unsigned char micros_grant_syscall_test_supervisor_resume[];

uintptr_t micros_grant_syscall_test_saved_sp;
uintptr_t micros_grant_syscall_test_saved_gp;
uintptr_t micros_grant_syscall_test_saved_tp;

static struct micros_process_handle
    processes[GRANT_SYSCALL_PROCESS_COUNT];
static struct micros_thread_handle
    threads[GRANT_SYSCALL_PROCESS_COUNT];
static micros_endpoint_t endpoints[GRANT_SYSCALL_PROCESS_COUNT];
static bool thread_live[GRANT_SYSCALL_PROCESS_COUNT];
static bool address_space_live[GRANT_SYSCALL_PROCESS_COUNT];
static uint64_t
    user_physical[GRANT_SYSCALL_PROCESS_COUNT][GRANT_SYSCALL_PAGE_COUNT];
static struct grant_syscall_script
    scripts[GRANT_SYSCALL_PROCESS_COUNT];
static struct micros_endpoint_registry *endpoint_registry;
static struct micros_grant_registry *grant_registry;
static micros_grant_t read_grant;
static micros_grant_t write_grant;
static micros_grant_t remote_read_only_grant;
static micros_grant_t unmapped_grant;
static micros_grant_t stale_grant;
static micros_endpoint_t old_grantor_endpoint;
static enum grant_syscall_control_action control_action;
static size_t control_next_actor;
static enum grant_syscall_command control_next_command;
static size_t dispatch_actor = GRANT_SYSCALL_PROCESS_COUNT;
static uint64_t baseline_owned;
static uint64_t baseline_free;
static size_t baseline_processes;
static size_t baseline_threads;
static size_t baseline_harts;

static struct micros_frame_ownership ownership_snapshot;
static struct micros_frame_allocator allocator_snapshot;
static struct micros_kernel_objects objects_snapshot;
static struct micros_kernel_objects objects_observed;
static struct micros_endpoint_registry endpoint_snapshot;
static struct micros_grant_registry grant_snapshot;
static bool root_snapshot[GRANT_SYSCALL_PROCESS_COUNT];
static bool root_observed[GRANT_SYSCALL_PROCESS_COUNT];
static unsigned char page_table_snapshot[
    GRANT_SYSCALL_PROCESS_COUNT
][3][MICROS_SV39_PAGE_SIZE];
static unsigned char page_table_observed[
    GRANT_SYSCALL_PROCESS_COUNT
][3][MICROS_SV39_PAGE_SIZE];
static uint64_t user_physical_snapshot[
    GRANT_SYSCALL_PROCESS_COUNT
][GRANT_SYSCALL_PAGE_COUNT];
static unsigned char user_bytes_snapshot[
    GRANT_SYSCALL_PROCESS_COUNT
][GRANT_SYSCALL_PAGE_COUNT][MICROS_SV39_PAGE_SIZE];
static unsigned char user_bytes_observed[
    GRANT_SYSCALL_PROCESS_COUNT
][GRANT_SYSCALL_PAGE_COUNT][MICROS_SV39_PAGE_SIZE];
static bool failure_snapshot_active;

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

static uint64_t read_satp(void)
{
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}

static uint64_t user_address_of(const unsigned char *symbol)
{
    return TEST_CODE_ADDRESS
        + (
            (uintptr_t)symbol
            - (uintptr_t)micros_grant_syscall_payload_start
        );
}

static uint64_t expected_satp(uint64_t root)
{
    return MICROS_RISCV_SATP_MODE_SV39 | (root >> 12);
}

static uint64_t abi_result(int64_t result)
{
    return (uint64_t)result;
}

static uint64_t physical_for(
    size_t process_index,
    uint64_t virtual_address
)
{
    size_t page_index;
    uint64_t offset;

    if (
        process_index >= GRANT_SYSCALL_PROCESS_COUNT
        || virtual_address < TEST_CODE_ADDRESS
        || virtual_address
            >= TEST_CODE_ADDRESS
                + GRANT_SYSCALL_PAGE_COUNT
                    * MICROS_SV39_PAGE_SIZE
    ) {
        return 0;
    }
    page_index = (size_t)(
        (virtual_address - TEST_CODE_ADDRESS)
        / MICROS_SV39_PAGE_SIZE
    );
    if (user_physical[process_index][page_index] == 0) {
        return 0;
    }
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
        uint64_t physical = physical_for(
            process_index,
            virtual_address + index
        );

        if (
            physical == 0
            || *(const unsigned char *)(uintptr_t)physical
                != (unsigned char)(seed + index)
        ) {
            return false;
        }
    }
    return true;
}

static bool range_is_value(
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char value
)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        uint64_t physical = physical_for(
            process_index,
            virtual_address + index
        );

        if (
            physical == 0
            || *(const unsigned char *)(uintptr_t)physical != value
        ) {
            return false;
        }
    }
    return true;
}

static void fill_context_pattern(
    struct micros_user_context *context,
    uint64_t base
)
{
    uint64_t words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    for (index = 0; index < sizeof(words) / sizeof(words[0]); ++index) {
        words[index] = base + index;
    }
    copy_bytes(context, words, sizeof(*context));
}

static size_t current_actor(const struct micros_hart *hart)
{
    size_t index;

    if (hart == NULL) {
        return GRANT_SYSCALL_PROCESS_COUNT;
    }
    for (index = 0; index < GRANT_SYSCALL_PROCESS_COUNT; ++index) {
        if (
            thread_live[index]
            && hart->current_thread.slot == threads[index].slot
            && hart->current_thread.generation
                == threads[index].generation
        ) {
            return index;
        }
    }
    return GRANT_SYSCALL_PROCESS_COUNT;
}

static bool test_mismatch(
    uint64_t stage,
    size_t actor,
    enum grant_syscall_command command,
    const struct micros_trap_frame *frame
)
{
    uart_write("MICROS_TEST_FAILURE grant-syscall-stage=");
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

static void normalize_objects(struct micros_kernel_objects *objects)
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

static bool capture_page_tables(
    const struct micros_kernel_objects *objects,
    bool roots[GRANT_SYSCALL_PROCESS_COUNT],
    unsigned char tables[GRANT_SYSCALL_PROCESS_COUNT][3]
        [MICROS_SV39_PAGE_SIZE]
)
{
    struct page_table {
        uint64_t entries[MICROS_SV39_TABLE_ENTRY_COUNT];
    };
    uint16_t root_index;
    uint16_t middle_index;
    size_t process_index;

    fill_bytes(roots, 0, sizeof(bool) * GRANT_SYSCALL_PROCESS_COUNT);
    fill_bytes(
        tables,
        0,
        GRANT_SYSCALL_PROCESS_COUNT * 3 * MICROS_SV39_PAGE_SIZE
    );
    if (
        micros_sv39_vpn_index(TEST_CODE_ADDRESS, 2, &root_index)
            != MICROS_SV39_OK
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
        process_index < GRANT_SYSCALL_PROCESS_COUNT;
        ++process_index
    ) {
        const struct micros_process *record =
            &objects->processes[processes[process_index].slot];
        struct page_table *root;
        struct page_table *middle;
        struct page_table *leaf;
        struct micros_sv39_decoded_pte root_entry;
        struct micros_sv39_decoded_pte middle_entry;

        if (
            record->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || record->generation
                != processes[process_index].generation
        ) {
            return false;
        }
        if (record->address_space_root == 0) {
            continue;
        }
        root = (struct page_table *)(uintptr_t)
            record->address_space_root;
        if (
            micros_sv39_decode_pte(
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
        roots[process_index] = true;
        copy_bytes(
            tables[process_index][0],
            root,
            MICROS_SV39_PAGE_SIZE
        );
        copy_bytes(
            tables[process_index][1],
            middle,
            MICROS_SV39_PAGE_SIZE
        );
        copy_bytes(
            tables[process_index][2],
            leaf,
            MICROS_SV39_PAGE_SIZE
        );
    }
    return true;
}

static void snapshot_user_bytes(void)
{
    size_t process_index;
    size_t page_index;

    copy_bytes(
        user_physical_snapshot,
        user_physical,
        sizeof(user_physical_snapshot)
    );
    fill_bytes(
        user_bytes_snapshot,
        0,
        sizeof(user_bytes_snapshot)
    );
    for (
        process_index = 0;
        process_index < GRANT_SYSCALL_PROCESS_COUNT;
        ++process_index
    ) {
        for (
            page_index = 0;
            page_index < GRANT_SYSCALL_PAGE_COUNT;
            ++page_index
        ) {
            if (user_physical[process_index][page_index] == 0) {
                continue;
            }
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

    if (!bytes_equal(
        user_physical_snapshot,
        user_physical,
        sizeof(user_physical_snapshot)
    )) {
        return false;
    }
    fill_bytes(
        user_bytes_observed,
        0,
        sizeof(user_bytes_observed)
    );
    for (
        process_index = 0;
        process_index < GRANT_SYSCALL_PROCESS_COUNT;
        ++process_index
    ) {
        for (
            page_index = 0;
            page_index < GRANT_SYSCALL_PAGE_COUNT;
            ++page_index
        ) {
            if (user_physical[process_index][page_index] == 0) {
                continue;
            }
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

static bool snapshot_failure_state(void)
{
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
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
    copy_bytes(&objects_snapshot, objects, sizeof(objects_snapshot));
    normalize_objects(&objects_snapshot);
    if (!capture_page_tables(
        objects,
        root_snapshot,
        page_table_snapshot
    )) {
        return false;
    }
    snapshot_user_bytes();
    failure_snapshot_active = true;
    return true;
}

static bool failure_state_matches(void)
{
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t bitmap_words;

    if (
        !failure_snapshot_active
        || ledger == NULL
        || ledger->allocator == NULL
        || objects == NULL
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
    if (
        !bytes_equal(
            &ownership_snapshot,
            ledger,
            offsetof(struct micros_frame_ownership, owners)
        )
        || !bytes_equal(
            ownership_snapshot.owners,
            ledger->owners,
            (size_t)ledger->managed_frame_count
                * sizeof(ledger->owners[0])
        )
        || !bytes_equal(
            ownership_snapshot.handoff_targets,
            ledger->handoff_targets,
            (size_t)ledger->managed_frame_count
        )
        || !bytes_equal(
            &allocator_snapshot,
            ledger->allocator,
            offsetof(
                struct micros_frame_allocator,
                allocated_bitmap
            )
        )
        || !bytes_equal(
            allocator_snapshot.allocated_bitmap,
            ledger->allocator->allocated_bitmap,
            bitmap_words
                * sizeof(
                    ledger->allocator->allocated_bitmap[0]
                )
        )
        || !bytes_equal(
            &endpoint_snapshot,
            endpoint_registry,
            sizeof(endpoint_snapshot)
        )
        || !bytes_equal(
            &grant_snapshot,
            grant_registry,
            sizeof(grant_snapshot)
        )
    ) {
        return false;
    }
    copy_bytes(&objects_observed, objects, sizeof(objects_observed));
    normalize_objects(&objects_observed);
    if (
        !bytes_equal(
            &objects_snapshot,
            &objects_observed,
            sizeof(objects_snapshot)
        )
        || !capture_page_tables(
            objects,
            root_observed,
            page_table_observed
        )
        || !bytes_equal(
            root_snapshot,
            root_observed,
            sizeof(root_snapshot)
        )
        || !bytes_equal(
            page_table_snapshot,
            page_table_observed,
            sizeof(page_table_snapshot)
        )
        || !user_bytes_match()
    ) {
        return false;
    }
    failure_snapshot_active = false;
    return true;
}

static bool scheduler_state_valid(
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
        || actor >= GRANT_SYSCALL_PROCESS_COUNT
        || !thread_live[actor]
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
        && read_satp() == expected_satp(process->address_space_root)
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

static void arm_raw(
    size_t actor,
    struct micros_user_context *context,
    enum grant_syscall_command command,
    uint64_t operation,
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5,
    uint64_t a6,
    uint64_t expected_result,
    bool token_result,
    bool preserve_state,
    enum micros_syscall_return expected_return
)
{
    struct grant_syscall_script *script = &scripts[actor];

    script->armed = true;
    script->awaiting = false;
    script->token_result = token_result;
    script->preserve_state = preserve_state;
    script->command = command;
    script->expected_result = expected_result;
    script->expected_return = expected_return;
    context->a0 = a0;
    context->a1 = a1;
    context->a2 = a2;
    context->a3 = a3;
    context->a4 = a4;
    context->a5 = a5;
    context->a6 = a6;
    context->a7 = operation;
    context->sepc =
        user_address_of(micros_grant_syscall_payload_ecall);
}

static bool arm_command(
    size_t actor,
    struct micros_user_context *context,
    enum grant_syscall_command command
)
{
    uint64_t stale_endpoint =
        (uint64_t)endpoints[GRANT_SYSCALL_GRANTOR]
        + (UINT64_C(1) << MICROS_ENDPOINT_SLOT_BITS);

    switch (command) {
    case GRANT_COMMAND_IPC_NOTIFY:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_NOTIFY,
            endpoints[GRANT_SYSCALL_GRANTEE],
            UINT64_C(5),
            0,
            0,
            UINT64_C(0x4444444444444444),
            UINT64_C(0x5555555555555555),
            UINT64_C(0x6666666666666666),
            MICROS_SYSCALL_ABI_OK,
            false,
            false,
            MICROS_SYSCALL_RETURN_CAPTURED
        );
        return true;
    case GRANT_COMMAND_CREATE_READ:
    case GRANT_COMMAND_CREATE_WRITE:
    case GRANT_COMMAND_CREATE_REMOTE_READ_ONLY:
    case GRANT_COMMAND_CREATE_UNMAPPED:
    case GRANT_COMMAND_CREATE_STALE:
    {
        uint64_t base = TEST_DATA_SECOND_ADDRESS - 64;
        uint64_t length = 256;
        uint64_t permissions =
            command == GRANT_COMMAND_CREATE_WRITE
                ? MICROS_GRANT_PERMISSION_WRITE
                : MICROS_GRANT_PERMISSION_READ;

        if (command == GRANT_COMMAND_CREATE_REMOTE_READ_ONLY) {
            base = TEST_READ_ONLY_ADDRESS;
            length = 64;
            permissions = MICROS_GRANT_PERMISSION_WRITE;
        } else if (command == GRANT_COMMAND_CREATE_UNMAPPED) {
            base = TEST_UNMAPPED_ADDRESS;
            length = 64;
        } else if (command == GRANT_COMMAND_CREATE_STALE) {
            base = TEST_DATA_ADDRESS;
            length = 64;
        }
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_CREATE,
            endpoints[GRANT_SYSCALL_GRANTEE],
            base,
            length,
            permissions,
            0,
            0,
            0,
            0,
            true,
            false,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    }
    case GRANT_COMMAND_CREATE_UPPER_ENDPOINT:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_CREATE,
            (UINT64_C(1) << 32)
                | endpoints[GRANT_SYSCALL_GRANTEE],
            TEST_DATA_ADDRESS,
            64,
            MICROS_GRANT_PERMISSION_READ,
            0,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_CREATE_UPPER_PERMISSION:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_CREATE,
            endpoints[GRANT_SYSCALL_GRANTEE],
            TEST_DATA_ADDRESS,
            64,
            (UINT64_C(1) << 32)
                | MICROS_GRANT_PERMISSION_READ,
            0,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_CREATE_UNUSED:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_CREATE,
            endpoints[GRANT_SYSCALL_GRANTEE],
            TEST_DATA_ADDRESS,
            64,
            MICROS_GRANT_PERMISSION_READ,
            1,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_REVOKE_STALE:
    case GRANT_COMMAND_REVOKE_READ:
    case GRANT_COMMAND_REVOKE_WRITE:
    {
        micros_grant_t token = stale_grant;

        if (command == GRANT_COMMAND_REVOKE_READ) {
            token = read_grant;
        } else if (command == GRANT_COMMAND_REVOKE_WRITE) {
            token = write_grant;
        }
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_REVOKE,
            token,
            0,
            0,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_OK,
            false,
            false,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    }
    case GRANT_COMMAND_REVOKE_UPPER_TOKEN:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_REVOKE,
            (UINT64_C(1) << 32) | stale_grant,
            0,
            0,
            0,
            0,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_REVOKE_UNUSED:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_REVOKE,
            stale_grant,
            1,
            0,
            0,
            0,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_FROM_CROSS:
        write_pattern(
            GRANT_SYSCALL_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 64,
            128,
            UINT8_C(0x10)
        );
        fill_bytes(
            (void *)(uintptr_t)
                user_physical[GRANT_SYSCALL_GRANTEE][2],
            UINT8_C(0xcc),
            MICROS_SV39_PAGE_SIZE
        );
        fill_bytes(
            (void *)(uintptr_t)
                user_physical[GRANT_SYSCALL_GRANTEE][3],
            UINT8_C(0xcc),
            MICROS_SV39_PAGE_SIZE
        );
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[GRANT_SYSCALL_GRANTOR],
            read_grant,
            0,
            TEST_DATA_SECOND_ADDRESS - 32,
            128,
            0,
            0,
            MICROS_SYSCALL_ABI_OK,
            false,
            false,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_TO_CROSS:
        write_pattern(
            GRANT_SYSCALL_GRANTEE,
            TEST_DATA_SECOND_ADDRESS - 48,
            96,
            UINT8_C(0x50)
        );
        fill_bytes(
            (void *)(uintptr_t)
                user_physical[GRANT_SYSCALL_GRANTOR][2],
            UINT8_C(0xdd),
            MICROS_SV39_PAGE_SIZE
        );
        fill_bytes(
            (void *)(uintptr_t)
                user_physical[GRANT_SYSCALL_GRANTOR][3],
            UINT8_C(0xdd),
            MICROS_SV39_PAGE_SIZE
        );
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_TO,
            endpoints[GRANT_SYSCALL_GRANTOR],
            write_grant,
            16,
            TEST_DATA_SECOND_ADDRESS - 48,
            96,
            0,
            0,
            MICROS_SYSCALL_ABI_OK,
            false,
            false,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_FROM_LOCAL:
        write_pattern(
            GRANT_SYSCALL_GRANTOR,
            TEST_DATA_SECOND_ADDRESS,
            32,
            UINT8_C(0x90)
        );
        fill_bytes(
            (void *)(uintptr_t)physical_for(
                GRANT_SYSCALL_GRANTEE,
                TEST_DATA_ADDRESS + UINT64_C(0x0ff)
            ),
            UINT8_C(0xcc),
            34
        );
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[GRANT_SYSCALL_GRANTOR],
            read_grant,
            64,
            TEST_DATA_ADDRESS + UINT64_C(0x100),
            32,
            0,
            0,
            MICROS_SYSCALL_ABI_OK,
            false,
            false,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_TO_LOCAL:
        write_pattern(
            GRANT_SYSCALL_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x180),
            32,
            UINT8_C(0xa0)
        );
        fill_bytes(
            (void *)(uintptr_t)physical_for(
                GRANT_SYSCALL_GRANTOR,
                TEST_DATA_SECOND_ADDRESS + UINT64_C(0x3f)
            ),
            UINT8_C(0xee),
            34
        );
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_TO,
            endpoints[GRANT_SYSCALL_GRANTOR],
            write_grant,
            128,
            TEST_DATA_ADDRESS + UINT64_C(0x180),
            32,
            0,
            0,
            MICROS_SYSCALL_ABI_OK,
            false,
            false,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_FROM_ZERO:
    case GRANT_COMMAND_COPY_TO_ZERO:
        arm_raw(
            actor,
            context,
            command,
            command == GRANT_COMMAND_COPY_FROM_ZERO
                ? MICROS_SYSCALL_ABI_GRANT_COPY_FROM
                : MICROS_SYSCALL_ABI_GRANT_COPY_TO,
            endpoints[GRANT_SYSCALL_GRANTOR],
            command == GRANT_COMMAND_COPY_FROM_ZERO
                ? read_grant
                : write_grant,
            0,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_OK,
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_FROM_WRONG_DIRECTION:
    case GRANT_COMMAND_COPY_TO_WRONG_DIRECTION:
        arm_raw(
            actor,
            context,
            command,
            command == GRANT_COMMAND_COPY_FROM_WRONG_DIRECTION
                ? MICROS_SYSCALL_ABI_GRANT_COPY_FROM
                : MICROS_SYSCALL_ABI_GRANT_COPY_TO,
            endpoints[GRANT_SYSCALL_GRANTOR],
            command == GRANT_COMMAND_COPY_FROM_WRONG_DIRECTION
                ? write_grant
                : read_grant,
            0,
            TEST_DATA_ADDRESS,
            1,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_UNAUTHORIZED),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_WRONG_GRANTOR:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[GRANT_SYSCALL_WRONG],
            read_grant,
            0,
            TEST_DATA_ADDRESS,
            1,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_UNAUTHORIZED),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_STALE_ENDPOINT:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            stale_endpoint,
            read_grant,
            0,
            TEST_DATA_ADDRESS,
            1,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_DEAD_ENDPOINT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_STALE_TOKEN:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[GRANT_SYSCALL_GRANTOR],
            stale_grant,
            0,
            TEST_DATA_ADDRESS,
            1,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_STALE_GRANT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_RANGE:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[GRANT_SYSCALL_GRANTOR],
            read_grant,
            257,
            TEST_DATA_ADDRESS,
            1,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_RANGE),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_OVERFLOW:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[GRANT_SYSCALL_GRANTOR],
            read_grant,
            UINT64_MAX,
            TEST_DATA_ADDRESS,
            2,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_RANGE),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_OVERSIZE:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[GRANT_SYSCALL_GRANTOR],
            read_grant,
            0,
            TEST_DATA_ADDRESS,
            MICROS_GRANT_COPY_MAX + 1,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_RANGE),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_LOCAL_UNMAPPED:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[GRANT_SYSCALL_GRANTOR],
            read_grant,
            0,
            TEST_UNMAPPED_ADDRESS,
            1,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_MEMORY_FAULT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_REMOTE_UNMAPPED:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[GRANT_SYSCALL_GRANTOR],
            unmapped_grant,
            0,
            TEST_DATA_ADDRESS,
            1,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_MEMORY_FAULT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_LOCAL_PERMISSION:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[GRANT_SYSCALL_GRANTOR],
            read_grant,
            0,
            TEST_READ_ONLY_ADDRESS,
            1,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_MEMORY_FAULT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_REMOTE_PERMISSION:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_TO,
            endpoints[GRANT_SYSCALL_GRANTOR],
            remote_read_only_grant,
            0,
            TEST_DATA_ADDRESS,
            1,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_MEMORY_FAULT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_UPPER_ENDPOINT:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            (UINT64_C(1) << 32)
                | endpoints[GRANT_SYSCALL_GRANTOR],
            read_grant,
            0,
            TEST_DATA_ADDRESS,
            1,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_UPPER_TOKEN:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[GRANT_SYSCALL_GRANTOR],
            (UINT64_C(1) << 32) | read_grant,
            0,
            TEST_DATA_ADDRESS,
            1,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_UNUSED:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[GRANT_SYSCALL_GRANTOR],
            read_grant,
            0,
            TEST_DATA_ADDRESS,
            1,
            1,
            0,
            abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_UNKNOWN_OPERATION:
        arm_raw(
            actor,
            context,
            command,
            UINT64_C(11),
            UINT64_C(0x1111),
            UINT64_C(0x2222),
            UINT64_C(0x3333),
            UINT64_C(0x4444),
            UINT64_C(0x5555),
            UINT64_C(0x6666),
            UINT64_C(0x7777),
            abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_WRONG_GRANTEE:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[GRANT_SYSCALL_GRANTOR],
            read_grant,
            0,
            TEST_DATA_ADDRESS,
            1,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_UNAUTHORIZED),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_REVOKED:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[GRANT_SYSCALL_GRANTOR],
            read_grant,
            0,
            TEST_DATA_ADDRESS,
            1,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_STALE_GRANT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_COPY_REUSED_ENDPOINT:
        arm_raw(
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            endpoints[GRANT_SYSCALL_GRANTOR],
            unmapped_grant,
            0,
            0,
            0,
            0,
            0,
            abi_result(MICROS_SYSCALL_ABI_STALE_GRANT),
            false,
            true,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    case GRANT_COMMAND_NONE:
        return false;
    }
    return false;
}

static void arm_control(struct micros_user_context *context)
{
    context->a0 = TEST_CONTROL_MAGIC;
    context->a7 = UINT64_MAX;
    context->sepc =
        user_address_of(micros_grant_syscall_payload_ecall);
}

static bool schedule_next(
    size_t current,
    size_t next,
    enum grant_syscall_command command,
    struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_user_context *context;

    if (
        objects == NULL
        || next >= GRANT_SYSCALL_PROCESS_COUNT
        || !thread_live[next]
    ) {
        return false;
    }
    context = current == next
        ? (struct micros_user_context *)frame
        : &objects->threads[threads[next].slot].user_context;
    if (!arm_command(next, context, command)) {
        return false;
    }
    if (current == next) {
        return true;
    }
    control_action = GRANT_CONTROL_SWITCH;
    control_next_actor = next;
    control_next_command = command;
    arm_control((struct micros_user_context *)frame);
    return true;
}

static bool schedule_reuse(
    struct micros_trap_frame *frame
)
{
    control_action = GRANT_CONTROL_REUSE_GRANTOR;
    control_next_actor = GRANT_SYSCALL_GRANTEE;
    control_next_command = GRANT_COMMAND_COPY_REUSED_ENDPOINT;
    arm_control((struct micros_user_context *)frame);
    return true;
}

static bool schedule_finish(struct micros_trap_frame *frame)
{
    control_action = GRANT_CONTROL_FINISH;
    control_next_actor = GRANT_SYSCALL_PROCESS_COUNT;
    control_next_command = GRANT_COMMAND_NONE;
    arm_control((struct micros_user_context *)frame);
    return true;
}

static bool transition_after_return(
    size_t actor,
    enum grant_syscall_command command,
    struct micros_trap_frame *frame
)
{
    switch (command) {
    case GRANT_COMMAND_IPC_NOTIFY:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_CREATE_READ,
            frame
        );
    case GRANT_COMMAND_CREATE_READ:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_CREATE_WRITE,
            frame
        );
    case GRANT_COMMAND_CREATE_WRITE:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_CREATE_REMOTE_READ_ONLY,
            frame
        );
    case GRANT_COMMAND_CREATE_REMOTE_READ_ONLY:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_CREATE_UNMAPPED,
            frame
        );
    case GRANT_COMMAND_CREATE_UNMAPPED:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_CREATE_STALE,
            frame
        );
    case GRANT_COMMAND_CREATE_STALE:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_CREATE_UPPER_ENDPOINT,
            frame
        );
    case GRANT_COMMAND_CREATE_UPPER_ENDPOINT:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_CREATE_UPPER_PERMISSION,
            frame
        );
    case GRANT_COMMAND_CREATE_UPPER_PERMISSION:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_CREATE_UNUSED,
            frame
        );
    case GRANT_COMMAND_CREATE_UNUSED:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_REVOKE_STALE,
            frame
        );
    case GRANT_COMMAND_REVOKE_STALE:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_REVOKE_UPPER_TOKEN,
            frame
        );
    case GRANT_COMMAND_REVOKE_UPPER_TOKEN:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_REVOKE_UNUSED,
            frame
        );
    case GRANT_COMMAND_REVOKE_UNUSED:
        return schedule_next(
            actor,
            GRANT_SYSCALL_GRANTEE,
            GRANT_COMMAND_COPY_FROM_CROSS,
            frame
        );
    case GRANT_COMMAND_COPY_FROM_CROSS:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_TO_CROSS,
            frame
        );
    case GRANT_COMMAND_COPY_TO_CROSS:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_FROM_LOCAL,
            frame
        );
    case GRANT_COMMAND_COPY_FROM_LOCAL:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_TO_LOCAL,
            frame
        );
    case GRANT_COMMAND_COPY_TO_LOCAL:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_FROM_ZERO,
            frame
        );
    case GRANT_COMMAND_COPY_FROM_ZERO:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_TO_ZERO,
            frame
        );
    case GRANT_COMMAND_COPY_TO_ZERO:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_FROM_WRONG_DIRECTION,
            frame
        );
    case GRANT_COMMAND_COPY_FROM_WRONG_DIRECTION:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_TO_WRONG_DIRECTION,
            frame
        );
    case GRANT_COMMAND_COPY_TO_WRONG_DIRECTION:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_WRONG_GRANTOR,
            frame
        );
    case GRANT_COMMAND_COPY_WRONG_GRANTOR:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_STALE_ENDPOINT,
            frame
        );
    case GRANT_COMMAND_COPY_STALE_ENDPOINT:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_STALE_TOKEN,
            frame
        );
    case GRANT_COMMAND_COPY_STALE_TOKEN:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_RANGE,
            frame
        );
    case GRANT_COMMAND_COPY_RANGE:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_OVERFLOW,
            frame
        );
    case GRANT_COMMAND_COPY_OVERFLOW:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_OVERSIZE,
            frame
        );
    case GRANT_COMMAND_COPY_OVERSIZE:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_LOCAL_UNMAPPED,
            frame
        );
    case GRANT_COMMAND_COPY_LOCAL_UNMAPPED:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_REMOTE_UNMAPPED,
            frame
        );
    case GRANT_COMMAND_COPY_REMOTE_UNMAPPED:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_LOCAL_PERMISSION,
            frame
        );
    case GRANT_COMMAND_COPY_LOCAL_PERMISSION:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_REMOTE_PERMISSION,
            frame
        );
    case GRANT_COMMAND_COPY_REMOTE_PERMISSION:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_UPPER_ENDPOINT,
            frame
        );
    case GRANT_COMMAND_COPY_UPPER_ENDPOINT:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_UPPER_TOKEN,
            frame
        );
    case GRANT_COMMAND_COPY_UPPER_TOKEN:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_COPY_UNUSED,
            frame
        );
    case GRANT_COMMAND_COPY_UNUSED:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_UNKNOWN_OPERATION,
            frame
        );
    case GRANT_COMMAND_UNKNOWN_OPERATION:
        return schedule_next(
            actor,
            GRANT_SYSCALL_WRONG,
            GRANT_COMMAND_COPY_WRONG_GRANTEE,
            frame
        );
    case GRANT_COMMAND_COPY_WRONG_GRANTEE:
        return schedule_next(
            actor,
            GRANT_SYSCALL_GRANTOR,
            GRANT_COMMAND_REVOKE_READ,
            frame
        );
    case GRANT_COMMAND_REVOKE_READ:
        return schedule_next(
            actor,
            actor,
            GRANT_COMMAND_REVOKE_WRITE,
            frame
        );
    case GRANT_COMMAND_REVOKE_WRITE:
        return schedule_next(
            actor,
            GRANT_SYSCALL_GRANTEE,
            GRANT_COMMAND_COPY_REVOKED,
            frame
        );
    case GRANT_COMMAND_COPY_REVOKED:
        return schedule_reuse(frame);
    case GRANT_COMMAND_COPY_REUSED_ENDPOINT:
        return schedule_finish(frame);
    case GRANT_COMMAND_NONE:
        return false;
    }
    return false;
}

static bool validate_command_effect(
    size_t actor,
    enum grant_syscall_command command,
    const struct micros_trap_frame *frame
)
{
    const struct micros_endpoint_record *grantee_endpoint;

    switch (command) {
    case GRANT_COMMAND_IPC_NOTIFY:
        grantee_endpoint =
            &endpoint_registry->endpoints[
                processes[GRANT_SYSCALL_GRANTEE].slot
            ];
        return (
            grantee_endpoint->pending_notification_sources
                == (
                    UINT64_C(1)
                    << processes[GRANT_SYSCALL_GRANTOR].slot
                )
            && grantee_endpoint->pending_events[
                processes[GRANT_SYSCALL_GRANTOR].slot
            ] == UINT64_C(5)
        );
    case GRANT_COMMAND_CREATE_READ:
        read_grant = (micros_grant_t)frame->a0;
        return true;
    case GRANT_COMMAND_CREATE_WRITE:
        write_grant = (micros_grant_t)frame->a0;
        return write_grant != read_grant;
    case GRANT_COMMAND_CREATE_REMOTE_READ_ONLY:
        remote_read_only_grant = (micros_grant_t)frame->a0;
        return (
            remote_read_only_grant != read_grant
            && remote_read_only_grant != write_grant
        );
    case GRANT_COMMAND_CREATE_UNMAPPED:
        unmapped_grant = (micros_grant_t)frame->a0;
        return (
            unmapped_grant != read_grant
            && unmapped_grant != write_grant
            && unmapped_grant != remote_read_only_grant
        );
    case GRANT_COMMAND_CREATE_STALE:
        stale_grant = (micros_grant_t)frame->a0;
        return (
            stale_grant != read_grant
            && stale_grant != write_grant
            && stale_grant != remote_read_only_grant
            && stale_grant != unmapped_grant
        );
    case GRANT_COMMAND_COPY_FROM_CROSS:
        return (
            pattern_matches(
                GRANT_SYSCALL_GRANTEE,
                TEST_DATA_SECOND_ADDRESS - 32,
                128,
                UINT8_C(0x10)
            )
            && range_is_value(
                GRANT_SYSCALL_GRANTEE,
                TEST_DATA_SECOND_ADDRESS - 33,
                1,
                UINT8_C(0xcc)
            )
            && range_is_value(
                GRANT_SYSCALL_GRANTEE,
                TEST_DATA_SECOND_ADDRESS + 96,
                1,
                UINT8_C(0xcc)
            )
        );
    case GRANT_COMMAND_COPY_TO_CROSS:
        return (
            pattern_matches(
                GRANT_SYSCALL_GRANTOR,
                TEST_DATA_SECOND_ADDRESS - 48,
                96,
                UINT8_C(0x50)
            )
            && range_is_value(
                GRANT_SYSCALL_GRANTOR,
                TEST_DATA_SECOND_ADDRESS - 49,
                1,
                UINT8_C(0xdd)
            )
            && range_is_value(
                GRANT_SYSCALL_GRANTOR,
                TEST_DATA_SECOND_ADDRESS + 48,
                1,
                UINT8_C(0xdd)
            )
        );
    case GRANT_COMMAND_COPY_FROM_LOCAL:
        return (
            pattern_matches(
                GRANT_SYSCALL_GRANTEE,
                TEST_DATA_ADDRESS + UINT64_C(0x100),
                32,
                UINT8_C(0x90)
            )
            && range_is_value(
                GRANT_SYSCALL_GRANTEE,
                TEST_DATA_ADDRESS + UINT64_C(0x0ff),
                1,
                UINT8_C(0xcc)
            )
            && range_is_value(
                GRANT_SYSCALL_GRANTEE,
                TEST_DATA_ADDRESS + UINT64_C(0x120),
                1,
                UINT8_C(0xcc)
            )
        );
    case GRANT_COMMAND_COPY_TO_LOCAL:
        return (
            pattern_matches(
                GRANT_SYSCALL_GRANTOR,
                TEST_DATA_SECOND_ADDRESS + UINT64_C(0x40),
                32,
                UINT8_C(0xa0)
            )
            && range_is_value(
                GRANT_SYSCALL_GRANTOR,
                TEST_DATA_SECOND_ADDRESS + UINT64_C(0x3f),
                1,
                UINT8_C(0xee)
            )
            && range_is_value(
                GRANT_SYSCALL_GRANTOR,
                TEST_DATA_SECOND_ADDRESS + UINT64_C(0x60),
                1,
                UINT8_C(0xee)
            )
        );
    case GRANT_COMMAND_REVOKE_STALE:
    case GRANT_COMMAND_REVOKE_READ:
    case GRANT_COMMAND_REVOKE_WRITE:
        return frame->a0 == MICROS_SYSCALL_ABI_OK;
    case GRANT_COMMAND_COPY_REUSED_ENDPOINT:
        return (
            actor == GRANT_SYSCALL_GRANTEE
            && endpoints[GRANT_SYSCALL_GRANTOR]
                != old_grantor_endpoint
        );
    default:
        return true;
    }
}

bool micros_grant_syscall_test_before_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    size_t actor = current_actor(hart);
    struct grant_syscall_script *script;

    if (
        frame == NULL
        || actor >= GRANT_SYSCALL_PROCESS_COUNT
        || !scheduler_state_valid(hart, actor, false)
    ) {
        return test_mismatch(
            1,
            actor,
            GRANT_COMMAND_NONE,
            frame
        );
    }
    script = &scripts[actor];
    if (
        !script->armed
        || script->awaiting
        || frame->sepc
            != user_address_of(
                micros_grant_syscall_payload_ecall
            )
    ) {
        return test_mismatch(2, actor, script->command, frame);
    }
    if (
        script->preserve_state
        && !snapshot_failure_state()
    ) {
        return test_mismatch(
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
    dispatch_actor = actor;
    return true;
}

bool micros_grant_syscall_test_after_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
)
{
    size_t actor = dispatch_actor;
    uint64_t expected = actor < GRANT_SYSCALL_PROCESS_COUNT
        ? scripts[actor].expected_return
        : UINT64_MAX;

    if (
        frame == NULL
        || actor >= GRANT_SYSCALL_PROCESS_COUNT
        || !scripts[actor].awaiting
        || syscall_return != scripts[actor].expected_return
        || (
            syscall_return == MICROS_SYSCALL_RETURN_NORMAL
            && current_actor(hart) != actor
        )
    ) {
        uart_write("MICROS_TEST_FAILURE grant-dispatch expected=");
        uart_write_hex64(expected);
        uart_write(" actual=");
        uart_write_hex64(syscall_return);
        uart_write(" actor=");
        uart_write_hex64(actor);
        uart_write(" current=");
        uart_write_hex64(current_actor(hart));
        uart_write(" a0=");
        uart_write_hex64(frame == NULL ? 0 : frame->a0);
        uart_write("\n");
        uart_flush();
        return false;
    }
    return true;
}

bool micros_grant_syscall_test_after_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    size_t actor = current_actor(hart);
    struct grant_syscall_script *script;
    struct micros_user_context expected;
    enum grant_syscall_command completed;

    if (
        frame == NULL
        || actor >= GRANT_SYSCALL_PROCESS_COUNT
        || !scheduler_state_valid(hart, actor, true)
    ) {
        return test_mismatch(
            3,
            actor,
            GRANT_COMMAND_NONE,
            frame
        );
    }
    script = &scripts[actor];
    if (!script->awaiting) {
        return test_mismatch(4, actor, script->command, frame);
    }
    copy_bytes(&expected, &script->captured, sizeof(expected));
    if (script->token_result) {
        if (
            frame->a0 >= MICROS_GRANT_NONE
            || (int64_t)frame->a0 < 0
        ) {
            return test_mismatch(
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
    expected.sepc =
        user_address_of(
            micros_grant_syscall_payload_after_ecall
        );
    if (!bytes_equal(frame, &expected, sizeof(expected))) {
        return test_mismatch(5, actor, script->command, frame);
    }
    if (
        script->preserve_state
        && !failure_state_matches()
    ) {
        return test_mismatch(
            UINT64_C(0x50),
            actor,
            script->command,
            frame
        );
    }
    completed = script->command;
    if (!validate_command_effect(actor, completed, frame)) {
        return test_mismatch(
            UINT64_C(0x60),
            actor,
            completed,
            frame
        );
    }
    script->awaiting = false;
    script->command = GRANT_COMMAND_NONE;
    dispatch_actor = GRANT_SYSCALL_PROCESS_COUNT;
    if (!transition_after_return(actor, completed, frame)) {
        return test_mismatch(6, actor, completed, frame);
    }
    return true;
}

static bool release_address_space(
    struct micros_kernel_objects *objects,
    size_t actor
)
{
    size_t page_index;

    if (!address_space_live[actor]) {
        return true;
    }
    for (
        page_index = 0;
        page_index < GRANT_SYSCALL_PAGE_COUNT;
        ++page_index
    ) {
        uint64_t released = 0;

        if (
            micros_user_address_space_release_page(
                processes[actor],
                TEST_CODE_ADDRESS
                    + page_index * MICROS_SV39_PAGE_SIZE,
                &released
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || released != user_physical[actor][page_index]
        ) {
            return false;
        }
        user_physical[actor][page_index] = 0;
    }
    if (
        micros_user_address_space_destroy(processes[actor])
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return false;
    }
    address_space_live[actor] = false;
    (void)objects;
    return true;
}

static bool release_thread(
    struct micros_kernel_objects *objects,
    size_t actor
)
{
    if (!thread_live[actor]) {
        return true;
    }
    if (
        micros_thread_scheduler_remove(objects, threads[actor])
            != MICROS_KERNEL_OBJECT_OK
        || micros_user_execution_detach(threads[actor])
            != MICROS_USER_EXECUTION_OK
        || micros_thread_release(objects, threads[actor])
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    thread_live[actor] = false;
    return true;
}

static bool close_endpoint(
    struct micros_kernel_objects *objects,
    size_t actor
)
{
    struct micros_grant_cancel_plan plan;

    return (
        micros_grant_prepare_endpoint_cancel(
            grant_registry,
            endpoint_registry,
            objects,
            endpoints[actor],
            &plan
        ) == MICROS_GRANT_OK
        && micros_ipc_endpoint_close(
            endpoint_registry,
            objects,
            endpoints[actor]
        ) == MICROS_IPC_OK
        && micros_grant_commit_endpoint_cancel(
            grant_registry,
            &plan
        ) == MICROS_GRANT_OK
    );
}

static bool reuse_grantor(void)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_process_handle replacement;
    micros_endpoint_t replacement_endpoint;
    uint32_t old_generation =
        processes[GRANT_SYSCALL_GRANTOR].generation;
    uint16_t old_slot = processes[GRANT_SYSCALL_GRANTOR].slot;

    old_grantor_endpoint = endpoints[GRANT_SYSCALL_GRANTOR];
    if (
        objects == NULL
        || !close_endpoint(objects, GRANT_SYSCALL_GRANTOR)
        || !release_thread(objects, GRANT_SYSCALL_GRANTOR)
        || !release_address_space(
            objects,
            GRANT_SYSCALL_GRANTOR
        )
        || micros_frame_ownership_runtime_release_process(
            objects,
            processes[GRANT_SYSCALL_GRANTOR]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(objects, &replacement)
            != MICROS_KERNEL_OBJECT_OK
        || replacement.slot != old_slot
        || replacement.generation != old_generation + 1
        || micros_endpoint_reserve(
            endpoint_registry,
            objects,
            replacement,
            &replacement_endpoint
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_install_profile(
            endpoint_registry,
            objects,
            replacement,
            1
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            endpoint_registry,
            objects,
            replacement_endpoint
        ) != MICROS_ENDPOINT_OK
        || replacement_endpoint == old_grantor_endpoint
    ) {
        return false;
    }
    processes[GRANT_SYSCALL_GRANTOR] = replacement;
    endpoints[GRANT_SYSCALL_GRANTOR] = replacement_endpoint;
    return (
        grant_registry->active_count == 0
        && micros_grant_runtime_validate() == MICROS_GRANT_OK
        && micros_ipc_runtime_validate() == MICROS_ENDPOINT_OK
        && micros_kernel_objects_validate(objects)
            == MICROS_KERNEL_OBJECT_OK
        && micros_frame_ownership_runtime_validate(objects)
            == MICROS_FRAME_OWNERSHIP_OK
    );
}

enum micros_grant_syscall_test_control_result
micros_grant_syscall_test_handle_control_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t actor = current_actor(hart);
    struct micros_user_context expected;
    const uint64_t control_mask =
        MICROS_RISCV_SSTATUS_SIE
        | MICROS_RISCV_SSTATUS_SPIE
        | MICROS_RISCV_SSTATUS_SPP
        | MICROS_RISCV_SSTATUS_SUM
        | TEST_SSTATUS_UBE
        | TEST_SSTATUS_VS
        | TEST_SSTATUS_FS
        | TEST_SSTATUS_XS
        | TEST_SSTATUS_MXR
        | TEST_SSTATUS_SD;

    if (
        objects == NULL
        || hart == NULL
        || frame == NULL
        || actor >= GRANT_SYSCALL_PROCESS_COUNT
        || frame->a0 != TEST_CONTROL_MAGIC
        || frame->a7 != UINT64_MAX
        || frame->sepc
            != user_address_of(
                micros_grant_syscall_payload_ecall
            )
        || control_action == GRANT_CONTROL_NONE
    ) {
        return MICROS_GRANT_SYSCALL_TEST_CONTROL_MISMATCH;
    }
    if (control_action == GRANT_CONTROL_SWITCH) {
        const struct micros_thread *next =
            &objects->threads[threads[control_next_actor].slot];

        copy_bytes(&expected, &next->user_context, sizeof(expected));
        if (
            (
                next->scheduler_assigned
                ? micros_thread_runtime_flags_unset(
                    objects,
                    threads[control_next_actor],
                    MICROS_THREAD_RTS_INACTIVE
                )
                : micros_thread_scheduler_admit(
                    objects,
                    micros_kernel_object_runtime_boot_hart_handle(),
                    threads[control_next_actor],
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
            || current_actor(hart) != control_next_actor
            || !bytes_equal(frame, &expected, sizeof(expected))
            || !scheduler_state_valid(
                hart,
                control_next_actor,
                true
            )
        ) {
            return MICROS_GRANT_SYSCALL_TEST_CONTROL_MISMATCH;
        }
        control_action = GRANT_CONTROL_NONE;
        return MICROS_GRANT_SYSCALL_TEST_CONTROL_USER_RETURN;
    }
    if (control_action == GRANT_CONTROL_REUSE_GRANTOR) {
        if (
            actor != GRANT_SYSCALL_GRANTEE
            || !reuse_grantor()
            || !arm_command(
                actor,
                (struct micros_user_context *)frame,
                control_next_command
            )
            || micros_scheduler_select_user_return(hart, frame)
                != MICROS_SCHEDULER_OK
            || current_actor(hart) != actor
            || !scheduler_state_valid(hart, actor, true)
        ) {
            return MICROS_GRANT_SYSCALL_TEST_CONTROL_MISMATCH;
        }
        control_action = GRANT_CONTROL_NONE;
        return MICROS_GRANT_SYSCALL_TEST_CONTROL_USER_RETURN;
    }
    if (
        control_action != GRANT_CONTROL_FINISH
        || micros_scheduler_test_prepare_supervisor_return(
            hart,
            frame
        ) != MICROS_SCHEDULER_OK
    ) {
        return MICROS_GRANT_SYSCALL_TEST_CONTROL_MISMATCH;
    }
    frame->sp = micros_grant_syscall_test_saved_sp;
    frame->sepc =
        (uintptr_t)micros_grant_syscall_test_supervisor_resume;
    frame->sstatus &= ~control_mask;
    frame->sstatus |= MICROS_RISCV_SSTATUS_SPP;
    control_action = GRANT_CONTROL_NONE;
    return MICROS_GRANT_SYSCALL_TEST_CONTROL_SUPERVISOR_RETURN;
}

static bool prepare_process(
    struct micros_kernel_objects *objects,
    size_t actor,
    const unsigned char *payload,
    size_t payload_size
)
{
    static const uint32_t page_permissions[
        GRANT_SYSCALL_PAGE_COUNT
    ] = {
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE,
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE,
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE,
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE,
        MICROS_SV39_PERMISSION_READ,
    };
    struct micros_user_context context;
    size_t page_index;

    if (
        micros_user_address_space_create(processes[actor])
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return false;
    }
    address_space_live[actor] = true;
    for (
        page_index = 0;
        page_index < GRANT_SYSCALL_PAGE_COUNT;
        ++page_index
    ) {
        if (
            micros_user_address_space_allocate_page(
                processes[actor],
                TEST_CODE_ADDRESS
                    + page_index * MICROS_SV39_PAGE_SIZE,
                page_permissions[page_index],
                &user_physical[actor][page_index]
            ) != MICROS_USER_ADDRESS_SPACE_OK
        ) {
            return false;
        }
        fill_bytes(
            (void *)(uintptr_t)
                user_physical[actor][page_index],
            (unsigned char)(UINT8_C(0xc0) + actor),
            MICROS_SV39_PAGE_SIZE
        );
    }
    if (
        micros_thread_create(
            objects,
            processes[actor],
            &threads[actor]
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    thread_live[actor] = true;
    copy_bytes(
        (void *)(uintptr_t)user_physical[actor][0],
        payload,
        payload_size
    );
    fill_context_pattern(
        &context,
        UINT64_C(0x1000) + actor * UINT64_C(0x1000)
    );
    context.sepc =
        user_address_of(micros_grant_syscall_payload_spin);
    context.sp = TEST_STACK_ADDRESS + MICROS_SV39_PAGE_SIZE;
    context.sstatus = 0;
    return micros_user_execution_prepare(threads[actor], &context)
        == MICROS_USER_EXECUTION_OK;
}

static bool endpoint_registry_is_empty(void)
{
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (
            endpoint_registry->endpoints[index].state
                != MICROS_ENDPOINT_STATE_FREE
        ) {
            return false;
        }
    }
    return true;
}

static bool scheduler_is_at_idle_baseline(
    const struct micros_kernel_objects *objects
)
{
    const struct micros_hart *hart =
        &objects->harts[
            micros_kernel_object_runtime_boot_hart_handle().slot
        ];
    size_t priority;

    if (
        hart->current_thread.slot != 0
        || hart->current_thread.generation != 0
        || hart->trap.primary_stack_bottom
            != hart->idle_primary_stack_bottom
        || hart->trap.primary_stack_top
            != hart->idle_primary_stack_top
    ) {
        return false;
    }
    for (
        priority = 0;
        priority < MICROS_SCHEDULER_PRIORITY_COUNT;
        ++priority
    ) {
        if (
            hart->ready_head[priority].slot != 0
            || hart->ready_head[priority].generation != 0
            || hart->ready_tail[priority].slot != 0
            || hart->ready_tail[priority].generation != 0
        ) {
            return false;
        }
    }
    return true;
}

_Noreturn void micros_grant_syscall_test_finish(void)
{
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    const struct micros_kernel_address_space_report *kernel_space =
        micros_kernel_address_space_report();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t actor;
    uint64_t failure_stage = 1;

    if (
        ledger == NULL
        || kernel_space == NULL
        || objects == NULL
        || endpoint_registry == NULL
        || grant_registry == NULL
    ) {
        goto failure;
    }
    for (
        actor = 0;
        actor < GRANT_SYSCALL_PROCESS_COUNT;
        ++actor
    ) {
        if (!close_endpoint(objects, actor)) {
            failure_stage = UINT64_C(0x10) + actor;
            goto failure;
        }
    }
    failure_stage = 2;
    for (
        actor = 0;
        actor < GRANT_SYSCALL_PROCESS_COUNT;
        ++actor
    ) {
        if (
            !release_thread(objects, actor)
            || !release_address_space(objects, actor)
            || micros_frame_ownership_runtime_release_process(
                objects,
                processes[actor]
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            failure_stage = UINT64_C(0x20) + actor;
            goto failure;
        }
    }
    failure_stage = 3;
    if (
        grant_registry->active_count != 0
        || !endpoint_registry_is_empty()
        || objects->live_process_count != baseline_processes
        || objects->live_thread_count != baseline_threads
        || objects->registered_hart_count != baseline_harts
        || ledger->owned_frame_count != baseline_owned
        || ledger->allocator->free_frame_count != baseline_free
        || !scheduler_is_at_idle_baseline(objects)
        || read_satp()
            != expected_satp(kernel_space->root_physical_address)
    ) {
        goto failure;
    }
    failure_stage = 4;
    if (
        micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto failure;
    }
    uart_write(
        "MICROS_GRANT_SYSCALL_TEST_PASS "
        "namespace=unified phase=bootstrap lifecycle=checked "
        "directions=checked errors=stable "
        "registers=preserved cleanup=complete\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );

failure:
    uart_write("MICROS_TEST_FAILURE grant-syscall-finish=");
    uart_write_hex64(failure_stage);
    uart_write("\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}

_Noreturn void micros_grant_syscall_runtime_run_self_test(void)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "GRANT_SYSCALL_GRANTOR",
            .operations = MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .notify_targets = UINT32_C(1) << 2,
        },
        {
            .id = 2,
            .name = "GRANT_SYSCALL_GRANTEE",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        },
        {
            .id = 3,
            .name = "GRANT_SYSCALL_WRONG",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        },
    };
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    size_t payload_size;
    size_t actor;
    uintptr_t saved_status = riscv_irq_save();

    ledger = micros_frame_ownership_runtime_ledger();
    objects = micros_kernel_object_runtime_test_registry();
    if (ledger == NULL || objects == NULL) {
        goto failure;
    }
    baseline_owned = ledger->owned_frame_count;
    baseline_free = ledger->allocator->free_frame_count;
    baseline_processes = objects->live_process_count;
    baseline_threads = objects->live_thread_count;
    baseline_harts = objects->registered_hart_count;
    if (
        micros_ipc_runtime_initialize(
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
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
        goto failure;
    }
    payload_size =
        (uintptr_t)micros_grant_syscall_payload_end
        - (uintptr_t)micros_grant_syscall_payload_start;
    if (
        payload_size == 0
        || payload_size > MICROS_SV39_PAGE_SIZE
    ) {
        goto failure;
    }
    for (
        actor = 0;
        actor < GRANT_SYSCALL_PROCESS_COUNT;
        ++actor
    ) {
        if (
            micros_process_create(objects, &processes[actor])
                != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                endpoint_registry,
                objects,
                processes[actor],
                &endpoints[actor]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                endpoint_registry,
                objects,
                processes[actor],
                (uint8_t)(actor + 1)
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                endpoint_registry,
                objects,
                endpoints[actor]
            ) != MICROS_ENDPOINT_OK
            || !prepare_process(
                objects,
                actor,
                micros_grant_syscall_payload_start,
                payload_size
            )
        ) {
            goto failure;
        }
    }
    if (
        !arm_command(
            GRANT_SYSCALL_GRANTOR,
            &objects->threads[
                threads[GRANT_SYSCALL_GRANTOR].slot
            ].user_context,
            GRANT_COMMAND_IPC_NOTIFY
        )
    ) {
        goto failure;
    }
    if (
        micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto failure;
    }
    __asm__ volatile("fence.i" : : : "memory");
    __asm__ volatile(
        "mv %0, sp\n"
        "mv %1, gp\n"
        "mv %2, tp"
        : "=r"(micros_grant_syscall_test_saved_sp),
          "=r"(micros_grant_syscall_test_saved_gp),
          "=r"(micros_grant_syscall_test_saved_tp)
    );
    (void)saved_status;
    micros_scheduler_test_enter_without_timer(
        threads[GRANT_SYSCALL_GRANTOR]
    );

failure:
    riscv_irq_restore(saved_status);
    uart_write("MICROS_TEST_FAILURE grant-syscall-setup\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
