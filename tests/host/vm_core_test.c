#include "servers/vm/vm_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/sv39.h"

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

enum {
    TEST_MANAGED_FRAME_COUNT = 48,
    TEST_FIRST_FREE_FRAME = 6,
    TEST_VM_SERVICE_ID = 2,
    TEST_VM_ENDPOINT = 0x00001001,
    TEST_BOOT_ADDRESS_SPACE_COUNT = 2,
};

_Alignas(MICROS_VM_BOOT_INFO_ALIGNMENT)
static struct micros_vm_boot_info boot_info;
static struct micros_vm_state state;
static uint8_t executable[MICROS_APPLICATION_EXECUTABLE_SIZE_MAX];
static uint8_t request[MICROS_SPAWN_REQUEST_SIZE_MAX];
static uint8_t scratch_page[MICROS_APPLICATION_STACK_PAGE_SIZE];

static void write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static bool bytes_are_zero(const void *storage, size_t size)
{
    const uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static uint64_t frame_address(size_t frame_index)
{
    return UINT64_C(0x80001000)
        + frame_index * MICROS_FRAME_SIZE;
}

static bool initialize_boot_info(size_t managed_frame_count)
{
    struct micros_vm_boot_summary summary;
    size_t index;

    if (
        managed_frame_count < TEST_FIRST_FREE_FRAME
        || managed_frame_count > MICROS_VM_MAX_MANAGED_FRAMES
    ) {
        return false;
    }
    memset(&boot_info, 0, sizeof(boot_info));
    boot_info.header.version = MICROS_VM_BOOT_INFO_VERSION;
    boot_info.header.header_size = MICROS_VM_BOOT_HEADER_SIZE;
    boot_info.header.total_size = MICROS_VM_BOOT_INFO_SIZE;
    boot_info.header.physical_range_size =
        MICROS_VM_PHYSICAL_RANGE_SIZE;
    boot_info.header.managed_range_size =
        MICROS_VM_MANAGED_RANGE_SIZE;
    boot_info.header.address_space_entry_size =
        MICROS_VM_ADDRESS_SPACE_SIZE;
    boot_info.header.mapping_entry_size = MICROS_VM_MAPPING_SIZE;
    boot_info.header.memory_range_capacity =
        MICROS_VM_MAX_MEMORY_RANGES;
    boot_info.header.memory_range_count = 1;
    boot_info.header.reserved_range_capacity =
        MICROS_VM_MAX_RESERVED_RANGES;
    boot_info.header.reserved_range_count = 1;
    boot_info.header.managed_range_capacity =
        MICROS_VM_MAX_MANAGED_RANGES;
    boot_info.header.managed_range_count = 1;
    boot_info.header.address_space_capacity =
        MICROS_VM_MAX_STATIC_ADDRESS_SPACES;
    boot_info.header.address_space_count =
        TEST_BOOT_ADDRESS_SPACE_COUNT;
    boot_info.header.mapping_capacity = MICROS_VM_MAX_STATIC_MAPPINGS;
    boot_info.header.mapping_count = 3;
    boot_info.header.frame_state_capacity =
        MICROS_VM_MAX_MANAGED_FRAMES;
    boot_info.header.frame_state_size = 1;
    boot_info.header.service_id = TEST_VM_SERVICE_ID;
    boot_info.header.self_endpoint = TEST_VM_ENDPOINT;
    boot_info.header.ownership_phase =
        MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP;
    boot_info.header.managed_frame_count = managed_frame_count;
    boot_info.header.free_frame_count =
        managed_frame_count - TEST_FIRST_FREE_FRAME;
    boot_info.header.kernel_frame_count = 3;
    boot_info.header.vm_self_wired_frame_count = 2;
    boot_info.header.service_wired_frame_count = 1;

    boot_info.memory_ranges[0] = (struct micros_vm_physical_range){
        .base = UINT64_C(0x80000000),
        .size = (managed_frame_count + 1) * MICROS_FRAME_SIZE,
    };
    boot_info.reserved_ranges[0] = (struct micros_vm_physical_range){
        .base = UINT64_C(0x80000000),
        .size = MICROS_FRAME_SIZE,
    };
    boot_info.managed_ranges[0] = (struct micros_vm_managed_range){
        .base = UINT64_C(0x80001000),
        .frame_count = managed_frame_count,
        .frame_index = 0,
    };
    boot_info.address_spaces[0] =
        (struct micros_vm_address_space){
            .service_id = TEST_VM_SERVICE_ID,
            .endpoint = TEST_VM_ENDPOINT,
            .process_generation = 1,
            .process_slot = 1,
            .mapping_count = 2,
            .mapping_index = 0,
            .root_physical_address = frame_address(0),
        };
    boot_info.address_spaces[1] =
        (struct micros_vm_address_space){
            .service_id = 1,
            .endpoint = UINT32_C(0x00001000),
            .process_generation = 1,
            .process_slot = 0,
            .mapping_count = 1,
            .mapping_index = 2,
            .root_physical_address = frame_address(3),
        };
    boot_info.mappings[0] = (struct micros_vm_mapping){
        .process_generation = 1,
        .process_slot = 1,
        .permissions =
            MICROS_VM_PERMISSION_READ | MICROS_VM_PERMISSION_EXECUTE,
        .role = MICROS_VM_MAPPING_SELF_WIRED,
        .virtual_address = MICROS_USER_VIRTUAL_BASE,
        .physical_address = frame_address(1),
    };
    boot_info.mappings[1] = (struct micros_vm_mapping){
        .process_generation = 1,
        .process_slot = 1,
        .permissions =
            MICROS_VM_PERMISSION_READ | MICROS_VM_PERMISSION_WRITE,
        .role = MICROS_VM_MAPPING_SELF_WIRED,
        .virtual_address = MICROS_USER_VIRTUAL_BASE + MICROS_FRAME_SIZE,
        .physical_address = frame_address(2),
    };
    boot_info.mappings[2] = (struct micros_vm_mapping){
        .process_generation = 1,
        .process_slot = 0,
        .permissions =
            MICROS_VM_PERMISSION_READ | MICROS_VM_PERMISSION_EXECUTE,
        .role = MICROS_VM_MAPPING_SERVICE_WIRED,
        .virtual_address = MICROS_USER_VIRTUAL_BASE,
        .physical_address = frame_address(4),
    };
    boot_info.frame_states[0] = MICROS_VM_FRAME_KERNEL;
    boot_info.frame_states[1] = MICROS_VM_FRAME_SELF_WIRED;
    boot_info.frame_states[2] = MICROS_VM_FRAME_SELF_WIRED;
    boot_info.frame_states[3] = MICROS_VM_FRAME_KERNEL;
    boot_info.frame_states[4] = MICROS_VM_FRAME_SERVICE_WIRED;
    boot_info.frame_states[5] = MICROS_VM_FRAME_KERNEL;
    for (
        index = TEST_FIRST_FREE_FRAME;
        index < managed_frame_count;
        ++index
    ) {
        boot_info.frame_states[index] = MICROS_VM_FRAME_FREE;
    }
    return micros_vm_boot_finalize(&boot_info, &summary)
        == MICROS_VM_BOOT_OK;
}

static enum micros_vm_core_error initialize_state(void)
{
    return micros_vm_state_initialize(
        &state,
        &boot_info,
        TEST_VM_SERVICE_ID,
        TEST_VM_ENDPOINT,
        TEST_BOOT_ADDRESS_SPACE_COUNT
    );
}

static size_t build_request(void)
{
    static const uint8_t path[] = "/bin/app";
    static const uint8_t argument[] = "app";
    static const uint8_t environment[] = "A=B";
    size_t cursor = MICROS_SPAWN_REQUEST_HEADER_SIZE;

    memset(request, 0, sizeof(request));
    write_u32_le(&request[0], MICROS_SPAWN_REQUEST_VERSION);
    write_u32_le(&request[4], MICROS_SPAWN_REQUEST_HEADER_SIZE);
    write_u16_le(
        &request[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET],
        1
    );
    write_u16_le(
        &request[MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET],
        1
    );
    write_u16_le(
        &request[MICROS_SPAWN_PATH_REFERENCE_OFFSET],
        (uint16_t)cursor
    );
    write_u16_le(
        &request[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 2],
        sizeof(path)
    );
    memcpy(&request[cursor], path, sizeof(path));
    cursor += sizeof(path);
    write_u16_le(
        &request[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET],
        (uint16_t)cursor
    );
    write_u16_le(
        &request[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET + 2],
        sizeof(argument)
    );
    memcpy(&request[cursor], argument, sizeof(argument));
    cursor += sizeof(argument);
    write_u16_le(
        &request[MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET],
        (uint16_t)cursor
    );
    write_u16_le(
        &request[MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET + 2],
        sizeof(environment)
    );
    memcpy(&request[cursor], environment, sizeof(environment));
    cursor += sizeof(environment);
    write_u32_le(&request[8], (uint32_t)cursor);
    return cursor;
}

static size_t build_elf(
    uint64_t text_pages,
    uint64_t rodata_pages,
    uint64_t data_pages
)
{
    static const uint32_t flags[3] = {
        UINT32_C(0x5),
        UINT32_C(0x4),
        UINT32_C(0x6),
    };
    uint64_t page_counts[3] = {
        text_pages,
        rodata_pages,
        data_pages,
    };
    uint64_t virtual_address = MICROS_APPLICATION_ENTRY;
    size_t index;

    memset(executable, 0, sizeof(executable));
    executable[0] = 0x7f;
    executable[1] = 'E';
    executable[2] = 'L';
    executable[3] = 'F';
    executable[4] = 2;
    executable[5] = 1;
    executable[6] = 1;
    write_u16_le(&executable[16], 2);
    write_u16_le(&executable[18], 243);
    write_u32_le(&executable[20], 1);
    write_u64_le(&executable[24], MICROS_APPLICATION_ENTRY);
    write_u64_le(&executable[32], 64);
    write_u16_le(&executable[52], 64);
    write_u16_le(&executable[54], 56);
    write_u16_le(&executable[56], 3);

    for (index = 0; index < 3; ++index) {
        size_t offset = 64 + index * 56;
        uint64_t file_offset = index * UINT64_C(4096);
        uint64_t file_size = index == 2
            ? UINT64_C(1024)
            : UINT64_C(4096);
        uint64_t memory_size =
            page_counts[index] * UINT64_C(4096);

        write_u32_le(&executable[offset], 1);
        write_u32_le(&executable[offset + 4], flags[index]);
        write_u64_le(&executable[offset + 8], file_offset);
        write_u64_le(&executable[offset + 16], virtual_address);
        write_u64_le(&executable[offset + 24], virtual_address);
        write_u64_le(&executable[offset + 32], file_size);
        write_u64_le(&executable[offset + 40], memory_size);
        write_u64_le(&executable[offset + 48], 4096);
        virtual_address += memory_size;
    }
    for (index = 232; index < 8192 + 1024; ++index) {
        executable[index] = (uint8_t)(index * 17 + 3);
    }
    return 8192 + 1024;
}

static struct micros_application_stack_values stack_values(
    uint64_t pid
)
{
    const struct micros_application_stack_values values = {
        .pm_endpoint = UINT32_C(0x00001003),
        .vfs_endpoint = UINT32_C(0x00001004),
        .pid = pid,
    };

    return values;
}

static bool prepare_minimal_load(void)
{
    struct micros_application_stack_values values = stack_values(7);
    size_t executable_size = build_elf(1, 1, 1);
    size_t request_size = build_request();

    return micros_vm_load_prepare(
        &state,
        UINT64_C(0x1111),
        values.pid,
        executable,
        executable_size,
        request,
        request_size,
        &values
    ) == MICROS_VM_CORE_OK;
}

static uint64_t digest_byte(uint64_t digest, uint8_t byte)
{
    digest ^= byte;
    return digest * UINT64_C(1099511628211);
}

static uint64_t digest_u32(uint64_t digest, uint32_t value)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        digest = digest_byte(
            digest,
            (uint8_t)(value >> (index * 8))
        );
    }
    return digest;
}

static uint64_t digest_u64(uint64_t digest, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        digest = digest_byte(
            digest,
            (uint8_t)(value >> (index * 8))
        );
    }
    return digest;
}

static uint64_t expected_page_digest(void)
{
    const struct micros_vm_load_transaction *transaction =
        &state.transaction;
    uint64_t digest = UINT64_C(14695981039346656037);
    size_t index;

    digest = digest_u32(digest, transaction->elf_plan.page_count);
    digest = digest_u32(digest, transaction->page_count);
    for (index = 0; index < transaction->page_count; ++index) {
        uint64_t virtual_address = index
                < transaction->elf_plan.page_count
            ? transaction->elf_plan.pages[index].virtual_address
            : MICROS_APPLICATION_STACK_BASE;
        uint32_t permissions = index
                < transaction->elf_plan.page_count
            ? transaction->elf_plan.pages[index].permissions
            : MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE;

        digest = digest_u64(digest, virtual_address);
        digest = digest_u64(
            digest,
            transaction->data_frames[index]
        );
        digest = digest_u32(digest, permissions);
        digest = digest_u32(digest, 0);
    }
    return digest;
}

static bool run_to_frozen(uint64_t mapping_generation)
{
    struct micros_vm_load_page_action action;
    uint32_t page_index;

    if (
        !prepare_minimal_load()
        || micros_vm_load_bind(
            &state,
            (struct micros_process_handle){2, 7},
            UINT64_C(0x2222)
        ) != MICROS_VM_CORE_OK
    ) {
        return false;
    }
    for (
        page_index = 0;
        page_index < MICROS_APPLICATION_RESIDENT_PAGE_MAX;
        ++page_index
    ) {
        if (
            micros_vm_load_next_page(&state, &action)
                != MICROS_VM_CORE_OK
        ) {
            break;
        }
        if (
            action.page_index != page_index
            || micros_vm_load_scratch_mapped(&state, page_index)
                != MICROS_VM_CORE_OK
            || micros_vm_load_fill_scratch(
                &state,
                page_index,
                scratch_page,
                sizeof(scratch_page)
            ) != MICROS_VM_CORE_OK
            || micros_vm_load_scratch_committed(&state, page_index)
                != MICROS_VM_CORE_OK
        ) {
            return false;
        }
    }
    return (
        page_index == state.transaction.page_count
        && micros_vm_load_frozen(&state, mapping_generation)
            == MICROS_VM_CORE_OK
    );
}

static bool test_record_abi(void)
{
    return (
        sizeof(struct micros_vm_mapping_entry) == 24
        && sizeof(struct micros_vm_mapping_record) == 512
        && sizeof(state.mapping_records) == 32768
        && sizeof(state.initial_free_bitmap) == 32768
        && sizeof(struct micros_vm_state)
            <= 50 * MICROS_APPLICATION_STACK_PAGE_SIZE
        && offsetof(struct micros_vm_mapping_record, entries) == 32
        && MICROS_VM_MAPPING_ENTRY_CAPACITY == 20
    );
}

static bool test_boot_import(void)
{
    struct micros_vm_state sentinel;

    EXPECT_TRUE(initialize_boot_info(TEST_MANAGED_FRAME_COUNT));
    memset(&state, 0xa5, sizeof(state));
    sentinel = state;
    boot_info.header.digest ^= 1;
    EXPECT_TRUE(
        initialize_state()
            == MICROS_VM_CORE_ERROR_BOOT
        && memcmp(&state, &sentinel, sizeof(state)) == 0
    );
    boot_info.header.digest ^= 1;
    EXPECT_TRUE(
        micros_vm_state_initialize(
            &state,
            &boot_info,
            TEST_VM_SERVICE_ID + 1,
            TEST_VM_ENDPOINT,
            TEST_BOOT_ADDRESS_SPACE_COUNT
        ) == MICROS_VM_CORE_ERROR_BOOT
        && memcmp(&state, &sentinel, sizeof(state)) == 0
        && micros_vm_state_initialize(
            &state,
            &boot_info,
            TEST_VM_SERVICE_ID,
            TEST_VM_ENDPOINT + 1,
            TEST_BOOT_ADDRESS_SPACE_COUNT
        ) == MICROS_VM_CORE_ERROR_BOOT
        && memcmp(&state, &sentinel, sizeof(state)) == 0
        && micros_vm_state_initialize(
            &state,
            &boot_info,
            TEST_VM_SERVICE_ID,
            TEST_VM_ENDPOINT,
            TEST_BOOT_ADDRESS_SPACE_COUNT + 1
        ) == MICROS_VM_CORE_ERROR_BOOT
        && memcmp(&state, &sentinel, sizeof(state)) == 0
        && initialize_state()
            == MICROS_VM_CORE_OK
        && state.phase == MICROS_VM_LOADER_IDLE
        && state.boot_info == &boot_info
        && state.boot_summary.managed_frame_count
            == TEST_MANAGED_FRAME_COUNT
        && state.initial_free_bitmap[0]
            == (
                (
                (UINT64_C(1) << TEST_MANAGED_FRAME_COUNT) - 1
                ) & ~(
                    (UINT64_C(1) << TEST_FIRST_FREE_FRAME) - 1
                )
            )
        && bytes_are_zero(
            &state.initial_free_bitmap[1],
            sizeof(state.initial_free_bitmap)
                - sizeof(state.initial_free_bitmap[0])
        )
        && bytes_are_zero(
            state.mapping_records,
            sizeof(state.mapping_records)
        )
        && micros_vm_state_validate(&state) == MICROS_VM_CORE_OK
    );
    return true;
}

static bool test_load_lifecycle(void)
{
    struct micros_vm_freeze_plan freeze_plan;
    struct micros_vm_exit_action exit_action;
    struct micros_vm_load_page_action action;
    const struct micros_vm_mapping_record *record;
    uint32_t page_index;
    size_t index;

    EXPECT_TRUE(
        initialize_boot_info(TEST_MANAGED_FRAME_COUNT)
        && initialize_state()
            == MICROS_VM_CORE_OK
        && prepare_minimal_load()
        && state.phase == MICROS_VM_LOADER_INPUT_COPIED
        && state.transaction.page_count == 4
    );
    for (index = 0; index < MICROS_VM_PAGE_TABLE_FRAME_COUNT; ++index) {
        EXPECT_TRUE(
            state.transaction.table_frames[index]
                == frame_address(TEST_FIRST_FREE_FRAME + index)
            && boot_info.frame_states[TEST_FIRST_FREE_FRAME + index]
                == MICROS_VM_LOCAL_FRAME_FREE
        );
    }
    for (index = 0; index < state.transaction.page_count; ++index) {
        EXPECT_TRUE(
            state.transaction.data_frames[index]
                == frame_address(
                    TEST_FIRST_FREE_FRAME
                        + MICROS_VM_PAGE_TABLE_FRAME_COUNT
                        + index
                )
        );
    }
    EXPECT_TRUE(
        micros_vm_load_bind(
            &state,
            (struct micros_process_handle){2, 7},
            UINT64_C(0x2222)
        ) == MICROS_VM_CORE_OK
        && state.phase == MICROS_VM_LOADER_LOADING
    );
    record = &state.mapping_records[2];
    EXPECT_TRUE(
        record->load_token == UINT64_C(0x2222)
        && record->semantic_pid == 7
        && record->process_generation == 7
        && record->process_slot == 2
        && record->phase == MICROS_VM_MAPPING_RECORD_LOADING
        && record->entry_count == 8
    );
    memset(&freeze_plan, 0xa5, sizeof(freeze_plan));
    {
        struct micros_vm_freeze_plan sentinel = freeze_plan;

        EXPECT_TRUE(
            micros_vm_load_freeze_plan(&state, &freeze_plan)
                == MICROS_VM_CORE_ERROR_STATE
            && memcmp(
                &freeze_plan,
                &sentinel,
                sizeof(freeze_plan)
            ) == 0
        );
    }
    for (index = 0; index < MICROS_VM_PAGE_TABLE_FRAME_COUNT; ++index) {
        EXPECT_TRUE(
            boot_info.frame_states[TEST_FIRST_FREE_FRAME + index]
                == MICROS_VM_LOCAL_FRAME_LOAD_RESERVED
        );
    }

    for (page_index = 0; page_index < 4; ++page_index) {
        memset(&action, 0xa5, sizeof(action));
        EXPECT_TRUE(
            micros_vm_load_next_page(&state, &action)
                == MICROS_VM_CORE_OK
            && action.page_index == page_index
            && action.physical_address
                == state.transaction.data_frames[page_index]
            && action.kind
                == (
                    page_index < 3
                        ? MICROS_VM_LOAD_PAGE_IMAGE
                        : MICROS_VM_LOAD_PAGE_STACK
                )
            && micros_vm_load_scratch_mapped(&state, page_index)
                == MICROS_VM_CORE_OK
            && boot_info.frame_states[
                TEST_FIRST_FREE_FRAME
                    + MICROS_VM_PAGE_TABLE_FRAME_COUNT
                    + page_index
            ] == MICROS_VM_LOCAL_FRAME_TRANSFERABLE
            && micros_vm_load_scratch_committed(&state, page_index)
                == MICROS_VM_CORE_ERROR_STATE
            && state.transaction.completed_page_count == page_index
            && state.transaction.scratch_page == page_index + 1
            && state.transaction.scratch_filled == 0
            && micros_vm_load_fill_scratch(
                &state,
                page_index,
                scratch_page,
                sizeof(scratch_page)
            ) == MICROS_VM_CORE_OK
            && state.transaction.scratch_filled == 1
        );
        if (page_index == 0) {
            EXPECT_TRUE(memcmp(
                scratch_page,
                executable,
                sizeof(scratch_page)
            ) == 0);
        } else if (page_index == 2) {
            EXPECT_TRUE(
                memcmp(
                    scratch_page,
                    &executable[8192],
                    1024
                ) == 0
                && bytes_are_zero(
                    &scratch_page[1024],
                    sizeof(scratch_page) - 1024
                )
            );
        } else if (page_index == 3) {
            EXPECT_TRUE(
                scratch_page[
                    state.transaction.stack_plan.argc_offset
                ] == 1
            );
        }
        EXPECT_TRUE(
            micros_vm_load_scratch_committed(&state, page_index)
                == MICROS_VM_CORE_OK
            && state.transaction.completed_page_count
                == page_index + 1
            && state.transaction.scratch_page == 0
            && state.transaction.scratch_filled == 0
        );
    }
    EXPECT_TRUE(
        micros_vm_load_freeze_plan(&state, &freeze_plan)
            == MICROS_VM_CORE_OK
        && freeze_plan.load_token == UINT64_C(0x2222)
        && freeze_plan.entry == MICROS_APPLICATION_ENTRY
        && freeze_plan.stack_pointer
            == state.transaction.stack_plan.stack_pointer
        && freeze_plan.image_page_count == 3
        && freeze_plan.data_page_count == 4
        && freeze_plan.page_plan_digest == expected_page_digest()
        && micros_vm_load_frozen(&state, UINT64_C(0x3333))
            == MICROS_VM_CORE_OK
        && state.phase == MICROS_VM_LOADER_FROZEN
        && record->phase == MICROS_VM_MAPPING_RECORD_FROZEN
        && record->mapping_generation == UINT64_C(0x3333)
        && micros_vm_state_validate(&state) == MICROS_VM_CORE_OK
    );
    for (index = 0; index < MICROS_VM_PAGE_TABLE_FRAME_COUNT; ++index) {
        EXPECT_TRUE(
            boot_info.frame_states[TEST_FIRST_FREE_FRAME + index]
                == MICROS_VM_LOCAL_FRAME_PROCESS_PAGE_TABLE
        );
    }
    for (index = 0; index < 4; ++index) {
        EXPECT_TRUE(
            boot_info.frame_states[
                TEST_FIRST_FREE_FRAME
                    + MICROS_VM_PAGE_TABLE_FRAME_COUNT
                    + index
            ] == MICROS_VM_LOCAL_FRAME_PROCESS_WIRED
        );
    }
    EXPECT_TRUE(
        micros_vm_load_committed(
            &state,
            UINT64_C(0x2222),
            UINT64_C(0x3333),
            7
        ) == MICROS_VM_CORE_OK
        && state.phase == MICROS_VM_LOADER_IDLE
        && record->phase == MICROS_VM_MAPPING_RECORD_ACTIVE
        && bytes_are_zero(
            &state.transaction,
            sizeof(state.transaction)
        )
        && bytes_are_zero(
            state.executable_bytes,
            sizeof(state.executable_bytes)
        )
        && bytes_are_zero(
            state.request_bytes,
            sizeof(state.request_bytes)
        )
        && micros_vm_state_validate(&state) == MICROS_VM_CORE_OK
    );
    {
        struct micros_application_stack_values values =
            stack_values(8);
        size_t executable_size = build_elf(1, 1, 1);
        size_t request_size = build_request();

        EXPECT_TRUE(
            micros_vm_load_prepare(
                &state,
                UINT64_C(0x4444),
                values.pid,
                executable,
                executable_size,
                request,
                request_size,
                &values
            ) == MICROS_VM_CORE_OK
            && state.transaction.table_frames[0]
                == frame_address(14)
            && micros_vm_load_bind(
                &state,
                (struct micros_process_handle){3, 9},
                UINT64_C(0x5555)
            ) == MICROS_VM_CORE_OK
            && micros_vm_load_aborted(
                &state,
                UINT64_C(0x5555),
                0
            ) == MICROS_VM_CORE_OK
            && state.mapping_records[2].phase
                == MICROS_VM_MAPPING_RECORD_ACTIVE
            && bytes_are_zero(
                &state.mapping_records[3],
                sizeof(state.mapping_records[3])
            )
            && micros_vm_state_validate(&state)
                == MICROS_VM_CORE_OK
        );
    }
    memset(&exit_action, 0xa5, sizeof(exit_action));
    {
        struct micros_vm_exit_action sentinel = exit_action;

        EXPECT_TRUE(
            micros_vm_exit_release_begin(
                &state,
                UINT64_C(0x2223),
                UINT64_C(0x3333),
                7,
                &exit_action
            ) == MICROS_VM_CORE_ERROR_STATE
            && memcmp(
                &exit_action,
                &sentinel,
                sizeof(exit_action)
            ) == 0
            && state.mapping_records[2].phase
                == MICROS_VM_MAPPING_RECORD_ACTIVE
        );
    }
    EXPECT_TRUE(
        micros_vm_exit_release_begin(
            &state,
            UINT64_C(0x2222),
            UINT64_C(0x3333),
            7,
            &exit_action
        ) == MICROS_VM_CORE_OK
        && exit_action.process_slot == 2
        && exit_action.process_generation == 7
        && state.phase == MICROS_VM_LOADER_EXIT_RELEASING
        && record->phase
            == MICROS_VM_MAPPING_RECORD_EXIT_RELEASING
        && micros_vm_exit_release_complete(&state, &exit_action)
            == MICROS_VM_CORE_OK
        && state.phase == MICROS_VM_LOADER_IDLE
        && bytes_are_zero(record, sizeof(*record))
        && micros_vm_state_validate(&state) == MICROS_VM_CORE_OK
    );
    for (index = TEST_FIRST_FREE_FRAME; index < 14; ++index) {
        EXPECT_TRUE(
            boot_info.frame_states[index] == MICROS_VM_LOCAL_FRAME_FREE
        );
    }
    return true;
}

static bool test_abort_paths(void)
{
    struct micros_vm_load_page_action action;
    size_t index;

    EXPECT_TRUE(
        initialize_boot_info(TEST_MANAGED_FRAME_COUNT)
        && initialize_state()
            == MICROS_VM_CORE_OK
        && prepare_minimal_load()
        && micros_vm_load_aborted(&state, 0, 0)
            == MICROS_VM_CORE_OK
        && state.phase == MICROS_VM_LOADER_IDLE
        && bytes_are_zero(
            state.mapping_records,
            sizeof(state.mapping_records)
        )
        && prepare_minimal_load()
        && micros_vm_load_bind(
            &state,
            (struct micros_process_handle){2, 7},
            UINT64_C(0x2222)
        ) == MICROS_VM_CORE_OK
        && micros_vm_load_next_page(&state, &action)
            == MICROS_VM_CORE_OK
        && micros_vm_load_scratch_mapped(&state, 0)
            == MICROS_VM_CORE_OK
        && micros_vm_load_fill_scratch(
            &state,
            0,
            scratch_page,
            sizeof(scratch_page)
        ) == MICROS_VM_CORE_OK
        && micros_vm_load_scratch_committed(&state, 0)
            == MICROS_VM_CORE_OK
        && micros_vm_load_next_page(&state, &action)
            == MICROS_VM_CORE_OK
        && micros_vm_load_scratch_mapped(&state, 1)
            == MICROS_VM_CORE_OK
        && micros_vm_load_aborted(
            &state,
            UINT64_C(0x2222),
            0
        ) == MICROS_VM_CORE_OK
        && state.phase == MICROS_VM_LOADER_IDLE
        && bytes_are_zero(
            &state.mapping_records[2],
            sizeof(state.mapping_records[2])
        )
        && micros_vm_state_validate(&state) == MICROS_VM_CORE_OK
    );
    for (index = TEST_FIRST_FREE_FRAME; index < 14; ++index) {
        EXPECT_TRUE(
            boot_info.frame_states[index] == MICROS_VM_LOCAL_FRAME_FREE
        );
    }

    EXPECT_TRUE(
        initialize_boot_info(TEST_MANAGED_FRAME_COUNT)
        && initialize_state()
            == MICROS_VM_CORE_OK
        && run_to_frozen(UINT64_C(0x3333))
        && micros_vm_load_aborted(
            &state,
            UINT64_C(0x2223),
            UINT64_C(0x3333)
        ) == MICROS_VM_CORE_ERROR_STATE
        && state.phase == MICROS_VM_LOADER_FROZEN
        && micros_vm_load_aborted(
            &state,
            UINT64_C(0x2222),
            UINT64_C(0x3333)
        ) == MICROS_VM_CORE_OK
        && state.phase == MICROS_VM_LOADER_IDLE
        && bytes_are_zero(
            &state.mapping_records[2],
            sizeof(state.mapping_records[2])
        )
        && micros_vm_state_validate(&state) == MICROS_VM_CORE_OK
    );
    for (index = TEST_FIRST_FREE_FRAME; index < 14; ++index) {
        EXPECT_TRUE(
            boot_info.frame_states[index] == MICROS_VM_LOCAL_FRAME_FREE
        );
    }
    return true;
}

static bool test_capacity_and_invariants(void)
{
    struct micros_application_stack_values values = stack_values(7);
    struct micros_vm_load_page_action action;
    struct micros_vm_load_page_action sentinel;
    size_t executable_size;
    size_t request_size;

    EXPECT_TRUE(
        initialize_boot_info(TEST_FIRST_FREE_FRAME + 7)
        && initialize_state()
            == MICROS_VM_CORE_OK
    );
    executable_size = build_elf(1, 1, 1);
    request_size = build_request();
    EXPECT_TRUE(
        micros_vm_load_prepare(
            &state,
            UINT64_C(0x1111),
            values.pid,
            executable,
            executable_size,
            request,
            request_size,
            &values
        ) == MICROS_VM_CORE_ERROR_NO_SPACE
        && state.phase == MICROS_VM_LOADER_IDLE
        && bytes_are_zero(
            state.mapping_records,
            sizeof(state.mapping_records)
        )
        && micros_vm_state_validate(&state) == MICROS_VM_CORE_OK
    );

    EXPECT_TRUE(
        initialize_boot_info(TEST_MANAGED_FRAME_COUNT)
        && initialize_state()
            == MICROS_VM_CORE_OK
    );
    executable_size = build_elf(5, 5, 5);
    request_size = build_request();
    EXPECT_TRUE(
        micros_vm_load_prepare(
            &state,
            UINT64_C(0x1111),
            values.pid,
            executable,
            executable_size,
            request,
            request_size,
            &values
        ) == MICROS_VM_CORE_OK
        && state.transaction.page_count
            == MICROS_APPLICATION_RESIDENT_PAGE_MAX
        && micros_vm_load_bind(
            &state,
            (struct micros_process_handle){2, 7},
            UINT64_C(0x2222)
        ) == MICROS_VM_CORE_OK
        && state.mapping_records[2].entry_count
            == MICROS_VM_MAPPING_ENTRY_CAPACITY
        && micros_vm_state_validate(&state) == MICROS_VM_CORE_OK
    );
    state.mapping_records[2].entries[4].physical_address =
        state.mapping_records[2].entries[5].physical_address;
    EXPECT_TRUE(
        micros_vm_state_validate(&state)
            == MICROS_VM_CORE_ERROR_INVARIANT
    );

    EXPECT_TRUE(
        initialize_boot_info(TEST_MANAGED_FRAME_COUNT)
        && initialize_state()
            == MICROS_VM_CORE_OK
    );
    memset(&sentinel, 0xa5, sizeof(sentinel));
    action = sentinel;
    EXPECT_TRUE(
        micros_vm_load_next_page(&state, &action)
            == MICROS_VM_CORE_ERROR_STATE
        && memcmp(&action, &sentinel, sizeof(action)) == 0
    );
    return true;
}

static bool test_staged_inputs_and_immutable_database(void)
{
    struct micros_application_stack_values values = stack_values(7);
    size_t executable_size;
    size_t request_size;

    EXPECT_TRUE(
        initialize_boot_info(TEST_MANAGED_FRAME_COUNT)
        && initialize_state()
            == MICROS_VM_CORE_OK
    );
    executable_size = build_elf(1, 1, 1);
    request_size = build_request();
    memcpy(state.executable_bytes, executable, executable_size);
    memcpy(state.request_bytes, request, request_size);
    EXPECT_TRUE(
        micros_vm_load_prepare(
            &state,
            UINT64_C(0x1111),
            values.pid,
            state.executable_bytes,
            executable_size,
            state.request_bytes,
            request_size,
            &values
        ) == MICROS_VM_CORE_OK
        && micros_vm_load_aborted(&state, 0, 0)
            == MICROS_VM_CORE_OK
        && bytes_are_zero(
            state.executable_bytes,
            sizeof(state.executable_bytes)
        )
        && bytes_are_zero(
            state.request_bytes,
            sizeof(state.request_bytes)
        )
    );

    executable_size = build_elf(1, 1, 1);
    request_size = build_request();
    executable[0] = 0;
    memcpy(state.executable_bytes, executable, executable_size);
    memcpy(state.request_bytes, request, request_size);
    EXPECT_TRUE(
        micros_vm_load_prepare(
            &state,
            UINT64_C(0x1111),
            values.pid,
            state.executable_bytes,
            executable_size,
            state.request_bytes,
            request_size,
            &values
        ) == MICROS_VM_CORE_ERROR_EXECUTABLE
        && state.phase == MICROS_VM_LOADER_IDLE
        && bytes_are_zero(
            state.executable_bytes,
            sizeof(state.executable_bytes)
        )
        && bytes_are_zero(
            state.request_bytes,
            sizeof(state.request_bytes)
        )
        && micros_vm_state_validate(&state) == MICROS_VM_CORE_OK
    );

    boot_info.mappings[0].physical_address += MICROS_FRAME_SIZE;
    EXPECT_TRUE(
        micros_vm_state_validate(&state)
            == MICROS_VM_CORE_ERROR_INVARIANT
    );
    boot_info.mappings[0].physical_address -= MICROS_FRAME_SIZE;
    EXPECT_TRUE(micros_vm_state_validate(&state) == MICROS_VM_CORE_OK);
    state.initial_free_bitmap[0] ^= UINT64_C(1) << TEST_FIRST_FREE_FRAME;
    EXPECT_TRUE(
        micros_vm_state_validate(&state)
            == MICROS_VM_CORE_ERROR_INVARIANT
    );
    return true;
}

int main(void)
{
    if (
        !test_record_abi()
        || !test_boot_import()
        || !test_load_lifecycle()
        || !test_abort_paths()
        || !test_capacity_and_invariants()
        || !test_staged_inputs_and_immutable_database()
    ) {
        return 1;
    }
    puts("vm core tests passed");
    return 0;
}
