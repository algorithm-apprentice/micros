#include "micros/stack_observation.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "arch/riscv64/platform.h"
#include "micros/kernel_object_runtime.h"

struct micros_stack_observation micros_stack_observations[131];
volatile uint64_t micros_stack_observer_failure;
const unsigned char __boot_stack_bottom[1];
const unsigned char __boot_stack_top[1];
const unsigned char __trap_stack_bottom[1];
const unsigned char __trap_stack_top[1];
const unsigned char __trap_emergency_stack_bottom[1];
const unsigned char __trap_emergency_stack_top[1];
const unsigned char __thread_kernel_stacks_start[128 * 16384];
static struct micros_kernel_objects objects;

const struct micros_kernel_objects *micros_kernel_object_runtime_registry(void)
{
    return &objects;
}

struct micros_hart *micros_kernel_object_runtime_boot_hart(void)
{
    return &objects.harts[0];
}

void uart_write(const char *text)
{
    fputs(text, stdout);
}

void uart_write_hex64(uint64_t value)
{
    printf("0x%016llx", (unsigned long long)value);
}

void uart_flush(void)
{
    fflush(stdout);
}

int main(int argc, char **argv)
{
    struct micros_thread_handle first = {0, 1};
    struct micros_thread_handle second = {0, 2};

    assert(argc == 2);
    objects.threads[0].generation = 1;
    objects.threads[0].context_attached = true;
    micros_stack_observer_attached(&objects, first);
    assert(micros_stack_observations[3].generation == 1);
    if (strcmp(argv[1], "sticky") == 0) {
        micros_stack_observations[3].failure = 1;
        micros_stack_observations[3].closed = 1;
        objects.threads[0].generation = 2;
        micros_stack_observer_attached(&objects, second);
        if (
            micros_stack_observer_failure == 0
            || micros_stack_observations[3].failure != 1
        ) {
            fputs("counter failure was cleared during generation reuse\n", stderr);
            return 1;
        }
        return 0;
    }
    micros_stack_observations[3].use = 512;
    micros_stack_observations[3].samples = 1;
    micros_stack_observer_detaching(&objects, first);
    assert(micros_stack_observations[3].closed == 1);
    objects.threads[0].generation = 2;
    micros_stack_observer_attached(&objects, second);
    assert(micros_stack_observations[3].generation == 2);
    assert(micros_stack_observations[3].samples == 0);
    micros_stack_observations[3].use = 768;
    micros_stack_observations[3].samples = 2;
    micros_stack_observer_report();
    assert(micros_stack_observer_failure == 0);
    return 0;
}
