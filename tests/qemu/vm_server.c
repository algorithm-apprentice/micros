#include "tests/qemu/vm_server_internal.h"

void micros_service_main(void)
{
    struct vm_server_context *context = vm_server_context_get();

    if (
        context->vm_data != UINT64_C(0x564d44415441564d)
        || !vm_server_validate_configuration()
        || micros_vm_state_initialize(
            &context->vm_state,
            &micros_vm_boot_info,
            MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID,
            micros_bootstrap_service_config.self_endpoint,
            micros_bootstrap_service_config.service_count
        ) != MICROS_VM_CORE_OK
    ) {
        __builtin_trap();
    }
#ifdef MICROS_VM_READY_EARLY
    if (!vm_server_send_ready()) {
        __builtin_trap();
    }
    __builtin_trap();
#endif
    if (!vm_server_complete_handoff()) {
        __builtin_trap();
    }
#ifdef MICROS_VM_SELF_FAULT_RUNNING
    micros_vm_trigger_self_fault();
#endif
    if (!vm_server_send_ready()) {
        __builtin_trap();
    }
#ifdef MICROS_VM_TTY_HANDOFF_TEST
    if (!vm_server_map_tty_uart(context)) {
        __builtin_trap();
    }
#elif defined(MICROS_VM_SCRATCH_PREEMPTION_TEST)
    vm_server_run_scratch_preemption(context);
#elif !defined(MICROS_PM_SERVICE_TEST)
    if (!vm_server_serve_probe(context)) {
        __builtin_trap();
    }
#endif
#ifdef MICROS_VM_SELF_FAULT_SEALED
    micros_vm_trigger_self_fault();
#endif
    for (;;) {
        struct micros_ipc_message message;

        vm_server_clear_bytes(&message, sizeof(message));
        if (
            micros_runtime_receive(MICROS_ENDPOINT_ANY, &message)
                != MICROS_SYSCALL_ABI_OK
        ) {
            __builtin_trap();
        }
#ifdef MICROS_PM_SERVICE_TEST
        if (!pm_test_serve_request(context, &message)) {
            __builtin_trap();
        }
#elif defined(MICROS_VM_TTY_HANDOFF_TEST)
        __builtin_trap();
#endif
    }
}
