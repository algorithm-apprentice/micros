#include "servers/pm/pm_service_internal.h"

#ifdef MICROS_VM_SCRATCH_PREEMPTION_TEST
_Noreturn void pm_service_run_scratch_preemption_test(
    const struct micros_ipc_message *request
)
{
    struct micros_pm_reservation_result reservation;
    struct micros_ipc_message reply;
    struct micros_ipc_message blocked;
    micros_endpoint_t driver_endpoint =
        micros_bootstrap_service_config.services[
            MICROS_VM_SCRATCH_PREEMPTION_DRIVER_SERVICE_ID - 1
        ].endpoint;

    pm_service_clear_bytes(&reservation, sizeof(reservation));
    pm_service_clear_bytes(&reply, sizeof(reply));
    pm_service_clear_bytes(&blocked, sizeof(blocked));
    if (
        request == NULL
        || request->source != driver_endpoint
        || request->type != MICROS_VM_SCRATCH_PREEMPTION_RESERVE
        || request->reply_token == 0
        || pm_service_read_u32_le(&request->payload[0])
            != MICROS_VM_SCRATCH_PREEMPTION_VERSION
        || !pm_service_bytes_are_zero(
            &request->payload[4],
            sizeof(request->payload) - 4
        )
        || micros_pm_control_reserve(&reservation)
            != MICROS_SYSCALL_ABI_OK
        || reservation.version != MICROS_PM_RESERVATION_VERSION
        || reservation.size != MICROS_PM_RESERVATION_SIZE
        || reservation.transaction == 0
        || reservation.reserved[0] != 0
        || reservation.reserved[1] != 0
    ) {
        __builtin_trap();
    }
    reply.type = MICROS_VM_SCRATCH_PREEMPTION_RESERVATION;
    pm_service_write_u32_le(
        &reply.payload[0],
        MICROS_VM_SCRATCH_PREEMPTION_VERSION
    );
    pm_service_write_u64_le(
        &reply.payload[8],
        reservation.transaction
    );
    if (
        micros_runtime_reply(request->reply_token, &reply)
            != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    micros_vm_scratch_preemption_peer_hold(
        MICROS_VM_SCRATCH_PREEMPTION_PEER_HOLD_MAGIC
    );
    micros_vm_scratch_preemption_peer_run(
        MICROS_VM_SCRATCH_PREEMPTION_PEER_RUN_MAGIC
    );
    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &blocked)
            != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    __builtin_trap();
}

#endif
