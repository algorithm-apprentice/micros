#ifndef MICROS_TESTS_HOST_VM_CORE_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_VM_CORE_TEST_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "servers/vm/vm_core.h"

enum {
    TEST_MANAGED_FRAME_COUNT = 48,
    TEST_FIRST_FREE_FRAME = 6,
    TEST_VM_SERVICE_ID = 2,
    TEST_VM_ENDPOINT = 0x00001001,
    TEST_BOOT_ADDRESS_SPACE_COUNT = 2,
};

struct vm_core_test_context {
    _Alignas(MICROS_VM_BOOT_INFO_ALIGNMENT)
    struct micros_vm_boot_info boot_info;
    struct micros_vm_state state;
    uint8_t executable[MICROS_APPLICATION_EXECUTABLE_SIZE_MAX];
    uint8_t request[MICROS_SPAWN_REQUEST_SIZE_MAX];
    uint8_t scratch_page[MICROS_APPLICATION_STACK_PAGE_SIZE];
};

struct vm_core_test_context *vm_core_test_context_get(void);
void vm_core_test_write_u16_le(uint8_t *bytes, uint16_t value);
void vm_core_test_write_u32_le(uint8_t *bytes, uint32_t value);
void vm_core_test_write_u64_le(uint8_t *bytes, uint64_t value);
bool vm_core_test_bytes_are_zero(const void *storage, size_t size);
uint64_t vm_core_test_frame_address(size_t frame_index);
bool vm_core_test_initialize_boot_info(
    struct vm_core_test_context *context,
    size_t managed_frame_count
);
enum micros_vm_core_error vm_core_test_initialize_state(
    struct vm_core_test_context *context
);
size_t vm_core_test_build_request(
    struct vm_core_test_context *context
);
size_t vm_core_test_build_elf(
    struct vm_core_test_context *context,
    uint64_t text_pages,
    uint64_t rodata_pages,
    uint64_t data_pages
);
struct micros_application_stack_values vm_core_test_stack_values(
    uint64_t pid
);
bool vm_core_test_prepare_minimal_load(
    struct vm_core_test_context *context
);
uint64_t vm_core_test_expected_page_digest(
    const struct vm_core_test_context *context
);
bool vm_core_test_run_to_frozen(
    struct vm_core_test_context *context,
    uint64_t mapping_generation
);

bool test_record_abi(struct vm_core_test_context *context);
bool test_boot_import(struct vm_core_test_context *context);
bool test_load_lifecycle(struct vm_core_test_context *context);
bool test_abort_paths(struct vm_core_test_context *context);
bool test_capacity_and_invariants(
    struct vm_core_test_context *context
);
bool test_staged_inputs_and_immutable_database(
    struct vm_core_test_context *context
);

#endif
