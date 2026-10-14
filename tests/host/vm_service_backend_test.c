#include "tests/host/vm_service_test_internal.h"

#include "lib/runtime/raw_syscall.h"

int64_t micros_runtime_raw_syscall(
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
    uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7
)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    const uint64_t arguments[8] = {a0, a1, a2, a3, a4, a5, a6, a7};

    memcpy(context->backend_registers, arguments, sizeof(arguments));
    ++context->backend_calls;
    return context->backend_result;
}

static bool service_backend_registers_are(
    const struct vm_core_test_context *context,
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
    uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7
)
{
    const uint64_t expected[8] = {a0, a1, a2, a3, a4, a5, a6, a7};

    VM_SERVICE_EXPECT(memcmp(context->backend_registers, expected, sizeof(expected)) == 0);
    return true;
}

bool test_vm_service_runtime_backend(struct vm_core_test_context *context)
{
    const struct micros_vm_service_backend *backend = micros_vm_service_runtime_backend();
    struct micros_vm_load_begin_result begin;
    struct micros_vm_load_freeze_plan plan = {0};
    struct micros_vm_load_freeze_result freeze;
    struct micros_vm_load_page_action page = {
        .virtual_address = MICROS_APPLICATION_ENTRY,
        .physical_address = UINT64_C(0x80100000),
        .permissions = 0x1b,
    };
    uint8_t bytes[64];

    context->backend_result = MICROS_SYSCALL_ABI_UNAUTHORIZED;
    context->backend_calls = 0;
    VM_SERVICE_EXPECT(
        backend != NULL
        && backend->copy_from(NULL, UINT32_C(0x1005), 65, 7, (uintptr_t)bytes, 13)
            == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && service_backend_registers_are(context, UINT32_C(0x1005), 65, 7, (uintptr_t)bytes,
                                         13, 0, 0, MICROS_SYSCALL_ABI_GRANT_COPY_FROM)
        && backend->load_begin(NULL, 101, &begin) == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && service_backend_registers_are(context, MICROS_VM_LOAD_BEGIN, 101, (uintptr_t)&begin,
                                         32, 0, 0, 0, MICROS_SYSCALL_ABI_VM_HANDOFF)
        && backend->scratch_map(NULL, 201, &page) == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && service_backend_registers_are(context, MICROS_VM_SCRATCH_MAP, 201,
                                         MICROS_APPLICATION_ENTRY, UINT64_C(0x80100000),
                                         0x1b, UINT64_C(0x7fffe000), 0, MICROS_SYSCALL_ABI_VM_HANDOFF)
        && (uintptr_t)backend->scratch_page(NULL) == UINT64_C(0x7fffe000)
        && backend->scratch_commit(NULL, 201, &page) == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && service_backend_registers_are(context, MICROS_VM_SCRATCH_COMMIT, 201,
                                         MICROS_APPLICATION_ENTRY, UINT64_C(0x80100000),
                                         UINT64_C(0x7fffe000), 0, 0, MICROS_SYSCALL_ABI_VM_HANDOFF)
        && backend->freeze(NULL, &plan, &freeze) == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && service_backend_registers_are(context, MICROS_VM_LOAD_FREEZE, (uintptr_t)&plan, 96,
                                         (uintptr_t)&freeze, 32, 0, 0, MICROS_SYSCALL_ABI_VM_HANDOFF)
        && backend->abort_load(NULL, 201, 301) == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && service_backend_registers_are(context, MICROS_VM_LOAD_ABORT, 201, 301,
                                         0, 0, 0, 0, MICROS_SYSCALL_ABI_VM_HANDOFF)
        && backend->commit_load(NULL, 201, 301) == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && service_backend_registers_are(context, MICROS_VM_LOAD_COMMIT, 201, 301,
                                         0, 0, 0, 0, MICROS_SYSCALL_ABI_VM_HANDOFF)
        && backend->exit_release(NULL, 701, 201, 301) == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && service_backend_registers_are(context, MICROS_VM_EXIT_RELEASE, 701, 201,
                                         301, 0, 0, 0, MICROS_SYSCALL_ABI_VM_HANDOFF)
        && context->backend_calls == 8
    );
    return true;
}
