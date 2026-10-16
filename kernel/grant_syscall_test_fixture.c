#include "grant_syscall_test_internal.h"

const uint64_t TEST_CODE_ADDRESS =
    MICROS_USER_VIRTUAL_BASE;
const uint64_t TEST_STACK_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + MICROS_SV39_PAGE_SIZE;
const uint64_t TEST_DATA_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x2000);
const uint64_t TEST_DATA_SECOND_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x3000);
const uint64_t TEST_READ_ONLY_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x4000);
const uint64_t TEST_UNMAPPED_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x6000);
const uint64_t TEST_CONTROL_MAGIC =
    UINT64_C(0x4752414e54535953);

static struct grant_syscall_test_fixture fixture_state = {
    .dispatch_actor = GRANT_SYSCALL_PROCESS_COUNT,
};

struct grant_syscall_test_fixture *grant_syscall_test_context(void)
{
    return &fixture_state;
}

void grant_syscall_test_copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

bool grant_syscall_test_bytes_equal(
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

void grant_syscall_test_fill_bytes(void *storage, unsigned char value, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = value;
    }
}

uint64_t grant_syscall_test_read_satp(void)
{
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}

uint64_t grant_syscall_test_user_address_of(const unsigned char *symbol)
{
    return TEST_CODE_ADDRESS
        + (
            (uintptr_t)symbol
            - (uintptr_t)micros_grant_syscall_payload_start
        );
}

uint64_t grant_syscall_test_expected_satp(uint64_t root)
{
    return MICROS_RISCV_SATP_MODE_SV39 | (root >> 12);
}

uint64_t grant_syscall_test_abi_result(int64_t result)
{
    return (uint64_t)result;
}

uint64_t grant_syscall_test_physical_for(
    struct grant_syscall_test_fixture *fixture,
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
    if (fixture->user_physical[process_index][page_index] == 0) {
        return 0;
    }
    offset = virtual_address % MICROS_SV39_PAGE_SIZE;
    return fixture->user_physical[process_index][page_index] + offset;
}

void grant_syscall_test_write_pattern(
    struct grant_syscall_test_fixture *fixture,
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char seed
)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        *(unsigned char *)(uintptr_t)grant_syscall_test_physical_for(
            fixture,
            process_index,
            virtual_address + index
        ) = (unsigned char)(seed + index);
    }
}

bool grant_syscall_test_pattern_matches(
    struct grant_syscall_test_fixture *fixture,
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char seed
)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        uint64_t physical = grant_syscall_test_physical_for(
            fixture,
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

bool grant_syscall_test_range_is_value(
    struct grant_syscall_test_fixture *fixture,
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char value
)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        uint64_t physical = grant_syscall_test_physical_for(
            fixture,
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

void grant_syscall_test_fill_context_pattern(
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
    grant_syscall_test_copy_bytes(context, words, sizeof(*context));
}

size_t grant_syscall_test_current_actor(
    struct grant_syscall_test_fixture *fixture,
    const struct micros_hart *hart
)
{
    size_t index;

    if (hart == NULL) {
        return GRANT_SYSCALL_PROCESS_COUNT;
    }
    for (index = 0; index < GRANT_SYSCALL_PROCESS_COUNT; ++index) {
        if (
            fixture->thread_live[index]
            && hart->current_thread.slot == fixture->threads[index].slot
            && hart->current_thread.generation
                == fixture->threads[index].generation
        ) {
            return index;
        }
    }
    return GRANT_SYSCALL_PROCESS_COUNT;
}

bool grant_syscall_test_test_mismatch(
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

static void grant_syscall_test_normalize_objects(struct micros_kernel_objects *objects)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        grant_syscall_test_fill_bytes(
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

static bool grant_syscall_test_capture_page_tables(
    struct grant_syscall_test_fixture *fixture,
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

    grant_syscall_test_fill_bytes(roots, 0, sizeof(bool) * GRANT_SYSCALL_PROCESS_COUNT);
    grant_syscall_test_fill_bytes(
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
            &objects->processes[fixture->processes[process_index].slot];
        struct page_table *root;
        struct page_table *middle;
        struct page_table *leaf;
        struct micros_sv39_decoded_pte root_entry;
        struct micros_sv39_decoded_pte middle_entry;

        if (
            record->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || record->generation
                != fixture->processes[process_index].generation
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
        grant_syscall_test_copy_bytes(
            tables[process_index][0],
            root,
            MICROS_SV39_PAGE_SIZE
        );
        grant_syscall_test_copy_bytes(
            tables[process_index][1],
            middle,
            MICROS_SV39_PAGE_SIZE
        );
        grant_syscall_test_copy_bytes(
            tables[process_index][2],
            leaf,
            MICROS_SV39_PAGE_SIZE
        );
    }
    return true;
}

static void grant_syscall_test_snapshot_user_bytes(
    struct grant_syscall_test_fixture *fixture
)
{
    size_t process_index;
    size_t page_index;

    grant_syscall_test_copy_bytes(
        fixture->user_physical_snapshot,
        fixture->user_physical,
        sizeof(fixture->user_physical_snapshot)
    );
    grant_syscall_test_fill_bytes(
        fixture->user_bytes_snapshot,
        0,
        sizeof(fixture->user_bytes_snapshot)
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
            if (fixture->user_physical[process_index][page_index] == 0) {
                continue;
            }
            grant_syscall_test_copy_bytes(
                fixture->user_bytes_snapshot[process_index][page_index],
                (const void *)(uintptr_t)
                    fixture->user_physical[process_index][page_index],
                MICROS_SV39_PAGE_SIZE
            );
        }
    }
}

static bool grant_syscall_test_user_bytes_match(
    struct grant_syscall_test_fixture *fixture
)
{
    size_t process_index;
    size_t page_index;

    if (!grant_syscall_test_bytes_equal(
        fixture->user_physical_snapshot,
        fixture->user_physical,
        sizeof(fixture->user_physical_snapshot)
    )) {
        return false;
    }
    grant_syscall_test_fill_bytes(
        fixture->user_bytes_observed,
        0,
        sizeof(fixture->user_bytes_observed)
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
            if (fixture->user_physical[process_index][page_index] == 0) {
                continue;
            }
            grant_syscall_test_copy_bytes(
                fixture->user_bytes_observed[process_index][page_index],
                (const void *)(uintptr_t)
                    fixture->user_physical[process_index][page_index],
                MICROS_SV39_PAGE_SIZE
            );
        }
    }
    return grant_syscall_test_bytes_equal(
        fixture->user_bytes_snapshot,
        fixture->user_bytes_observed,
        sizeof(fixture->user_bytes_snapshot)
    );
}

bool grant_syscall_test_snapshot_failure_state(
    struct grant_syscall_test_fixture *fixture
)
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
        || fixture->endpoint_registry == NULL
        || fixture->grant_registry == NULL
        || ledger->managed_frame_count
            > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
    ) {
        return false;
    }
    bitmap_words = (size_t)(
        (ledger->managed_frame_count + 63) / 64
    );
    grant_syscall_test_fill_bytes(
        &fixture->ownership_snapshot,
        0,
        sizeof(fixture->ownership_snapshot)
    );
    grant_syscall_test_copy_bytes(
        &fixture->ownership_snapshot,
        ledger,
        offsetof(struct micros_frame_ownership, owners)
    );
    grant_syscall_test_copy_bytes(
        fixture->ownership_snapshot.owners,
        ledger->owners,
        (size_t)ledger->managed_frame_count
            * sizeof(ledger->owners[0])
    );
    grant_syscall_test_copy_bytes(
        fixture->ownership_snapshot.handoff_targets,
        ledger->handoff_targets,
        (size_t)ledger->managed_frame_count
    );
    grant_syscall_test_fill_bytes(&fixture->allocator_snapshot, 0, sizeof(fixture->allocator_snapshot));
    grant_syscall_test_copy_bytes(
        &fixture->allocator_snapshot,
        ledger->allocator,
        offsetof(struct micros_frame_allocator, allocated_bitmap)
    );
    grant_syscall_test_copy_bytes(
        fixture->allocator_snapshot.allocated_bitmap,
        ledger->allocator->allocated_bitmap,
        bitmap_words
            * sizeof(ledger->allocator->allocated_bitmap[0])
    );
    grant_syscall_test_copy_bytes(
        &fixture->endpoint_snapshot,
        fixture->endpoint_registry,
        sizeof(fixture->endpoint_snapshot)
    );
    grant_syscall_test_copy_bytes(
        &fixture->grant_snapshot,
        fixture->grant_registry,
        sizeof(fixture->grant_snapshot)
    );
    grant_syscall_test_copy_bytes(&fixture->objects_snapshot, objects, sizeof(fixture->objects_snapshot));
    grant_syscall_test_normalize_objects(&fixture->objects_snapshot);
    if (!grant_syscall_test_capture_page_tables(
        fixture,
        objects,
        fixture->root_snapshot,
        fixture->page_table_snapshot
    )) {
        return false;
    }
    grant_syscall_test_snapshot_user_bytes(fixture);
    fixture->failure_snapshot_active = true;
    return true;
}

bool grant_syscall_test_failure_state_matches(
    struct grant_syscall_test_fixture *fixture
)
{
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t bitmap_words;

    if (
        !fixture->failure_snapshot_active
        || ledger == NULL
        || ledger->allocator == NULL
        || objects == NULL
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
    if (
        !grant_syscall_test_bytes_equal(
            &fixture->ownership_snapshot,
            ledger,
            offsetof(struct micros_frame_ownership, owners)
        )
        || !grant_syscall_test_bytes_equal(
            fixture->ownership_snapshot.owners,
            ledger->owners,
            (size_t)ledger->managed_frame_count
                * sizeof(ledger->owners[0])
        )
        || !grant_syscall_test_bytes_equal(
            fixture->ownership_snapshot.handoff_targets,
            ledger->handoff_targets,
            (size_t)ledger->managed_frame_count
        )
        || !grant_syscall_test_bytes_equal(
            &fixture->allocator_snapshot,
            ledger->allocator,
            offsetof(
                struct micros_frame_allocator,
                allocated_bitmap
            )
        )
        || !grant_syscall_test_bytes_equal(
            fixture->allocator_snapshot.allocated_bitmap,
            ledger->allocator->allocated_bitmap,
            bitmap_words
                * sizeof(
                    ledger->allocator->allocated_bitmap[0]
                )
        )
        || !grant_syscall_test_bytes_equal(
            &fixture->endpoint_snapshot,
            fixture->endpoint_registry,
            sizeof(fixture->endpoint_snapshot)
        )
        || !grant_syscall_test_bytes_equal(
            &fixture->grant_snapshot,
            fixture->grant_registry,
            sizeof(fixture->grant_snapshot)
        )
    ) {
        return false;
    }
    grant_syscall_test_copy_bytes(&fixture->objects_observed, objects, sizeof(fixture->objects_observed));
    grant_syscall_test_normalize_objects(&fixture->objects_observed);
    if (
        !grant_syscall_test_bytes_equal(
            &fixture->objects_snapshot,
            &fixture->objects_observed,
            sizeof(fixture->objects_snapshot)
        )
        || !grant_syscall_test_capture_page_tables(
            fixture,
            objects,
            fixture->root_observed,
            fixture->page_table_observed
        )
        || !grant_syscall_test_bytes_equal(
            fixture->root_snapshot,
            fixture->root_observed,
            sizeof(fixture->root_snapshot)
        )
        || !grant_syscall_test_bytes_equal(
            fixture->page_table_snapshot,
            fixture->page_table_observed,
            sizeof(fixture->page_table_snapshot)
        )
        || !grant_syscall_test_user_bytes_match(fixture)
    ) {
        return false;
    }
    fixture->failure_snapshot_active = false;
    return true;
}

bool grant_syscall_test_scheduler_state_valid(
    struct grant_syscall_test_fixture *fixture,
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
        || !fixture->thread_live[actor]
    ) {
        return false;
    }
    thread = &objects->threads[fixture->threads[actor].slot];
    process = &objects->processes[fixture->processes[actor].slot];
    return (
        thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
        && thread->generation == fixture->threads[actor].generation
        && process->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
        && process->generation == fixture->processes[actor].generation
        && hart->current_thread.slot == fixture->threads[actor].slot
        && hart->current_thread.generation
            == fixture->threads[actor].generation
        && thread->runtime_flags == 0
        && thread->scheduler_assigned
        && thread->ready_linked
        && hart->ready_head[thread->scheduler_priority].slot
            == fixture->threads[actor].slot
        && hart->ready_head[thread->scheduler_priority].generation
            == fixture->threads[actor].generation
        && hart->trap.primary_stack_bottom
            == thread->kernel_stack_bottom
        && hart->trap.primary_stack_top == thread->kernel_stack_top
        && grant_syscall_test_read_satp() == grant_syscall_test_expected_satp(process->address_space_root)
        && (
            !after_return
            || (
                hart->accounting_owner
                    == MICROS_SCHEDULER_ACCOUNTING_THREAD
                && hart->accounted_thread.slot
                    == fixture->threads[actor].slot
                && hart->accounted_thread.generation
                    == fixture->threads[actor].generation
            )
        )
    );
}

void grant_syscall_test_arm_raw(
    struct grant_syscall_test_fixture *fixture,
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
    struct grant_syscall_script *script = &fixture->scripts[actor];

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
        grant_syscall_test_user_address_of(micros_grant_syscall_payload_ecall);
}

void grant_syscall_test_arm_control(struct micros_user_context *context)
{
    context->a0 = TEST_CONTROL_MAGIC;
    context->a7 = UINT64_MAX;
    context->sepc =
        grant_syscall_test_user_address_of(micros_grant_syscall_payload_ecall);
}
