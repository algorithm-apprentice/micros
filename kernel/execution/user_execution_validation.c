#include "kernel/execution/user_execution_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/objects/kernel_object_runtime_internal.h"
#include "micros/kernel_object_runtime.h"
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

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool bytes_equal(
    const void *left_storage,
    const void *right_storage,
    size_t size
)
{
    const unsigned char *left = left_storage;
    const unsigned char *right = right_storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
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

enum micros_user_execution_error
micros_user_execution_map_object_error(
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

enum micros_user_execution_error
micros_user_execution_resolve_thread_and_process(
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
        return micros_user_execution_map_object_error(error);
    }
    error = micros_process_resolve(
        registry,
        resolved_thread->owner,
        &resolved_process
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return micros_user_execution_map_object_error(error);
    }
    *objects = registry;
    *thread = resolved_thread;
    *process = resolved_process;
    return MICROS_USER_EXECUTION_OK;
}

static enum micros_user_execution_error
validate_address_space_context(
    struct micros_process_handle process,
    const struct micros_user_context *context
)
{
    enum micros_user_execution_error error;

    if (
        micros_user_address_space_validate(process)
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return MICROS_USER_EXECUTION_ERROR_MAPPING;
    }
    error = validate_user_mappings(process, context);
    if (error != MICROS_USER_EXECUTION_OK) {
        return error;
    }
    return MICROS_USER_EXECUTION_OK;
}

static enum micros_user_execution_error
finish_prepared_context(
    uint64_t live_status,
    const struct micros_user_context *source,
    struct micros_user_context *prepared
)
{
    if (
        (live_status & USER_SSTATUS_UXL_MASK)
            != USER_SSTATUS_UXL_64
    ) {
        return MICROS_USER_EXECUTION_ERROR_CONTEXT;
    }
    copy_context(prepared, source);
    prepared->sstatus = derive_user_status(live_status);
    return MICROS_USER_EXECUTION_OK;
}

enum micros_user_execution_error
micros_user_execution_prepare_post_handoff_context(
    struct micros_process_handle process,
    const struct micros_user_context *initial_context,
    struct micros_user_context *prepared
)
{
    struct micros_user_context canonical;
    enum micros_user_execution_error error;

    if (initial_context == NULL || prepared == NULL) {
        return MICROS_USER_EXECUTION_ERROR_ARGUMENT;
    }
    clear_bytes(&canonical, sizeof(canonical));
    canonical.sp = initial_context->sp;
    canonical.sepc = initial_context->sepc;
    if (
        initial_context->sstatus != 0
        || !bytes_equal(
            initial_context,
            &canonical,
            sizeof(canonical)
        )
    ) {
        return MICROS_USER_EXECUTION_ERROR_CONTEXT;
    }
    error = validate_address_space_context(process, initial_context);
    if (error != MICROS_USER_EXECUTION_OK) {
        return error;
    }
    return finish_prepared_context(
        read_sstatus(),
        &canonical,
        prepared
    );
}

enum micros_user_execution_error
micros_user_execution_prepare_bootstrap_context(
    struct micros_process_handle process,
    const struct micros_user_context *initial_context,
    struct micros_user_context *prepared
)
{
    enum micros_user_execution_error error;

    if (initial_context == NULL || prepared == NULL) {
        return MICROS_USER_EXECUTION_ERROR_ARGUMENT;
    }
    if (initial_context->sstatus != 0) {
        return MICROS_USER_EXECUTION_ERROR_CONTEXT;
    }
    error = validate_address_space_context(process, initial_context);
    if (error != MICROS_USER_EXECUTION_OK) {
        return error;
    }
    return finish_prepared_context(
        read_sstatus(),
        initial_context,
        prepared
    );
}

enum micros_user_execution_error
micros_user_execution_validate_trap_capture(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame,
    struct micros_kernel_objects **objects,
    struct micros_thread_handle *thread_handle
)
{
    struct micros_kernel_objects *registry =
        micros_kernel_object_runtime_authoritative_registry();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    const struct micros_thread *thread;
    const struct micros_process *process;
    uintptr_t frame_address = (uintptr_t)frame;

    if (
        hart == NULL
        || frame == NULL
        || objects == NULL
        || thread_handle == NULL
    ) {
        return MICROS_USER_EXECUTION_ERROR_ARGUMENT;
    }
    if (registry == NULL) {
        return MICROS_USER_EXECUTION_ERROR_NOT_INITIALIZED;
    }
    if ((frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0) {
        return MICROS_USER_EXECUTION_ERROR_CONTEXT;
    }
    if (
        hart != &registry->harts[hart_handle.slot]
        || micros_hart_current_thread(
            registry,
            hart_handle,
            thread_handle
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_resolve(
            registry,
            *thread_handle,
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
            registry,
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
    *objects = registry;
    return MICROS_USER_EXECUTION_OK;
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
    error = micros_user_execution_resolve_thread_and_process(
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
