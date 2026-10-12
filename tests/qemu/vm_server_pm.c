#include "tests/qemu/vm_server_internal.h"

#include "lib/runtime/raw_syscall.h"
#include "micros/sv39.h"
#include "micros/vm.h"

#ifdef MICROS_PM_SERVICE_TEST

enum {
    MICROS_PM_TEST_DATA_FRAME_COUNT = 4,
    MICROS_PM_TEST_FRAME_COUNT =
        MICROS_PM_TEST_DATA_FRAME_COUNT + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT,
};

#define MICROS_PM_TEST_PAGE_DIGEST_OFFSET UINT64_C(14695981039346656037)
#define MICROS_PM_TEST_PAGE_DIGEST_PRIME UINT64_C(1099511628211)

extern const uint8_t micros_pm_lifecycle_child_start[];
extern const uint8_t micros_pm_lifecycle_child_end[];
extern const uint8_t micros_pm_lifecycle_child_pm_endpoint[];

static void pm_test_write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static bool pm_test_collect_free_frames(
    uint64_t frames[MICROS_PM_TEST_FRAME_COUNT]
)
{
    uint32_t range_index;
    size_t count = 0;

    if (frames == NULL) {
        return false;
    }
    for (
        range_index = 0;
        range_index < micros_vm_boot_info.header.managed_range_count;
        ++range_index
    ) {
        const struct micros_vm_managed_range *range =
            &micros_vm_boot_info.managed_ranges[range_index];
        uint64_t offset;

        for (offset = 0; offset < range->frame_count; ++offset) {
            uint64_t frame_index = range->frame_index + offset;

            if (
                frame_index >= micros_vm_boot_info.header.managed_frame_count
            ) {
                return false;
            }
            if (
                micros_vm_boot_info.frame_states[frame_index]
                    == MICROS_VM_FRAME_FREE
            ) {
                frames[count] =
                    range->base + offset * MICROS_SV39_PAGE_SIZE;
                ++count;
                if (count == MICROS_PM_TEST_FRAME_COUNT) {
                    return true;
                }
            }
        }
    }
    return false;
}

static uint64_t pm_test_digest_byte(uint64_t digest, uint8_t byte)
{
    digest ^= byte;
    return digest * MICROS_PM_TEST_PAGE_DIGEST_PRIME;
}

static uint64_t pm_test_digest_u32(uint64_t digest, uint32_t value)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        digest = pm_test_digest_byte(
            digest,
            (uint8_t)(value >> (index * 8))
        );
    }
    return digest;
}

static uint64_t pm_test_digest_u64(uint64_t digest, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        digest = pm_test_digest_byte(
            digest,
            (uint8_t)(value >> (index * 8))
        );
    }
    return digest;
}

static uint64_t pm_test_page_plan_digest(
    const uint64_t physical_addresses[MICROS_PM_TEST_DATA_FRAME_COUNT],
    const uint32_t permissions[MICROS_PM_TEST_DATA_FRAME_COUNT]
)
{
    static const uint64_t virtual_addresses[
        MICROS_PM_TEST_DATA_FRAME_COUNT
    ] = {
        MICROS_APPLICATION_ENTRY,
        MICROS_APPLICATION_ENTRY + MICROS_SV39_PAGE_SIZE,
        MICROS_APPLICATION_ENTRY + 2 * MICROS_SV39_PAGE_SIZE,
        MICROS_APPLICATION_STACK_BASE,
    };
    uint64_t digest = MICROS_PM_TEST_PAGE_DIGEST_OFFSET;
    size_t index;

    digest = pm_test_digest_u32(digest, 3);
    digest = pm_test_digest_u32(
        digest,
        MICROS_PM_TEST_DATA_FRAME_COUNT
    );
    for (index = 0; index < MICROS_PM_TEST_DATA_FRAME_COUNT; ++index) {
        digest = pm_test_digest_u64(digest, virtual_addresses[index]);
        digest = pm_test_digest_u64(digest, physical_addresses[index]);
        digest = pm_test_digest_u32(digest, permissions[index]);
        digest = pm_test_digest_u32(digest, 0);
    }
    return digest;
}

static bool pm_test_write_child_page(
    uint32_t pm_endpoint,
    volatile uint8_t *scratch
)
{
    uintptr_t blob_start =
        (uintptr_t)micros_pm_lifecycle_child_start;
    uintptr_t blob_end =
        (uintptr_t)micros_pm_lifecycle_child_end;
    uintptr_t endpoint_address =
        (uintptr_t)micros_pm_lifecycle_child_pm_endpoint;
    size_t blob_size;
    size_t endpoint_offset;
    size_t index;

    if (
        blob_end < blob_start
        || endpoint_address < blob_start
        || endpoint_address + sizeof(uint32_t) > blob_end
    ) {
        return false;
    }
    blob_size = blob_end - blob_start;
    endpoint_offset = endpoint_address - blob_start;
    if (blob_size > MICROS_SV39_PAGE_SIZE) {
        return false;
    }
    for (index = 0; index < MICROS_SV39_PAGE_SIZE; ++index) {
        scratch[index] = 0;
    }
    for (index = 0; index < blob_size; ++index) {
        scratch[index] = micros_pm_lifecycle_child_start[index];
    }
    for (index = 0; index < sizeof(uint32_t); ++index) {
        scratch[endpoint_offset + index] =
            (uint8_t)(pm_endpoint >> (index * 8));
    }
    return true;
}

static bool pm_test_initialize_pages(
    uint64_t load_token,
    const uint64_t frames[MICROS_PM_TEST_FRAME_COUNT],
    const uint32_t permissions[MICROS_PM_TEST_DATA_FRAME_COUNT],
    uint32_t pm_endpoint
)
{
    static const uint64_t virtual_addresses[
        MICROS_PM_TEST_DATA_FRAME_COUNT
    ] = {
        MICROS_APPLICATION_ENTRY,
        MICROS_APPLICATION_ENTRY + MICROS_SV39_PAGE_SIZE,
        MICROS_APPLICATION_ENTRY + 2 * MICROS_SV39_PAGE_SIZE,
        MICROS_APPLICATION_STACK_BASE,
    };
    volatile uint8_t *scratch =
        (volatile uint8_t *)(uintptr_t)MICROS_VM_SCRATCH_VIRTUAL_ADDRESS;
    size_t index;

    for (index = 0; index < MICROS_PM_TEST_DATA_FRAME_COUNT; ++index) {
        if (
            micros_runtime_raw_syscall(
                MICROS_VM_SCRATCH_MAP,
                load_token,
                virtual_addresses[index],
                frames[index],
                permissions[index],
                MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
                0,
                MICROS_SYSCALL_ABI_VM_HANDOFF
            ) != MICROS_SYSCALL_ABI_OK
        ) {
            return false;
        }
        if (index == 0) {
            if (!pm_test_write_child_page(pm_endpoint, scratch)) {
                return false;
            }
        } else {
            size_t byte_index;

            for (byte_index = 0; byte_index < MICROS_SV39_PAGE_SIZE; ++byte_index) {
                scratch[byte_index] = 0;
            }
        }
        if (
            micros_runtime_raw_syscall(
                MICROS_VM_SCRATCH_COMMIT,
                load_token,
                virtual_addresses[index],
                frames[index],
                MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
                0,
                0,
                MICROS_SYSCALL_ABI_VM_HANDOFF
            ) != MICROS_SYSCALL_ABI_OK
        ) {
            return false;
        }
    }
    return true;
}

static void pm_test_build_freeze_plan(
    uint64_t load_token,
    const uint64_t frames[MICROS_PM_TEST_FRAME_COUNT],
    const uint32_t permissions[MICROS_PM_TEST_DATA_FRAME_COUNT],
    struct micros_vm_load_freeze_plan *plan
)
{
    size_t index;

    plan->version = MICROS_VM_LOAD_FREEZE_PLAN_VERSION;
    plan->size = MICROS_VM_LOAD_FREEZE_PLAN_SIZE;
    plan->load_token = load_token;
    for (index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
        plan->table_frames[index] =
            frames[MICROS_PM_TEST_DATA_FRAME_COUNT + index];
    }
    plan->entry = MICROS_APPLICATION_ENTRY;
    plan->stack_pointer = MICROS_APPLICATION_STACK_TOP - 16;
    plan->image_page_count = 3;
    plan->data_page_count = MICROS_PM_TEST_DATA_FRAME_COUNT;
    plan->page_plan_digest = pm_test_page_plan_digest(frames, permissions);
}

static bool pm_test_load_child(
    struct vm_server_context *context,
    const struct micros_ipc_message *request,
    struct micros_ipc_message *reply,
    uint32_t pm_endpoint
)
{
    static const uint32_t permissions[
        MICROS_PM_TEST_DATA_FRAME_COUNT
    ] = {
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_EXECUTE,
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ,
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_WRITE,
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_WRITE,
    };
    struct micros_vm_load_begin_result begin_result;
    struct micros_vm_load_freeze_plan freeze_plan;
    struct micros_vm_load_freeze_result freeze_result;
    uint64_t frames[MICROS_PM_TEST_FRAME_COUNT];
    uint64_t transaction =
        vm_server_read_u64_le(&request->payload[8]);

    vm_server_clear_bytes(&begin_result, sizeof(begin_result));
    vm_server_clear_bytes(&freeze_plan, sizeof(freeze_plan));
    vm_server_clear_bytes(&freeze_result, sizeof(freeze_result));
    if (
        request->type != MICROS_PM_TEST_VM_LOAD
        || request->reply_token == 0
        || vm_server_read_u32_le(&request->payload[0])
            != MICROS_PM_TEST_PROTOCOL_VERSION
        || transaction == 0
        || context->pm_test_load_token != 0
        || context->pm_test_mapping_generation != 0
        || !pm_test_collect_free_frames(frames)
        || micros_runtime_raw_syscall(
            MICROS_VM_LOAD_BEGIN,
            transaction,
            (uintptr_t)&begin_result,
            MICROS_VM_LOAD_BEGIN_RESULT_SIZE,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
        || begin_result.version != MICROS_VM_LOAD_BEGIN_RESULT_VERSION
        || begin_result.size != MICROS_VM_LOAD_BEGIN_RESULT_SIZE
        || begin_result.load_token == 0
        || begin_result.process_slot != MICROS_PM_TEST_RESERVED_PROCESS_SLOT
        || begin_result.process_generation != 1
        || begin_result.reserved0 != 0
        || begin_result.reserved1 != 0
    ) {
        return false;
    }
    if (!pm_test_initialize_pages(
        begin_result.load_token, frames, permissions, pm_endpoint
    )) {
        return false;
    }
    pm_test_build_freeze_plan(
        begin_result.load_token, frames, permissions, &freeze_plan
    );
    if (
        freeze_plan.page_plan_digest == 0
        || micros_runtime_raw_syscall(
            MICROS_VM_LOAD_FREEZE,
            (uintptr_t)&freeze_plan,
            MICROS_VM_LOAD_FREEZE_PLAN_SIZE,
            (uintptr_t)&freeze_result,
            MICROS_VM_LOAD_FREEZE_RESULT_SIZE,
            0,
            0,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
        || freeze_result.version != MICROS_VM_LOAD_FREEZE_RESULT_VERSION
        || freeze_result.size != MICROS_VM_LOAD_FREEZE_RESULT_SIZE
        || freeze_result.mapping_generation == 0
        || !vm_server_bytes_are_zero(
            freeze_result.reserved,
            sizeof(freeze_result.reserved)
        )
    ) {
        return false;
    }
    context->pm_test_load_token = begin_result.load_token;
    context->pm_test_mapping_generation = freeze_result.mapping_generation;
    reply->type = MICROS_PM_TEST_VM_LOADED;
    vm_server_write_u32_le(
        &reply->payload[0],
        MICROS_PM_TEST_PROTOCOL_VERSION
    );
    pm_test_write_u64_le(&reply->payload[8], transaction);
    pm_test_write_u64_le(&reply->payload[16], context->pm_test_load_token);
    pm_test_write_u64_le(
        &reply->payload[24],
        context->pm_test_mapping_generation
    );
    vm_server_write_u32_le(
        &reply->payload[32],
        begin_result.process_slot
    );
    vm_server_write_u32_le(
        &reply->payload[36],
        begin_result.process_generation
    );
    return true;
}

static bool pm_test_commit_child(
    struct vm_server_context *context,
    const struct micros_ipc_message *request,
    struct micros_ipc_message *reply
)
{
    uint64_t load_token =
        vm_server_read_u64_le(&request->payload[8]);
    uint64_t mapping_generation =
        vm_server_read_u64_le(&request->payload[16]);

    if (
        request->type != MICROS_PM_TEST_VM_COMMIT
        || request->reply_token == 0
        || vm_server_read_u32_le(&request->payload[0])
            != MICROS_PM_TEST_PROTOCOL_VERSION
        || load_token == 0
        || load_token != context->pm_test_load_token
        || mapping_generation == 0
        || mapping_generation != context->pm_test_mapping_generation
        || micros_runtime_raw_syscall(
            MICROS_VM_LOAD_COMMIT,
            load_token,
            mapping_generation,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    reply->type = MICROS_PM_TEST_VM_COMMITTED;
    vm_server_write_u32_le(
        &reply->payload[0],
        MICROS_PM_TEST_PROTOCOL_VERSION
    );
    pm_test_write_u64_le(&reply->payload[8], load_token);
    pm_test_write_u64_le(&reply->payload[16], mapping_generation);
    return true;
}

static bool pm_test_release_child(
    struct vm_server_context *context,
    const struct micros_ipc_message *request,
    struct micros_ipc_message *reply
)
{
    uint64_t exit_token =
        vm_server_read_u64_le(&request->payload[8]);
    uint64_t load_token =
        vm_server_read_u64_le(&request->payload[16]);
    uint64_t mapping_generation =
        vm_server_read_u64_le(&request->payload[24]);

    if (
        request->type != MICROS_PM_TEST_VM_EXIT_RELEASE
        || request->reply_token == 0
        || vm_server_read_u32_le(&request->payload[0])
            != MICROS_PM_TEST_PROTOCOL_VERSION
        || exit_token == 0
        || load_token == 0
        || load_token != context->pm_test_load_token
        || mapping_generation == 0
        || mapping_generation != context->pm_test_mapping_generation
        || micros_runtime_raw_syscall(
            MICROS_VM_EXIT_RELEASE,
            exit_token,
            load_token,
            mapping_generation,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    context->pm_test_load_token = 0;
    context->pm_test_mapping_generation = 0;
    reply->type = MICROS_PM_TEST_VM_EXIT_RELEASED;
    vm_server_write_u32_le(
        &reply->payload[0],
        MICROS_PM_TEST_PROTOCOL_VERSION
    );
    pm_test_write_u64_le(&reply->payload[8], exit_token);
    return true;
}

bool pm_test_serve_request(
    struct vm_server_context *context,
    const struct micros_ipc_message *request
)
{
    struct micros_ipc_message reply;
    int pm_index =
        vm_server_find_service(MICROS_PM_TEST_PM_SERVICE_ID);
    bool handled;

    vm_server_clear_bytes(&reply, sizeof(reply));
    if (
        pm_index < 0
        || request->source
            != micros_bootstrap_service_config.services[pm_index].endpoint
    ) {
        return false;
    }
    handled = request->type == MICROS_PM_TEST_VM_LOAD
        ? pm_test_load_child(context, request, &reply, request->source)
        : request->type == MICROS_PM_TEST_VM_COMMIT
            ? pm_test_commit_child(context, request, &reply)
            : request->type == MICROS_PM_TEST_VM_EXIT_RELEASE
                ? pm_test_release_child(context, request, &reply)
                : false;
    return (
        handled
        && micros_runtime_reply(request->reply_token, &reply)
            == MICROS_SYSCALL_ABI_OK
    );
}

#endif
