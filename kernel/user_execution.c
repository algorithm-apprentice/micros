#include "micros/user_execution.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/kernel_object_runtime_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

#define USER_SSTATUS_UBE (UINT64_C(1) << 6)
#define USER_SSTATUS_VS (UINT64_C(3) << 9)
#define USER_SSTATUS_FS (UINT64_C(3) << 13)
#define USER_SSTATUS_XS (UINT64_C(3) << 15)
#define USER_SSTATUS_MXR (UINT64_C(1) << 19)
#define USER_SSTATUS_UXL_MASK (UINT64_C(3) << 32)
#define USER_SSTATUS_UXL_64 (UINT64_C(2) << 32)
#define USER_SSTATUS_SD (UINT64_C(1) << 63)

#define USER_SSTATUS_CONTROL_MASK \
    ( \
        MICROS_RISCV_SSTATUS_SIE \
        | MICROS_RISCV_SSTATUS_SPIE \
        | MICROS_RISCV_SSTATUS_SPP \
        | USER_SSTATUS_UBE \
        | USER_SSTATUS_VS \
        | USER_SSTATUS_FS \
        | USER_SSTATUS_XS \
        | MICROS_RISCV_SSTATUS_SUM \
        | USER_SSTATUS_MXR \
    )

extern const unsigned char __thread_kernel_stacks_start[];
extern const unsigned char __thread_kernel_stacks_end[];

_Noreturn void micros_riscv_enter_user(
    const struct micros_user_context *context
);

static void copy_context(
    struct micros_user_context *destination,
    const struct micros_user_context *source
)
{
    unsigned char *destination_bytes =
        (unsigned char *)destination;
    const unsigned char *source_bytes =
        (const unsigned char *)source;
    size_t index;

    for (index = 0; index < sizeof(*destination); ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

static uint64_t read_sstatus(void)
{
    uint64_t status;

    __asm__ volatile("csrr %0, sstatus" : "=r"(status));
    return status;
}

static uint64_t read_satp(void)
{
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}

static _Noreturn void panic_invariant(const char *reason)
{
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();
    uintptr_t hart_id = hart == NULL ? 0 : hart->hardware_id;

    MICROS_PANIC(hart_id, reason);
}

static enum micros_user_execution_error map_object_error(
    enum micros_kernel_object_error error
)
{
    switch (error) {
    case MICROS_KERNEL_OBJECT_OK:
        return MICROS_USER_EXECUTION_OK;
    case MICROS_KERNEL_OBJECT_ERROR_ARGUMENT:
        return MICROS_USER_EXECUTION_ERROR_ARGUMENT;
    case MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED:
        return MICROS_USER_EXECUTION_ERROR_NOT_INITIALIZED;
    case MICROS_KERNEL_OBJECT_ERROR_STALE:
        return MICROS_USER_EXECUTION_ERROR_STALE;
    case MICROS_KERNEL_OBJECT_ERROR_STACK:
        return MICROS_USER_EXECUTION_ERROR_STACK;
    case MICROS_KERNEL_OBJECT_ERROR_STATE:
        return MICROS_USER_EXECUTION_ERROR_STATE;
    default:
        return MICROS_USER_EXECUTION_ERROR_INVARIANT;
    }
}

static bool derive_stack_bounds(
    struct micros_thread_handle thread,
    uintptr_t *stack_bottom,
    uintptr_t *stack_top
)
{
    uintptr_t pool_start =
        (uintptr_t)__thread_kernel_stacks_start;
    uintptr_t pool_end = (uintptr_t)__thread_kernel_stacks_end;
    uintptr_t expected_size =
        MICROS_THREAD_CAPACITY * MICROS_THREAD_KERNEL_STACK_SIZE;
    uintptr_t bottom;

    if (
        stack_bottom == NULL
        || stack_top == NULL
        || thread.slot >= MICROS_THREAD_CAPACITY
        || pool_end < pool_start
        || pool_end - pool_start != expected_size
        || pool_start % MICROS_SV39_PAGE_SIZE != 0
    ) {
        return false;
    }
    bottom = pool_start
        + (thread.slot * MICROS_THREAD_KERNEL_STACK_SIZE);
    *stack_bottom = bottom;
    *stack_top = bottom + MICROS_THREAD_KERNEL_STACK_SIZE;
    return (
        *stack_bottom % MICROS_SV39_PAGE_SIZE == 0
        && *stack_top % MICROS_TRAP_STACK_ALIGNMENT == 0
        && *stack_top <= pool_end
    );
}

static void clear_stack(uintptr_t stack_bottom)
{
    uint64_t *words = (uint64_t *)stack_bottom;
    size_t index;

    for (
        index = 0;
        index < MICROS_THREAD_KERNEL_STACK_SIZE / sizeof(uint64_t);
        ++index
    ) {
        words[index] = 0;
    }
}

static uint64_t derive_user_status(uint64_t live_status)
{
    return (
        live_status
        & ~(USER_SSTATUS_CONTROL_MASK | USER_SSTATUS_SD)
    ) | MICROS_RISCV_SSTATUS_SPIE;
}

static bool status_is_valid_user_return(
    uint64_t status,
    uint64_t preserved_status
)
{
    uint64_t expected = derive_user_status(preserved_status);

    return (
        (expected & USER_SSTATUS_UXL_MASK)
            == USER_SSTATUS_UXL_64
        && status == expected
    );
}

static enum micros_user_execution_error validate_user_mappings(
    struct micros_process_handle process,
    const struct micros_user_context *context
)
{
    uint64_t physical_address;
    uint32_t permissions;
    uint64_t pc_page;
    uint64_t stack_page;

    if (
        context == NULL
        || context->sepc % 2 != 0
        || context->sp == 0
        || context->sp % 16 != 0
    ) {
        return MICROS_USER_EXECUTION_ERROR_CONTEXT;
    }
    pc_page = context->sepc
        & ~((uint64_t)MICROS_SV39_PAGE_SIZE - 1);
    if (
        micros_user_address_space_lookup(
            process,
            pc_page,
            &physical_address,
            &permissions
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || (permissions & MICROS_SV39_PERMISSION_EXECUTE) == 0
    ) {
        return MICROS_USER_EXECUTION_ERROR_MAPPING;
    }
    stack_page = (context->sp - 1)
        & ~((uint64_t)MICROS_SV39_PAGE_SIZE - 1);
    if (
        micros_user_address_space_lookup(
            process,
            stack_page,
            &physical_address,
            &permissions
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || (
            permissions
            & (
                MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
            )
        )
            != (
                MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
            )
    ) {
        return MICROS_USER_EXECUTION_ERROR_MAPPING;
    }
    return MICROS_USER_EXECUTION_OK;
}

static enum micros_user_execution_error resolve_thread_and_process(
    struct micros_thread_handle thread_handle,
    struct micros_kernel_objects **objects,
    const struct micros_thread **thread,
    const struct micros_process **process
)
{
    struct micros_kernel_objects *registry =
        micros_kernel_object_runtime_authoritative_registry();
    const struct micros_thread *resolved_thread;
    const struct micros_process *resolved_process;
    enum micros_kernel_object_error error;

    if (registry == NULL) {
        return MICROS_USER_EXECUTION_ERROR_NOT_INITIALIZED;
    }
    error = micros_thread_resolve(
        registry,
        thread_handle,
        &resolved_thread
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return map_object_error(error);
    }
    error = micros_process_resolve(
        registry,
        resolved_thread->owner,
        &resolved_process
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return map_object_error(error);
    }
    *objects = registry;
    *thread = resolved_thread;
    *process = resolved_process;
    return MICROS_USER_EXECUTION_OK;
}

enum micros_user_execution_error micros_user_execution_prepare(
    struct micros_thread_handle thread_handle,
    const struct micros_user_context *initial_context
)
{
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    const struct micros_thread *thread;
    const struct micros_process *process;
    struct micros_user_context prepared;
    uintptr_t stack_bottom;
    uintptr_t stack_top;
    uint64_t live_status;
    enum micros_user_execution_error error;
    enum micros_kernel_object_error object_error;
    uintptr_t saved_status;

    if (initial_context == NULL) {
        return MICROS_USER_EXECUTION_ERROR_ARGUMENT;
    }
    saved_status = riscv_irq_save();
    error = resolve_thread_and_process(
        thread_handle,
        &objects,
        &thread,
        &process
    );
    if (error != MICROS_USER_EXECUTION_OK) {
        goto done;
    }
    ledger = micros_frame_ownership_runtime_ledger();
    if (ledger == NULL) {
        error = MICROS_USER_EXECUTION_ERROR_NOT_INITIALIZED;
        goto done;
    }
    if (
        ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
    ) {
        error = MICROS_USER_EXECUTION_ERROR_PHASE;
        goto done;
    }
    if (
        thread->runtime_flags != MICROS_THREAD_RTS_INACTIVE
        || thread->scheduler_assigned
        || thread->context_attached
    ) {
        error = MICROS_USER_EXECUTION_ERROR_STATE;
        goto done;
    }
    if (initial_context->sstatus != 0) {
        error = MICROS_USER_EXECUTION_ERROR_CONTEXT;
        goto done;
    }
    if (
        micros_user_address_space_validate(thread->owner)
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        error = MICROS_USER_EXECUTION_ERROR_MAPPING;
        goto done;
    }
    error = validate_user_mappings(
        thread->owner,
        initial_context
    );
    if (error != MICROS_USER_EXECUTION_OK) {
        goto done;
    }
    live_status = read_sstatus();
    if (
        (live_status & USER_SSTATUS_UXL_MASK)
            != USER_SSTATUS_UXL_64
    ) {
        error = MICROS_USER_EXECUTION_ERROR_CONTEXT;
        goto done;
    }
    if (
        !derive_stack_bounds(
            thread_handle,
            &stack_bottom,
            &stack_top
        )
    ) {
        error = MICROS_USER_EXECUTION_ERROR_STACK;
        goto done;
    }
    if (
        micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        error = MICROS_USER_EXECUTION_ERROR_INVARIANT;
        goto done;
    }
    copy_context(&prepared, initial_context);
    prepared.sstatus = derive_user_status(live_status);
    clear_stack(stack_bottom);
    object_error = micros_thread_attach_execution_context(
        objects,
        thread_handle,
        stack_bottom,
        stack_top,
        &prepared
    );
    error = map_object_error(object_error);
    if (
        error == MICROS_USER_EXECUTION_OK
        && micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        if (
            micros_thread_detach_execution_context(
                objects,
                thread_handle
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            panic_invariant("user-context-prepare-rollback");
        }
        error = MICROS_USER_EXECUTION_ERROR_INVARIANT;
    }

done:
    (void)process;
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_user_execution_error micros_user_execution_inspect(
    struct micros_thread_handle thread,
    struct micros_user_context *context
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    uintptr_t stack_bottom;
    uintptr_t stack_top;
    enum micros_kernel_object_error error;
    uintptr_t saved_status;

    if (context == NULL) {
        return MICROS_USER_EXECUTION_ERROR_ARGUMENT;
    }
    if (objects == NULL) {
        return MICROS_USER_EXECUTION_ERROR_NOT_INITIALIZED;
    }
    saved_status = riscv_irq_save();
    error = micros_thread_inspect_execution_context(
        objects,
        thread,
        context,
        &stack_bottom,
        &stack_top
    );
    riscv_irq_restore(saved_status);
    return map_object_error(error);
}

enum micros_user_execution_error micros_user_execution_detach(
    struct micros_thread_handle thread
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    enum micros_kernel_object_error error;
    uintptr_t saved_status;

    if (objects == NULL) {
        return MICROS_USER_EXECUTION_ERROR_NOT_INITIALIZED;
    }
    saved_status = riscv_irq_save();
    error = micros_thread_detach_execution_context(objects, thread);
    if (
        error == MICROS_KERNEL_OBJECT_OK
        && micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        error = MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    riscv_irq_restore(saved_status);
    return map_object_error(error);
}

enum micros_user_execution_error
micros_user_execution_capture_trap(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct micros_thread_handle thread_handle;
    const struct micros_thread *thread;
    const struct micros_process *process;
    struct micros_user_context context;
    uintptr_t frame_address = (uintptr_t)frame;
    enum micros_kernel_object_error object_error;

    if (hart == NULL || frame == NULL) {
        return MICROS_USER_EXECUTION_ERROR_ARGUMENT;
    }
    if (objects == NULL) {
        return MICROS_USER_EXECUTION_ERROR_NOT_INITIALIZED;
    }
    if ((frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0) {
        return MICROS_USER_EXECUTION_ERROR_CONTEXT;
    }
    if (
        hart != &objects->harts[hart_handle.slot]
        || micros_hart_current_thread(
            objects,
            hart_handle,
            &thread_handle
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_resolve(
            objects,
            thread_handle,
            &thread
        ) != MICROS_KERNEL_OBJECT_OK
        || !thread->context_attached
        || hart->trap.primary_stack_bottom
            != thread->kernel_stack_bottom
        || hart->trap.primary_stack_top
            != thread->kernel_stack_top
        || frame_address < thread->kernel_stack_bottom
        || frame_address >= thread->kernel_stack_top
        || sizeof(*frame) > thread->kernel_stack_top - frame_address
        || micros_process_resolve(
            objects,
            thread->owner,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
        || read_satp()
            != (
                MICROS_RISCV_SATP_MODE_SV39
                | (process->address_space_root >> 12)
            )
    ) {
        return MICROS_USER_EXECUTION_ERROR_INVARIANT;
    }
    copy_context(
        &context,
        (const struct micros_user_context *)frame
    );
    object_error = micros_thread_capture_execution_context(
        objects,
        thread_handle,
        &context
    );
    return map_object_error(object_error);
}

enum micros_user_execution_error
micros_user_execution_validate_return(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct micros_thread_handle thread_handle;
    const struct micros_thread *thread;
    const struct micros_process *process;
    uintptr_t frame_address = (uintptr_t)frame;
    enum micros_user_execution_error error;

    if (hart == NULL || frame == NULL) {
        return MICROS_USER_EXECUTION_ERROR_ARGUMENT;
    }
    if (
        objects == NULL
        || hart != &objects->harts[hart_handle.slot]
        || micros_hart_current_thread(
            objects,
            hart_handle,
            &thread_handle
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_resolve(
            objects,
            thread_handle,
            &thread
        ) != MICROS_KERNEL_OBJECT_OK
        || !thread->context_attached
        || frame->hart_context != (uintptr_t)hart
        || frame_address < thread->kernel_stack_bottom
        || frame_address >= thread->kernel_stack_top
        || sizeof(*frame) > thread->kernel_stack_top - frame_address
        || micros_process_resolve(
            objects,
            thread->owner,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
        || read_satp()
            != (
                MICROS_RISCV_SATP_MODE_SV39
                | (process->address_space_root >> 12)
            )
    ) {
        return MICROS_USER_EXECUTION_ERROR_INVARIANT;
    }
    if (
        !status_is_valid_user_return(
            frame->sstatus,
            thread->user_context.sstatus
        )
    ) {
        return MICROS_USER_EXECUTION_ERROR_CONTEXT;
    }
    {
        struct micros_user_context candidate;

        copy_context(
            &candidate,
            (const struct micros_user_context *)frame
        );
        error = validate_user_mappings(
            thread->owner,
            &candidate
        );
    }
    return error;
}

enum micros_user_execution_error
micros_user_execution_validate_context(
    struct micros_thread_handle thread_handle,
    const struct micros_user_context *context
)
{
    struct micros_kernel_objects *objects;
    const struct micros_thread *thread;
    const struct micros_process *process;
    enum micros_user_execution_error error;
    uintptr_t saved_status;

    if (context == NULL) {
        return MICROS_USER_EXECUTION_ERROR_ARGUMENT;
    }
    saved_status = riscv_irq_save();
    error = resolve_thread_and_process(
        thread_handle,
        &objects,
        &thread,
        &process
    );
    if (
        error == MICROS_USER_EXECUTION_OK
        && (
            !thread->context_attached
            || !status_is_valid_user_return(
                context->sstatus,
                thread->user_context.sstatus
            )
        )
    ) {
        error = MICROS_USER_EXECUTION_ERROR_CONTEXT;
    }
    if (error == MICROS_USER_EXECUTION_OK) {
        error = validate_user_mappings(thread->owner, context);
    }
    (void)objects;
    (void)process;
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_user_execution_error
micros_user_execution_store_context(
    struct micros_thread_handle thread,
    const struct micros_user_context *context
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    enum micros_kernel_object_error error;
    uintptr_t saved_status;

    if (context == NULL) {
        return MICROS_USER_EXECUTION_ERROR_ARGUMENT;
    }
    if (objects == NULL) {
        return MICROS_USER_EXECUTION_ERROR_NOT_INITIALIZED;
    }
    saved_status = riscv_irq_save();
    error = micros_thread_capture_execution_context(
        objects,
        thread,
        context
    );
    riscv_irq_restore(saved_status);
    return map_object_error(error);
}

void micros_user_execution_install_return_frame(
    struct micros_trap_frame *frame,
    const struct micros_user_context *context,
    struct micros_hart *hart
)
{
    if (frame == NULL || context == NULL || hart == NULL) {
        panic_invariant("user-context-install-argument");
    }
    copy_context(
        (struct micros_user_context *)frame,
        context
    );
    frame->scause = 0;
    frame->stval = 0;
    frame->hart_context = (uintptr_t)hart;
}

#if defined(MICROS_BUILD_USER_EXECUTION_TEST) \
    || defined(MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_TEST) \
    || defined(MICROS_BUILD_IPC_ECALL_CORE_TEST) \
    || defined(MICROS_BUILD_IPC_SYSCALL_TEST) \
    || defined(MICROS_BUILD_IPC_SYSCALL_PANIC_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
bool micros_user_execution_test_stack_bounds(
    struct micros_thread_handle thread,
    uintptr_t *stack_bottom,
    uintptr_t *stack_top
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    const struct micros_thread *resolved;

    return (
        objects != NULL
        && stack_bottom != NULL
        && stack_top != NULL
        && micros_thread_resolve(objects, thread, &resolved)
            == MICROS_KERNEL_OBJECT_OK
        && derive_stack_bounds(thread, stack_bottom, stack_top)
    );
}
#endif
