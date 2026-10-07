#include "kernel/ipc_syscall_test.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/ipc_runtime_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_abi.h"
#include "micros/ipc_core.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/scheduler_core.h"
#include "micros/sv39.h"
#include "micros/timer.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

#define TEST_SSTATUS_UBE (UINT64_C(1) << 6)
#define TEST_SSTATUS_VS (UINT64_C(3) << 9)
#define TEST_SSTATUS_FS (UINT64_C(3) << 13)
#define TEST_SSTATUS_XS (UINT64_C(3) << 15)
#define TEST_SSTATUS_MXR (UINT64_C(1) << 19)
#define TEST_SSTATUS_SD (UINT64_C(1) << 63)

enum {
    IPC_SYSCALL_CLIENT = 0,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_PEER,
    IPC_SYSCALL_PROCESS_COUNT,
};

enum ipc_syscall_command {
    IPC_COMMAND_NONE = 0,
    IPC_COMMAND_CLIENT_SEND_ONE,
    IPC_COMMAND_SERVER_RECEIVE_SEND_ONE,
    IPC_COMMAND_SERVER_WAIT_CALL_ONE,
    IPC_COMMAND_CLIENT_CALL_ONE,
    IPC_COMMAND_SERVER_REPLY_ONE,
    IPC_COMMAND_SERVER_WAIT_CALL_TWO,
    IPC_COMMAND_CLIENT_CALL_TWO,
    IPC_COMMAND_SERVER_REPLY_RECEIVE_TWO,
    IPC_COMMAND_CLIENT_WAIT_SERVER_CALL,
    IPC_COMMAND_PEER_SEND,
    IPC_COMMAND_SERVER_CALL_CLIENT,
    IPC_COMMAND_PEER_NOTIFY_ONE,
    IPC_COMMAND_PEER_NOTIFY_TWO,
    IPC_COMMAND_PEER_BLOCK,
    IPC_COMMAND_CLIENT_REPLY_THREE,
    IPC_COMMAND_SERVER_RECEIVE_NOTIFY,
    IPC_COMMAND_SERVER_WAKE_PEER,
    IPC_COMMAND_SERVER_BLOCK_FINAL,
    IPC_COMMAND_CLIENT_ERROR_UNAUTHORIZED,
    IPC_COMMAND_CLIENT_ERROR_ARGUMENT,
    IPC_COMMAND_CLIENT_ERROR_FAULT,
};

enum ipc_token_expectation {
    IPC_TOKEN_ZERO = 0,
    IPC_TOKEN_NONZERO,
};

struct ipc_thread_script {
    bool armed;
    bool awaiting;
    enum ipc_syscall_command command;
    uint64_t expected_result;
    struct micros_user_context captured;
};

struct ipc_negative_buffer_snapshot {
    struct micros_ipc_message inbound;
    struct micros_ipc_message outbound;
};

static const uint64_t TEST_CODE_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE;
static const uint64_t TEST_DATA_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00002000);
static const uint64_t TEST_DATA_SECOND_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00003000);
static const uint64_t TEST_STACK_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00004000);
static const uint64_t TEST_OUTBOUND_ADDRESS =
    TEST_DATA_SECOND_VIRTUAL_ADDRESS - UINT64_C(32);
static const uint64_t TEST_INBOUND_ADDRESS =
    TEST_DATA_VIRTUAL_ADDRESS + UINT64_C(0x100);
static const uint64_t TEST_TIMER_INTERVAL =
    UINT64_C(0x0000000010000000);
static const uint64_t TEST_DEFERRED_TIMER_INTERVAL =
    UINT64_C(0x00100000);
static const uint8_t TEST_SERVER_PRIORITY =
    MICROS_SCHEDULER_PRIORITY_DEFAULT_USER - 2;
static const uint8_t TEST_PEER_PRIORITY =
    MICROS_SCHEDULER_PRIORITY_DEFAULT_USER - 1;
static const uint8_t TEST_BACKGROUND_PRIORITY =
    MICROS_SCHEDULER_PRIORITY_DEFAULT_USER + 1;

extern const unsigned char micros_ipc_syscall_test_payload_start[];
extern const unsigned char micros_ipc_syscall_test_payload_ecall[];
extern const unsigned char micros_ipc_syscall_test_payload_after_ecall[];
extern const unsigned char micros_ipc_syscall_test_payload_spin[];
extern const unsigned char micros_ipc_syscall_test_payload_end[];
extern const unsigned char micros_ipc_syscall_test_supervisor_resume[];

uintptr_t micros_ipc_syscall_test_saved_sp;
uintptr_t micros_ipc_syscall_test_saved_gp;
uintptr_t micros_ipc_syscall_test_saved_tp;

static struct micros_process_handle processes[IPC_SYSCALL_PROCESS_COUNT];
static struct micros_thread_handle threads[IPC_SYSCALL_PROCESS_COUNT];
static micros_endpoint_t endpoints[IPC_SYSCALL_PROCESS_COUNT];
static struct micros_endpoint_registry *registry;
static struct ipc_thread_script scripts[IPC_SYSCALL_PROCESS_COUNT];
static uint64_t code_physical[IPC_SYSCALL_PROCESS_COUNT];
static uint64_t data_physical[IPC_SYSCALL_PROCESS_COUNT][2];
static uint64_t stack_physical[IPC_SYSCALL_PROCESS_COUNT];
static uint64_t baseline_owned;
static uint64_t baseline_free;
static size_t baseline_processes;
static size_t baseline_threads;
static size_t baseline_harts;
static uint64_t token_one;
static uint64_t token_two;
static uint64_t token_three;
static bool finish_ready;
static size_t scenario_phase;
static bool timer_completion_started;
static struct micros_user_context timer_expected_context;
static bool negative_snapshot_active;
static enum ipc_syscall_command negative_snapshot_command;
static struct micros_endpoint_registry negative_registry_snapshot;
static struct micros_kernel_objects negative_objects_snapshot;
static struct micros_kernel_objects negative_objects_candidate;
static struct ipc_negative_buffer_snapshot
    negative_buffer_snapshots[IPC_SYSCALL_PROCESS_COUNT];

static const size_t scenario_threads[] = {
    IPC_SYSCALL_CLIENT,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_CLIENT,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_CLIENT,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_CLIENT,
    IPC_SYSCALL_PEER,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_PEER,
    IPC_SYSCALL_PEER,
    IPC_SYSCALL_PEER,
    IPC_SYSCALL_CLIENT,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_CLIENT,
    IPC_SYSCALL_CLIENT,
    IPC_SYSCALL_CLIENT,
};

static const enum ipc_syscall_command scenario_commands[] = {
    IPC_COMMAND_CLIENT_SEND_ONE,
    IPC_COMMAND_SERVER_RECEIVE_SEND_ONE,
    IPC_COMMAND_SERVER_WAIT_CALL_ONE,
    IPC_COMMAND_CLIENT_CALL_ONE,
    IPC_COMMAND_SERVER_REPLY_ONE,
    IPC_COMMAND_SERVER_WAIT_CALL_TWO,
    IPC_COMMAND_CLIENT_CALL_TWO,
    IPC_COMMAND_SERVER_REPLY_RECEIVE_TWO,
    IPC_COMMAND_CLIENT_WAIT_SERVER_CALL,
    IPC_COMMAND_PEER_SEND,
    IPC_COMMAND_SERVER_CALL_CLIENT,
    IPC_COMMAND_PEER_NOTIFY_ONE,
    IPC_COMMAND_PEER_NOTIFY_TWO,
    IPC_COMMAND_PEER_BLOCK,
    IPC_COMMAND_CLIENT_REPLY_THREE,
    IPC_COMMAND_SERVER_RECEIVE_NOTIFY,
    IPC_COMMAND_SERVER_WAKE_PEER,
    IPC_COMMAND_SERVER_BLOCK_FINAL,
    IPC_COMMAND_CLIENT_ERROR_UNAUTHORIZED,
    IPC_COMMAND_CLIENT_ERROR_ARGUMENT,
    IPC_COMMAND_CLIENT_ERROR_FAULT,
};

_Static_assert(
    sizeof(scenario_threads) / sizeof(scenario_threads[0])
        == sizeof(scenario_commands) / sizeof(scenario_commands[0]),
    "IPC syscall scenario arrays must stay aligned"
);

static bool test_mismatch(
    uint64_t stage,
    size_t thread_index,
    enum ipc_syscall_command command
)
{
    uart_write("MICROS_TEST_FAILURE ipc-syscall-stage=");
    uart_write_hex64(stage);
    uart_write(" thread=");
    uart_write_hex64(thread_index);
    uart_write(" command=");
    uart_write_hex64((uint64_t)command);
    uart_write("\n");
    uart_flush();
    return false;
}

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

static bool bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

static void fill_context_pattern(
    struct micros_user_context *context,
    uint64_t base
)
{
    uint64_t words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    for (index = 0; index < sizeof(words) / sizeof(words[0]); ++index) {
        words[index] = base + index;
    }
    copy_bytes(context, words, sizeof(*context));
}

static uint64_t user_address_of(const unsigned char *symbol)
{
    return TEST_CODE_VIRTUAL_ADDRESS
        + (
            (uintptr_t)symbol
            - (uintptr_t)micros_ipc_syscall_test_payload_start
        );
}

static size_t current_thread_index(const struct micros_hart *hart)
{
    size_t index;

    if (hart == NULL) {
        return IPC_SYSCALL_PROCESS_COUNT;
    }
    for (index = 0; index < IPC_SYSCALL_PROCESS_COUNT; ++index) {
        if (
            hart->current_thread.slot == threads[index].slot
            && hart->current_thread.generation
                == threads[index].generation
        ) {
            return index;
        }
    }
    return IPC_SYSCALL_PROCESS_COUNT;
}

static void copy_to_user_buffer(
    size_t process_index,
    uint64_t user_address,
    const struct micros_ipc_message *message
)
{
    const unsigned char *input = (const unsigned char *)message;
    size_t index;

    for (index = 0; index < sizeof(*message); ++index) {
        uint64_t offset =
            user_address - TEST_DATA_VIRTUAL_ADDRESS + index;
        uint64_t physical =
            offset < MICROS_SV39_PAGE_SIZE
                ? data_physical[process_index][0] + offset
                : data_physical[process_index][1]
                    + offset - MICROS_SV39_PAGE_SIZE;

        *(unsigned char *)(uintptr_t)physical = input[index];
    }
}

static void copy_from_user_buffer(
    size_t process_index,
    uint64_t user_address,
    struct micros_ipc_message *message
)
{
    unsigned char *output = (unsigned char *)message;
    size_t index;

    for (index = 0; index < sizeof(*message); ++index) {
        uint64_t offset =
            user_address - TEST_DATA_VIRTUAL_ADDRESS + index;
        uint64_t physical =
            offset < MICROS_SV39_PAGE_SIZE
                ? data_physical[process_index][0] + offset
                : data_physical[process_index][1]
                    + offset - MICROS_SV39_PAGE_SIZE;

        output[index] =
            *(const unsigned char *)(uintptr_t)physical;
    }
}

static bool command_has_stable_error(
    enum ipc_syscall_command command
)
{
    return (
        command == IPC_COMMAND_CLIENT_ERROR_UNAUTHORIZED
        || command == IPC_COMMAND_CLIENT_ERROR_ARGUMENT
        || command == IPC_COMMAND_CLIENT_ERROR_FAULT
    );
}

static bool snapshot_negative_state(
    const struct micros_kernel_objects *objects,
    enum ipc_syscall_command command
)
{
    size_t index;

    if (
        objects == NULL
        || registry == NULL
        || !command_has_stable_error(command)
    ) {
        return false;
    }
    copy_bytes(
        &negative_registry_snapshot,
        registry,
        sizeof(negative_registry_snapshot)
    );
    copy_bytes(
        &negative_objects_snapshot,
        objects,
        sizeof(negative_objects_snapshot)
    );
    for (index = 0; index < IPC_SYSCALL_PROCESS_COUNT; ++index) {
        copy_from_user_buffer(
            index,
            TEST_INBOUND_ADDRESS,
            &negative_buffer_snapshots[index].inbound
        );
        copy_from_user_buffer(
            index,
            TEST_OUTBOUND_ADDRESS,
            &negative_buffer_snapshots[index].outbound
        );
    }
    negative_snapshot_command = command;
    negative_snapshot_active = true;
    return true;
}

static bool negative_state_matches(
    const struct micros_kernel_objects *objects,
    enum ipc_syscall_command command
)
{
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct ipc_negative_buffer_snapshot observed;
    size_t index;
    bool matches;

    if (
        !negative_snapshot_active
        || negative_snapshot_command != command
        || objects == NULL
        || registry == NULL
        || hart_handle.slot >= MICROS_HART_CAPACITY
    ) {
        return false;
    }
    copy_bytes(
        &negative_objects_candidate,
        objects,
        sizeof(negative_objects_candidate)
    );
    copy_bytes(
        &negative_objects_candidate.threads[
            threads[IPC_SYSCALL_CLIENT].slot
        ].user_context,
        &negative_objects_snapshot.threads[
            threads[IPC_SYSCALL_CLIENT].slot
        ].user_context,
        sizeof(struct micros_user_context)
    );
    negative_objects_candidate.harts[
        hart_handle.slot
    ].accounting_owner =
        negative_objects_snapshot.harts[
            hart_handle.slot
        ].accounting_owner;
    negative_objects_candidate.harts[
        hart_handle.slot
    ].accounting_started_at =
        negative_objects_snapshot.harts[
            hart_handle.slot
        ].accounting_started_at;
    copy_bytes(
        &negative_objects_candidate.harts[
            hart_handle.slot
        ].accounted_thread,
        &negative_objects_snapshot.harts[
            hart_handle.slot
        ].accounted_thread,
        sizeof(struct micros_thread_handle)
    );
    negative_objects_candidate.harts[
        hart_handle.slot
    ].kernel_counter_ticks =
        negative_objects_snapshot.harts[
            hart_handle.slot
        ].kernel_counter_ticks;
    matches = (
        bytes_equal(
            &negative_registry_snapshot,
            registry,
            sizeof(negative_registry_snapshot)
        )
        && bytes_equal(
            &negative_objects_snapshot,
            &negative_objects_candidate,
            sizeof(negative_objects_snapshot)
        )
    );
    for (
        index = 0;
        matches && index < IPC_SYSCALL_PROCESS_COUNT;
        ++index
    ) {
        copy_from_user_buffer(
            index,
            TEST_INBOUND_ADDRESS,
            &observed.inbound
        );
        copy_from_user_buffer(
            index,
            TEST_OUTBOUND_ADDRESS,
            &observed.outbound
        );
        matches = bytes_equal(
            &negative_buffer_snapshots[index],
            &observed,
            sizeof(observed)
        );
    }
    negative_snapshot_active = false;
    return matches;
}

static void fill_message(
    size_t process_index,
    uint64_t user_address,
    uint32_t type,
    uint8_t seed
)
{
    struct micros_ipc_message message;
    size_t index;

    clear_bytes(&message, sizeof(message));
    message.source = UINT32_C(0xaaaaaaaa);
    message.type = type;
    message.reply_token = UINT64_C(0xbbbbbbbbbbbbbbbb);
    for (index = 0; index < sizeof(message.payload); ++index) {
        message.payload[index] = (uint8_t)(seed + index);
    }
    copy_to_user_buffer(process_index, user_address, &message);
}

static bool message_matches(
    size_t process_index,
    uint64_t user_address,
    micros_endpoint_t source,
    uint32_t type,
    uint8_t seed,
    enum ipc_token_expectation token_expectation,
    uint64_t *token
)
{
    struct micros_ipc_message message;
    size_t index;

    copy_from_user_buffer(
        process_index,
        user_address,
        &message
    );
    if (
        message.source != source
        || message.type != type
        || (
            token_expectation == IPC_TOKEN_ZERO
                ? message.reply_token != 0
                : message.reply_token == 0
        )
    ) {
        return false;
    }
    for (index = 0; index < sizeof(message.payload); ++index) {
        if (message.payload[index] != (uint8_t)(seed + index)) {
            return false;
        }
    }
    if (token != NULL) {
        *token = message.reply_token;
    }
    return true;
}

static uint64_t notification_mask(
    const struct micros_ipc_message *message
)
{
    uint64_t mask = 0;
    size_t index;

    for (index = 0; index < sizeof(mask); ++index) {
        mask |= (uint64_t)message->payload[index] << (index * 8);
    }
    return mask;
}

static bool notification_matches(
    size_t process_index,
    micros_endpoint_t source,
    uint64_t mask
)
{
    struct micros_ipc_message message;
    size_t index;

    copy_from_user_buffer(
        process_index,
        TEST_INBOUND_ADDRESS,
        &message
    );
    if (
        message.source != source
        || message.type != MICROS_IPC_TYPE_KERNEL_NOTIFICATION
        || message.reply_token != 0
        || notification_mask(&message) != mask
    ) {
        return false;
    }
    for (index = sizeof(mask); index < sizeof(message.payload); ++index) {
        if (message.payload[index] != 0) {
            return false;
        }
    }
    return true;
}

static void arm_command(
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    uint64_t operation,
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3
)
{
    scripts[thread_index].armed = true;
    scripts[thread_index].awaiting = false;
    scripts[thread_index].command = command;
    scripts[thread_index].expected_result = MICROS_IPC_ABI_OK;
    context->a0 = a0;
    context->a1 = a1;
    context->a2 = a2;
    context->a3 = a3;
    context->a7 = operation;
    context->sepc =
        user_address_of(micros_ipc_syscall_test_payload_ecall);
}

static void arm_send(
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    micros_endpoint_t destination,
    uint32_t type,
    uint8_t seed
)
{
    fill_message(
        thread_index,
        TEST_OUTBOUND_ADDRESS,
        type,
        seed
    );
    arm_command(
        thread_index,
        context,
        command,
        MICROS_IPC_ABI_SEND,
        destination,
        TEST_OUTBOUND_ADDRESS,
        0,
        0
    );
}

static void arm_receive(
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    micros_endpoint_t source
)
{
    {
        struct micros_ipc_message zero_message;

        clear_bytes(&zero_message, sizeof(zero_message));
        copy_to_user_buffer(
            thread_index,
            TEST_INBOUND_ADDRESS,
            &zero_message
        );
    }
    arm_command(
        thread_index,
        context,
        command,
        MICROS_IPC_ABI_RECEIVE,
        source,
        TEST_INBOUND_ADDRESS,
        0,
        0
    );
}

static void arm_call(
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    micros_endpoint_t destination,
    uint32_t type,
    uint8_t seed
)
{
    fill_message(
        thread_index,
        TEST_OUTBOUND_ADDRESS,
        type,
        seed
    );
    arm_command(
        thread_index,
        context,
        command,
        MICROS_IPC_ABI_CALL,
        destination,
        TEST_OUTBOUND_ADDRESS,
        0,
        0
    );
}

static void arm_reply(
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    uint64_t token,
    uint32_t type,
    uint8_t seed
)
{
    fill_message(
        thread_index,
        TEST_OUTBOUND_ADDRESS,
        type,
        seed
    );
    arm_command(
        thread_index,
        context,
        command,
        MICROS_IPC_ABI_REPLY,
        token,
        TEST_OUTBOUND_ADDRESS,
        0,
        0
    );
}

static void arm_reply_receive(
    size_t thread_index,
    struct micros_user_context *context,
    uint64_t token,
    uint32_t type,
    uint8_t seed
)
{
    fill_message(
        thread_index,
        TEST_OUTBOUND_ADDRESS,
        type,
        seed
    );
    {
        struct micros_ipc_message zero_message;

        clear_bytes(&zero_message, sizeof(zero_message));
        copy_to_user_buffer(
            thread_index,
            TEST_INBOUND_ADDRESS,
            &zero_message
        );
    }
    arm_command(
        thread_index,
        context,
        IPC_COMMAND_SERVER_REPLY_RECEIVE_TWO,
        MICROS_IPC_ABI_REPLY_RECEIVE,
        token,
        TEST_OUTBOUND_ADDRESS,
        MICROS_ENDPOINT_ANY,
        TEST_INBOUND_ADDRESS
    );
}

static void arm_notify(
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    micros_endpoint_t destination,
    uint64_t mask
)
{
    arm_command(
        thread_index,
        context,
        command,
        MICROS_IPC_ABI_NOTIFY,
        destination,
        mask,
        0,
        0
    );
}

bool micros_ipc_syscall_test_before_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    size_t thread_index = current_thread_index(hart);
    struct ipc_thread_script *script;

    if (
        frame == NULL
        || thread_index >= IPC_SYSCALL_PROCESS_COUNT
    ) {
        return test_mismatch(
            1,
            thread_index,
            IPC_COMMAND_NONE
        );
    }
    script = &scripts[thread_index];
    if (
        !script->armed
        || script->awaiting
        || scenario_phase
            >= sizeof(scenario_commands)
                / sizeof(scenario_commands[0])
        || scenario_threads[scenario_phase] != thread_index
        || scenario_commands[scenario_phase] != script->command
        || frame->sepc
            != user_address_of(
                micros_ipc_syscall_test_payload_ecall
            )
    ) {
        return test_mismatch(
            2,
            thread_index,
            script->command
        );
    }
    if (scenario_phase == 1) {
        const struct micros_thread *client =
            &micros_kernel_object_runtime_test_registry()
                ->threads[threads[IPC_SYSCALL_CLIENT].slot];

        if (
            client->runtime_flags != MICROS_THREAD_RTS_IPC_SEND
            || client->ipc_queue_kind != MICROS_IPC_QUEUE_SENDER
        ) {
            return test_mismatch(
                UINT64_C(0x21),
                thread_index,
                script->command
            );
        }
    }
    if (
        script->command == IPC_COMMAND_PEER_NOTIFY_ONE
        || script->command == IPC_COMMAND_PEER_NOTIFY_TWO
    ) {
        const struct micros_thread *server =
            &micros_kernel_object_runtime_test_registry()
                ->threads[threads[IPC_SYSCALL_SERVER].slot];

        if (
            server->runtime_flags != MICROS_THREAD_RTS_IPC_REPLY
            || server->ipc_reply_token == 0
            || server->ipc_delivery_pending
        ) {
            return test_mismatch(
                UINT64_C(0x22),
                thread_index,
                script->command
            );
        }
    }
    if (
        command_has_stable_error(script->command)
        && !snapshot_negative_state(
            micros_kernel_object_runtime_test_registry(),
            script->command
        )
    ) {
        return test_mismatch(
            UINT64_C(0x23),
            thread_index,
            script->command
        );
    }
    ++scenario_phase;
    copy_bytes(
        &script->captured,
        (const struct micros_user_context *)frame,
        sizeof(script->captured)
    );
    script->armed = false;
    script->awaiting = true;
    return true;
}

static bool validate_return(
    size_t thread_index,
    const struct micros_trap_frame *frame
)
{
    const struct ipc_thread_script *script = &scripts[thread_index];
    struct micros_user_context expected;

    copy_bytes(&expected, &script->captured, sizeof(expected));
    expected.a0 = script->expected_result;
    expected.sepc =
        user_address_of(
            micros_ipc_syscall_test_payload_after_ecall
        );

    return (
        bytes_equal(
            frame,
            &expected,
            sizeof(expected)
        )
    );
}

static void report_return_mismatch(
    size_t thread_index,
    const struct micros_trap_frame *frame
)
{
    const struct ipc_thread_script *script = &scripts[thread_index];

    uart_write("MICROS_TEST_FAILURE ipc-return expected=");
    uart_write_hex64(script->expected_result);
    uart_write(" actual=");
    uart_write_hex64(frame->a0);
    uart_write(" sepc=");
    uart_write_hex64(frame->sepc);
    uart_write(" a1=");
    uart_write_hex64(frame->a1);
    uart_write(" a7=");
    uart_write_hex64(frame->a7);
    uart_write("\n");
    uart_flush();
}

static bool transition_after_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    size_t thread_index,
    enum ipc_syscall_command command
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_user_context *context =
        (struct micros_user_context *)frame;

    switch (command) {
    case IPC_COMMAND_CLIENT_SEND_ONE:
        if (
            micros_thread_install_policy(
                objects,
                threads[IPC_SYSCALL_SERVER],
                TEST_SERVER_PRIORITY,
                UINT64_MAX
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        arm_call(
            thread_index,
            context,
            IPC_COMMAND_CLIENT_CALL_ONE,
            endpoints[IPC_SYSCALL_SERVER],
            UINT32_C(0x1101),
            UINT8_C(0x20)
        );
        return true;
    case IPC_COMMAND_SERVER_RECEIVE_SEND_ONE:
        if (
            !message_matches(
                thread_index,
                TEST_INBOUND_ADDRESS,
                endpoints[IPC_SYSCALL_CLIENT],
                UINT32_C(0x1001),
                UINT8_C(0x10),
                IPC_TOKEN_ZERO,
                NULL
            )
        ) {
            (void)test_mismatch(
                UINT64_C(0x51),
                thread_index,
                command
            );
            return false;
        }
        arm_receive(
            thread_index,
            context,
            IPC_COMMAND_SERVER_WAIT_CALL_ONE,
            MICROS_ENDPOINT_ANY
        );
        return true;
    case IPC_COMMAND_SERVER_WAIT_CALL_ONE:
        if (
            !message_matches(
                thread_index,
                TEST_INBOUND_ADDRESS,
                endpoints[IPC_SYSCALL_CLIENT],
                UINT32_C(0x1101),
                UINT8_C(0x20),
                IPC_TOKEN_NONZERO,
                &token_one
            )
        ) {
            return false;
        }
        arm_reply(
            thread_index,
            context,
            IPC_COMMAND_SERVER_REPLY_ONE,
            token_one,
            UINT32_C(0x1201),
            UINT8_C(0x30)
        );
        return true;
    case IPC_COMMAND_SERVER_REPLY_ONE:
        arm_receive(
            thread_index,
            context,
            IPC_COMMAND_SERVER_WAIT_CALL_TWO,
            MICROS_ENDPOINT_ANY
        );
        return true;
    case IPC_COMMAND_CLIENT_CALL_ONE:
        if (
            !message_matches(
                thread_index,
                TEST_OUTBOUND_ADDRESS,
                endpoints[IPC_SYSCALL_SERVER],
                UINT32_C(0x1201),
                UINT8_C(0x30),
                IPC_TOKEN_ZERO,
                NULL
            )
        ) {
            return false;
        }
        arm_call(
            thread_index,
            context,
            IPC_COMMAND_CLIENT_CALL_TWO,
            endpoints[IPC_SYSCALL_SERVER],
            UINT32_C(0x1301),
            UINT8_C(0x40)
        );
        return true;
    case IPC_COMMAND_SERVER_WAIT_CALL_TWO:
        if (
            !message_matches(
                thread_index,
                TEST_INBOUND_ADDRESS,
                endpoints[IPC_SYSCALL_CLIENT],
                UINT32_C(0x1301),
                UINT8_C(0x40),
                IPC_TOKEN_NONZERO,
                &token_two
            )
        ) {
            return false;
        }
        arm_reply_receive(
            thread_index,
            context,
            token_two,
            UINT32_C(0x1401),
            UINT8_C(0x50)
        );
        return true;
    case IPC_COMMAND_CLIENT_CALL_TWO:
        if (
            !message_matches(
                thread_index,
                TEST_OUTBOUND_ADDRESS,
                endpoints[IPC_SYSCALL_SERVER],
                UINT32_C(0x1401),
                UINT8_C(0x50),
                IPC_TOKEN_ZERO,
                NULL
            )
        ) {
            struct micros_ipc_message observed;

            copy_from_user_buffer(
                thread_index,
                TEST_OUTBOUND_ADDRESS,
                &observed
            );

            uart_write("MICROS_TEST_FAILURE call-two source=");
            uart_write_hex64(observed.source);
            uart_write(" type=");
            uart_write_hex64(observed.type);
            uart_write(" token=");
            uart_write_hex64(observed.reply_token);
            uart_write("\n");
            uart_flush();
            return false;
        }
        arm_receive(
            thread_index,
            context,
            IPC_COMMAND_CLIENT_WAIT_SERVER_CALL,
            MICROS_ENDPOINT_ANY
        );
        {
            enum micros_kernel_object_error admit_error =
                micros_thread_scheduler_admit(
                    objects,
                    micros_kernel_object_runtime_boot_hart_handle(),
                    threads[IPC_SYSCALL_PEER],
                    MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
                    UINT64_MAX,
                    true
                );

            if (admit_error != MICROS_KERNEL_OBJECT_OK) {
                const struct micros_thread *peer =
                    &objects->threads[
                        threads[IPC_SYSCALL_PEER].slot
                    ];

                uart_write("MICROS_TEST_FAILURE peer-admit=");
                uart_write_hex64((uint64_t)admit_error);
                uart_write(" assigned=");
                uart_write_hex64(peer->scheduler_assigned);
                uart_write(" context=");
                uart_write_hex64(peer->context_attached);
                uart_write(" flags=");
                uart_write_hex64(peer->runtime_flags);
                uart_write(" pending=");
                uart_write_hex64(peer->ipc_delivery_pending);
                uart_write(" kind=");
                uart_write_hex64(peer->ipc_queue_kind);
                uart_write(" result=");
                uart_write_hex64(peer->ipc_staged_result);
                uart_write(" buffer=");
                uart_write_hex64(peer->ipc_receive_buffer);
                uart_write(" token=");
                uart_write_hex64(peer->ipc_reply_token);
                uart_write(" ready=");
                uart_write_hex64(peer->ready_linked);
                uart_write(" next=");
                uart_write_hex64(peer->ready_next.slot);
                uart_write(" validate=");
                uart_write_hex64(
                    (uint64_t)micros_scheduler_core_validate(
                        objects
                    )
                );
                uart_write("\n");
                uart_flush();
                return false;
            }
        }
        return true;
    case IPC_COMMAND_SERVER_REPLY_RECEIVE_TWO:
        if (
            !message_matches(
                thread_index,
                TEST_INBOUND_ADDRESS,
                endpoints[IPC_SYSCALL_PEER],
                UINT32_C(0x1501),
                UINT8_C(0x60),
                IPC_TOKEN_ZERO,
                NULL
            )
        ) {
            return false;
        }
        if (
            micros_thread_install_policy(
                objects,
                threads[IPC_SYSCALL_PEER],
                TEST_PEER_PRIORITY,
                UINT64_MAX
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        arm_call(
            thread_index,
            context,
            IPC_COMMAND_SERVER_CALL_CLIENT,
            endpoints[IPC_SYSCALL_CLIENT],
            UINT32_C(0x1601),
            UINT8_C(0x70)
        );
        return true;
    case IPC_COMMAND_PEER_SEND:
        arm_notify(
            thread_index,
            context,
            IPC_COMMAND_PEER_NOTIFY_ONE,
            endpoints[IPC_SYSCALL_SERVER],
            UINT64_C(1)
        );
        return true;
    case IPC_COMMAND_PEER_NOTIFY_ONE:
        arm_notify(
            thread_index,
            context,
            IPC_COMMAND_PEER_NOTIFY_TWO,
            endpoints[IPC_SYSCALL_SERVER],
            UINT64_C(4)
        );
        return true;
    case IPC_COMMAND_PEER_NOTIFY_TWO:
        arm_receive(
            thread_index,
            context,
            IPC_COMMAND_PEER_BLOCK,
            MICROS_ENDPOINT_ANY
        );
        return true;
    case IPC_COMMAND_CLIENT_WAIT_SERVER_CALL:
        if (
            !message_matches(
                thread_index,
                TEST_INBOUND_ADDRESS,
                endpoints[IPC_SYSCALL_SERVER],
                UINT32_C(0x1601),
                UINT8_C(0x70),
                IPC_TOKEN_NONZERO,
                &token_three
            )
        ) {
            return false;
        }
        arm_reply(
            thread_index,
            context,
            IPC_COMMAND_CLIENT_REPLY_THREE,
            token_three,
            UINT32_C(0x1701),
            UINT8_C(0x80)
        );
        return true;
    case IPC_COMMAND_SERVER_CALL_CLIENT:
        if (
            !message_matches(
                thread_index,
                TEST_OUTBOUND_ADDRESS,
                endpoints[IPC_SYSCALL_CLIENT],
                UINT32_C(0x1701),
                UINT8_C(0x80),
                IPC_TOKEN_ZERO,
                NULL
            )
        ) {
            return false;
        }
        arm_receive(
            thread_index,
            context,
            IPC_COMMAND_SERVER_RECEIVE_NOTIFY,
            MICROS_ENDPOINT_ANY
        );
        return true;
    case IPC_COMMAND_SERVER_RECEIVE_NOTIFY:
        if (
            !notification_matches(
                thread_index,
                endpoints[IPC_SYSCALL_PEER],
                UINT64_C(5)
            )
        ) {
            return false;
        }
        if (
            micros_thread_install_policy(
                objects,
                threads[IPC_SYSCALL_PEER],
                TEST_BACKGROUND_PRIORITY,
                UINT64_MAX
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        arm_send(
            thread_index,
            context,
            IPC_COMMAND_SERVER_WAKE_PEER,
            endpoints[IPC_SYSCALL_PEER],
            UINT32_C(0x1801),
            UINT8_C(0x90)
        );
        return true;
    case IPC_COMMAND_SERVER_WAKE_PEER:
        finish_ready = true;
        objects->threads[
            threads[IPC_SYSCALL_PEER].slot
        ].user_context.sepc =
            user_address_of(
                micros_ipc_syscall_test_payload_spin
            );
        arm_receive(
            thread_index,
            context,
            IPC_COMMAND_SERVER_BLOCK_FINAL,
            MICROS_ENDPOINT_ANY
        );
        return true;
    case IPC_COMMAND_CLIENT_REPLY_THREE:
        if (!finish_ready) {
            (void)test_mismatch(
                UINT64_C(0xf1),
                thread_index,
                command
            );
            return false;
        }
        arm_notify(
            thread_index,
            context,
            IPC_COMMAND_CLIENT_ERROR_UNAUTHORIZED,
            endpoints[IPC_SYSCALL_SERVER],
            UINT64_C(8)
        );
        scripts[thread_index].expected_result =
            (uint64_t)(int64_t)MICROS_IPC_ABI_UNAUTHORIZED;
        return true;
    case IPC_COMMAND_CLIENT_ERROR_UNAUTHORIZED:
        arm_command(
            thread_index,
            context,
            IPC_COMMAND_CLIENT_ERROR_ARGUMENT,
            MICROS_IPC_ABI_SEND,
            UINT64_C(1) << 32,
            TEST_OUTBOUND_ADDRESS,
            0,
            0
        );
        scripts[thread_index].expected_result =
            (uint64_t)(int64_t)MICROS_IPC_ABI_ARGUMENT;
        return true;
    case IPC_COMMAND_CLIENT_ERROR_ARGUMENT:
        arm_command(
            thread_index,
            context,
            IPC_COMMAND_CLIENT_ERROR_FAULT,
            MICROS_IPC_ABI_SEND,
            endpoints[IPC_SYSCALL_SERVER],
            TEST_STACK_VIRTUAL_ADDRESS
                + MICROS_SV39_PAGE_SIZE,
            0,
            0
        );
        scripts[thread_index].expected_result =
            (uint64_t)(int64_t)MICROS_IPC_ABI_MESSAGE_FAULT;
        return true;
    case IPC_COMMAND_CLIENT_ERROR_FAULT:
        if (
            scenario_phase
                != sizeof(scenario_commands)
                    / sizeof(scenario_commands[0])
        ) {
            return test_mismatch(
                UINT64_C(0xf30),
                thread_index,
                command
            );
        }
        if (
            micros_ipc_stage_no_message_completion(
                registry,
                objects,
                threads[IPC_SYSCALL_CLIENT],
                MICROS_IPC_ERROR_DEAD_ENDPOINT
            ) != MICROS_IPC_OK
        ) {
            return test_mismatch(
                UINT64_C(0xf31),
                thread_index,
                command
            );
        }
        copy_bytes(
            &timer_expected_context,
            frame,
            sizeof(timer_expected_context)
        );
        timer_expected_context.a0 =
            (uint64_t)(int64_t)MICROS_IPC_ABI_DEAD_ENDPOINT;
        timer_expected_context.sepc =
            user_address_of(
                micros_ipc_syscall_test_payload_spin
            );
        frame->sepc = timer_expected_context.sepc;
        if (
            !micros_timer_stop(hart)
            || !micros_timer_start(
                hart,
                TEST_DEFERRED_TIMER_INTERVAL
            )
        ) {
            return test_mismatch(
                UINT64_C(0xf32),
                thread_index,
                command
            );
        }
        timer_completion_started = true;
        return true;
    case IPC_COMMAND_NONE:
    case IPC_COMMAND_PEER_BLOCK:
    case IPC_COMMAND_SERVER_BLOCK_FINAL:
        return false;
    }
    return false;
}

bool micros_ipc_syscall_test_after_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    size_t thread_index = current_thread_index(hart);
    struct ipc_thread_script *script;
    enum ipc_syscall_command completed;

    if (
        frame == NULL
        || thread_index >= IPC_SYSCALL_PROCESS_COUNT
    ) {
        return test_mismatch(
            3,
            thread_index,
            IPC_COMMAND_NONE
        );
    }
    script = &scripts[thread_index];
    if (script->armed && !script->awaiting) {
        return true;
    }
    if (
        !script->awaiting
        || !validate_return(thread_index, frame)
    ) {
        if (script->awaiting) {
            report_return_mismatch(thread_index, frame);
        }
        return test_mismatch(
            4,
            thread_index,
            script->command
        );
    }
    completed = script->command;
    if (
        command_has_stable_error(completed)
        && !negative_state_matches(
            micros_kernel_object_runtime_test_registry(),
            completed
        )
    ) {
        return test_mismatch(
            UINT64_C(0x24),
            thread_index,
            completed
        );
    }
    script->awaiting = false;
    script->command = IPC_COMMAND_NONE;
    if (
        !transition_after_return(
            hart,
            frame,
            thread_index,
            completed
        )
    ) {
        return test_mismatch(5, thread_index, completed);
    }
    return true;
}

static bool prepare_supervisor_exit(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    const uint64_t control_mask =
        MICROS_RISCV_SSTATUS_SIE
        | MICROS_RISCV_SSTATUS_SPIE
        | MICROS_RISCV_SSTATUS_SPP
        | MICROS_RISCV_SSTATUS_SUM
        | TEST_SSTATUS_UBE
        | TEST_SSTATUS_VS
        | TEST_SSTATUS_FS
        | TEST_SSTATUS_XS
        | TEST_SSTATUS_MXR
        | TEST_SSTATUS_SD;

    if (
        objects == NULL
        || micros_scheduler_account_user_trap(
            objects,
            micros_kernel_object_runtime_boot_hart_handle(),
            riscv_read_time()
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_scheduler_test_prepare_supervisor_return(
            hart,
            frame
        ) != MICROS_SCHEDULER_OK
    ) {
        return false;
    }
    frame->sp = micros_ipc_syscall_test_saved_sp;
    frame->sepc =
        (uintptr_t)micros_ipc_syscall_test_supervisor_resume;
    frame->sstatus &= ~control_mask;
    frame->sstatus |= MICROS_RISCV_SSTATUS_SPP;
    return true;
}

bool micros_ipc_syscall_test_before_timer_return(
    struct micros_hart *hart
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    enum micros_kernel_object_error error;

    if (
        !timer_completion_started
        || objects == NULL
        || hart == NULL
        || current_thread_index(hart) != IPC_SYSCALL_CLIENT
        || !objects->threads[
            threads[IPC_SYSCALL_CLIENT].slot
        ].ipc_delivery_pending
    ) {
        return test_mismatch(
            UINT64_C(0xf40),
            current_thread_index(hart),
            IPC_COMMAND_NONE
        );
    }
    error = micros_thread_install_policy(
        objects,
        threads[IPC_SYSCALL_PEER],
        TEST_PEER_PRIORITY,
        UINT64_MAX
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return test_mismatch(
            UINT64_C(0xf50) + (uint64_t)error,
            IPC_SYSCALL_PEER,
            IPC_COMMAND_NONE
        );
    }
    return true;
}

bool micros_ipc_syscall_test_after_timer_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    const struct micros_thread *client;
    struct micros_user_context peer_expected;

    if (
        !timer_completion_started
        || objects == NULL
        || hart == NULL
        || frame == NULL
        || current_thread_index(hart) != IPC_SYSCALL_PEER
    ) {
        return false;
    }
    client = &objects->threads[threads[IPC_SYSCALL_CLIENT].slot];
    copy_bytes(
        &peer_expected,
        &scripts[IPC_SYSCALL_PEER].captured,
        sizeof(peer_expected)
    );
    peer_expected.a0 = MICROS_IPC_ABI_OK;
    peer_expected.sepc =
        user_address_of(
            micros_ipc_syscall_test_payload_spin
        );
    if (
        !client->ipc_delivery_pending
        || client->ipc_staged_result
            != MICROS_IPC_ERROR_DEAD_ENDPOINT
        || !bytes_equal(
            frame,
            &peer_expected,
            sizeof(peer_expected)
        )
        || !message_matches(
            IPC_SYSCALL_PEER,
            TEST_INBOUND_ADDRESS,
            endpoints[IPC_SYSCALL_SERVER],
            UINT32_C(0x1801),
            UINT8_C(0x90),
            IPC_TOKEN_ZERO,
            NULL
        )
        || !micros_thread_ipc_state_is_clear(
            &objects->threads[threads[IPC_SYSCALL_PEER].slot]
        )
        || micros_scheduler_account_user_trap(
            objects,
            micros_kernel_object_runtime_boot_hart_handle(),
            riscv_read_time()
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_scheduler_hold(
            objects,
            threads[IPC_SYSCALL_PEER]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_scheduler_select_user_return(hart, frame)
            != MICROS_SCHEDULER_OK
        || current_thread_index(hart) != IPC_SYSCALL_CLIENT
        || !bytes_equal(
            frame,
            &timer_expected_context,
            sizeof(timer_expected_context)
        )
        || !micros_thread_ipc_state_is_clear(
            &objects->threads[threads[IPC_SYSCALL_CLIENT].slot]
        )
        || !prepare_supervisor_exit(hart, frame)
    ) {
        return false;
    }
    timer_completion_started = false;
    return true;
}

static bool prepare_process(
    struct micros_kernel_objects *objects,
    size_t index,
    const unsigned char *payload,
    size_t payload_size
)
{
    const uint32_t code_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_EXECUTE;
    const uint32_t data_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE;
    struct micros_user_context context;
    uintptr_t kernel_stack_bottom;
    uintptr_t kernel_stack_top;

    if (
        micros_user_address_space_create(processes[index])
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            processes[index],
            TEST_CODE_VIRTUAL_ADDRESS,
            code_permissions,
            &code_physical[index]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            processes[index],
            TEST_DATA_VIRTUAL_ADDRESS,
            data_permissions,
            &data_physical[index][0]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            processes[index],
            TEST_DATA_SECOND_VIRTUAL_ADDRESS,
            data_permissions,
            &data_physical[index][1]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            processes[index],
            TEST_STACK_VIRTUAL_ADDRESS,
            data_permissions,
            &stack_physical[index]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_thread_create(
            objects,
            processes[index],
            &threads[index]
        ) != MICROS_KERNEL_OBJECT_OK
        || !micros_user_execution_test_stack_bounds(
            threads[index],
            &kernel_stack_bottom,
            &kernel_stack_top
        )
    ) {
        return false;
    }
    (void)kernel_stack_bottom;
    (void)kernel_stack_top;
    copy_bytes(
        (void *)(uintptr_t)code_physical[index],
        payload,
        payload_size
    );
    clear_bytes(
        (void *)(uintptr_t)data_physical[index][0],
        MICROS_SV39_PAGE_SIZE
    );
    clear_bytes(
        (void *)(uintptr_t)data_physical[index][1],
        MICROS_SV39_PAGE_SIZE
    );
    fill_context_pattern(
        &context,
        UINT64_C(0x1000) + index * UINT64_C(0x1000)
    );
    context.sepc =
        user_address_of(micros_ipc_syscall_test_payload_ecall);
    context.sp =
        TEST_STACK_VIRTUAL_ADDRESS + MICROS_SV39_PAGE_SIZE;
    context.a4 = UINT64_C(0x4400) + index;
    context.a5 = UINT64_C(0x5500) + index;
    context.a6 = UINT64_C(0x6600) + index;
    context.sstatus = 0;
    if (index == IPC_SYSCALL_CLIENT) {
        arm_send(
            index,
            &context,
            IPC_COMMAND_CLIENT_SEND_ONE,
            endpoints[IPC_SYSCALL_SERVER],
            UINT32_C(0x1001),
            UINT8_C(0x10)
        );
    } else if (index == IPC_SYSCALL_SERVER) {
        arm_receive(
            index,
            &context,
            IPC_COMMAND_SERVER_RECEIVE_SEND_ONE,
            MICROS_ENDPOINT_ANY
        );
    } else {
        arm_send(
            index,
            &context,
            IPC_COMMAND_PEER_SEND,
            endpoints[IPC_SYSCALL_SERVER],
            UINT32_C(0x1501),
            UINT8_C(0x60)
        );
    }
    return micros_user_execution_prepare(
        threads[index],
        &context
    ) == MICROS_USER_EXECUTION_OK;
}

static bool cleanup_process(
    struct micros_kernel_objects *objects,
    size_t index
)
{
    uint64_t released;

    return (
        micros_thread_scheduler_remove(
            objects,
            threads[index]
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_user_execution_detach(threads[index])
            == MICROS_USER_EXECUTION_OK
        && micros_thread_release(objects, threads[index])
            == MICROS_KERNEL_OBJECT_OK
        && micros_user_address_space_release_page(
            processes[index],
            TEST_CODE_VIRTUAL_ADDRESS,
            &released
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && released == code_physical[index]
        && micros_user_address_space_release_page(
            processes[index],
            TEST_DATA_VIRTUAL_ADDRESS,
            &released
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && released == data_physical[index][0]
        && micros_user_address_space_release_page(
            processes[index],
            TEST_DATA_SECOND_VIRTUAL_ADDRESS,
            &released
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && released == data_physical[index][1]
        && micros_user_address_space_release_page(
            processes[index],
            TEST_STACK_VIRTUAL_ADDRESS,
            &released
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && released == stack_physical[index]
        && micros_user_address_space_destroy(processes[index])
            == MICROS_USER_ADDRESS_SPACE_OK
        && micros_frame_ownership_runtime_release_process(
            objects,
            processes[index]
        ) == MICROS_KERNEL_OBJECT_OK
    );
}

_Noreturn void micros_ipc_syscall_test_finish(void)
{
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t index;
    uint64_t failure_stage = 1;

    if (
        ledger == NULL
        || objects == NULL
        || registry == NULL
    ) {
        goto failure;
    }
    for (index = 0; index < IPC_SYSCALL_PROCESS_COUNT; ++index) {
        if (
            (
                objects->threads[threads[index].slot].runtime_flags
                & MICROS_THREAD_RTS_INACTIVE
            ) == 0
            && micros_thread_runtime_flags_set(
                objects,
                threads[index],
                MICROS_THREAD_RTS_INACTIVE
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            failure_stage = UINT64_C(0x80) + index;
            goto failure;
        }
    }
    for (index = 0; index < IPC_SYSCALL_PROCESS_COUNT; ++index) {
        enum micros_ipc_error close_error =
            micros_ipc_endpoint_close(
                registry,
                objects,
                endpoints[index]
            );

        if (close_error != MICROS_IPC_OK) {
            const struct micros_thread *thread =
                &objects->threads[threads[index].slot];
            const struct micros_endpoint_record *endpoint =
                &registry->endpoints[processes[index].slot];

            uart_write("MICROS_TEST_FAILURE close flags=");
            uart_write_hex64(thread->runtime_flags);
            uart_write(" kind=");
            uart_write_hex64(thread->ipc_queue_kind);
            uart_write(" source=");
            uart_write_hex64(thread->ipc_receive_source);
            uart_write(" buffer=");
            uart_write_hex64(thread->ipc_receive_buffer);
            uart_write(" delivery=");
            uart_write_hex64(thread->ipc_delivery_pending);
            uart_write(" result=");
            uart_write_hex64(thread->ipc_staged_result);
            uart_write(" notifications=");
            uart_write_hex64(
                endpoint->pending_notification_sources
            );
            uart_write("\n");
            uart_flush();
            failure_stage =
                UINT64_C(0x100)
                + index * UINT64_C(0x10)
                + close_error;
            goto failure;
        }
    }
    failure_stage = 2;
    for (index = 0; index < IPC_SYSCALL_PROCESS_COUNT; ++index) {
        if (!cleanup_process(objects, index)) {
            failure_stage = UINT64_C(0x200) + index;
            goto failure;
        }
    }
    failure_stage = 3;
    if (
        objects->live_process_count != baseline_processes
        || objects->live_thread_count != baseline_threads
        || objects->registered_hart_count != baseline_harts
        || ledger->owned_frame_count != baseline_owned
        || ledger->allocator->free_frame_count != baseline_free
    ) {
        goto failure;
    }
    failure_stage = 4;
    if (
        micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto failure;
    }
    uart_write(
        "MICROS_IPC_SYSCALL_TEST_PASS "
        "spaces=three operations=six blocking=validated "
        "calls=tokenized notifications=coalesced "
        "errors=stable completion=deferred "
        "registers=preserved cleanup=complete\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );

failure:
    uart_write("MICROS_TEST_FAILURE ipc-syscall-test stage=");
    uart_write_hex64(failure_stage);
    uart_write("\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}

_Noreturn void micros_ipc_syscall_runtime_run_self_test(void)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "SYSCALL_CLIENT",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_CALL
                | MICROS_PRIVILEGE_OPERATION_REPLY,
            .call_targets = UINT32_C(1) << 2,
            .send_targets = UINT32_C(1) << 2,
        },
        {
            .id = 2,
            .name = "SYSCALL_SERVER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_CALL
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
            .call_targets = UINT32_C(1) << 1,
            .send_targets = UINT32_C(1) << 3,
        },
        {
            .id = 3,
            .name = "SYSCALL_PEER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .send_targets = UINT32_C(1) << 2,
            .notify_targets = UINT32_C(1) << 2,
        },
    };
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    size_t payload_size;
    size_t index;
    uintptr_t saved_status = riscv_irq_save();

    ledger = micros_frame_ownership_runtime_ledger();
    objects = micros_kernel_object_runtime_test_registry();
    if (ledger == NULL || objects == NULL) {
        goto failure;
    }
    baseline_owned = ledger->owned_frame_count;
    baseline_free = ledger->allocator->free_frame_count;
    baseline_processes = objects->live_process_count;
    baseline_threads = objects->live_thread_count;
    baseline_harts = objects->registered_hart_count;
    if (
        micros_ipc_runtime_initialize(
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
    ) {
        goto failure;
    }
    registry = micros_ipc_runtime_authoritative_registry();
    payload_size =
        (uintptr_t)micros_ipc_syscall_test_payload_end
        - (uintptr_t)micros_ipc_syscall_test_payload_start;
    if (
        registry == NULL
        || payload_size == 0
        || payload_size > MICROS_SV39_PAGE_SIZE
    ) {
        goto failure;
    }
    for (index = 0; index < IPC_SYSCALL_PROCESS_COUNT; ++index) {
        if (
            micros_process_create(objects, &processes[index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                registry,
                objects,
                processes[index],
                &endpoints[index]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                registry,
                objects,
                processes[index],
                (uint8_t)(index + 1)
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                registry,
                objects,
                endpoints[index]
            ) != MICROS_ENDPOINT_OK
        ) {
            goto failure;
        }
    }
    for (index = 0; index < IPC_SYSCALL_PROCESS_COUNT; ++index) {
        if (
            !prepare_process(
                objects,
                index,
                micros_ipc_syscall_test_payload_start,
                payload_size
            )
        ) {
            goto failure;
        }
    }
    __asm__ volatile("fence.i" : : : "memory");
    if (
        micros_scheduler_initialize(TEST_TIMER_INTERVAL)
            != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            threads[IPC_SYSCALL_CLIENT],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            UINT64_MAX
        ) != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            threads[IPC_SYSCALL_SERVER],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            UINT64_MAX
        ) != MICROS_SCHEDULER_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
    ) {
        goto failure;
    }
    __asm__ volatile(
        "mv %0, sp\n"
        "mv %1, gp\n"
        "mv %2, tp"
        : "=r"(micros_ipc_syscall_test_saved_sp),
          "=r"(micros_ipc_syscall_test_saved_gp),
          "=r"(micros_ipc_syscall_test_saved_tp)
    );
    (void)saved_status;
    (void)micros_scheduler_start();

failure:
    riscv_irq_restore(saved_status);
    uart_write("MICROS_TEST_FAILURE ipc-syscall-setup\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
