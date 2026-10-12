#include "tests/qemu/vm_server_internal.h"

#include "lib/runtime/raw_syscall.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/vm.h"

#ifdef MICROS_VM_SCRATCH_PREEMPTION_TEST

static bool vm_server_find_free_frame(uint64_t *physical_address)
{
    uint32_t range_index;

    if (physical_address == NULL) {
        return false;
    }
    for (
        range_index = 0;
        range_index
            < micros_vm_boot_info.header.managed_range_count;
        ++range_index
    ) {
        const struct micros_vm_managed_range *range =
            &micros_vm_boot_info.managed_ranges[range_index];
        uint64_t offset;

        for (offset = 0; offset < range->frame_count; ++offset) {
            uint64_t frame_index = range->frame_index + offset;

            if (
                frame_index
                    >= micros_vm_boot_info.header.managed_frame_count
            ) {
                return false;
            }
            if (
                micros_vm_boot_info.frame_states[frame_index]
                    == MICROS_VM_FRAME_FREE
            ) {
                *physical_address =
                    range->base + offset * MICROS_SV39_PAGE_SIZE;
                return true;
            }
        }
    }
    return false;
}

_Noreturn void vm_server_run_scratch_preemption(struct vm_server_context *context)
{
    struct micros_ipc_message request;
    volatile uint64_t *scratch =
        (volatile uint64_t *)(uintptr_t)
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS;
    uint64_t transaction;
    uint64_t physical_address = 0;
    int driver_index = vm_server_find_service(
        MICROS_VM_SCRATCH_PREEMPTION_DRIVER_SERVICE_ID
    );

    vm_server_clear_bytes(&request, sizeof(request));
    vm_server_clear_bytes(
        &context->scratch_begin_result,
        sizeof(context->scratch_begin_result)
    );
    if (
        driver_index < 0
        || micros_runtime_receive(
            MICROS_ENDPOINT_ANY,
            &request
        ) != MICROS_SYSCALL_ABI_OK
        || request.source
            != micros_bootstrap_service_config.services[driver_index]
                .endpoint
        || request.type != MICROS_VM_SCRATCH_PREEMPTION_START
        || request.reply_token == 0
        || vm_server_read_u32_le(&request.payload[0])
            != MICROS_VM_SCRATCH_PREEMPTION_VERSION
        || !vm_server_bytes_are_zero(
            &request.payload[16],
            sizeof(request.payload) - 16
        )
        || (
            transaction =
                vm_server_read_u64_le(&request.payload[8])
        ) == 0
        || !vm_server_find_free_frame(&physical_address)
        || micros_runtime_raw_syscall(
            MICROS_VM_LOAD_BEGIN,
            transaction,
            (uintptr_t)&context->scratch_begin_result,
            MICROS_VM_LOAD_BEGIN_RESULT_SIZE,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
        || context->scratch_begin_result.version
            != MICROS_VM_LOAD_BEGIN_RESULT_VERSION
        || context->scratch_begin_result.size
            != MICROS_VM_LOAD_BEGIN_RESULT_SIZE
        || context->scratch_begin_result.load_token == 0
        || context->scratch_begin_result.process_slot
            != MICROS_VM_SCRATCH_PREEMPTION_RESERVED_PROCESS_SLOT
        || context->scratch_begin_result.process_generation != 1
        || context->scratch_begin_result.reserved0 != 0
        || context->scratch_begin_result.reserved1 != 0
        || micros_runtime_raw_syscall(
            MICROS_VM_SCRATCH_MAP,
            context->scratch_begin_result.load_token,
            MICROS_USER_VIRTUAL_BASE,
            physical_address,
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE,
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
            0,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    *scratch = MICROS_VM_SCRATCH_PREEMPTION_SENTINEL;
    micros_vm_scratch_preemption_vm_hook(
        MICROS_VM_SCRATCH_PREEMPTION_VM_HOOK_MAGIC
    );
    if (
        *scratch != MICROS_VM_SCRATCH_PREEMPTION_SENTINEL
        || micros_runtime_raw_syscall(
            MICROS_VM_SCRATCH_COMMIT,
            context->scratch_begin_result.load_token,
            MICROS_USER_VIRTUAL_BASE,
            physical_address,
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
            0,
            0,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    micros_vm_scratch_preemption_stale_load(
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
    );
}

#endif
