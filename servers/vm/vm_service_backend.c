#include "servers/vm/vm_service_core.h"

#include "lib/runtime/raw_syscall.h"

static micros_runtime_result_t vm_backend_copy(
    void *context, micros_endpoint_t grantor, micros_grant_t grant,
    size_t offset, uintptr_t destination, size_t size
)
{
    (void)context;
    return micros_runtime_grant_copy_from(grantor, grant, offset, destination, size);
}

static micros_runtime_result_t vm_backend_begin(
    void *context, uint64_t reservation, struct micros_vm_load_begin_result *result
)
{
    (void)context;
    return micros_runtime_raw_syscall(
        MICROS_VM_LOAD_BEGIN, reservation, (uintptr_t)result, sizeof(*result),
        0, 0, 0, MICROS_SYSCALL_ABI_VM_HANDOFF
    );
}

static micros_runtime_result_t vm_backend_map(
    void *context, uint64_t token, const struct micros_vm_load_page_action *page
)
{
    (void)context;
    return micros_runtime_raw_syscall(
        MICROS_VM_SCRATCH_MAP, token, page->virtual_address, page->physical_address,
        page->permissions, MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, 0, MICROS_SYSCALL_ABI_VM_HANDOFF
    );
}

static uint8_t *vm_backend_scratch(void *context)
{
    (void)context;
    return (uint8_t *)(uintptr_t)MICROS_VM_SCRATCH_VIRTUAL_ADDRESS;
}

static micros_runtime_result_t vm_backend_scratch_commit(
    void *context, uint64_t token, const struct micros_vm_load_page_action *page
)
{
    (void)context;
    return micros_runtime_raw_syscall(
        MICROS_VM_SCRATCH_COMMIT, token, page->virtual_address, page->physical_address,
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, 0, 0, MICROS_SYSCALL_ABI_VM_HANDOFF
    );
}

static micros_runtime_result_t vm_backend_freeze(
    void *context, const struct micros_vm_load_freeze_plan *plan,
    struct micros_vm_load_freeze_result *result
)
{
    (void)context;
    return micros_runtime_raw_syscall(
        MICROS_VM_LOAD_FREEZE, (uintptr_t)plan, sizeof(*plan), (uintptr_t)result,
        sizeof(*result), 0, 0, MICROS_SYSCALL_ABI_VM_HANDOFF
    );
}

static micros_runtime_result_t vm_backend_abort(void *context, uint64_t token, uint64_t generation)
{
    (void)context;
    return micros_runtime_raw_syscall(
        MICROS_VM_LOAD_ABORT, token, generation, 0, 0, 0, 0, MICROS_SYSCALL_ABI_VM_HANDOFF
    );
}

static micros_runtime_result_t vm_backend_commit(void *context, uint64_t token, uint64_t generation)
{
    (void)context;
    return micros_runtime_raw_syscall(
        MICROS_VM_LOAD_COMMIT, token, generation, 0, 0, 0, 0, MICROS_SYSCALL_ABI_VM_HANDOFF
    );
}

static micros_runtime_result_t vm_backend_exit(
    void *context, uint64_t exit_token, uint64_t token, uint64_t generation
)
{
    (void)context;
    return micros_runtime_raw_syscall(
        MICROS_VM_EXIT_RELEASE, exit_token, token, generation,
        0, 0, 0, MICROS_SYSCALL_ABI_VM_HANDOFF
    );
}

const struct micros_vm_service_backend *micros_vm_service_runtime_backend(void)
{
    static const struct micros_vm_service_backend backend = {
        .copy_from = vm_backend_copy,
        .load_begin = vm_backend_begin,
        .scratch_map = vm_backend_map,
        .scratch_page = vm_backend_scratch,
        .scratch_commit = vm_backend_scratch_commit,
        .freeze = vm_backend_freeze,
        .abort_load = vm_backend_abort,
        .commit_load = vm_backend_commit,
        .exit_release = vm_backend_exit,
    };

    return &backend;
}
