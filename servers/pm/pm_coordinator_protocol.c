#include "servers/pm/pm_coordinator.h"
#include "servers/pm/pm_internal.h"

#include "micros/vfs.h"
#include "micros/vm_service.h"

static bool endpoint_word_is_valid(uint64_t word)
{
    return (
        word <= UINT32_MAX
        && pm_common_endpoint_is_concrete((micros_endpoint_t)word)
    );
}

static bool grant_pair_is_valid(uint64_t word, uint32_t minimum, uint32_t maximum)
{
    uint32_t size = (uint32_t)(word >> 32);

    return (uint32_t)word != 0 && size >= minimum && size <= maximum;
}

static bool vfs_command_is_defined(uint32_t command)
{
    return command >= MICROS_VFS_CONTROL_PREPARE_IMAGE
        && command <= MICROS_VFS_CONTROL_EXIT_PROCESS;
}

static bool vm_command_is_defined(uint32_t command)
{
    return command >= MICROS_VM_CONTROL_LOAD
        && command <= MICROS_VM_CONTROL_EXIT_RELEASE;
}

static bool vfs_arguments_are_valid(uint32_t command, const uint64_t arguments[5])
{
    if (arguments[0] == 0) {
        return false;
    }
    switch (command) {
    case MICROS_VFS_CONTROL_PREPARE_IMAGE:
        return (
            arguments[1] != 0
            && grant_pair_is_valid(arguments[3], MICROS_SPAWN_REQUEST_SIZE_MIN,
                                   MICROS_SPAWN_REQUEST_SIZE_MAX)
            && (
                (arguments[4] == 0 && endpoint_word_is_valid(arguments[2]))
                || (arguments[4] == 1 && arguments[2] == 0)
            )
        );
    case MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS:
        return arguments[1] != 0 && arguments[2] != 0
            && arguments[3] == 0 && arguments[4] == 0;
    case MICROS_VFS_CONTROL_COMMIT_HIDDEN:
    case MICROS_VFS_CONTROL_REVERT_HIDDEN:
        return arguments[1] != 0 && arguments[2] != 0
            && endpoint_word_is_valid(arguments[3]) && arguments[4] == 0;
    case MICROS_VFS_CONTROL_ABORT_DESCRIPTORS:
        return arguments[1] != 0 && arguments[2] != 0
            && arguments[3] == 0 && arguments[4] == 0;
    case MICROS_VFS_CONTROL_RELEASE_IMAGE:
        return arguments[1] != 0 && arguments[2] == 0
            && arguments[3] == 0 && arguments[4] == 0;
    case MICROS_VFS_CONTROL_EXIT_PROCESS:
        return endpoint_word_is_valid(arguments[1]) && arguments[2] != 0
            && arguments[3] == 0 && arguments[4] == 0;
    default:
        return false;
    }
}

static bool vm_arguments_are_valid(uint32_t command, const uint64_t arguments[5])
{
    if (arguments[0] == 0 || arguments[1] == 0) {
        return false;
    }
    switch (command) {
    case MICROS_VM_CONTROL_LOAD:
        return (
            grant_pair_is_valid(arguments[2], 1, MICROS_APPLICATION_EXECUTABLE_SIZE_MAX)
            && grant_pair_is_valid(arguments[3], MICROS_SPAWN_REQUEST_SIZE_MIN,
                                   MICROS_SPAWN_REQUEST_SIZE_MAX)
            && arguments[4] == 0
        );
    case MICROS_VM_CONTROL_ABORT_LOAD:
    case MICROS_VM_CONTROL_COMMIT_LOAD:
        return arguments[2] != 0 && arguments[3] == 0 && arguments[4] == 0;
    case MICROS_VM_CONTROL_EXIT_RELEASE:
        return arguments[2] != 0 && arguments[3] != 0 && arguments[4] == 0;
    default:
        return false;
    }
}

static void build_control_message(
    uint32_t type, uint32_t version, uint32_t command, const uint64_t arguments[5],
    struct micros_ipc_message *message
)
{
    struct micros_ipc_message built;
    size_t index;

    pm_common_zero_bytes(&built, sizeof(built));
    built.source = MICROS_ENDPOINT_NONE;
    built.type = type;
    pm_common_write_u64_le(&built.payload[0], version | (uint64_t)command << 32);
    for (index = 0; index < 5; ++index) {
        pm_common_write_u64_le(&built.payload[(index + 1) * 8], arguments[index]);
    }
    *message = built;
}

enum micros_pm_coordinator_protocol_status
micros_pm_coordinator_build_vfs_control(
    uint32_t command, const uint64_t arguments[5], struct micros_ipc_message *message
)
{
    if (
        arguments == NULL || message == NULL
        || !vfs_command_is_defined(command) || !vfs_arguments_are_valid(command, arguments)
    ) {
        return MICROS_PM_COORDINATOR_PROTOCOL_ERROR_ARGUMENT;
    }
    build_control_message(
        MICROS_VFS_MESSAGE_PM_CONTROL, MICROS_VFS_PROTOCOL_VERSION,
        command, arguments, message
    );
    return MICROS_PM_COORDINATOR_PROTOCOL_OK;
}

enum micros_pm_coordinator_protocol_status
micros_pm_coordinator_build_vm_control(
    uint32_t command, const uint64_t arguments[5], struct micros_ipc_message *message
)
{
    if (
        arguments == NULL || message == NULL
        || !vm_command_is_defined(command) || !vm_arguments_are_valid(command, arguments)
    ) {
        return MICROS_PM_COORDINATOR_PROTOCOL_ERROR_ARGUMENT;
    }
    build_control_message(
        MICROS_VM_MESSAGE_PM_CONTROL, MICROS_VM_CONTROL_PROTOCOL_VERSION,
        command, arguments, message
    );
    return MICROS_PM_COORDINATOR_PROTOCOL_OK;
}

static bool outputs_are_zero(const uint64_t output[4], size_t first)
{
    size_t index;

    for (index = first; index < 4; ++index) {
        if (output[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool decode_control_envelope(
    micros_endpoint_t peer, uint32_t type, uint32_t version, uint32_t command,
    int32_t minimum, const struct micros_ipc_message *message,
    struct micros_pm_coordinator_control_result *decoded
)
{
    uint64_t word = pm_common_read_u64_le(&message->payload[8]);
    int64_t result;
    size_t index;

    if (
        message->source != peer || message->type != type || message->reply_token != 0
        || pm_common_read_u64_le(&message->payload[0])
            != (version | (uint64_t)command << 32)
        || word > UINT32_MAX
    ) {
        return false;
    }
    result = (int64_t)word;
    if (result > INT32_MAX) {
        result -= INT64_C(1) << 32;
    }
    if (result > 0 || result < minimum) {
        return false;
    }
    pm_common_zero_bytes(decoded, sizeof(*decoded));
    decoded->result = (int32_t)result;
    for (index = 0; index < 4; ++index) {
        decoded->output[index] = pm_common_read_u64_le(&message->payload[(index + 2) * 8]);
    }
    return decoded->result == 0 || outputs_are_zero(decoded->output, 0);
}

static bool vfs_success_output_is_valid(
    uint32_t command, const struct micros_pm_coordinator_control_result *result
)
{
    if (result->result != 0) {
        return true;
    }
    if (command == MICROS_VFS_CONTROL_PREPARE_IMAGE) {
        return (
            result->output[0] != 0
            && grant_pair_is_valid(result->output[1], 1, MICROS_APPLICATION_EXECUTABLE_SIZE_MAX)
            && grant_pair_is_valid(result->output[2], MICROS_SPAWN_REQUEST_SIZE_MIN,
                                   MICROS_SPAWN_REQUEST_SIZE_MAX)
            && outputs_are_zero(result->output, 3)
        );
    }
    if (command == MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS) {
        return result->output[0] != 0 && outputs_are_zero(result->output, 1);
    }
    return outputs_are_zero(result->output, 0);
}

static bool vm_success_output_is_valid(
    uint32_t command, const struct micros_pm_coordinator_control_result *result
)
{
    if (result->result != 0) {
        return true;
    }
    if (command == MICROS_VM_CONTROL_LOAD) {
        return (
            result->output[0] != 0 && result->output[1] != 0
            && result->output[2] == MICROS_APPLICATION_ENTRY
            && result->output[3]
                >= MICROS_APPLICATION_STACK_TOP - MICROS_APPLICATION_STACK_OCCUPIED_MAX
            && result->output[3] < MICROS_APPLICATION_STACK_TOP
            && (result->output[3] & UINT64_C(15)) == 0
        );
    }
    return outputs_are_zero(result->output, 0);
}

enum micros_pm_coordinator_protocol_status
micros_pm_coordinator_decode_vfs_result(
    micros_endpoint_t peer, uint32_t command, const struct micros_ipc_message *message,
    struct micros_pm_coordinator_control_result *result
)
{
    struct micros_pm_coordinator_control_result decoded;

    if (
        message == NULL || result == NULL || !pm_common_endpoint_is_concrete(peer)
        || !vfs_command_is_defined(command)
    ) {
        return MICROS_PM_COORDINATOR_PROTOCOL_ERROR_ARGUMENT;
    }
    if (
        !decode_control_envelope(
            peer, MICROS_VFS_MESSAGE_PM_CONTROL_RESULT, MICROS_VFS_PROTOCOL_VERSION,
            command, MICROS_VFS_CONTROL_RESULT_EXECUTABLE, message, &decoded
        )
        || !vfs_success_output_is_valid(command, &decoded)
    ) {
        return MICROS_PM_COORDINATOR_PROTOCOL_ERROR_MESSAGE;
    }
    *result = decoded;
    return MICROS_PM_COORDINATOR_PROTOCOL_OK;
}

enum micros_pm_coordinator_protocol_status
micros_pm_coordinator_decode_vm_result(
    micros_endpoint_t peer, uint32_t command, const struct micros_ipc_message *message,
    struct micros_pm_coordinator_control_result *result
)
{
    struct micros_pm_coordinator_control_result decoded;

    if (
        message == NULL || result == NULL || !pm_common_endpoint_is_concrete(peer)
        || !vm_command_is_defined(command)
    ) {
        return MICROS_PM_COORDINATOR_PROTOCOL_ERROR_ARGUMENT;
    }
    if (
        !decode_control_envelope(
            peer, MICROS_VM_MESSAGE_PM_CONTROL_RESULT, MICROS_VM_CONTROL_PROTOCOL_VERSION,
            command, MICROS_VM_CONTROL_RESULT_TOO_BIG, message, &decoded
        )
        || !vm_success_output_is_valid(command, &decoded)
    ) {
        return MICROS_PM_COORDINATOR_PROTOCOL_ERROR_MESSAGE;
    }
    *result = decoded;
    return MICROS_PM_COORDINATOR_PROTOCOL_OK;
}
