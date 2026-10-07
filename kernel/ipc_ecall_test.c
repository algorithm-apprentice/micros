#include "kernel/ipc_ecall_test.h"

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
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

#define TEST_SSTATUS_UBE (UINT64_C(1) << 6)
#define TEST_SSTATUS_VS (UINT64_C(3) << 9)
#define TEST_SSTATUS_FS (UINT64_C(3) << 13)
#define TEST_SSTATUS_XS (UINT64_C(3) << 15)
#define TEST_SSTATUS_MXR (UINT64_C(1) << 19)
#define TEST_SSTATUS_SD (UINT64_C(1) << 63)

enum {
    IPC_ECALL_CLIENT = 0,
    IPC_ECALL_SERVER,
    IPC_ECALL_PROCESS_COUNT,
};

static const uint64_t TEST_CODE_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE;
static const uint64_t TEST_DATA_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00002000);
static const uint64_t TEST_STACK_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00004000);
static const uint64_t TEST_CONTROL_OFFSET = UINT64_C(0x80);
static const uint64_t TEST_A4 = UINT64_C(0x4444444444444444);
static const uint64_t TEST_A5 = UINT64_C(0x5555555555555555);
static const uint64_t TEST_A6 = UINT64_C(0x6666666666666666);

extern const unsigned char micros_ipc_ecall_test_payload_start[];
extern const unsigned char micros_ipc_ecall_test_payload_done[];
extern const unsigned char micros_ipc_ecall_test_payload_end[];
extern const unsigned char micros_ipc_ecall_test_supervisor_resume[];

uintptr_t micros_ipc_ecall_test_saved_sp;
uintptr_t micros_ipc_ecall_test_saved_gp;
uintptr_t micros_ipc_ecall_test_saved_tp;

static struct micros_process_handle processes[IPC_ECALL_PROCESS_COUNT];
static struct micros_thread_handle client_thread;
static micros_endpoint_t endpoints[IPC_ECALL_PROCESS_COUNT];
static struct micros_endpoint_registry *registry;
static uint64_t code_physical;
static uint64_t data_physical;
static uint64_t stack_physical;
static uint64_t baseline_owned;
static uint64_t baseline_free;
static size_t baseline_processes;
static size_t baseline_threads;
static size_t baseline_harts;

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

static uint64_t user_address_of(const unsigned char *symbol)
{
    return TEST_CODE_VIRTUAL_ADDRESS
        + (
            (uintptr_t)symbol
            - (uintptr_t)micros_ipc_ecall_test_payload_start
        );
}

static bool thread_ipc_state_is_clear(
    const struct micros_thread *thread
)
{
    return thread != NULL && micros_thread_ipc_state_is_clear(thread);
}

bool micros_ipc_ecall_core_test_finish_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    const struct micros_thread *thread;
    const struct micros_endpoint_record *server;
    const uint64_t control =
        *(const volatile uint64_t *)(uintptr_t)(
            data_physical + TEST_CONTROL_OFFSET
        );
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
        || hart == NULL
        || frame == NULL
        || registry == NULL
        || control != 3
        || frame->a4 != TEST_A4
        || frame->a5 != TEST_A5
        || frame->a6 != TEST_A6
        || frame->a7 != UINT64_MAX
        || frame->sepc
            != user_address_of(
                micros_ipc_ecall_test_payload_done
            )
    ) {
        return false;
    }
    thread = &objects->threads[client_thread.slot];
    server = &registry->endpoints[processes[IPC_ECALL_SERVER].slot];
    if (
        !thread_ipc_state_is_clear(thread)
        || server->pending_events[processes[IPC_ECALL_CLIENT].slot]
            != UINT64_C(5)
        || (
            server->pending_notification_sources
            & (UINT64_C(1) << processes[IPC_ECALL_CLIENT].slot)
        ) == 0
        || micros_scheduler_test_prepare_supervisor_return(
            hart,
            frame
        ) != MICROS_SCHEDULER_OK
    ) {
        return false;
    }
    frame->sp = micros_ipc_ecall_test_saved_sp;
    frame->sepc =
        (uintptr_t)micros_ipc_ecall_test_supervisor_resume;
    frame->sstatus &= ~control_mask;
    frame->sstatus |= MICROS_RISCV_SSTATUS_SPP;
    return true;
}

_Noreturn void micros_ipc_ecall_core_test_finish(void)
{
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    uint64_t released;
    uint64_t failure_stage = 1;

    if (
        ledger == NULL
        || objects == NULL
        || registry == NULL
        || micros_ipc_endpoint_close(
            registry,
            objects,
            endpoints[IPC_ECALL_CLIENT]
        ) != MICROS_IPC_OK
        || micros_ipc_endpoint_close(
            registry,
            objects,
            endpoints[IPC_ECALL_SERVER]
        ) != MICROS_IPC_OK
    ) {
        goto failure;
    }
    failure_stage = 2;
    if (
        micros_thread_scheduler_remove(
            objects,
            client_thread
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_user_execution_detach(client_thread)
            != MICROS_USER_EXECUTION_OK
        || micros_thread_release(objects, client_thread)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        goto failure;
    }
    failure_stage = 3;
    if (
        micros_user_address_space_release_page(
            processes[IPC_ECALL_CLIENT],
            TEST_CODE_VIRTUAL_ADDRESS,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != code_physical
        || micros_user_address_space_release_page(
            processes[IPC_ECALL_CLIENT],
            TEST_DATA_VIRTUAL_ADDRESS,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != data_physical
        || micros_user_address_space_release_page(
            processes[IPC_ECALL_CLIENT],
            TEST_STACK_VIRTUAL_ADDRESS,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != stack_physical
        || micros_user_address_space_destroy(
            processes[IPC_ECALL_CLIENT]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_frame_ownership_runtime_release_process(
            objects,
            processes[IPC_ECALL_CLIENT]
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        goto failure;
    }
    failure_stage = 4;
    if (
        micros_frame_ownership_runtime_release_process(
            objects,
            processes[IPC_ECALL_SERVER]
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        goto failure;
    }
    failure_stage = 5;
    if (
        objects->live_process_count != baseline_processes
        || objects->live_thread_count != baseline_threads
        || objects->registered_hart_count != baseline_harts
        || ledger->owned_frame_count != baseline_owned
        || ledger->allocator->free_frame_count != baseline_free
    ) {
        goto failure;
    }
    failure_stage = 6;
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
        "MICROS_IPC_ECALL_CORE_TEST_PASS "
        "dispatch=production guard=transactional "
        "completion=returned errors=stable registers=preserved\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );

failure:
    uart_write("MICROS_TEST_FAILURE ipc-ecall-core stage=");
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

_Noreturn void micros_ipc_ecall_core_runtime_run_self_test(void)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "ECALL_CLIENT",
            .operations =
                MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .notify_targets = UINT32_C(1) << 2,
        },
        {
            .id = 2,
            .name = "ECALL_SERVER",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        },
    };
    static const uint8_t profile_ids[IPC_ECALL_PROCESS_COUNT] = {
        1,
        2,
    };
    const uint32_t code_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_EXECUTE;
    const uint32_t data_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE;
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    struct micros_user_context context;
    struct micros_ipc_message *message;
    uintptr_t kernel_stack_bottom;
    uintptr_t kernel_stack_top;
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
    if (registry == NULL) {
        goto failure;
    }
    for (index = 0; index < IPC_ECALL_PROCESS_COUNT; ++index) {
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
                profile_ids[index]
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
    payload_size =
        (uintptr_t)micros_ipc_ecall_test_payload_end
        - (uintptr_t)micros_ipc_ecall_test_payload_start;
    if (
        payload_size == 0
        || payload_size > MICROS_SV39_PAGE_SIZE
        || micros_user_address_space_create(
            processes[IPC_ECALL_CLIENT]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            processes[IPC_ECALL_CLIENT],
            TEST_CODE_VIRTUAL_ADDRESS,
            code_permissions,
            &code_physical
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            processes[IPC_ECALL_CLIENT],
            TEST_DATA_VIRTUAL_ADDRESS,
            data_permissions,
            &data_physical
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            processes[IPC_ECALL_CLIENT],
            TEST_STACK_VIRTUAL_ADDRESS,
            data_permissions,
            &stack_physical
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_thread_create(
            objects,
            processes[IPC_ECALL_CLIENT],
            &client_thread
        ) != MICROS_KERNEL_OBJECT_OK
        || !micros_user_execution_test_stack_bounds(
            client_thread,
            &kernel_stack_bottom,
            &kernel_stack_top
        )
    ) {
        goto failure;
    }
    (void)kernel_stack_bottom;
    (void)kernel_stack_top;
    copy_bytes(
        (void *)(uintptr_t)code_physical,
        micros_ipc_ecall_test_payload_start,
        payload_size
    );
    clear_bytes((void *)(uintptr_t)data_physical, MICROS_SV39_PAGE_SIZE);
    message = (struct micros_ipc_message *)(uintptr_t)data_physical;
    message->type = UINT32_C(0x7101);
    for (index = 0; index < sizeof(message->payload); ++index) {
        message->payload[index] = (uint8_t)(UINT8_C(0x40) + index);
    }
    clear_bytes(&context, sizeof(context));
    context.sepc = TEST_CODE_VIRTUAL_ADDRESS;
    context.sp =
        TEST_STACK_VIRTUAL_ADDRESS + MICROS_SV39_PAGE_SIZE;
    context.s0 =
        TEST_DATA_VIRTUAL_ADDRESS + TEST_CONTROL_OFFSET;
    context.s1 = endpoints[IPC_ECALL_SERVER];
    context.s2 = TEST_DATA_VIRTUAL_ADDRESS;
    context.a0 = endpoints[IPC_ECALL_SERVER];
    context.a1 = UINT64_C(5);
    context.a4 = TEST_A4;
    context.a5 = TEST_A5;
    context.a6 = TEST_A6;
    context.a7 = MICROS_IPC_ABI_NOTIFY;
    if (
        micros_user_execution_prepare(client_thread, &context)
            != MICROS_USER_EXECUTION_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
    ) {
        goto failure;
    }
    __asm__ volatile("fence.i" : : : "memory");
    __asm__ volatile(
        "mv %0, sp\n"
        "mv %1, gp\n"
        "mv %2, tp"
        : "=r"(micros_ipc_ecall_test_saved_sp),
          "=r"(micros_ipc_ecall_test_saved_gp),
          "=r"(micros_ipc_ecall_test_saved_tp)
    );
    (void)saved_status;
    micros_scheduler_test_enter_without_timer(client_thread);

failure:
    riscv_irq_restore(saved_status);
    uart_write("MICROS_TEST_FAILURE ipc-ecall-setup\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
