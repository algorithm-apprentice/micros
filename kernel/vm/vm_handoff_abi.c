#include "kernel/vm/vm_handoff_internal.h"

#include <stddef.h>
#include <stdint.h>

static bool fits_u32(uint64_t value)
{
    return value <= UINT32_MAX;
}

static bool vm_handoff_command_is_known(uint32_t command)
{
    return (
        command == MICROS_VM_HANDOFF_READY
        || command == MICROS_VM_HANDOFF_MAP_TTY_UART
        || command == MICROS_VM_LOAD_BEGIN
        || command == MICROS_VM_SCRATCH_MAP
        || command == MICROS_VM_SCRATCH_COMMIT
        || command == MICROS_VM_LOAD_FREEZE
        || command == MICROS_VM_LOAD_ABORT
        || command == MICROS_VM_LOAD_COMMIT
        || command == MICROS_VM_EXIT_RELEASE
    );
}

enum micros_syscall_abi_result micros_vm_handoff_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_vm_handoff_request *request
)
{
    struct micros_vm_handoff_request candidate;
    uint32_t packed_counts;
    uint32_t command;

    if (
        arguments == NULL
        || request == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            arguments,
            _Alignof(struct micros_syscall_arguments)
        )
        || !micros_vm_handoff_pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_handoff_request)
        )
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    if (
        micros_vm_handoff_decode_command(arguments, &command)
            != MICROS_SYSCALL_ABI_OK
        || !fits_u32(arguments->a1)
        || !fits_u32(arguments->a2)
        || !fits_u32(arguments->a3)
        || !fits_u32(arguments->a4)
        || !fits_u32(arguments->a5)
        || command != MICROS_VM_HANDOFF_READY
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    packed_counts = (uint32_t)arguments->a2;
    micros_vm_handoff_clear_bytes(&candidate, sizeof(candidate));
    candidate.command = (uint32_t)arguments->a0;
    candidate.version = (uint32_t)arguments->a1;
    candidate.managed_range_count = (uint16_t)packed_counts;
    candidate.address_space_count = (uint16_t)(packed_counts >> 16);
    candidate.managed_frame_count = (uint32_t)arguments->a3;
    candidate.free_frame_count = (uint32_t)arguments->a4;
    candidate.mapping_count = (uint32_t)arguments->a5;
    candidate.digest = arguments->a6;
    micros_vm_handoff_copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result micros_vm_handoff_decode_command(
    const struct micros_syscall_arguments *arguments,
    uint32_t *command
)
{
    uint32_t candidate;

    if (
        arguments == NULL
        || command == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            arguments,
            _Alignof(struct micros_syscall_arguments)
        )
        || !micros_vm_handoff_pointer_is_aligned(command, _Alignof(uint32_t))
        || arguments->a7 != MICROS_SYSCALL_ABI_VM_HANDOFF
        || !fits_u32(arguments->a0)
        || !vm_handoff_command_is_known((uint32_t)arguments->a0)
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    candidate = (uint32_t)arguments->a0;
    micros_vm_handoff_copy_bytes(command, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result
micros_vm_handoff_decode_tty_mapping(
    const struct micros_syscall_arguments *arguments,
    struct micros_vm_tty_mapping_request *request
)
{
    struct micros_vm_tty_mapping_request candidate;
    uint32_t command;
    uint32_t irq_source;
    uint32_t mapped_length;

    if (
        arguments == NULL
        || request == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            arguments,
            _Alignof(struct micros_syscall_arguments)
        )
        || !micros_vm_handoff_pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_tty_mapping_request)
        )
        || micros_vm_handoff_decode_command(
            arguments,
            &command
        ) != MICROS_SYSCALL_ABI_OK
        || command != MICROS_VM_HANDOFF_MAP_TTY_UART
        || !fits_u32(arguments->a1)
        || !fits_u32(arguments->a2)
        || !fits_u32(arguments->a3)
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    irq_source = (uint32_t)(arguments->a6 >> 32);
    mapped_length = (uint32_t)arguments->a6;
    if (irq_source == 0 || mapped_length == 0) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }

    micros_vm_handoff_clear_bytes(&candidate, sizeof(candidate));
    candidate.command = command;
    candidate.version = (uint32_t)arguments->a1;
    candidate.service_id = (uint32_t)arguments->a2;
    candidate.endpoint = (uint32_t)arguments->a3;
    candidate.virtual_base = arguments->a4;
    candidate.physical_base = arguments->a5;
    candidate.irq_source = irq_source;
    candidate.mapped_length = mapped_length;
    micros_vm_handoff_copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result micros_vm_load_begin_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_vm_load_begin_request *request
)
{
    struct micros_vm_load_begin_request candidate;
    uint32_t command;

    if (
        arguments == NULL
        || request == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            arguments,
            _Alignof(struct micros_syscall_arguments)
        )
        || !micros_vm_handoff_pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_load_begin_request)
        )
        || micros_vm_handoff_decode_command(arguments, &command)
            != MICROS_SYSCALL_ABI_OK
        || command != MICROS_VM_LOAD_BEGIN
        || arguments->a1 == 0
        || arguments->a2 == 0
        || arguments->a2 % 8 != 0
        || arguments->a3 != MICROS_VM_LOAD_BEGIN_RESULT_SIZE
        || arguments->a4 != 0
        || arguments->a5 != 0
        || arguments->a6 != 0
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    micros_vm_handoff_clear_bytes(&candidate, sizeof(candidate));
    candidate.process_reservation_token = arguments->a1;
    candidate.output_address = arguments->a2;
    micros_vm_handoff_copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result micros_vm_scratch_map_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_vm_scratch_map_request *request
)
{
    struct micros_vm_scratch_map_request candidate;
    uint32_t command;

    if (
        arguments == NULL
        || request == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            arguments,
            _Alignof(struct micros_syscall_arguments)
        )
        || !micros_vm_handoff_pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_scratch_map_request)
        )
        || micros_vm_handoff_decode_command(arguments, &command)
            != MICROS_SYSCALL_ABI_OK
        || command != MICROS_VM_SCRATCH_MAP
        || arguments->a1 == 0
        || !micros_vm_handoff_user_page_is_valid(arguments->a2)
        || arguments->a2 == MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
        || !micros_vm_handoff_address_is_page_aligned(arguments->a3)
        || !fits_u32(arguments->a4)
        || !micros_vm_handoff_user_permissions_are_valid((uint32_t)arguments->a4)
        || arguments->a5 != MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
        || arguments->a6 != 0
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    micros_vm_handoff_clear_bytes(&candidate, sizeof(candidate));
    candidate.load_token = arguments->a1;
    candidate.virtual_address = arguments->a2;
    candidate.physical_address = arguments->a3;
    candidate.permissions = (uint32_t)arguments->a4;
    micros_vm_handoff_copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result micros_vm_scratch_commit_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_vm_scratch_commit_request *request
)
{
    struct micros_vm_scratch_commit_request candidate;
    uint32_t command;

    if (
        arguments == NULL
        || request == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            arguments,
            _Alignof(struct micros_syscall_arguments)
        )
        || !micros_vm_handoff_pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_scratch_commit_request)
        )
        || micros_vm_handoff_decode_command(arguments, &command)
            != MICROS_SYSCALL_ABI_OK
        || command != MICROS_VM_SCRATCH_COMMIT
        || arguments->a1 == 0
        || !micros_vm_handoff_user_page_is_valid(arguments->a2)
        || arguments->a2 == MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
        || !micros_vm_handoff_address_is_page_aligned(arguments->a3)
        || arguments->a4 != MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
        || arguments->a5 != 0
        || arguments->a6 != 0
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    micros_vm_handoff_clear_bytes(&candidate, sizeof(candidate));
    candidate.load_token = arguments->a1;
    candidate.virtual_address = arguments->a2;
    candidate.physical_address = arguments->a3;
    micros_vm_handoff_copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result micros_vm_load_freeze_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_vm_load_freeze_request *request
)
{
    struct micros_vm_load_freeze_request candidate;
    uint32_t command;

    if (
        arguments == NULL
        || request == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            arguments,
            _Alignof(struct micros_syscall_arguments)
        )
        || !micros_vm_handoff_pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_load_freeze_request)
        )
        || micros_vm_handoff_decode_command(arguments, &command)
            != MICROS_SYSCALL_ABI_OK
        || command != MICROS_VM_LOAD_FREEZE
        || arguments->a1 == 0
        || arguments->a1 % 8 != 0
        || arguments->a2 != MICROS_VM_LOAD_FREEZE_PLAN_SIZE
        || arguments->a3 == 0
        || arguments->a3 % 8 != 0
        || arguments->a4 != MICROS_VM_LOAD_FREEZE_RESULT_SIZE
        || arguments->a5 != 0
        || arguments->a6 != 0
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    micros_vm_handoff_clear_bytes(&candidate, sizeof(candidate));
    candidate.plan_address = arguments->a1;
    candidate.output_address = arguments->a3;
    micros_vm_handoff_copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result micros_vm_load_abort_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_vm_load_abort_request *request
)
{
    struct micros_vm_load_abort_request candidate;
    uint32_t command;

    if (
        arguments == NULL
        || request == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            arguments,
            _Alignof(struct micros_syscall_arguments)
        )
        || !micros_vm_handoff_pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_load_abort_request)
        )
        || micros_vm_handoff_decode_command(arguments, &command)
            != MICROS_SYSCALL_ABI_OK
        || command != MICROS_VM_LOAD_ABORT
        || arguments->a1 == 0
        || arguments->a3 != 0
        || arguments->a4 != 0
        || arguments->a5 != 0
        || arguments->a6 != 0
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    micros_vm_handoff_clear_bytes(&candidate, sizeof(candidate));
    candidate.load_token = arguments->a1;
    candidate.mapping_generation = arguments->a2;
    micros_vm_handoff_copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result micros_vm_load_commit_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_vm_load_commit_request *request
)
{
    struct micros_vm_load_commit_request candidate;
    uint32_t command;

    if (
        arguments == NULL
        || request == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            arguments,
            _Alignof(struct micros_syscall_arguments)
        )
        || !micros_vm_handoff_pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_load_commit_request)
        )
        || micros_vm_handoff_decode_command(arguments, &command)
            != MICROS_SYSCALL_ABI_OK
        || command != MICROS_VM_LOAD_COMMIT
        || arguments->a1 == 0
        || arguments->a2 == 0
        || arguments->a3 != 0
        || arguments->a4 != 0
        || arguments->a5 != 0
        || arguments->a6 != 0
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    micros_vm_handoff_clear_bytes(&candidate, sizeof(candidate));
    candidate.load_token = arguments->a1;
    candidate.mapping_generation = arguments->a2;
    micros_vm_handoff_copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result micros_vm_exit_release_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_vm_exit_release_request *request
)
{
    struct micros_vm_exit_release_request candidate;
    uint32_t command;

    if (
        arguments == NULL
        || request == NULL
        || !micros_vm_handoff_pointer_is_aligned(
            arguments,
            _Alignof(struct micros_syscall_arguments)
        )
        || !micros_vm_handoff_pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_exit_release_request)
        )
        || micros_vm_handoff_decode_command(arguments, &command)
            != MICROS_SYSCALL_ABI_OK
        || command != MICROS_VM_EXIT_RELEASE
        || arguments->a1 == 0
        || arguments->a2 == 0
        || arguments->a3 == 0
        || arguments->a4 != 0
        || arguments->a5 != 0
        || arguments->a6 != 0
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    micros_vm_handoff_clear_bytes(&candidate, sizeof(candidate));
    candidate.exit_token = arguments->a1;
    candidate.load_token = arguments->a2;
    candidate.mapping_generation = arguments->a3;
    micros_vm_handoff_copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}
