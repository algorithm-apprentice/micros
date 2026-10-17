#include "kernel/scheduler_test_internal.h"

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/sv39.h"
#include "micros/timer.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

bool scheduler_test_reserve_process(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects,
    size_t index,
    uint32_t code_permissions,
    uint32_t data_permissions
)
{
    if (
        micros_process_create(objects, &fixture->processes[index])
            != MICROS_KERNEL_OBJECT_OK
        || micros_endpoint_reserve(
            fixture->registry,
            objects,
            fixture->processes[index],
            &fixture->endpoints[index]
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_install_profile(
            fixture->registry,
            objects,
            fixture->processes[index],
            1
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            fixture->registry,
            objects,
            fixture->endpoints[index]
        ) != MICROS_ENDPOINT_OK
        || micros_user_address_space_create(fixture->processes[index])
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            fixture->processes[index],
            TEST_CODE_VIRTUAL_ADDRESS,
            code_permissions,
            &fixture->code_physical[index]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            fixture->processes[index],
            TEST_COUNTER_VIRTUAL_ADDRESS,
            data_permissions,
            &fixture->counter_physical[index]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            fixture->processes[index],
            TEST_STACK_VIRTUAL_ADDRESS,
            data_permissions,
            &fixture->stack_physical[index]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_thread_create(
            objects,
            fixture->processes[index],
            &fixture->threads[index]
        ) != MICROS_KERNEL_OBJECT_OK
        || !micros_user_execution_test_stack_bounds(
            fixture->threads[index],
            &fixture->kernel_stack_bottom[index],
            &fixture->kernel_stack_top[index]
        )
    ) {
        return false;
    }
    return true;
}

bool scheduler_test_prepare_process_image(
    struct scheduler_test_context *fixture,
    size_t index,
    size_t payload_size
)
{
    scheduler_test_copy_bytes(
        (void *)(uintptr_t)fixture->code_physical[index],
        micros_scheduler_payload_start,
        payload_size
    );
    scheduler_test_fill_context_pattern(
        &fixture->prepared_contexts[index],
        UINT64_C(0x1000) + index * UINT64_C(0x1000)
    );
    fixture->prepared_contexts[index].sepc = TEST_CODE_VIRTUAL_ADDRESS;
    fixture->prepared_contexts[index].sp =
        TEST_STACK_VIRTUAL_ADDRESS + MICROS_SV39_PAGE_SIZE;
    fixture->prepared_contexts[index].s0 = TEST_COUNTER_VIRTUAL_ADDRESS;
    fixture->prepared_contexts[index].s1 = 0;
    fixture->prepared_contexts[index].t1 = 1;
    fixture->prepared_contexts[index].sstatus = 0;
    if (
        micros_user_execution_prepare(
            fixture->threads[index],
            &fixture->prepared_contexts[index]
        ) != MICROS_USER_EXECUTION_OK
        || micros_user_execution_inspect(
            fixture->threads[index],
            &fixture->prepared_contexts[index]
        ) != MICROS_USER_EXECUTION_OK
    ) {
        return false;
    }
    return true;
}

bool scheduler_test_admit_threads(struct scheduler_test_context *fixture)
{
    if (
        micros_scheduler_initialize(TEST_TIMER_INTERVAL)
            != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            fixture->threads[0],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            TEST_QUANTUM
        ) != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            fixture->threads[1],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            TEST_QUANTUM
        ) != MICROS_SCHEDULER_OK
    ) {
        return false;
    }
    return true;
}

bool scheduler_test_prove_malformed_completion(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects
)
{
    objects->threads[fixture->threads[1].slot].ipc_receive_buffer =
        TEST_IPC_BUFFER_VIRTUAL_ADDRESS;
    if (
        !micros_scheduler_test_rejects_malformed_completion(
            fixture->threads[1]
        )
    ) {
        return false;
    }
    objects->threads[fixture->threads[1].slot].ipc_receive_buffer = 0;
    return true;
}

bool scheduler_test_stage_start_completion(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects
)
{
    if (
        micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || !scheduler_test_expect_no_message_completion(
            fixture,
            objects,
            0,
            MICROS_IPC_OK,
            TEST_COMPLETION_START
        )
    ) {
        return false;
    }
    return true;
}

bool scheduler_test_prove_start_program_failure(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects
)
{
    micros_timer_test_fail_next_program();
    if (
        !scheduler_test_snapshot_start_state(fixture, objects)
        || micros_scheduler_start() != MICROS_SCHEDULER_ERROR_TIMER
        || !scheduler_test_start_state_matches(fixture, objects)
    ) {
        return false;
    }
    fixture->kernel_ticks_before_start =
        micros_kernel_object_runtime_boot_hart()
            ->kernel_counter_ticks;
    micros_timer_test_delay_next_program(
        TEST_START_TIMER_DELAY
    );
    return true;
}

bool scheduler_test_snapshot_start_state(
    struct scheduler_test_context *fixture,
    const struct micros_kernel_objects *objects
)
{
    if (objects == NULL) {
        return false;
    }
    scheduler_test_copy_bytes(&fixture->start_snapshot, objects, sizeof(fixture->start_snapshot));
    fixture->start_satp_snapshot = scheduler_test_read_satp();
    fixture->start_sie_snapshot = riscv_irq_is_enabled();
    fixture->start_stie_snapshot = riscv_timer_interrupt_is_enabled();
    fixture->start_timer_attempts = micros_timer_test_program_attempts();
    return true;
}

bool scheduler_test_start_state_matches(
    struct scheduler_test_context *fixture,
    const struct micros_kernel_objects *objects
)
{
    return (
        objects != NULL
        && scheduler_test_bytes_equal(
            objects,
            &fixture->start_snapshot,
            sizeof(fixture->start_snapshot)
        )
        && scheduler_test_read_satp() == fixture->start_satp_snapshot
        && riscv_irq_is_enabled() == fixture->start_sie_snapshot
        && riscv_timer_interrupt_is_enabled() == fixture->start_stie_snapshot
        && micros_timer_test_program_attempts()
            == fixture->start_timer_attempts + 1
    );
}
