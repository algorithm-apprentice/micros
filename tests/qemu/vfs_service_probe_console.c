#include "tests/qemu/vfs_service_probe_internal.h"

#include "tests/qemu/vfs_service_protocol.h"

bool probe_test_console(struct vfs_service_probe_context *context) {
    static const uint8_t input_trigger[] = MICROS_VFS_TEST_INPUT_TRIGGER;
    static const uint8_t expected_input[] = MICROS_VFS_TEST_EXPECTED_INPUT;
    static const uint8_t pass_marker[] = MICROS_VFS_TEST_PASS;
    struct probe_vfs_result result;

    if (!probe_write(context, 1, input_trigger, sizeof(input_trigger) - 1)) {
        return false;
    }
    probe_clear_bytes(context->buffer, sizeof(context->buffer));
    if (!probe_transfer(context, MICROS_VFS_MESSAGE_READ, 0, sizeof(context->buffer), &result) ||
        result.result != MICROS_VFS_RESULT_OK ||
        result.transferred_count != sizeof(expected_input) - 1 || result.position != 0 ||
        !probe_bytes_equal(context->buffer, expected_input, sizeof(expected_input) - 1)) {
        return false;
    }
    return probe_write(context, 1, pass_marker, sizeof(pass_marker) - 1);
}

bool probe_request_drain(struct vfs_service_probe_context *context) {
    struct micros_ipc_message message;
    struct probe_vfs_result result;

    probe_clear_bytes(&message, sizeof(message));
    message.type = MICROS_VFS_TEST_MESSAGE_DRAIN;
    probe_write_u32_le(&message.payload[0], MICROS_VFS_PROTOCOL_VERSION);
    return (probe_call_vfs(context, &message, MICROS_VFS_TEST_MESSAGE_DRAIN, &result) &&
            result.result == MICROS_VFS_RESULT_OK && result.descriptor == 0 && result.mode == 0 &&
            result.transferred_count == 0 && result.position == 0);
}
