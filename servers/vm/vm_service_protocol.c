#include "servers/vm/vm_service_internal.h"

void vm_service_clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static uint64_t service_read_word(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static void service_write_word(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static bool service_request_shape_is_valid(const struct vm_service_request *request)
{
    const uint64_t *arguments = request->arguments;

    if (arguments[0] == 0 || arguments[1] == 0 || arguments[4] != 0) {
        return false;
    }
    switch (request->command) {
    case MICROS_VM_CONTROL_LOAD:
        return (uint32_t)arguments[2] != 0 && (arguments[2] >> 32) != 0
            && (uint32_t)arguments[3] != 0
            && (arguments[3] >> 32) >= MICROS_SPAWN_REQUEST_SIZE_MIN;
    case MICROS_VM_CONTROL_ABORT_LOAD:
    case MICROS_VM_CONTROL_COMMIT_LOAD:
        return arguments[2] != 0 && arguments[3] == 0;
    case MICROS_VM_CONTROL_EXIT_RELEASE:
        return arguments[2] != 0 && arguments[3] != 0;
    default:
        return false;
    }
}

enum micros_vm_control_result vm_service_decode_request(
    const struct micros_vm_service *service, const struct micros_ipc_message *message,
    struct vm_service_request *request
)
{
    uint64_t header = service_read_word(&message->payload[0]);
    size_t index;

    request->command = (uint32_t)(header >> 32);
    if (message->source != service->bindings.pm) {
        return MICROS_VM_CONTROL_RESULT_CALLER;
    }
    if (
        message->type != MICROS_VM_MESSAGE_PM_CONTROL
        || (uint32_t)header != MICROS_VM_CONTROL_PROTOCOL_VERSION
    ) {
        return MICROS_VM_CONTROL_RESULT_MALFORMED;
    }
    for (index = 0; index < 5; ++index) {
        request->arguments[index] = service_read_word(&message->payload[(index + 1) * 8]);
    }
    return service_request_shape_is_valid(request)
        ? MICROS_VM_CONTROL_RESULT_OK : MICROS_VM_CONTROL_RESULT_MALFORMED;
}

void vm_service_build_reply(
    uint32_t command, enum micros_vm_control_result result, const uint64_t outputs[4],
    struct micros_ipc_message *reply
)
{
    struct micros_ipc_message candidate;
    size_t index;

    vm_service_clear_bytes(&candidate, sizeof(candidate));
    candidate.source = MICROS_ENDPOINT_NONE;
    candidate.type = MICROS_VM_MESSAGE_PM_CONTROL_RESULT;
    service_write_word(&candidate.payload[0],
                       MICROS_VM_CONTROL_PROTOCOL_VERSION | (uint64_t)command << 32);
    service_write_word(&candidate.payload[8], (uint32_t)result);
    if (result == MICROS_VM_CONTROL_RESULT_OK) {
        for (index = 0; index < 4; ++index) {
            service_write_word(&candidate.payload[16 + index * 8], outputs[index]);
        }
    }
    *reply = candidate;
}
