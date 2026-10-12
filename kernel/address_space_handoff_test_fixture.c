#include "kernel/address_space_handoff_test_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
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
#include "micros/sv39.h"
#include "micros/syscall_abi.h"
#include "micros/tty.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

static struct micros_address_space_handoff_test_context test_context;

struct micros_address_space_handoff_test_context *
micros_address_space_handoff_test_context(void)
{
    return &test_context;
}

void handoff_test_copy_bytes(
    struct micros_address_space_handoff_test_context *fixture,
    void *destination, const void *source, size_t size
)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    (void)fixture;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

bool handoff_test_bytes_equal(
    struct micros_address_space_handoff_test_context *fixture,
    const void *left,
    const void *right,
    size_t size
)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    size_t index;

    (void)fixture;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

void handoff_test_fill_bytes(
    struct micros_address_space_handoff_test_context *fixture,
    void *storage, unsigned char value, size_t size
)
{
    unsigned char *bytes = storage;
    size_t index;

    (void)fixture;

    for (index = 0; index < size; ++index) {
        bytes[index] = value;
    }
}

uint64_t handoff_test_physical_for(
    struct micros_address_space_handoff_test_context *fixture,
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
    return fixture->user_physical[process_index][page_index] + offset;
}

void handoff_test_write_pattern(
    struct micros_address_space_handoff_test_context *fixture,
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char seed
)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        *(unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
            process_index,
            virtual_address + index
        ) = (unsigned char)(seed + index);
    }
}

bool handoff_test_pattern_matches(
    struct micros_address_space_handoff_test_context *fixture,
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char seed
)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        if (
            *(const unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
                process_index,
                virtual_address + index
            ) != (unsigned char)(seed + index)
        ) {
            return false;
        }
    }
    return true;
}

void handoff_test_write_virtual(
    struct micros_address_space_handoff_test_context *fixture,
    size_t process_index,
    uint64_t virtual_address,
    const void *source,
    size_t size
)
{
    const unsigned char *bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        *(unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
            process_index,
            virtual_address + index
        ) = bytes[index];
    }
}

bool handoff_test_virtual_matches(
    struct micros_address_space_handoff_test_context *fixture,
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
            *(const unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
                process_index,
                virtual_address + index
            ) != bytes[index]
        ) {
            return false;
        }
    }
    return true;
}

bool handoff_test_frame_index_for_physical(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_frame_ownership *ledger,
    uint64_t physical_address,
    uint64_t *frame_index
)
{
    size_t range_index;

    (void)fixture;

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

uint64_t *handoff_test_leaf_pte_for(
    struct micros_address_space_handoff_test_context *fixture,
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

    (void)fixture;

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

void handoff_test_prepare_context(
    struct micros_address_space_handoff_test_context *fixture,
    struct micros_user_context *context,
    size_t process_index
)
{
    uint64_t words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    (void)fixture;

    for (index = 0; index < sizeof(words) / sizeof(words[0]); ++index) {
        words[index] =
            UINT64_C(0x1000)
            + process_index * UINT64_C(0x1000)
            + index;
    }
    handoff_test_copy_bytes(fixture, context, words, sizeof(*context));
    context->sepc = TEST_CODE_ADDRESS
        + (
            (uintptr_t)micros_address_space_handoff_payload_spin
            - (uintptr_t)micros_address_space_handoff_payload_start
        );
    context->sp = TEST_STACK_ADDRESS + MICROS_SV39_PAGE_SIZE;
    context->sstatus = 0;
}

static uint64_t handoff_test_read_satp(void)
{
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}

uint64_t handoff_test_user_address_of(
    struct micros_address_space_handoff_test_context *fixture,
    const unsigned char *symbol
)
{
    (void)fixture;

    return TEST_CODE_ADDRESS
        + (
            (uintptr_t)symbol
            - (uintptr_t)micros_address_space_handoff_payload_start
        );
}

uint64_t handoff_test_expected_satp(uint64_t root)
{
    return MICROS_RISCV_SATP_MODE_SV39 | (root >> 12);
}

uint64_t handoff_test_abi_result(int64_t result)
{
    return (uint64_t)result;
}

size_t handoff_test_current_actor(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_hart *hart
)
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
            hart->current_thread.slot == fixture->threads[process_index].slot
            && hart->current_thread.generation
                == fixture->threads[process_index].generation
        ) {
            return process_index;
        }
    }
    return HANDOFF_PROCESS_COUNT;
}

bool handoff_test_scheduler_state_valid(
    struct micros_address_space_handoff_test_context *fixture,
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
        && handoff_test_read_satp()
            == handoff_test_expected_satp(process->address_space_root)
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

bool handoff_test_syscall_mismatch(
    struct micros_address_space_handoff_test_context *fixture,
    uint64_t stage,
    size_t actor,
    enum handoff_syscall_command command,
    const struct micros_trap_frame *frame
)
{
    (void)fixture;

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
