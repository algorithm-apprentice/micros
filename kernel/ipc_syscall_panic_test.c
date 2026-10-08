#include "kernel/ipc_syscall_panic_test.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/ipc_runtime_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_abi.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

enum {
    IPC_PANIC_CLIENT = 0,
    IPC_PANIC_SERVER,
    IPC_PANIC_PROCESS_COUNT,
};

static const uint64_t TEST_CODE_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE;
static const uint64_t TEST_DATA_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00002000);
static const uint64_t TEST_STACK_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00004000);
static const uint64_t TEST_RECEIVE_OFFSET = UINT64_C(0x100);
static const uint64_t TEST_TIMER_INTERVAL =
    UINT64_C(0x0000000010000000);

extern const unsigned char micros_ipc_syscall_panic_payload_start[];
extern const unsigned char micros_ipc_syscall_panic_payload_end[];

static struct micros_process_handle processes[IPC_PANIC_PROCESS_COUNT];
static struct micros_thread_handle threads[IPC_PANIC_PROCESS_COUNT];
static micros_endpoint_t endpoints[IPC_PANIC_PROCESS_COUNT];
static struct micros_endpoint_registry *registry;
static uint64_t code_physical[IPC_PANIC_PROCESS_COUNT];
static uint64_t data_physical[IPC_PANIC_PROCESS_COUNT];
static uint64_t stack_physical[IPC_PANIC_PROCESS_COUNT];
static bool buffer_invalidated;

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

bool micros_ipc_syscall_panic_test_before_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
)
{
    uint64_t released;

    if (hart == NULL || frame == NULL) {
        return false;
    }
    if (frame->a7 == MICROS_IPC_ABI_RECEIVE) {
        return (
            !buffer_invalidated
            && syscall_return == MICROS_SYSCALL_RETURN_CAPTURED
        );
    }
    if (
        frame->a7 != MICROS_IPC_ABI_SEND
        || syscall_return != MICROS_SYSCALL_RETURN_CAPTURED
        || buffer_invalidated
        || micros_user_address_space_release_page(
            processes[IPC_PANIC_CLIENT],
            TEST_DATA_VIRTUAL_ADDRESS,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != data_physical[IPC_PANIC_CLIENT]
    ) {
        return false;
    }
    buffer_invalidated = true;
    return true;
}

static bool prepare_process(
    struct micros_kernel_objects *objects,
    size_t index,
    uint8_t profile_id,
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
    struct micros_ipc_message *message;
    uintptr_t kernel_stack_bottom;
    uintptr_t kernel_stack_top;
    size_t payload_index;

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
            profile_id
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            registry,
            objects,
            endpoints[index]
        ) != MICROS_ENDPOINT_OK
        || micros_user_address_space_create(processes[index])
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
            &data_physical[index]
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
        (void *)(uintptr_t)data_physical[index],
        MICROS_SV39_PAGE_SIZE
    );
    clear_bytes(&context, sizeof(context));
    context.sepc = TEST_CODE_VIRTUAL_ADDRESS;
    context.sp =
        TEST_STACK_VIRTUAL_ADDRESS + MICROS_SV39_PAGE_SIZE;
    if (index == IPC_PANIC_CLIENT) {
        context.a0 = MICROS_ENDPOINT_ANY;
        context.a1 =
            TEST_DATA_VIRTUAL_ADDRESS + TEST_RECEIVE_OFFSET;
        context.a7 = MICROS_IPC_ABI_RECEIVE;
    } else {
        message = (struct micros_ipc_message *)(uintptr_t)
            data_physical[index];
        message->type = UINT32_C(0x7201);
        for (
            payload_index = 0;
            payload_index < sizeof(message->payload);
            ++payload_index
        ) {
            message->payload[payload_index] =
                (uint8_t)(UINT8_C(0x70) + payload_index);
        }
        context.a0 = endpoints[IPC_PANIC_CLIENT];
        context.a1 = TEST_DATA_VIRTUAL_ADDRESS;
        context.a7 = MICROS_IPC_ABI_SEND;
    }
    return micros_user_execution_prepare(
        threads[index],
        &context
    ) == MICROS_USER_EXECUTION_OK;
}

_Noreturn void micros_ipc_syscall_panic_runtime_run_self_test(void)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "PANIC_CLIENT",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        },
        {
            .id = 2,
            .name = "PANIC_SERVER",
            .operations = MICROS_PRIVILEGE_OPERATION_SEND,
            .send_targets = UINT32_C(1) << 1,
        },
    };
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    size_t payload_size;
    size_t index;
    uintptr_t saved_status = riscv_irq_save();

    ledger = micros_frame_ownership_runtime_ledger();
    objects = micros_kernel_object_runtime_test_registry();
    payload_size =
        (uintptr_t)micros_ipc_syscall_panic_payload_end
        - (uintptr_t)micros_ipc_syscall_panic_payload_start;
    if (
        ledger == NULL
        || objects == NULL
        || payload_size == 0
        || payload_size > MICROS_SV39_PAGE_SIZE
        || micros_ipc_runtime_initialize(
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
    ) {
        goto failure;
    }
    registry = micros_ipc_runtime_authoritative_registry();
    if (registry == NULL) {
        goto failure;
    }
    for (index = 0; index < IPC_PANIC_PROCESS_COUNT; ++index) {
        if (
            !prepare_process(
                objects,
                index,
                (uint8_t)(index + 1),
                micros_ipc_syscall_panic_payload_start,
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
            threads[IPC_PANIC_CLIENT],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER - 1,
            UINT64_MAX
        ) != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            threads[IPC_PANIC_SERVER],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            UINT64_MAX
        ) != MICROS_SCHEDULER_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
    ) {
        goto failure;
    }
    (void)saved_status;
    (void)micros_scheduler_start();

failure:
    riscv_irq_restore(saved_status);
    uart_write("MICROS_TEST_FAILURE ipc-syscall-panic-setup\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
