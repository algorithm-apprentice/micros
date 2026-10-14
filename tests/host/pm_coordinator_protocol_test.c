#include "tests/host/pm_coordinator_test_internal.h"

struct coordinator_control_case {
    uint32_t command;
    uint64_t arguments[5];
};

typedef enum micros_pm_coordinator_protocol_status (*control_builder)(
    uint32_t, const uint64_t[5], struct micros_ipc_message *
);
typedef enum micros_pm_coordinator_protocol_status (*control_decoder)(
    micros_endpoint_t, uint32_t, const struct micros_ipc_message *,
    struct micros_pm_coordinator_control_result *
);

static void coordinator_test_write_word(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static uint64_t coordinator_test_read_word(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static struct micros_ipc_message control_reply(
    micros_endpoint_t source, uint32_t type, uint32_t command, int32_t result
)
{
    struct micros_ipc_message message;

    memset(&message, 0, sizeof(message));
    message.source = source;
    message.type = type;
    coordinator_test_write_word(
        &message.payload[0], UINT64_C(1) | (uint64_t)command << 32
    );
    coordinator_test_write_word(&message.payload[8], (uint32_t)result);
    return message;
}

static bool check_control_request(
    control_builder build, uint32_t type, const struct coordinator_control_case *test
)
{
    struct micros_ipc_message message;
    struct micros_ipc_message sentinel;
    uint64_t zero[5] = {0};
    size_t index;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    message = sentinel;
    COORDINATOR_EXPECT(
        build(test->command, test->arguments, &message)
            == MICROS_PM_COORDINATOR_PROTOCOL_OK
        && message.source == MICROS_ENDPOINT_NONE
        && message.reply_token == 0
        && message.type == type
        && coordinator_test_read_word(&message.payload[0])
            == (UINT64_C(1) | (uint64_t)test->command << 32)
    );
    for (index = 0; index < 5; ++index) {
        COORDINATOR_EXPECT(
            coordinator_test_read_word(&message.payload[(index + 1) * 8])
                == test->arguments[index]
        );
    }
    message = sentinel;
    COORDINATOR_EXPECT(
        build(test->command, zero, &message)
            == MICROS_PM_COORDINATOR_PROTOCOL_ERROR_ARGUMENT
        && memcmp(&message, &sentinel, sizeof(message)) == 0
        && build(UINT32_MAX, test->arguments, &message)
            == MICROS_PM_COORDINATOR_PROTOCOL_ERROR_ARGUMENT
        && memcmp(&message, &sentinel, sizeof(message)) == 0
        && build(test->command, NULL, &message)
            == MICROS_PM_COORDINATOR_PROTOCOL_ERROR_ARGUMENT
        && build(test->command, test->arguments, NULL)
            == MICROS_PM_COORDINATOR_PROTOCOL_ERROR_ARGUMENT
    );
    return true;
}

bool test_coordinator_vfs_control_contract(void)
{
    const uint64_t request = UINT64_C(256) << 32 | UINT64_C(0x100);
    const uint64_t executable = UINT64_C(4096) << 32 | UINT64_C(0x101);
    const micros_endpoint_t child =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) | UINT32_C(6);
    const micros_endpoint_t peer =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) | UINT32_C(5);
    const struct coordinator_control_case cases[] = {
        {MICROS_VFS_CONTROL_PREPARE_IMAGE, {11, 2, child, request, 0}},
        {MICROS_VFS_CONTROL_PREPARE_IMAGE, {11, 1, 0, request, 1}},
        {MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS, {11, 77, 2, 0, 0}},
        {MICROS_VFS_CONTROL_COMMIT_HIDDEN, {11, 77, 88, child, 0}},
        {MICROS_VFS_CONTROL_REVERT_HIDDEN, {11, 77, 88, child, 0}},
        {MICROS_VFS_CONTROL_ABORT_DESCRIPTORS, {11, 77, 88, 0, 0}},
        {MICROS_VFS_CONTROL_RELEASE_IMAGE, {11, 77, 0, 0, 0}},
        {MICROS_VFS_CONTROL_EXIT_PROCESS, {99, child, 2, 0, 0}},
    };
    struct micros_pm_coordinator_control_result result;
    struct micros_ipc_message message;
    size_t index;
    int32_t failure;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        COORDINATOR_EXPECT(check_control_request(
            micros_pm_coordinator_build_vfs_control, MICROS_VFS_MESSAGE_PM_CONTROL,
            &cases[index]
        ));
        message = control_reply(
            peer, MICROS_VFS_MESSAGE_PM_CONTROL_RESULT, cases[index].command, 0
        );
        if (cases[index].command == MICROS_VFS_CONTROL_PREPARE_IMAGE) {
            coordinator_test_write_word(&message.payload[16], 77);
            coordinator_test_write_word(&message.payload[24], executable);
            coordinator_test_write_word(&message.payload[32], request);
        } else if (cases[index].command == MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS) {
            coordinator_test_write_word(&message.payload[16], 88);
        }
        COORDINATOR_EXPECT(
            micros_pm_coordinator_decode_vfs_result(
                peer, cases[index].command, &message, &result
            ) == MICROS_PM_COORDINATOR_PROTOCOL_OK
            && result.result == 0 && result.reserved == 0
            && result.output[0] == (
                cases[index].command == MICROS_VFS_CONTROL_PREPARE_IMAGE ? 77
                    : cases[index].command == MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS ? 88 : 0
            )
            && result.output[1] == (
                cases[index].command == MICROS_VFS_CONTROL_PREPARE_IMAGE ? executable : 0
            )
            && result.output[2] == (
                cases[index].command == MICROS_VFS_CONTROL_PREPARE_IMAGE ? request : 0
            )
            && result.output[3] == 0
        );
    }
    for (failure = MICROS_VFS_CONTROL_RESULT_EXECUTABLE; failure < 0; ++failure) {
        message = control_reply(
            peer, MICROS_VFS_MESSAGE_PM_CONTROL_RESULT,
            MICROS_VFS_CONTROL_PREPARE_IMAGE, failure
        );
        COORDINATOR_EXPECT(
            micros_pm_coordinator_decode_vfs_result(
                peer, MICROS_VFS_CONTROL_PREPARE_IMAGE, &message, &result
            ) == MICROS_PM_COORDINATOR_PROTOCOL_OK
            && result.result == failure
            && result.output[0] == 0 && result.output[1] == 0
            && result.output[2] == 0 && result.output[3] == 0
        );
    }
    return true;
}

bool test_coordinator_vm_control_contract(void)
{
    const uint64_t request = UINT64_C(256) << 32 | UINT64_C(0x100);
    const uint64_t executable = UINT64_C(4096) << 32 | UINT64_C(0x101);
    const micros_endpoint_t peer =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) | UINT32_C(1);
    const struct coordinator_control_case cases[] = {
        {MICROS_VM_CONTROL_LOAD, {21, 2, executable, request, 0}},
        {MICROS_VM_CONTROL_ABORT_LOAD, {31, 41, 2, 0, 0}},
        {MICROS_VM_CONTROL_COMMIT_LOAD, {31, 41, 2, 0, 0}},
        {MICROS_VM_CONTROL_EXIT_RELEASE, {51, 31, 41, 2, 0}},
    };
    struct micros_pm_coordinator_control_result result;
    struct micros_ipc_message message;
    size_t index;
    int32_t failure;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        COORDINATOR_EXPECT(check_control_request(
            micros_pm_coordinator_build_vm_control, MICROS_VM_MESSAGE_PM_CONTROL,
            &cases[index]
        ));
        message = control_reply(
            peer, MICROS_VM_MESSAGE_PM_CONTROL_RESULT, cases[index].command, 0
        );
        if (cases[index].command == MICROS_VM_CONTROL_LOAD) {
            coordinator_test_write_word(&message.payload[16], 31);
            coordinator_test_write_word(&message.payload[24], 41);
            coordinator_test_write_word(&message.payload[32], MICROS_APPLICATION_ENTRY);
            coordinator_test_write_word(&message.payload[40], MICROS_APPLICATION_STACK_TOP - 128);
        }
        COORDINATOR_EXPECT(
            micros_pm_coordinator_decode_vm_result(
                peer, cases[index].command, &message, &result
            ) == MICROS_PM_COORDINATOR_PROTOCOL_OK
            && result.result == 0 && result.reserved == 0
            && result.output[0] == (
                cases[index].command == MICROS_VM_CONTROL_LOAD ? 31 : 0
            )
            && result.output[1] == (
                cases[index].command == MICROS_VM_CONTROL_LOAD ? 41 : 0
            )
            && result.output[2] == (
                cases[index].command == MICROS_VM_CONTROL_LOAD ? MICROS_APPLICATION_ENTRY : 0
            )
            && result.output[3] == (
                cases[index].command == MICROS_VM_CONTROL_LOAD
                    ? MICROS_APPLICATION_STACK_TOP - 128 : 0
            )
        );
    }
    for (failure = MICROS_VM_CONTROL_RESULT_TOO_BIG; failure < 0; ++failure) {
        message = control_reply(
            peer, MICROS_VM_MESSAGE_PM_CONTROL_RESULT, MICROS_VM_CONTROL_LOAD, failure
        );
        COORDINATOR_EXPECT(
            micros_pm_coordinator_decode_vm_result(
                peer, MICROS_VM_CONTROL_LOAD, &message, &result
            ) == MICROS_PM_COORDINATOR_PROTOCOL_OK
            && result.result == failure
            && result.output[0] == 0 && result.output[1] == 0
            && result.output[2] == 0 && result.output[3] == 0
        );
    }
    return true;
}

static bool rejects_control_reply(
    control_decoder decode, micros_endpoint_t peer, uint32_t command,
    const struct micros_ipc_message *message
)
{
    struct micros_pm_coordinator_control_result result;
    struct micros_pm_coordinator_control_result sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    result = sentinel;
    COORDINATOR_EXPECT(
        decode(peer, command, message, &result)
            == MICROS_PM_COORDINATOR_PROTOCOL_ERROR_MESSAGE
        && memcmp(&result, &sentinel, sizeof(result)) == 0
    );
    return true;
}

static bool check_control_envelope_rejections(
    control_decoder decode, micros_endpoint_t peer, uint32_t type, uint32_t command
)
{
    struct micros_ipc_message baseline = control_reply(peer, type, command, -4);
    struct micros_ipc_message message;

    message = baseline;
    message.source = MICROS_ENDPOINT_NONE;
    COORDINATOR_EXPECT(rejects_control_reply(decode, peer, command, &message));
    message = baseline;
    message.source ^= UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS;
    COORDINATOR_EXPECT(rejects_control_reply(decode, peer, command, &message));
    message = baseline;
    message.type ^= UINT32_C(1);
    COORDINATOR_EXPECT(rejects_control_reply(decode, peer, command, &message));
    message = baseline;
    message.reply_token = 9;
    COORDINATOR_EXPECT(rejects_control_reply(decode, peer, command, &message));
    message = baseline;
    coordinator_test_write_word(&message.payload[0], UINT64_C(2) | (uint64_t)command << 32);
    COORDINATOR_EXPECT(rejects_control_reply(decode, peer, command, &message));
    message = baseline;
    coordinator_test_write_word(&message.payload[0], UINT64_C(1) | (uint64_t)(command + 1) << 32);
    COORDINATOR_EXPECT(rejects_control_reply(decode, peer, command, &message));
    message = baseline;
    coordinator_test_write_word(&message.payload[8], UINT64_C(1) << 32);
    COORDINATOR_EXPECT(rejects_control_reply(decode, peer, command, &message));
    message = baseline;
    coordinator_test_write_word(&message.payload[16], 1);
    COORDINATOR_EXPECT(rejects_control_reply(decode, peer, command, &message));
    message = baseline;
    coordinator_test_write_word(&message.payload[8], 1);
    COORDINATOR_EXPECT(rejects_control_reply(decode, peer, command, &message));
    message = baseline;
    coordinator_test_write_word(&message.payload[8], UINT32_C(0xfffffff0));
    COORDINATOR_EXPECT(rejects_control_reply(decode, peer, command, &message));
    return true;
}

static bool check_vfs_output_rejections(micros_endpoint_t peer)
{
    struct micros_ipc_message baseline = control_reply(
        peer, MICROS_VFS_MESSAGE_PM_CONTROL_RESULT, MICROS_VFS_CONTROL_PREPARE_IMAGE, 0
    );
    struct micros_ipc_message message;
    const struct {
        size_t offset;
        uint64_t word;
    } invalid[] = {
        {16, 0},
        {24, 0},
        {24, UINT64_C(131073) << 32 | 1},
        {24, UINT64_C(4096) << 32},
        {32, 0},
        {32, UINT64_C(255) << 32 | 1},
        {32, UINT64_C(4097) << 32 | 1},
        {32, UINT64_C(256) << 32},
        {40, 1},
    };
    size_t index;

    coordinator_test_write_word(&baseline.payload[16], 77);
    coordinator_test_write_word(&baseline.payload[24], UINT64_C(4096) << 32 | 1);
    coordinator_test_write_word(&baseline.payload[32], UINT64_C(256) << 32 | 2);
    for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        message = baseline;
        coordinator_test_write_word(&message.payload[invalid[index].offset], invalid[index].word);
        COORDINATOR_EXPECT(rejects_control_reply(
            micros_pm_coordinator_decode_vfs_result, peer,
            MICROS_VFS_CONTROL_PREPARE_IMAGE, &message
        ));
    }
    message = control_reply(
        peer, MICROS_VFS_MESSAGE_PM_CONTROL_RESULT, MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS, 0
    );
    coordinator_test_write_word(&message.payload[16], 88);
    coordinator_test_write_word(&message.payload[24], 1);
    COORDINATOR_EXPECT(rejects_control_reply(
        micros_pm_coordinator_decode_vfs_result, peer,
        MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS, &message
    ));
    return true;
}

static bool check_vm_output_rejections(micros_endpoint_t peer)
{
    struct micros_ipc_message baseline = control_reply(
        peer, MICROS_VM_MESSAGE_PM_CONTROL_RESULT, MICROS_VM_CONTROL_LOAD, 0
    );
    struct micros_ipc_message message;
    const uint64_t invalid_stack[] = {
        MICROS_APPLICATION_STACK_TOP, MICROS_APPLICATION_STACK_TOP - 1,
        MICROS_APPLICATION_STACK_TOP - MICROS_APPLICATION_STACK_OCCUPIED_MAX - 16,
    };
    size_t index;

    coordinator_test_write_word(&baseline.payload[16], 31);
    coordinator_test_write_word(&baseline.payload[24], 41);
    coordinator_test_write_word(&baseline.payload[32], MICROS_APPLICATION_ENTRY);
    coordinator_test_write_word(&baseline.payload[40], MICROS_APPLICATION_STACK_TOP - 128);
    for (index = 0; index < sizeof(invalid_stack) / sizeof(invalid_stack[0]); ++index) {
        message = baseline;
        coordinator_test_write_word(&message.payload[40], invalid_stack[index]);
        COORDINATOR_EXPECT(rejects_control_reply(
            micros_pm_coordinator_decode_vm_result, peer, MICROS_VM_CONTROL_LOAD, &message
        ));
    }
    for (index = 2; index < 5; ++index) {
        message = baseline;
        coordinator_test_write_word(&message.payload[index * 8], 0);
        COORDINATOR_EXPECT(rejects_control_reply(
            micros_pm_coordinator_decode_vm_result, peer, MICROS_VM_CONTROL_LOAD, &message
        ));
    }
    return true;
}

bool test_coordinator_control_rejections(void)
{
    const micros_endpoint_t vfs =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) | UINT32_C(5);
    const micros_endpoint_t vm =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) | UINT32_C(1);
    struct micros_ipc_message message;

    COORDINATOR_EXPECT(
        check_control_envelope_rejections(
            micros_pm_coordinator_decode_vfs_result, vfs,
            MICROS_VFS_MESSAGE_PM_CONTROL_RESULT, MICROS_VFS_CONTROL_PREPARE_IMAGE
        )
        && check_control_envelope_rejections(
            micros_pm_coordinator_decode_vm_result, vm,
            MICROS_VM_MESSAGE_PM_CONTROL_RESULT, MICROS_VM_CONTROL_LOAD
        )
        && check_vfs_output_rejections(vfs)
        && check_vm_output_rejections(vm)
    );
    message = control_reply(vfs, MICROS_VFS_MESSAGE_PM_CONTROL_RESULT,
                            MICROS_VFS_CONTROL_PREPARE_IMAGE, 0);
    COORDINATOR_EXPECT(rejects_control_reply(
        micros_pm_coordinator_decode_vfs_result, vfs,
        MICROS_VFS_CONTROL_PREPARE_IMAGE, &message
    ));
    message = control_reply(vm, MICROS_VM_MESSAGE_PM_CONTROL_RESULT,
                            MICROS_VM_CONTROL_LOAD, 0);
    COORDINATOR_EXPECT(rejects_control_reply(
        micros_pm_coordinator_decode_vm_result, vm, MICROS_VM_CONTROL_LOAD, &message
    ));
    return true;
}
