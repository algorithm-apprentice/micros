#include "kernel/elf_spawn_test_internal.h"

#include "arch/riscv64/platform.h"
#include "micros/panic.h"
#include "micros/trap.h"

static _Noreturn void elf_spawn_report_failure(
    struct micros_hart *hart, struct micros_trap_frame *frame, const char *reason
)
{
    micros_panic_seize();
    uart_write("MICROS_ELF_SPAWN_TEST_FAILURE stage=");
    uart_write_hex64(frame->a0);
    uart_write(" detail=");
    uart_write_hex64(frame->a1);
    uart_write("\n");
    uart_flush();
    MICROS_TRAP_PANIC(hart->hardware_id, reason, frame);
}

static bool elf_spawn_capture(struct micros_elf_spawn_resources *resources)
{
    return elf_spawn_test_runtime_valid()
        && elf_spawn_test_capture_kernel(&resources->kernel)
        && elf_spawn_test_capture_services(&resources->services);
}

static bool elf_spawn_runtime_idle(const struct micros_elf_spawn_resources *resources)
{
    return resources->kernel.objects.live_process_count == MICROS_ELF_SPAWN_SERVICE_COUNT + 1
        && resources->kernel.objects.live_thread_count == MICROS_ELF_SPAWN_SERVICE_COUNT + 1
        && resources->kernel.table_count == MICROS_ELF_SPAWN_TABLE_CAPACITY
        && resources->kernel.grants.active_count == 0
        && resources->kernel.pm.active_transaction == 0
        && resources->kernel.pm.prepared.token == 0
        && resources->kernel.pm.exit.phase == MICROS_PM_CONTROL_EXIT_NONE
        && resources->kernel.vm.load.phase == MICROS_VM_LOAD_PHASE_IDLE
        && resources->kernel.vm.load.scratch_phase == MICROS_VM_SCRATCH_CLEAR
        && resources->kernel.ownership.owner_counts[MICROS_FRAME_OWNER_VM_TRANSFERABLE] == 0;
}

static bool elf_spawn_uart_empty(void)
{
    volatile uint8_t *uart = (volatile uint8_t *)(uintptr_t)MICROS_TTY_UART_PHYSICAL_BASE;
    uint64_t attempt;

    for (attempt = 0; attempt < UINT64_C(1000000); ++attempt) {
        __asm__ volatile("fence iorw, iorw" ::: "memory");
        if ((uart[5] & UINT8_C(0x60)) == UINT8_C(0x60)) {
            return true;
        }
    }
    return false;
}

static bool elf_spawn_baseline(
    struct elf_spawn_test_context *context, struct micros_process_handle process,
    micros_endpoint_t endpoint
)
{
    if (
        context->phase != ELF_SPAWN_TEST_BASELINE
        || !elf_spawn_capture(&context->baseline)
        || !elf_spawn_runtime_idle(&context->baseline)
        || context->baseline.services.pm.last_pid != MICROS_ELF_SPAWN_INIT_PID
        || !elf_spawn_test_services_idle(&context->baseline.services, endpoint)
    ) {
        return false;
    }
    context->init_process = process;
    context->init_endpoint = endpoint;
    context->phase = ELF_SPAWN_TEST_ROLLBACK;
    return true;
}

static bool elf_spawn_rollback(
    struct elf_spawn_test_context *context, struct micros_process_handle process
)
{
    return (
        context->phase == ELF_SPAWN_TEST_ROLLBACK
        && process.slot == context->init_process.slot
        && process.generation == context->init_process.generation
        && elf_spawn_capture(&context->current)
        && elf_spawn_runtime_idle(&context->current)
        && context->current.services.pm.last_pid == MICROS_ELF_SPAWN_FAILED_PID
        && context->current.kernel.vm.last_mapping_generation
            == context->baseline.kernel.vm.last_mapping_generation + 1
        && context->current.kernel.pm.last_prepared_token
            == context->baseline.kernel.pm.last_prepared_token
        && elf_spawn_test_services_idle(&context->current.services, context->init_endpoint)
        && micros_elf_spawn_same_kernel_resources(&context->baseline.kernel, &context->current.kernel)
        && micros_elf_spawn_same_service_resources(&context->baseline.services, &context->current.services)
    );
}

static bool elf_spawn_child(
    struct elf_spawn_test_context *context, micros_endpoint_t endpoint
)
{
    size_t index;
    const struct micros_pm_process_record *child = NULL;

    if (
        context->phase != ELF_SPAWN_TEST_CHILD
        || !elf_spawn_test_runtime_valid()
        || !elf_spawn_test_capture_services(&context->current.services)
        || !elf_spawn_test_child_descriptors(
            &context->current.services, context->init_endpoint, endpoint
        )
    ) {
        return false;
    }
    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct micros_pm_process_record *record = &context->current.services.pm.records[index];

        if (record->state == MICROS_PM_PROCESS_RUNNING && record->endpoint == endpoint) {
            child = record;
        }
    }
    return child != NULL && child->pid == MICROS_ELF_SPAWN_CHILD_PID
        && !child->reaper && child->parent.slot == context->current.services.pm.reaper.slot
        && child->parent.generation == context->current.services.pm.reaper.generation;
}

static bool elf_spawn_final(
    struct elf_spawn_test_context *context, struct micros_process_handle process
)
{
    return (
        context->phase == ELF_SPAWN_TEST_FINAL
        && process.slot == context->init_process.slot
        && process.generation == context->init_process.generation
        && elf_spawn_capture(&context->current)
        && elf_spawn_runtime_idle(&context->current)
        && context->current.services.pm.last_pid == MICROS_ELF_SPAWN_CHILD_PID
        && elf_spawn_test_services_idle(&context->current.services, context->init_endpoint)
        && micros_elf_spawn_same_kernel_resources(&context->baseline.kernel, &context->current.kernel)
        && micros_elf_spawn_same_service_resources(&context->baseline.services, &context->current.services)
        && elf_spawn_uart_empty()
    );
}

bool elf_spawn_test_process_report(struct micros_hart *hart, struct micros_trap_frame *frame)
{
    struct elf_spawn_test_context *context = elf_spawn_test_context_get();
    struct micros_process_handle process;
    micros_endpoint_t endpoint;
    bool valid = false;

    if (!elf_spawn_test_current_process(&process, &endpoint)) {
        elf_spawn_report_failure(hart, frame, "elf-spawn-reporter");
    }
    if (frame->a0 == MICROS_ELF_SPAWN_REPORT_FAILURE) {
        elf_spawn_report_failure(hart, frame, "elf-spawn-application-failure");
    }
    if (frame->a0 == MICROS_ELF_SPAWN_REPORT_CHILD) {
        valid = frame->sepc == micros_elf_spawn_report_addresses[1]
            && frame->a1 == MICROS_ELF_SPAWN_CHILD_PID && elf_spawn_child(context, endpoint);
        if (valid) {
            context->phase = ELF_SPAWN_TEST_FINAL;
        }
    } else if (frame->sepc == micros_elf_spawn_report_addresses[0]) {
        if (frame->a0 == MICROS_ELF_SPAWN_REPORT_BASELINE) {
            valid = frame->a1 == MICROS_ELF_SPAWN_INIT_PID
                && elf_spawn_baseline(context, process, endpoint);
        } else if (frame->a0 == MICROS_ELF_SPAWN_REPORT_ROLLBACK) {
            valid = frame->a1 == MICROS_ELF_SPAWN_FAILED_PID && elf_spawn_rollback(context, process);
            if (valid) {
                context->phase = ELF_SPAWN_TEST_CHILD;
            }
        } else if (frame->a0 == MICROS_ELF_SPAWN_REPORT_FINAL) {
            if (frame->a1 != MICROS_ELF_SPAWN_CHILD_PID || !elf_spawn_final(context, process)) {
                elf_spawn_report_failure(hart, frame, "elf-spawn-final-resources");
            }
            (void)sbi_system_reset(SBI_RESET_TYPE_SHUTDOWN, SBI_RESET_REASON_NONE);
            for (;;) {
                __asm__ volatile("wfi");
            }
        }
    }
    if (!valid) {
        elf_spawn_report_failure(hart, frame, "elf-spawn-checkpoint");
    }
    frame->sepc += 4;
    return true;
}
