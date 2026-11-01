#include "micros/stack_observation.h"

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#include "arch/riscv64/platform.h"
#include "micros/kernel_object_runtime.h"

extern const unsigned char __boot_stack_bottom[];
extern const unsigned char __boot_stack_top[];
extern const unsigned char __trap_stack_bottom[];
extern const unsigned char __trap_stack_top[];
extern const unsigned char __trap_emergency_stack_bottom[];
extern const unsigned char __trap_emergency_stack_top[];
extern const unsigned char __thread_kernel_stacks_start[];

static struct micros_stack_observation report_copies[131];
static struct micros_stack_observation retired_copies[16];
static uint16_t retired_slots[16];
static size_t retired_count;

static void copy_observation(
    struct micros_stack_observation *destination,
    const struct micros_stack_observation *source
)
{
    size_t index;

    destination->generation = source->generation;
    destination->use = source->use;
    destination->samples = source->samples;
    destination->failure = source->failure;
    destination->closed = source->closed;
    for (index = 0; index < 3; ++index) {
        destination->reserved[index] = source->reserved[index];
    }
}

void micros_stack_observer_attached(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
)
{
    struct micros_stack_observation *record;
    size_t index;

    if (objects != micros_kernel_object_runtime_registry()) {
        return;
    }
    if (
        objects == NULL
        || thread.slot >= MICROS_THREAD_CAPACITY || thread.generation == 0
        || objects->threads[thread.slot].generation != thread.generation
        || !objects->threads[thread.slot].context_attached
    ) {
        micros_stack_observer_failure = 1;
        return;
    }
    record = &micros_stack_observations[3 + thread.slot];
    if (record->failure != 0) {
        micros_stack_observer_failure = 1;
        return;
    }
    if (record->samples != 0) {
        if (record->closed != 1 || retired_count == 16) {
            micros_stack_observer_failure = 1;
            return;
        }
        retired_slots[retired_count] = thread.slot;
        copy_observation(&retired_copies[retired_count], record);
        ++retired_count;
    }
    record->generation = thread.generation;
    record->use = 0;
    record->samples = 0;
    record->failure = 0;
    record->closed = 0;
    for (index = 0; index < 3; ++index) {
        record->reserved[index] = 0;
    }
    atomic_thread_fence(memory_order_seq_cst);
#ifdef MICROS_STACK_OBSERVER_APPLICATIONS
    micros_stack_observer_user_attached(objects, thread);
#endif
}

void micros_stack_observer_detaching(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
)
{
    struct micros_stack_observation *record;

    if (objects != micros_kernel_object_runtime_registry()) {
        return;
    }
    if (
        objects == NULL || thread.slot >= MICROS_THREAD_CAPACITY
        || objects->threads[thread.slot].generation != thread.generation
        || !objects->threads[thread.slot].context_attached
    ) {
        micros_stack_observer_failure = 1;
        return;
    }
    record = &micros_stack_observations[3 + thread.slot];
    if (record->generation != thread.generation || record->closed != 0) {
        micros_stack_observer_failure = 1;
        return;
    }
#ifdef MICROS_STACK_OBSERVER_APPLICATIONS
    micros_stack_observer_user_detaching(objects, thread);
#endif
    record->closed = 1;
    atomic_thread_fence(memory_order_seq_cst);
}

static void report_write(const char *text)
{
#ifdef MICROS_STACK_OBSERVER_POLLED_REPORT
    volatile uint8_t *uart = (volatile uint8_t *)(uintptr_t)MICROS_RISCV_UART0_BASE;

    while (*text != '\0') {
        while ((uart[5] & 0x20U) == 0) {
        }
        uart[0] = (uint8_t)*text++;
    }
#else
    uart_write(text);
#endif
}

static void report_hex(uint64_t value)
{
#ifdef MICROS_STACK_OBSERVER_POLLED_REPORT
    static const char digits[] = "0123456789abcdef";
    char output[19];
    size_t index;

    output[0] = '0';
    output[1] = 'x';
    for (index = 0; index < 16; ++index) {
        output[index + 2] = digits[(value >> (60 - index * 4)) & 15U];
    }
    output[18] = '\0';
    report_write(output);
#else
    uart_write_hex64(value);
#endif
}

static void report_flush(void)
{
#ifdef MICROS_STACK_OBSERVER_POLLED_REPORT
    const volatile uint8_t *uart = (const volatile uint8_t *)(uintptr_t)MICROS_RISCV_UART0_BASE;

    while ((uart[5] & 0x40U) == 0) {
    }
#else
    uart_flush();
#endif
}

static void write_slot(uint16_t slot)
{
    char digits[4];
    size_t offset = 3;

    digits[3] = '\0';
    do {
        digits[--offset] = (char)('0' + slot % 10);
        slot /= 10;
    } while (slot != 0);
    report_write(&digits[offset]);
}

static void write_value(const char *name, uint64_t value)
{
    report_write(name);
    report_hex(value);
}

void micros_stack_observer_write_record(
    const char *image,
    const char *owner,
    uint16_t slot,
    uint64_t generation,
    const struct micros_stack_observation *record,
    uint64_t bottom,
    uint64_t top
)
{
    report_write("MICROS_STACK_OBSERVATION image=");
    report_write(image);
    report_write(" owner=");
    report_write(owner);
    report_write(" slot=");
    write_slot(slot);
    write_value(" generation=", generation);
    write_value(" samples=", record->samples);
    write_value(" use=", record->use);
    write_value(" bottom=", bottom);
    write_value(" top=", top);
    write_value(" failure=", record->failure | micros_stack_observer_failure);
    report_write("\n");
}

static void report_observation(
    size_t index,
    uint16_t slot,
    const struct micros_stack_observation *record
)
{
    uintptr_t bottom;
    uintptr_t top;
    const char *owner;

    if (record->samples == 0) {
        return;
    }
    if (index == 0) {
        owner = "boot";
        bottom = (uintptr_t)__boot_stack_bottom;
        top = (uintptr_t)__boot_stack_top;
    } else if (index == 1) {
        owner = "primary";
        bottom = (uintptr_t)__trap_stack_bottom;
        top = (uintptr_t)__trap_stack_top;
    } else if (index == 2) {
        owner = "emergency";
        bottom = (uintptr_t)__trap_emergency_stack_bottom;
        top = (uintptr_t)__trap_emergency_stack_top;
    } else {
        owner = "thread";
        bottom = (uintptr_t)__thread_kernel_stacks_start
            + (slot * MICROS_THREAD_KERNEL_STACK_SIZE);
        top = bottom + MICROS_THREAD_KERNEL_STACK_SIZE;
    }
    micros_stack_observer_write_record("kernel", owner, slot, record->generation, record, bottom, top);
}

void micros_stack_observer_report(void)
{
    size_t index;

    atomic_thread_fence(memory_order_seq_cst);
#ifdef MICROS_STACK_OBSERVER_USERS
    micros_stack_observer_capture_bootstrap_users();
#endif
    for (index = 0; index < 131; ++index) {
        copy_observation(&report_copies[index], &micros_stack_observations[index]);
    }
    for (index = 0; index < retired_count; ++index) {
        report_observation(3, retired_slots[index], &retired_copies[index]);
    }
    for (index = 0; index < 131; ++index) {
        report_observation(index, index < 3 ? 0 : (uint16_t)(index - 3), &report_copies[index]);
    }
    if (micros_stack_observer_failure != 0) {
        report_write("MICROS_TEST_FAILURE stack-observer\n");
    }
    report_flush();
}
