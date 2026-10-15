#include "servers/pm/pm_service_internal.h"

void micros_service_main(void)
{
    struct pm_service_context *context = pm_service_context_get();
    struct micros_ipc_message message;

    if (
        context->pm_service_data != UINT64_C(0x504d534552564943)
        || !pm_service_validate_configuration()
        || micros_pm_table_initialize(&context->process_table)
            != MICROS_PM_MODEL_OK
        || micros_pm_table_validate(&context->process_table)
            != MICROS_PM_MODEL_OK
    ) {
        __builtin_trap();
    }
    pm_service_configure_coordinator(context);
#ifdef MICROS_PM_SERVICE_TEST
    pm_service_copy_bytes(
        &context->initial_table,
        &context->process_table,
        sizeof(context->process_table)
    );
#endif
    if (!pm_service_send_ready()) {
        __builtin_trap();
    }
    pm_service_clear_bytes(&message, sizeof(message));
    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &message)
            != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    for (;;) {
        if (message.type == MICROS_IPC_TYPE_KERNEL_NOTIFICATION) {
#ifdef MICROS_PM_SERVICE_TEST
            if (
                !pm_service_bytes_equal(
                    &context->process_table,
                    &context->initial_table,
                    sizeof(context->process_table)
                )
            ) {
                __builtin_trap();
            }
#endif
            if (
                micros_pm_enable_runtime(&context->process_table, &message)
                    != MICROS_PM_MODEL_OK
            ) {
                __builtin_trap();
            }
#ifdef MICROS_PM_SERVICE_TEST
            pm_service_run_self_test(context);
#endif
#ifdef MICROS_BUILD_ELF_SPAWN_TEST
            pm_service_start_elf_spawn_init(context);
#endif
            pm_service_clear_bytes(&message, sizeof(message));
            if (
                micros_runtime_receive(
                    MICROS_ENDPOINT_ANY,
                    &message
                ) != MICROS_SYSCALL_ABI_OK
            ) {
                __builtin_trap();
            }
        } else {
#ifdef MICROS_VM_SCRATCH_PREEMPTION_TEST
            pm_service_run_scratch_preemption_test(&message);
#else
            struct micros_ipc_message reply;
            uint64_t reply_token = message.reply_token;

            enum micros_pm_coordinator_response_kind kind =
                pm_service_serve_request(context, &message, &reply);
            pm_service_clear_bytes(&message, sizeof(message));
            if (kind == MICROS_PM_COORDINATOR_RESPONSE_REPLY) {
                if (micros_runtime_reply_receive(
                    reply_token,
                    &reply,
                    MICROS_ENDPOINT_ANY,
                    &message
                ) != MICROS_SYSCALL_ABI_OK) {
                    __builtin_trap();
                }
            } else if (
                kind == MICROS_PM_COORDINATOR_RESPONSE_PARENT_LOST
                || kind == MICROS_PM_COORDINATOR_RESPONSE_EXIT_ACCEPTED
                || kind == MICROS_PM_COORDINATOR_RESPONSE_WAIT_BLOCKED
                || kind == MICROS_PM_COORDINATOR_RESPONSE_WAIT_REPLY_COMPLETED
            ) {
                if (micros_runtime_receive(MICROS_ENDPOINT_ANY, &message)
                    != MICROS_SYSCALL_ABI_OK) {
                    __builtin_trap();
                }
            } else {
                __builtin_trap();
            }
#endif
        }
    }
}
