#include "kernel/user_address_space_test_internal.h"

#include "arch/riscv64/mmu.h"
#include "micros/kernel_object_runtime.h"
#include "micros/sv39.h"

static uint32_t user_permissions(void)
{
    return (MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE);
}

bool micros_user_address_space_test_create_roots(
    struct micros_user_address_space_test_context *context
)
{
    return (
        context != NULL
        && micros_process_create(
               context->objects,
               &context->process_one
           ) == MICROS_KERNEL_OBJECT_OK
        && micros_process_create(
               context->objects,
               &context->process_two
           ) == MICROS_KERNEL_OBJECT_OK
        && micros_user_address_space_create(context->process_one)
            == MICROS_USER_ADDRESS_SPACE_OK
        && micros_user_address_space_create(context->process_two)
            == MICROS_USER_ADDRESS_SPACE_OK
    );
}

bool micros_user_address_space_test_last_page_lifecycle(
    struct micros_user_address_space_test_context *context
)
{
    uint64_t baseline_free;
    uint64_t baseline_owned;
    uint64_t baseline_process_owned;
    uint64_t observed_process_owned;
    uint64_t allocated_frame;
    uint64_t lookup_frame = UINT64_MAX;
    uint64_t released_frame;
    uint32_t lookup_permissions = UINT32_MAX;

    if (context == NULL) {
        return false;
    }
    baseline_free = context->ledger->allocator->free_frame_count;
    baseline_owned = context->ledger->owned_frame_count;
    if (micros_frame_ownership_count_process(
            context->ledger,
            context->process_one,
            &baseline_process_owned
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_user_address_space_test_table_at(
               context->process_one_root
           )
                ->entries
                    [MICROS_USER_ADDRESS_SPACE_TEST_USER_ROOT_INDEX]
            != 0
        || micros_user_address_space_allocate_page(
               context->process_one,
               MICROS_USER_VIRTUAL_END - MICROS_SV39_PAGE_SIZE,
               user_permissions(),
               &allocated_frame
           ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_lookup(
               context->process_one,
               MICROS_USER_VIRTUAL_END - MICROS_SV39_PAGE_SIZE,
               &lookup_frame,
               &lookup_permissions
           ) != MICROS_USER_ADDRESS_SPACE_OK
        || lookup_frame != allocated_frame
        || lookup_permissions != user_permissions()
        || micros_user_address_space_release_page(
               context->process_one,
               MICROS_USER_VIRTUAL_END - MICROS_SV39_PAGE_SIZE,
               &released_frame
           ) != MICROS_USER_ADDRESS_SPACE_OK
        || released_frame != allocated_frame) {
        return false;
    }
    return (
        context->ledger->allocator->free_frame_count == baseline_free
        && context->ledger->owned_frame_count == baseline_owned
        && micros_frame_ownership_count_process(
               context->ledger,
               context->process_one,
               &observed_process_owned
           ) == MICROS_FRAME_OWNERSHIP_OK
        && observed_process_owned == baseline_process_owned
        && micros_user_address_space_test_table_at(
               context->process_one_root
           )
                ->entries
                    [MICROS_USER_ADDRESS_SPACE_TEST_USER_ROOT_INDEX]
            == 0
        && micros_user_address_space_validate(context->process_one)
            == MICROS_USER_ADDRESS_SPACE_OK
    );
}

static bool stale_create_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint64_t expected_satp
)
{
    return (
        micros_user_address_space_test_snapshot_runtime(
            context,
            context->replacement_process,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        && micros_user_address_space_create(context->process_one)
            == MICROS_USER_ADDRESS_SPACE_ERROR_STALE
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

static bool stale_destroy_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint64_t expected_satp
)
{
    return (
        micros_user_address_space_test_snapshot_runtime(
            context,
            context->replacement_process,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        && micros_user_address_space_destroy(context->process_one)
            == MICROS_USER_ADDRESS_SPACE_ERROR_STALE
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

static bool stale_allocate_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint64_t expected_satp
)
{
    uint64_t physical_address = UINT64_C(0xfeedfacefeedface);

    return (
        micros_user_address_space_test_snapshot_runtime(
            context,
            context->replacement_process,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        && micros_user_address_space_allocate_page(
               context->process_one,
               MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
               user_permissions(),
               &physical_address
           ) == MICROS_USER_ADDRESS_SPACE_ERROR_STALE
        && physical_address == UINT64_C(0xfeedfacefeedface)
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

static bool stale_release_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint64_t expected_satp
)
{
    uint64_t physical_address = UINT64_C(0xfeedfacefeedface);

    return (
        micros_user_address_space_test_snapshot_runtime(
            context,
            context->replacement_process,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        && micros_user_address_space_release_page(
               context->process_one,
               MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
               &physical_address
           ) == MICROS_USER_ADDRESS_SPACE_ERROR_STALE
        && physical_address == UINT64_C(0xfeedfacefeedface)
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

static bool stale_lookup_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint64_t expected_satp
)
{
    uint64_t physical_address = UINT64_C(0xfeedfacefeedface);
    uint32_t permissions = UINT32_MAX;

    return (
        micros_user_address_space_test_snapshot_runtime(
            context,
            context->replacement_process,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        && micros_user_address_space_lookup(
               context->process_one,
               MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
               &physical_address,
               &permissions
           ) == MICROS_USER_ADDRESS_SPACE_ERROR_STALE
        && physical_address == UINT64_C(0xfeedfacefeedface)
        && permissions == UINT32_MAX
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

static bool stale_validate_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint64_t expected_satp
)
{
    return (
        micros_user_address_space_test_snapshot_runtime(
            context,
            context->replacement_process,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        && micros_user_address_space_validate(context->process_one)
            == MICROS_USER_ADDRESS_SPACE_ERROR_STALE
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

static bool stale_activate_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint64_t expected_satp
)
{
    return (
        micros_user_address_space_test_snapshot_runtime(
            context,
            context->replacement_process,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        && micros_user_address_space_activate(context->process_one)
            == MICROS_USER_ADDRESS_SPACE_ERROR_STALE
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

static bool stale_operations_are_rejected(
    struct micros_user_address_space_test_context *context
)
{
    uint64_t expected_satp = micros_user_address_space_test_read_satp();

    return (
        stale_create_is_rejected(context, expected_satp)
        && stale_destroy_is_rejected(context, expected_satp)
        && stale_allocate_is_rejected(context, expected_satp)
        && stale_release_is_rejected(context, expected_satp)
        && stale_lookup_is_rejected(context, expected_satp)
        && stale_validate_is_rejected(context, expected_satp)
        && stale_activate_is_rejected(context, expected_satp)
    );
}

bool micros_user_address_space_test_destroy_and_reuse(
    struct micros_user_address_space_test_context *context
)
{
    if (context == NULL
        || micros_thread_create(
               context->objects,
               context->process_one,
               &context->thread
           ) != MICROS_KERNEL_OBJECT_OK
        || !micros_user_address_space_test_snapshot_runtime(
            context,
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_destroy(context->process_one)
            != MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        || !micros_user_address_space_test_snapshot_matches(context)
        || micros_thread_release(context->objects, context->thread)
            != MICROS_KERNEL_OBJECT_OK
        || micros_user_address_space_destroy(context->process_one)
            != MICROS_USER_ADDRESS_SPACE_OK) {
        return false;
    }
    micros_user_address_space_test_fill_page(
        context->process_one_root,
        UINT64_C(0xa5a5a5a5a5a5a5a5)
    );
    if (micros_frame_ownership_runtime_release_process(
            context->objects,
            context->process_one
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(
               context->objects,
               &context->replacement_process
           ) != MICROS_KERNEL_OBJECT_OK
        || context->replacement_process.slot
            != context->process_one.slot
        || context->replacement_process.generation
            != context->process_one.generation + 1
        || micros_user_address_space_create(
               context->replacement_process
           ) != MICROS_USER_ADDRESS_SPACE_OK
        || !micros_user_address_space_test_root_matches_kernel(
            context,
            context->replacement_process,
            &context->replacement_root
        )
        || context->replacement_root != context->process_one_root) {
        return false;
    }
    return stale_operations_are_rejected(context);
}

bool micros_user_address_space_test_enter_handoff(
    struct micros_user_address_space_test_context *context
)
{
    uint64_t released_frame;

    return (
        context != NULL
        && micros_user_address_space_release_page(
               context->process_two,
               MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
               &released_frame
           ) == MICROS_USER_ADDRESS_SPACE_OK
        && released_frame == context->process_two_frame
        && micros_user_address_space_destroy(context->process_two)
            == MICROS_USER_ADDRESS_SPACE_OK
        && micros_frame_ownership_runtime_release_process(
               context->objects,
               context->process_two
           ) == MICROS_KERNEL_OBJECT_OK
        && micros_user_address_space_destroy(
               context->replacement_process
           ) == MICROS_USER_ADDRESS_SPACE_OK
        && context->ledger->owned_frame_count == context->baseline_owned
        && context->ledger->allocator->free_frame_count
            == context->baseline_free
        && micros_frame_ownership_runtime_validate(context->objects)
            == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_runtime_complete_handoff(
               context->objects
           ) == MICROS_FRAME_OWNERSHIP_OK
    );
}

static bool post_handoff_create_is_revoked(
    struct micros_user_address_space_test_context *context,
    uint64_t expected_satp
)
{
    return (
        micros_user_address_space_test_snapshot_core(context)
        && micros_user_address_space_create(
               context->replacement_process
           ) == MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

static bool post_handoff_translate_is_revoked(
    struct micros_user_address_space_test_context *context,
    uint64_t expected_satp
)
{
    uint64_t physical_address = UINT64_C(0xfeedfacefeedface);
    uint32_t permissions = UINT32_MAX;
    size_t contiguous_bytes = SIZE_MAX;

    return (
        micros_user_address_space_test_snapshot_core(context)
        && micros_user_address_space_translate(
               context->replacement_process,
               MICROS_USER_VIRTUAL_END,
               &physical_address,
               &permissions,
               &contiguous_bytes
           ) == MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        && physical_address == UINT64_C(0xfeedfacefeedface)
        && permissions == UINT32_MAX && contiguous_bytes == SIZE_MAX
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

static bool post_handoff_destroy_is_revoked(
    struct micros_user_address_space_test_context *context,
    uint64_t expected_satp
)
{
    return (
        micros_user_address_space_test_snapshot_core(context)
        && micros_user_address_space_destroy(
               context->replacement_process
           ) == MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

static bool post_handoff_allocate_is_revoked(
    struct micros_user_address_space_test_context *context,
    uint64_t expected_satp
)
{
    uint64_t physical_address = UINT64_C(0xfeedfacefeedface);

    return (
        micros_user_address_space_test_snapshot_core(context)
        && micros_user_address_space_allocate_page(
               context->replacement_process,
               MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
               user_permissions(),
               &physical_address
           ) == MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        && physical_address == UINT64_C(0xfeedfacefeedface)
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

static bool post_handoff_release_is_revoked(
    struct micros_user_address_space_test_context *context,
    uint64_t expected_satp
)
{
    uint64_t physical_address = UINT64_C(0xfeedfacefeedface);

    return (
        micros_user_address_space_test_snapshot_core(context)
        && micros_user_address_space_release_page(
               context->replacement_process,
               MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
               &physical_address
           ) == MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        && physical_address == UINT64_C(0xfeedfacefeedface)
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

static bool post_handoff_lookup_is_revoked(
    struct micros_user_address_space_test_context *context,
    uint64_t expected_satp
)
{
    uint64_t physical_address = UINT64_C(0xfeedfacefeedface);
    uint32_t permissions = UINT32_MAX;

    return (
        micros_user_address_space_test_snapshot_core(context)
        && micros_user_address_space_lookup(
               context->replacement_process,
               MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
               &physical_address,
               &permissions
           ) == MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        && physical_address == UINT64_C(0xfeedfacefeedface)
        && permissions == UINT32_MAX
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

static bool post_handoff_validate_is_revoked(
    struct micros_user_address_space_test_context *context,
    uint64_t expected_satp
)
{
    return (
        micros_user_address_space_test_snapshot_core(context)
        && micros_user_address_space_validate(
               context->replacement_process
           ) == MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

static bool post_handoff_activate_is_revoked(
    struct micros_user_address_space_test_context *context,
    uint64_t expected_satp
)
{
    return (
        micros_user_address_space_test_snapshot_core(context)
        && micros_user_address_space_activate(
               context->replacement_process
           ) == MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

static bool post_handoff_kernel_activation_works(
    struct micros_user_address_space_test_context *context
)
{
    return (
        context->kernel_report != NULL
        && micros_user_address_space_test_snapshot_core(context)
        && micros_user_address_space_activate_kernel()
            == MICROS_USER_ADDRESS_SPACE_OK
        && micros_user_address_space_test_read_satp()
            == (MICROS_RISCV_SATP_MODE_SV39
                | (context->kernel_report->root_physical_address >> 12))
        && micros_user_address_space_test_snapshot_matches(context)
    );
}

bool micros_user_address_space_test_post_handoff_revocation(
    struct micros_user_address_space_test_context *context
)
{
    uint64_t expected_satp;

    if (context == NULL) {
        return false;
    }
    expected_satp = micros_user_address_space_test_read_satp();
    return (
        post_handoff_create_is_revoked(context, expected_satp)
        && post_handoff_translate_is_revoked(context, expected_satp)
        && post_handoff_destroy_is_revoked(context, expected_satp)
        && post_handoff_allocate_is_revoked(context, expected_satp)
        && post_handoff_release_is_revoked(context, expected_satp)
        && post_handoff_lookup_is_revoked(context, expected_satp)
        && post_handoff_validate_is_revoked(context, expected_satp)
        && post_handoff_activate_is_revoked(context, expected_satp)
        && post_handoff_kernel_activation_works(context)
    );
}

bool micros_user_address_space_test_finish_lifecycle(
    struct micros_user_address_space_test_context *context
)
{
    return (
        context != NULL
        && micros_frame_ownership_runtime_release_process(
               context->objects,
               context->replacement_process
           ) == MICROS_KERNEL_OBJECT_OK
        && context->objects->live_process_count == 0
        && micros_kernel_objects_validate(context->objects)
            == MICROS_KERNEL_OBJECT_OK
        && micros_frame_ownership_runtime_validate(context->objects)
            == MICROS_FRAME_OWNERSHIP_OK
    );
}
