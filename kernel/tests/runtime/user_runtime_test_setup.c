#include "kernel/tests/runtime/user_runtime_test_internal.h"

#include "kernel/grant/grant_runtime_internal.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "kernel/tests/runtime/user_runtime_image.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/scheduler.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

bool user_runtime_test_preflight(
    struct user_runtime_test_context *runtime,
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects
)
{
    if (
        ledger == NULL
        || objects == NULL
        || micros_user_runtime_test_image.entry
            != MICROS_USER_VIRTUAL_BASE
        || micros_user_runtime_test_image.segment_count
            != MICROS_USER_RUNTIME_IMAGE_SEGMENT_COUNT
        || micros_user_runtime_test_image.image_end
            > MICROS_USER_VIRTUAL_END - MICROS_SV39_PAGE_SIZE
    ) {
        return false;
    }
    runtime->baseline_owned = ledger->owned_frame_count;
    runtime->baseline_free = ledger->allocator->free_frame_count;
    runtime->baseline_processes = objects->live_process_count;
    runtime->baseline_threads = objects->live_thread_count;
    runtime->baseline_harts = objects->registered_hart_count;
    return true;
}

bool user_runtime_test_initialize_mechanisms(
    struct user_runtime_test_context *runtime,
    const struct micros_privilege_profile *profiles,
    size_t profile_count
)
{
    if (
        micros_ipc_runtime_initialize(
            profiles,
            profile_count
        ) != MICROS_ENDPOINT_OK
        || micros_grant_runtime_initialize() != MICROS_GRANT_OK
        || (
            runtime->endpoint_registry =
                micros_ipc_runtime_authoritative_registry()
        ) == NULL
        || (
            runtime->grant_registry =
                micros_grant_runtime_authoritative_registry()
        ) == NULL
    ) {
        return false;
    }
    return true;
}

bool user_runtime_test_prepare_identities(
    struct user_runtime_test_context *runtime,
    struct micros_kernel_objects *objects
)
{
    size_t actor;

    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        if (
            micros_process_create(objects, &runtime->processes[actor])
                != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                runtime->endpoint_registry,
                objects,
                runtime->processes[actor],
                &runtime->endpoints[actor]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                runtime->endpoint_registry,
                objects,
                runtime->processes[actor],
                (uint8_t)(actor + 1)
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                runtime->endpoint_registry,
                objects,
                runtime->endpoints[actor]
            ) != MICROS_ENDPOINT_OK
        ) {
            return false;
        }
    }
    return true;
}

static bool user_runtime_test_prepare_process(
    struct user_runtime_test_context *runtime,
    struct micros_kernel_objects *objects,
    size_t actor
)
{
    struct micros_user_context context;
    uint64_t stack_address =
        MICROS_USER_VIRTUAL_END - MICROS_SV39_PAGE_SIZE;

    if (
        micros_user_address_space_create(runtime->processes[actor])
            != MICROS_USER_ADDRESS_SPACE_OK
        || !user_runtime_test_allocate_image_pages(runtime, actor)
        || !user_runtime_test_copy_image_segments(runtime, actor)
        || !user_runtime_test_verify_loaded_pages(runtime, actor)
        || micros_user_address_space_allocate_page(
            runtime->processes[actor],
            stack_address,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE,
            &runtime->images[actor].stack_physical
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return false;
    }
    user_runtime_test_fill_bytes(
        (void *)(uintptr_t)runtime->images[actor].stack_physical,
        (unsigned char)(UINT8_C(0xd0) + actor),
        MICROS_SV39_PAGE_SIZE
    );
    user_runtime_test_clear_bytes(
        (void *)(uintptr_t)runtime->images[actor].stack_physical,
        MICROS_SV39_PAGE_SIZE
    );
    if (
        !user_runtime_test_bytes_are_zero(
            (const void *)(uintptr_t)runtime->images[actor].stack_physical,
            MICROS_SV39_PAGE_SIZE
        )
        || !user_runtime_test_patch_configuration(runtime, actor)
        || !user_runtime_test_snapshot_writable_pages(runtime, actor)
        || micros_thread_create(
            objects,
            runtime->processes[actor],
            &runtime->threads[actor]
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    user_runtime_test_clear_bytes(&context, sizeof(context));
    context.ra = UINT64_C(0x7111111111111111) + actor;
    context.sp = MICROS_USER_VIRTUAL_END;
    context.gp = UINT64_C(0x3333333333333333);
    context.tp = UINT64_C(0x4444444444444444);
    context.t0 = UINT64_C(0x7555555555555555) + actor;
    context.s0 = UINT64_C(0x7666666666666666) + actor;
    context.a0 = UINT64_C(0x7777777777777777) + actor;
    context.a7 = UINT64_C(0x7888888888888888) + actor;
    context.sstatus = 0;
    context.sepc = micros_user_runtime_test_image.entry;
    return (
        micros_user_execution_prepare(runtime->threads[actor], &context)
            == MICROS_USER_EXECUTION_OK
    );
}

bool user_runtime_test_prepare_images(
    struct user_runtime_test_context *runtime,
    struct micros_kernel_objects *objects
)
{
    size_t actor;

    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        if (!user_runtime_test_prepare_process(runtime, objects, actor)) {
            return false;
        }
    }
    runtime->loader_validated = true;
    __asm__ volatile("fence.i" : : : "memory");
    return true;
}

bool user_runtime_test_admit_threads(
    const struct user_runtime_test_context *runtime,
    struct micros_kernel_objects *objects,
    uint64_t interval
)
{
    if (
        micros_scheduler_initialize(interval)
            != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            runtime->threads[USER_RUNTIME_SERVER],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            UINT64_MAX
        ) != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            runtime->threads[USER_RUNTIME_PEER],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            UINT64_MAX
        ) != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            runtime->threads[USER_RUNTIME_CLIENT],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            UINT64_MAX
        ) != MICROS_SCHEDULER_OK
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return false;
    }
    return true;
}
