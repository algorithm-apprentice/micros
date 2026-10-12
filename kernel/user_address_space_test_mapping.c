#include "kernel/user_address_space_test_internal.h"

#include "arch/riscv64/mmu.h"
#include "micros/ipc_buffer.h"
#include "micros/sv39.h"

static uint32_t user_permissions(void)
{
    return (MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE);
}

static bool invalid_permission_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint32_t permissions
)
{
    uint64_t unchanged = UINT64_C(0xfeedfacefeedface);

    return (
        micros_user_address_space_test_snapshot_runtime(
            context,
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        && micros_user_address_space_allocate_page(
               context->process_one,
               MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
               permissions,
               &unchanged
           ) == MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION
        && unchanged == UINT64_C(0xfeedfacefeedface)
        && micros_user_address_space_test_snapshot_matches(context)
    );
}

static bool allocation_failures_are_rejected(
    struct micros_user_address_space_test_context *context
)
{
    size_t allocation;

    for (allocation = 1; allocation <= 2; ++allocation) {
        uint64_t unchanged = UINT64_C(0xfeedfacefeedface);

        micros_user_address_space_test_fail_after_allocations(
            allocation
        );
        if (!micros_user_address_space_test_snapshot_runtime(
                context,
                context->process_one,
                MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
            )
            || micros_user_address_space_allocate_page(
                   context->process_one,
                   MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
                   user_permissions(),
                   &unchanged
               ) != MICROS_USER_ADDRESS_SPACE_ERROR_ALLOCATION
            || unchanged != UINT64_C(0xfeedfacefeedface)
            || !micros_user_address_space_test_snapshot_matches(
                context
            )) {
            return false;
        }
    }
    micros_user_address_space_test_fail_after_allocations(SIZE_MAX);
    return true;
}

static bool invalid_range_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint64_t invalid_address
)
{
    uint64_t physical_address;
    uint32_t observed_permissions;
    uint64_t expected_satp = micros_user_address_space_test_read_satp();

    physical_address = UINT64_C(0xfeedfacefeedface);
    if (!micros_user_address_space_test_snapshot_runtime(
            context,
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_allocate_page(
               context->process_one,
               invalid_address,
               user_permissions(),
               &physical_address
           ) != MICROS_USER_ADDRESS_SPACE_ERROR_RANGE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || !micros_user_address_space_test_snapshot_matches(context)
        || micros_user_address_space_test_read_satp()
            != expected_satp) {
        return false;
    }
    physical_address = UINT64_C(0xfeedfacefeedface);
    observed_permissions = UINT32_MAX;
    if (!micros_user_address_space_test_snapshot_runtime(
            context,
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_lookup(
               context->process_one,
               invalid_address,
               &physical_address,
               &observed_permissions
           ) != MICROS_USER_ADDRESS_SPACE_ERROR_RANGE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || observed_permissions != UINT32_MAX
        || !micros_user_address_space_test_snapshot_matches(context)
        || micros_user_address_space_test_read_satp()
            != expected_satp) {
        return false;
    }
    physical_address = UINT64_C(0xfeedfacefeedface);
    return (
        micros_user_address_space_test_snapshot_runtime(
            context,
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        && micros_user_address_space_release_page(
               context->process_one,
               invalid_address,
               &physical_address
           ) == MICROS_USER_ADDRESS_SPACE_ERROR_RANGE
        && physical_address == UINT64_C(0xfeedfacefeedface)
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
}

bool micros_user_address_space_test_mapping_rejections(
    struct micros_user_address_space_test_context *context
)
{
    static const uint32_t invalid_permissions[] = {
        MICROS_SV39_PERMISSION_USER,
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_USER,
        MICROS_SV39_PERMISSION_WRITE,
        MICROS_SV39_PERMISSION_WRITE | MICROS_SV39_PERMISSION_EXECUTE,
        UINT32_C(0x80000000),
    };
    static const uint64_t invalid_addresses[] = {
        MICROS_USER_VIRTUAL_BASE - MICROS_SV39_PAGE_SIZE,
        MICROS_USER_VIRTUAL_BASE + 1,
        MICROS_USER_VIRTUAL_END,
    };
    size_t index;

    if (context == NULL
        || !invalid_permission_is_rejected(context, 0)) {
        return false;
    }
    for (index = 0; index
         < sizeof(invalid_permissions) / sizeof(invalid_permissions[0]);
         ++index) {
        if (!invalid_permission_is_rejected(
                context,
                invalid_permissions[index]
            )) {
            return false;
        }
    }
    if (!allocation_failures_are_rejected(context)) {
        return false;
    }
    for (index = 0; index
         < sizeof(invalid_addresses) / sizeof(invalid_addresses[0]);
         ++index) {
        if (!invalid_range_is_rejected(
                context,
                invalid_addresses[index]
            )) {
            return false;
        }
    }
    return true;
}

static bool allocate_isolated_pages(
    struct micros_user_address_space_test_context *context
)
{
    if (micros_user_address_space_allocate_page(
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
            user_permissions(),
            &context->process_one_frame
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
               context->process_two,
               MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
               user_permissions(),
               &context->process_two_frame
           ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
               context->process_one,
               MICROS_USER_ADDRESS_SPACE_TEST_GUARD_VIRTUAL_ADDRESS,
               user_permissions(),
               &context->guard_frame
           ) != MICROS_USER_ADDRESS_SPACE_OK
        || context->process_one_frame == context->process_two_frame
        || context->guard_frame == context->process_one_frame
        || context->guard_frame == context->process_two_frame
        || !micros_user_address_space_test_page_is_zero(
            context->process_one_frame
        )
        || !micros_user_address_space_test_page_is_zero(
            context->process_two_frame
        )
        || !micros_user_address_space_test_page_is_zero(
            context->guard_frame
        )) {
        return false;
    }
    return (
        micros_user_address_space_test_frame_has_exact_owner(
            context->ledger,
            context->process_one_frame,
            MICROS_FRAME_OWNER_PROCESS_USER,
            context->process_one
        )
        && micros_user_address_space_test_frame_has_exact_owner(
            context->ledger,
            context->process_two_frame,
            MICROS_FRAME_OWNER_PROCESS_USER,
            context->process_two
        )
        && micros_user_address_space_test_frame_has_exact_owner(
            context->ledger,
            context->guard_frame,
            MICROS_FRAME_OWNER_PROCESS_USER,
            context->process_one
        )
        && micros_user_address_space_validate(context->process_one)
            == MICROS_USER_ADDRESS_SPACE_OK
        && micros_user_address_space_validate(context->process_two)
            == MICROS_USER_ADDRESS_SPACE_OK
    );
}

static bool lookup_isolation_is_preserved(
    struct micros_user_address_space_test_context *context
)
{
    uint64_t physical_address = UINT64_MAX;
    uint32_t permissions = UINT32_MAX;

    if (micros_user_address_space_lookup(
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
            &physical_address,
            &permissions
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || physical_address != context->process_one_frame
        || permissions != user_permissions()) {
        return false;
    }
    physical_address = UINT64_MAX;
    permissions = UINT32_MAX;
    if (micros_user_address_space_lookup(
            context->process_two,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
            &physical_address,
            &permissions
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || physical_address != context->process_two_frame
        || permissions != user_permissions()) {
        return false;
    }
    physical_address = UINT64_C(0xfeedfacefeedface);
    permissions = UINT32_MAX;
    return (
        micros_user_address_space_test_snapshot_runtime(
            context,
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        && micros_user_address_space_lookup(
               context->process_one,
               MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
               &physical_address,
               &permissions
           ) == MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED
        && physical_address == UINT64_C(0xfeedfacefeedface)
        && permissions == UINT32_MAX
        && micros_user_address_space_test_snapshot_matches(context)
    );
}

static void prepare_ipc_message(
    struct micros_ipc_message *message,
    uint64_t first_frame,
    uint64_t second_frame
)
{
    size_t index;

    for (index = 0; index < sizeof(*message); ++index) {
        unsigned char value = (unsigned char)(UINT8_C(0x40) + index);

        ((unsigned char *)message)[index] = value;
        if (index < 32) {
            ((unsigned char *)(uintptr_t)
                 first_frame)[MICROS_SV39_PAGE_SIZE - 32 + index]
                = value;
        } else {
            ((unsigned char *)(uintptr_t)second_frame)[index - 32]
                = value;
        }
    }
}

static bool ipc_cross_page_transfer_works(
    struct micros_user_address_space_test_context *context,
    uint64_t second_frame,
    struct micros_ipc_message *expected
)
{
    struct micros_ipc_message observed;
    uint64_t physical_address = 0;
    uint32_t permissions = 0;
    size_t contiguous_bytes = 0;
    size_t index;

    prepare_ipc_message(
        expected,
        context->process_two_frame,
        second_frame
    );
    if (micros_user_address_space_translate(
            context->process_two,
            MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS - 32,
            &physical_address,
            &permissions,
            &contiguous_bytes
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || physical_address
            != context->process_two_frame + MICROS_SV39_PAGE_SIZE - 32
        || permissions != user_permissions() || contiguous_bytes != 32
        || micros_ipc_buffer_snapshot(
               context->process_two,
               MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS
                   - 32,
               MICROS_IPC_BUFFER_READ | MICROS_IPC_BUFFER_WRITE,
               &observed
           ) != MICROS_IPC_BUFFER_OK
        || !micros_user_address_space_test_bytes_equal(
            &observed,
            expected,
            sizeof(*expected)
        )) {
        return false;
    }
    for (index = 0; index < sizeof(*expected); ++index) {
        ((unsigned char *)expected)[index]
            ^= (unsigned char)(UINT8_C(0x80) + index);
    }
    return (
        micros_ipc_buffer_write(
            context->process_two,
            MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS - 32,
            expected
        ) == MICROS_IPC_BUFFER_OK
        && micros_user_address_space_test_bytes_equal(
            (const void *)(uintptr_t)(context->process_two_frame
                                      + MICROS_SV39_PAGE_SIZE - 32),
            expected,
            32
        )
        && micros_user_address_space_test_bytes_equal(
            (const void *)(uintptr_t)second_frame,
            (const unsigned char *)expected + 32,
            32
        )
    );
}

static bool ipc_boundaries_are_enforced(
    struct micros_user_address_space_test_context *context,
    uint64_t second_frame
)
{
    struct micros_ipc_message observed;
    struct micros_ipc_message unchanged;
    uint64_t released_frame;
    size_t index;

    if (micros_ipc_buffer_validate(
            context->process_two,
            MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS - 31,
            MICROS_IPC_BUFFER_READ
        ) != MICROS_IPC_BUFFER_ERROR_ARGUMENT
        || micros_ipc_buffer_validate(
               context->process_two,
               MICROS_USER_ADDRESS_SPACE_TEST_ALIAS_VIRTUAL_ADDRESS,
               MICROS_IPC_BUFFER_READ
           ) != MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT
        || micros_user_address_space_release_page(
               context->process_two,
               MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
               &released_frame
           ) != MICROS_USER_ADDRESS_SPACE_OK
        || released_frame != second_frame) {
        return false;
    }
    for (index = 0; index < sizeof(unchanged); ++index) {
        ((unsigned char *)&unchanged)[index]
            = (unsigned char)(UINT8_C(0xa0) + index);
    }
    observed = unchanged;
    return (
        micros_ipc_buffer_snapshot(
            context->process_two,
            MICROS_USER_ADDRESS_SPACE_TEST_ALIAS_VIRTUAL_ADDRESS,
            MICROS_IPC_BUFFER_READ,
            &observed
        ) == MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT
        && micros_user_address_space_test_bytes_equal(
            &observed,
            &unchanged,
            sizeof(unchanged)
        )
        && micros_ipc_buffer_snapshot(
               context->process_two,
               MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
               MICROS_IPC_BUFFER_WRITE,
               &observed
           ) == MICROS_IPC_BUFFER_ERROR_ARGUMENT
        && micros_ipc_buffer_validate(
               context->process_two,
               MICROS_USER_VIRTUAL_END - 32,
               MICROS_IPC_BUFFER_READ
           ) == MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT
    );
}

static bool ipc_permissions_are_enforced(
    struct micros_user_address_space_test_context *context
)
{
    uint64_t second_frame;
    uint64_t released_frame;

    return (
        micros_user_address_space_allocate_page(
            context->process_two,
            MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
            MICROS_SV39_PERMISSION_READ,
            &second_frame
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && micros_ipc_buffer_validate(
               context->process_two,
               MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
               MICROS_IPC_BUFFER_WRITE
           ) == MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT
        && micros_ipc_buffer_validate(
               context->process_two,
               MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
               MICROS_IPC_BUFFER_READ
           ) == MICROS_IPC_BUFFER_OK
        && micros_user_address_space_release_page(
               context->process_two,
               MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
               &released_frame
           ) == MICROS_USER_ADDRESS_SPACE_OK
        && released_frame == second_frame
    );
}

static bool ipc_buffer_behavior_is_preserved(
    struct micros_user_address_space_test_context *context
)
{
    struct micros_ipc_message expected;
    uint64_t second_frame;

    return (
        micros_user_address_space_allocate_page(
            context->process_two,
            MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
            user_permissions(),
            &second_frame
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && ipc_cross_page_transfer_works(
            context,
            second_frame,
            &expected
        )
        && ipc_boundaries_are_enforced(context, second_frame)
        && ipc_permissions_are_enforced(context)
    );
}

static bool duplicate_mapping_is_rejected(
    struct micros_user_address_space_test_context *context
)
{
    uint64_t unchanged = UINT64_C(0xfeedfacefeedface);

    return (
        micros_user_address_space_test_snapshot_runtime(
            context,
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        && micros_user_address_space_allocate_page(
               context->process_one,
               MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
               user_permissions(),
               &unchanged
           ) == MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT
        && unchanged == UINT64_C(0xfeedfacefeedface)
        && micros_user_address_space_test_snapshot_matches(context)
    );
}

static bool active_mutations_are_rejected(
    struct micros_user_address_space_test_context *context
)
{
    uint64_t physical_address = UINT64_C(0xfeedfacefeedface);

    if (!micros_user_address_space_test_snapshot_runtime(
            context,
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_allocate_page(
               context->process_one,
               MICROS_USER_ADDRESS_SPACE_TEST_ACTIVE_VIRTUAL_ADDRESS,
               user_permissions(),
               &physical_address
           ) != MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || !micros_user_address_space_test_snapshot_matches(context)) {
        return false;
    }
    physical_address = UINT64_C(0xfeedfacefeedface);
    if (!micros_user_address_space_test_snapshot_runtime(
            context,
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_release_page(
               context->process_one,
               MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
               &physical_address
           ) != MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || !micros_user_address_space_test_snapshot_matches(context)) {
        return false;
    }
    return (
        micros_user_address_space_test_snapshot_runtime(
            context,
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
        )
        && micros_user_address_space_destroy(context->process_one)
            == MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        && micros_user_address_space_test_snapshot_matches(context)
    );
}

static uint64_t satp_for_root(uint64_t root)
{
    return MICROS_RISCV_SATP_MODE_SV39 | (root >> 12);
}

static bool activation_and_sum_are_isolated(
    struct micros_user_address_space_test_context *context
)
{
    if (micros_user_address_space_activate(context->process_one)
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_test_read_satp()
            != satp_for_root(context->process_one_root)) {
        return false;
    }
    context->sum_fault_virtual_address
        = MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS;
    context->sum_fault_state
        = MICROS_USER_ADDRESS_SPACE_TEST_SUM_FAULT_ARMED;
    micros_user_address_space_sum_fault_trigger(
        MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
    );
    if (context->sum_fault_state
            != MICROS_USER_ADDRESS_SPACE_TEST_SUM_FAULT_HANDLED
        || micros_user_address_space_sum_load(
               MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
           ) != UINT64_C(0x1111111111111111)
        || !active_mutations_are_rejected(context)) {
        return false;
    }
    return (
        micros_user_address_space_activate(context->process_two)
            == MICROS_USER_ADDRESS_SPACE_OK
        && micros_user_address_space_test_read_satp()
            == satp_for_root(context->process_two_root)
        && micros_user_address_space_sum_load(
               MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
           ) == UINT64_C(0x2222222222222222)
        && micros_user_address_space_activate(context->process_one)
            == MICROS_USER_ADDRESS_SPACE_OK
        && micros_user_address_space_test_read_satp()
            == satp_for_root(context->process_one_root)
        && micros_user_address_space_sum_load(
               MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS
           ) == UINT64_C(0x1111111111111111)
        && micros_user_address_space_activate_kernel()
            == MICROS_USER_ADDRESS_SPACE_OK
        && micros_user_address_space_test_read_satp()
            == satp_for_root(
                context->kernel_report->root_physical_address
            )
    );
}

bool micros_user_address_space_test_mapping_operations(
    struct micros_user_address_space_test_context *context
)
{
    if (context == NULL || !allocate_isolated_pages(context)
        || !lookup_isolation_is_preserved(context)
        || !ipc_buffer_behavior_is_preserved(context)) {
        return false;
    }
    micros_user_address_space_test_fill_page(
        context->process_one_frame,
        UINT64_C(0x1111111111111111)
    );
    micros_user_address_space_test_fill_page(
        context->process_two_frame,
        UINT64_C(0x2222222222222222)
    );
    return (
        duplicate_mapping_is_rejected(context)
        && activation_and_sum_are_isolated(context)
    );
}

bool micros_user_address_space_test_reuse_page(
    struct micros_user_address_space_test_context *context
)
{
    uint64_t released_frame;

    if (context == NULL) {
        return false;
    }
    micros_user_address_space_test_fill_page(
        context->process_one_frame,
        UINT64_C(0xa5a5a5a5a5a5a5a5)
    );
    return (
        micros_user_address_space_release_page(
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS,
            &released_frame
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && released_frame == context->process_one_frame
        && micros_user_address_space_allocate_page(
               context->process_one,
               MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
               user_permissions(),
               &context->reused_frame
           ) == MICROS_USER_ADDRESS_SPACE_OK
        && context->reused_frame == context->process_one_frame
        && micros_user_address_space_test_page_is_zero(
            context->reused_frame
        )
    );
}
