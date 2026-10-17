#include "kernel/tests/grant/grant_test.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "kernel/grant/grant_runtime_internal.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_copy.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_core.h"
#include "micros/kernel_object_runtime.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

enum {
    GRANT_TEST_GRANTOR = 0,
    GRANT_TEST_GRANTEE,
    GRANT_TEST_WRONG,
    GRANT_TEST_PROCESS_COUNT,
};

static const uint64_t TEST_DATA_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x2000);
static const uint64_t TEST_DATA_SECOND_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x3000);
static const uint64_t TEST_READ_ONLY_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x4000);
static const uint64_t TEST_UNMAPPED_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x6000);

static uint64_t data_physical[GRANT_TEST_PROCESS_COUNT][3];
static struct micros_grant_registry grant_failure_snapshot;
static struct micros_endpoint_registry endpoint_failure_snapshot;
static struct micros_kernel_objects object_failure_snapshot;

static void copy_storage(void *destination, const void *source, size_t size)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

static bool storage_matches(
    const void *left,
    const void *right,
    size_t size
)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

static void snapshot_failure_state(
    const struct micros_grant_registry *grant_registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects
)
{
    copy_storage(
        &grant_failure_snapshot,
        grant_registry,
        sizeof(grant_failure_snapshot)
    );
    copy_storage(
        &endpoint_failure_snapshot,
        endpoint_registry,
        sizeof(endpoint_failure_snapshot)
    );
    copy_storage(
        &object_failure_snapshot,
        objects,
        sizeof(object_failure_snapshot)
    );
}

static bool failure_state_matches(
    const struct micros_grant_registry *grant_registry,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *objects
)
{
    return (
        storage_matches(
            &grant_failure_snapshot,
            grant_registry,
            sizeof(grant_failure_snapshot)
        )
        && storage_matches(
            &endpoint_failure_snapshot,
            endpoint_registry,
            sizeof(endpoint_failure_snapshot)
        )
        && storage_matches(
            &object_failure_snapshot,
            objects,
            sizeof(object_failure_snapshot)
        )
    );
}

static void fill_bytes(void *storage, unsigned char value, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = value;
    }
}

static uint64_t physical_for(
    size_t process_index,
    uint64_t virtual_address
)
{
    uint64_t page;
    uint64_t offset;

    if (
        process_index >= GRANT_TEST_PROCESS_COUNT
        || virtual_address < TEST_DATA_ADDRESS
        || virtual_address
            >= TEST_READ_ONLY_ADDRESS + MICROS_SV39_PAGE_SIZE
    ) {
        return 0;
    }
    page =
        (virtual_address - TEST_DATA_ADDRESS)
        / MICROS_SV39_PAGE_SIZE;
    offset = virtual_address % MICROS_SV39_PAGE_SIZE;
    return data_physical[process_index][page] + offset;
}

static void write_pattern(
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char seed
)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        uint64_t physical = physical_for(
            process_index,
            virtual_address + index
        );

        *(unsigned char *)(uintptr_t)physical =
            (unsigned char)(seed + index);
    }
}

static bool pattern_matches(
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char seed
)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        uint64_t physical = physical_for(
            process_index,
            virtual_address + index
        );

        if (
            *(const unsigned char *)(uintptr_t)physical
                != (unsigned char)(seed + index)
        ) {
            return false;
        }
    }
    return true;
}

static bool range_is_value(
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char value
)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        uint64_t physical = physical_for(
            process_index,
            virtual_address + index
        );

        if (*(const unsigned char *)(uintptr_t)physical != value) {
            return false;
        }
    }
    return true;
}

struct grant_mechanism_case {
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    struct micros_endpoint_registry *endpoint_registry;
    struct micros_grant_registry *grant_registry;
    struct micros_process_handle processes[GRANT_TEST_PROCESS_COUNT];
    struct micros_process_handle replacement;
    micros_endpoint_t endpoints[GRANT_TEST_PROCESS_COUNT];
    micros_endpoint_t replacement_endpoint;
    struct micros_grant_cancel_plan cancel_plan;
    micros_grant_t read_grant;
    micros_grant_t write_grant;
    micros_grant_t remote_read_only_grant;
    micros_grant_t unmapped_grant;
    micros_grant_t cross_remote_permission_grant;
    micros_grant_t cross_remote_unmapped_grant;
    micros_grant_t stale_grant;
    size_t baseline_processes;
    size_t baseline_threads;
    uint64_t baseline_owned;
    uint64_t baseline_free;
    micros_endpoint_t old_grantor_endpoint;
    size_t process_index;
    size_t page_index;
    uint64_t failure_stage;
    unsigned char wrong_local_before;
    unsigned char grantor_remote_before;
    unsigned char grantee_local_before;
    unsigned char grantor_read_only_before;
    unsigned char grantee_read_only_before;
    enum micros_frame_ownership_phase saved_phase;
};

static bool grant_test_mechanism_authority_denials(struct grant_mechanism_case *work)
{
    return !(
        micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_WRONG],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->read_grant,
            0,
            TEST_DATA_ADDRESS,
            1
        ) != MICROS_GRANT_ERROR_UNAUTHORIZED
        || micros_grant_copy_to(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->read_grant,
            0,
            TEST_DATA_ADDRESS,
            1
        ) != MICROS_GRANT_ERROR_UNAUTHORIZED
        || micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_WRONG],
            work->read_grant,
            0,
            TEST_DATA_ADDRESS,
            1
        ) != MICROS_GRANT_ERROR_UNAUTHORIZED
    );
}

static bool grant_test_mechanism_range_denials(struct grant_mechanism_case *work)
{
    return !( micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->read_grant,
            0,
            TEST_DATA_ADDRESS,
            MICROS_GRANT_COPY_MAX + 1
        ) != MICROS_GRANT_ERROR_RANGE
        || micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->read_grant,
            257,
            TEST_DATA_ADDRESS,
            1
        ) != MICROS_GRANT_ERROR_RANGE
        || micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->read_grant,
            0,
            MICROS_USER_VIRTUAL_END - 1,
            2
        ) != MICROS_GRANT_ERROR_RANGE
    );
}

static bool grant_test_mechanism_mapping_zero_and_outputs(struct grant_mechanism_case *work)
{
    return !( micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->read_grant,
            0,
            TEST_UNMAPPED_ADDRESS,
            1
        ) != MICROS_GRANT_ERROR_FAULT
        || micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->read_grant,
            0,
            TEST_READ_ONLY_ADDRESS,
            1
        ) != MICROS_GRANT_ERROR_FAULT
        || micros_grant_copy_to(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->remote_read_only_grant,
            0,
            TEST_DATA_ADDRESS,
            1
        ) != MICROS_GRANT_ERROR_FAULT
        || micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->unmapped_grant,
            0,
            TEST_DATA_ADDRESS,
            1
        ) != MICROS_GRANT_ERROR_FAULT
        || micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->read_grant,
            0,
            0,
            0
        ) != MICROS_GRANT_OK
        || *(const unsigned char *)(uintptr_t)physical_for(
            GRANT_TEST_WRONG,
            TEST_DATA_ADDRESS
        ) != work->wrong_local_before
        || *(const unsigned char *)(uintptr_t)physical_for(
            GRANT_TEST_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 64
        ) != work->grantor_remote_before
        || *(const unsigned char *)(uintptr_t)physical_for(
            GRANT_TEST_GRANTEE,
            TEST_DATA_ADDRESS
        ) != work->grantee_local_before
        || *(const unsigned char *)(uintptr_t)physical_for(
            GRANT_TEST_GRANTOR,
            TEST_READ_ONLY_ADDRESS
        ) != work->grantor_read_only_before
        || *(const unsigned char *)(uintptr_t)physical_for(
            GRANT_TEST_GRANTEE,
            TEST_READ_ONLY_ADDRESS
        ) != work->grantee_read_only_before
        || !failure_state_matches(
            work->grant_registry,
            work->endpoint_registry,
            work->objects
        )
    );
}

static bool grant_test_mechanism_prepare_processes(struct grant_mechanism_case *work)
{
    for (
        work->process_index = 0;
        work->process_index < GRANT_TEST_PROCESS_COUNT;
        ++work->process_index
    ) {
        if (
            micros_process_create(
                work->objects,
                &work->processes[work->process_index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_user_address_space_create(
                work->processes[work->process_index]
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || micros_endpoint_reserve(
                work->endpoint_registry,
                work->objects,
                work->processes[work->process_index],
                &work->endpoints[work->process_index]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                work->endpoint_registry,
                work->objects,
                work->processes[work->process_index],
                1
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                work->endpoint_registry,
                work->objects,
                work->endpoints[work->process_index]
            ) != MICROS_ENDPOINT_OK
        ) {
            return false;
        }
        for (work->page_index = 0; work->page_index < 3; ++work->page_index) {
            uint32_t permissions =
                MICROS_SV39_PERMISSION_READ;

            if (work->page_index < 2) {
                permissions |= MICROS_SV39_PERMISSION_WRITE;
            }
            if (
                micros_user_address_space_allocate_page(
                    work->processes[work->process_index],
                    TEST_DATA_ADDRESS
                        + work->page_index * MICROS_SV39_PAGE_SIZE,
                    permissions,
                    &data_physical[work->process_index][work->page_index]
                ) != MICROS_USER_ADDRESS_SPACE_OK
            ) {
                return false;
            }
            fill_bytes(
                (void *)(uintptr_t)
                    data_physical[work->process_index][work->page_index],
                (unsigned char)(UINT8_C(0xc0) + work->process_index),
                MICROS_SV39_PAGE_SIZE
            );
        }
    }
    return true;
}

static bool grant_test_mechanism_create_grants(struct grant_mechanism_case *work)
{
    work->failure_stage = 2;

    if (
        micros_grant_create(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTOR],
            work->endpoints[GRANT_TEST_GRANTEE],
            TEST_DATA_SECOND_ADDRESS - 64,
            256,
            MICROS_GRANT_PERMISSION_READ,
            &work->read_grant
        ) != MICROS_GRANT_OK
        || micros_grant_create(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTOR],
            work->endpoints[GRANT_TEST_GRANTEE],
            TEST_DATA_SECOND_ADDRESS - 64,
            256,
            MICROS_GRANT_PERMISSION_WRITE,
            &work->write_grant
        ) != MICROS_GRANT_OK
        || micros_grant_create(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTOR],
            work->endpoints[GRANT_TEST_GRANTEE],
            TEST_READ_ONLY_ADDRESS,
            64,
            MICROS_GRANT_PERMISSION_WRITE,
            &work->remote_read_only_grant
        ) != MICROS_GRANT_OK
        || micros_grant_create(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTOR],
            work->endpoints[GRANT_TEST_GRANTEE],
            TEST_UNMAPPED_ADDRESS,
            64,
            MICROS_GRANT_PERMISSION_READ,
            &work->unmapped_grant
        ) != MICROS_GRANT_OK
        || micros_grant_create(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTOR],
            work->endpoints[GRANT_TEST_GRANTEE],
            TEST_READ_ONLY_ADDRESS - 32,
            64,
            MICROS_GRANT_PERMISSION_WRITE,
            &work->cross_remote_permission_grant
        ) != MICROS_GRANT_OK
        || micros_grant_create(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTOR],
            work->endpoints[GRANT_TEST_GRANTEE],
            TEST_READ_ONLY_ADDRESS
                + MICROS_SV39_PAGE_SIZE - 32,
            64,
            MICROS_GRANT_PERMISSION_READ,
            &work->cross_remote_unmapped_grant
        ) != MICROS_GRANT_OK
    ) {
        return false;
    }
    return true;
}

static bool grant_test_mechanism_cross_from(struct grant_mechanism_case *work)
{
    write_pattern(
        GRANT_TEST_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 64,
        128,
        UINT8_C(0x10)
    );
    fill_bytes(
        (void *)(uintptr_t)data_physical[GRANT_TEST_GRANTEE][0],
        UINT8_C(0xcc),
        MICROS_SV39_PAGE_SIZE
    );
    fill_bytes(
        (void *)(uintptr_t)data_physical[GRANT_TEST_GRANTEE][1],
        UINT8_C(0xcc),
        MICROS_SV39_PAGE_SIZE
    );
    if (
        micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->read_grant,
            0,
            TEST_DATA_SECOND_ADDRESS - 32,
            128
        ) != MICROS_GRANT_OK
        || !pattern_matches(
            GRANT_TEST_GRANTEE,
            TEST_DATA_SECOND_ADDRESS - 32,
            128,
            UINT8_C(0x10)
        )
        || !range_is_value(
            GRANT_TEST_GRANTEE,
            TEST_DATA_SECOND_ADDRESS - 33,
            1,
            UINT8_C(0xcc)
        )
        || !range_is_value(
            GRANT_TEST_GRANTEE,
            TEST_DATA_SECOND_ADDRESS + 96,
            1,
            UINT8_C(0xcc)
        )
    ) {
        return false;
    }
    return true;
}

static bool grant_test_mechanism_cross_to_and_local(struct grant_mechanism_case *work)
{
    work->failure_stage = 3;

    write_pattern(
        GRANT_TEST_GRANTEE,
        TEST_DATA_SECOND_ADDRESS - 48,
        96,
        UINT8_C(0x50)
    );
    fill_bytes(
        (void *)(uintptr_t)data_physical[GRANT_TEST_GRANTOR][0],
        UINT8_C(0xdd),
        MICROS_SV39_PAGE_SIZE
    );
    fill_bytes(
        (void *)(uintptr_t)data_physical[GRANT_TEST_GRANTOR][1],
        UINT8_C(0xdd),
        MICROS_SV39_PAGE_SIZE
    );
    if (
        micros_grant_copy_to(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->write_grant,
            16,
            TEST_DATA_SECOND_ADDRESS - 48,
            96
        ) != MICROS_GRANT_OK
        || !pattern_matches(
            GRANT_TEST_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 48,
            96,
            UINT8_C(0x50)
        )
        || micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->read_grant,
            64,
            TEST_DATA_ADDRESS + UINT64_C(0x100),
            32
        ) != MICROS_GRANT_OK
        || !pattern_matches(
            GRANT_TEST_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x100),
            32,
            UINT8_C(0x80)
        )
        || !range_is_value(
            GRANT_TEST_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x0ff),
            1,
            UINT8_C(0xcc)
        )
        || !range_is_value(
            GRANT_TEST_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x120),
            1,
            UINT8_C(0xcc)
        )
    ) {
        return false;
    }
    return true;
}

static void grant_test_mechanism_prepare_fault_canaries(struct grant_mechanism_case *work)
{
    work->failure_stage = 4;

    write_pattern(
        GRANT_TEST_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 64,
        64,
        UINT8_C(0x91)
    );
    write_pattern(
        GRANT_TEST_GRANTEE,
        TEST_READ_ONLY_ADDRESS - 32,
        64,
        UINT8_C(0xa1)
    );
    write_pattern(
        GRANT_TEST_GRANTEE,
        TEST_DATA_ADDRESS + UINT64_C(0x200),
        64,
        UINT8_C(0xb1)
    );
    write_pattern(
        GRANT_TEST_GRANTOR,
        TEST_READ_ONLY_ADDRESS - 32,
        64,
        UINT8_C(0xc1)
    );
    write_pattern(
        GRANT_TEST_GRANTOR,
        TEST_READ_ONLY_ADDRESS + MICROS_SV39_PAGE_SIZE - 32,
        32,
        UINT8_C(0xd1)
    );
    write_pattern(
        GRANT_TEST_GRANTEE,
        TEST_DATA_ADDRESS + UINT64_C(0x300),
        64,
        UINT8_C(0xe1)
    );
}

static bool grant_test_mechanism_cross_faults(struct grant_mechanism_case *work)
{
    snapshot_failure_state(
        work->grant_registry,
        work->endpoint_registry,
        work->objects
    );
    if (
        micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->read_grant,
            0,
            TEST_READ_ONLY_ADDRESS - 32,
            64
        ) != MICROS_GRANT_ERROR_FAULT
        || !pattern_matches(
            GRANT_TEST_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 64,
            64,
            UINT8_C(0x91)
        )
        || !pattern_matches(
            GRANT_TEST_GRANTEE,
            TEST_READ_ONLY_ADDRESS - 32,
            64,
            UINT8_C(0xa1)
        )
        || micros_grant_copy_to(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->cross_remote_permission_grant,
            0,
            TEST_DATA_ADDRESS + UINT64_C(0x200),
            64
        ) != MICROS_GRANT_ERROR_FAULT
        || !pattern_matches(
            GRANT_TEST_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x200),
            64,
            UINT8_C(0xb1)
        )
        || !pattern_matches(
            GRANT_TEST_GRANTOR,
            TEST_READ_ONLY_ADDRESS - 32,
            64,
            UINT8_C(0xc1)
        )
        || micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->cross_remote_unmapped_grant,
            0,
            TEST_DATA_ADDRESS + UINT64_C(0x300),
            64
        ) != MICROS_GRANT_ERROR_FAULT
        || !pattern_matches(
            GRANT_TEST_GRANTOR,
            TEST_READ_ONLY_ADDRESS + MICROS_SV39_PAGE_SIZE - 32,
            32,
            UINT8_C(0xd1)
        )
        || !pattern_matches(
            GRANT_TEST_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x300),
            64,
            UINT8_C(0xe1)
        )
    ) {
        return false;
    }
    return true;
}

static bool grant_test_mechanism_unmapped_local_fault(struct grant_mechanism_case *work)
{
    write_pattern(
        GRANT_TEST_GRANTEE,
        TEST_READ_ONLY_ADDRESS + MICROS_SV39_PAGE_SIZE - 32,
        32,
        UINT8_C(0x21)
    );
    write_pattern(
        GRANT_TEST_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 64,
        64,
        UINT8_C(0x31)
    );
    if (
        micros_grant_copy_to(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->write_grant,
            0,
            TEST_READ_ONLY_ADDRESS + MICROS_SV39_PAGE_SIZE - 32,
            64
        ) != MICROS_GRANT_ERROR_FAULT
        || !pattern_matches(
            GRANT_TEST_GRANTEE,
            TEST_READ_ONLY_ADDRESS + MICROS_SV39_PAGE_SIZE - 32,
            32,
            UINT8_C(0x21)
        )
        || !pattern_matches(
            GRANT_TEST_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 64,
            64,
            UINT8_C(0x31)
        )
        || !failure_state_matches(
            work->grant_registry,
            work->endpoint_registry,
            work->objects
        )
    ) {
        return false;
    }
    return true;
}

static bool grant_test_mechanism_authority_range_faults(struct grant_mechanism_case *work)
{
    work->wrong_local_before =
        *(const unsigned char *)(uintptr_t)physical_for(
            GRANT_TEST_WRONG,
            TEST_DATA_ADDRESS
        );
    work->grantor_remote_before =
        *(const unsigned char *)(uintptr_t)physical_for(
            GRANT_TEST_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 64
        );
    work->grantee_local_before =
        *(const unsigned char *)(uintptr_t)physical_for(
            GRANT_TEST_GRANTEE,
            TEST_DATA_ADDRESS
        );
    work->grantor_read_only_before =
        *(const unsigned char *)(uintptr_t)physical_for(
            GRANT_TEST_GRANTOR,
            TEST_READ_ONLY_ADDRESS
        );
    work->grantee_read_only_before =
        *(const unsigned char *)(uintptr_t)physical_for(
            GRANT_TEST_GRANTEE,
            TEST_READ_ONLY_ADDRESS
        );
    if (
        !grant_test_mechanism_authority_denials(work)
        || !grant_test_mechanism_range_denials(work)
        || !grant_test_mechanism_mapping_zero_and_outputs(work)
    ) {
        return false;
    }
    return true;
}

static bool grant_test_mechanism_phase_and_stale_grant(struct grant_mechanism_case *work)
{
    work->failure_stage = 5;

    work->saved_phase = work->ledger->phase;
    ((struct micros_frame_ownership *)(uintptr_t)work->ledger)->phase =
        MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF;
    if (
        micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->read_grant,
            0,
            0,
            0
        ) != MICROS_GRANT_OK
    ) {
        ((struct micros_frame_ownership *)(uintptr_t)work->ledger)->phase =
            work->saved_phase;
        return false;
    }
    ((struct micros_frame_ownership *)(uintptr_t)work->ledger)->phase =
        work->saved_phase;
    if (
        micros_frame_ownership_runtime_validate(work->objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || !failure_state_matches(
            work->grant_registry,
            work->endpoint_registry,
            work->objects
        )
    ) {
        return false;
    }

    if (
        micros_grant_create(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTOR],
            work->endpoints[GRANT_TEST_GRANTEE],
            TEST_DATA_ADDRESS,
            64,
            MICROS_GRANT_PERMISSION_READ,
            &work->stale_grant
        ) != MICROS_GRANT_OK
        || micros_grant_revoke(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTOR],
            work->stale_grant
        ) != MICROS_GRANT_OK
    ) {
        return false;
    }
    write_pattern(
        GRANT_TEST_GRANTOR,
        TEST_DATA_ADDRESS,
        64,
        UINT8_C(0x61)
    );
    write_pattern(
        GRANT_TEST_GRANTEE,
        TEST_DATA_ADDRESS + UINT64_C(0x400),
        64,
        UINT8_C(0x41)
    );
    snapshot_failure_state(
        work->grant_registry,
        work->endpoint_registry,
        work->objects
    );
    if (
        micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR],
            work->stale_grant,
            0,
            TEST_DATA_ADDRESS + UINT64_C(0x400),
            64
        ) != MICROS_GRANT_ERROR_STALE_GRANT
        || !pattern_matches(
            GRANT_TEST_GRANTOR,
            TEST_DATA_ADDRESS,
            64,
            UINT8_C(0x61)
        )
        || !pattern_matches(
            GRANT_TEST_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x400),
            64,
            UINT8_C(0x41)
        )
        || !failure_state_matches(
            work->grant_registry,
            work->endpoint_registry,
            work->objects
        )
    ) {
        return false;
    }
    return true;
}

static bool grant_test_mechanism_stale_endpoint(struct grant_mechanism_case *work)
{
    write_pattern(
        GRANT_TEST_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 64,
        64,
        UINT8_C(0x71)
    );
    write_pattern(
        GRANT_TEST_GRANTEE,
        TEST_DATA_ADDRESS + UINT64_C(0x400),
        64,
        UINT8_C(0x81)
    );
    snapshot_failure_state(
        work->grant_registry,
        work->endpoint_registry,
        work->objects
    );
    if (
        micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->endpoints[GRANT_TEST_GRANTOR]
                + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS),
            work->read_grant,
            0,
            TEST_DATA_ADDRESS + UINT64_C(0x400),
            64
        ) != MICROS_GRANT_ERROR_DEAD_ENDPOINT
        || !pattern_matches(
            GRANT_TEST_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 64,
            64,
            UINT8_C(0x71)
        )
        || !pattern_matches(
            GRANT_TEST_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x400),
            64,
            UINT8_C(0x81)
        )
        || !failure_state_matches(
            work->grant_registry,
            work->endpoint_registry,
            work->objects
        )
    ) {
        return false;
    }
    return true;
}

static bool grant_test_mechanism_cancel_close_reuse(struct grant_mechanism_case *work)
{
    work->failure_stage = 6;

    work->old_grantor_endpoint = work->endpoints[GRANT_TEST_GRANTOR];
    if (
        micros_grant_prepare_endpoint_cancel(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->endpoints[GRANT_TEST_GRANTOR],
            &work->cancel_plan
        ) != MICROS_GRANT_OK
        || micros_ipc_endpoint_close(
            work->endpoint_registry,
            work->objects,
            work->endpoints[GRANT_TEST_GRANTOR]
        ) != MICROS_IPC_OK
        || micros_grant_commit_endpoint_cancel(
            work->grant_registry,
            &work->cancel_plan
        ) != MICROS_GRANT_OK
        || micros_user_address_space_destroy(
            work->processes[GRANT_TEST_GRANTOR]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_process_release(
            work->objects,
            work->processes[GRANT_TEST_GRANTOR]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(work->objects, &work->replacement)
            != MICROS_KERNEL_OBJECT_OK
        || work->replacement.slot != work->processes[GRANT_TEST_GRANTOR].slot
        || work->replacement.generation
            != work->processes[GRANT_TEST_GRANTOR].generation + 1
        || micros_endpoint_reserve(
            work->endpoint_registry,
            work->objects,
            work->replacement,
            &work->replacement_endpoint
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_install_profile(
            work->endpoint_registry,
            work->objects,
            work->replacement,
            1
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            work->endpoint_registry,
            work->objects,
            work->replacement_endpoint
        ) != MICROS_ENDPOINT_OK
        || work->replacement_endpoint == work->old_grantor_endpoint
    ) {
        return false;
    }
    write_pattern(
        GRANT_TEST_GRANTEE,
        TEST_DATA_ADDRESS + UINT64_C(0x500),
        64,
        UINT8_C(0x51)
    );
    snapshot_failure_state(
        work->grant_registry,
        work->endpoint_registry,
        work->objects
    );
    if (
        micros_grant_copy_from(
            work->grant_registry,
            work->endpoint_registry,
            work->objects,
            work->processes[GRANT_TEST_GRANTEE],
            work->replacement_endpoint,
            work->read_grant,
            0,
            TEST_DATA_ADDRESS + UINT64_C(0x500),
            64
        ) != MICROS_GRANT_ERROR_STALE_GRANT
        || !pattern_matches(
            GRANT_TEST_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x500),
            64,
            UINT8_C(0x51)
        )
        || !failure_state_matches(
            work->grant_registry,
            work->endpoint_registry,
            work->objects
        )
    ) {
        return false;
    }
    work->processes[GRANT_TEST_GRANTOR] = work->replacement;
    work->endpoints[GRANT_TEST_GRANTOR] = work->replacement_endpoint;
    return true;
}

static bool grant_test_mechanism_cleanup_baseline(struct grant_mechanism_case *work)
{
    work->failure_stage = 7;

    for (
        work->process_index = 0;
        work->process_index < GRANT_TEST_PROCESS_COUNT;
        ++work->process_index
    ) {
        struct micros_grant_cancel_plan plan;

        if (
            micros_grant_prepare_endpoint_cancel(
                work->grant_registry,
                work->endpoint_registry,
                work->objects,
                work->endpoints[work->process_index],
                &plan
            ) != MICROS_GRANT_OK
            || micros_ipc_endpoint_close(
                work->endpoint_registry,
                work->objects,
                work->endpoints[work->process_index]
            ) != MICROS_IPC_OK
            || micros_grant_commit_endpoint_cancel(
                work->grant_registry,
                &plan
            ) != MICROS_GRANT_OK
            || (
                work->objects->processes[work->processes[work->process_index].slot]
                    .address_space_root != 0
                && micros_user_address_space_destroy(
                    work->processes[work->process_index]
                ) != MICROS_USER_ADDRESS_SPACE_OK
            )
            || micros_process_release(
                work->objects,
                work->processes[work->process_index]
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
    }
    if (
        work->grant_registry->active_count != 0
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_kernel_objects_validate(work->objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_validate(work->objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || work->objects->live_process_count != work->baseline_processes
        || work->objects->live_thread_count != work->baseline_threads
        || work->ledger->owned_frame_count != work->baseline_owned
        || work->ledger->allocator->free_frame_count != work->baseline_free
    ) {
        return false;
    }
    return true;
}

bool micros_grant_runtime_run_self_test(void)
{
    static const struct micros_privilege_profile profile = {
        .id = 1,
        .name = "GRANT_COPY_TEST",
        .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
    };
    struct grant_mechanism_case work;
    uintptr_t saved_status;
    bool passed = false;
    work.failure_stage = 1;

    saved_status = riscv_irq_save();
    work.objects = micros_kernel_object_runtime_test_registry();
    work.ledger = micros_frame_ownership_runtime_ledger();
    if (work.objects == NULL || work.ledger == NULL) {
        goto done;
    }
    work.baseline_processes = work.objects->live_process_count;
    work.baseline_threads = work.objects->live_thread_count;
    work.baseline_owned = work.ledger->owned_frame_count;
    work.baseline_free = work.ledger->allocator->free_frame_count;
    if (
        micros_ipc_runtime_registry() != NULL
        || micros_grant_runtime_registry() != NULL
        || micros_ipc_runtime_initialize(&profile, 1)
            != MICROS_ENDPOINT_OK
        || micros_grant_runtime_initialize() != MICROS_GRANT_OK
        || (
            work.endpoint_registry =
                micros_ipc_runtime_authoritative_registry()
        ) == NULL
        || (
            work.grant_registry =
                micros_grant_runtime_authoritative_registry()
        ) == NULL
    ) {
        goto done;
    }
    if (!grant_test_mechanism_prepare_processes(&work)) {
        goto done;
    }
    if (!grant_test_mechanism_create_grants(&work)) {
        goto done;
    }
    if (!grant_test_mechanism_cross_from(&work)) {
        goto done;
    }
    if (!grant_test_mechanism_cross_to_and_local(&work)) {
        goto done;
    }
    grant_test_mechanism_prepare_fault_canaries(&work);
    if (!grant_test_mechanism_cross_faults(&work)) {
        goto done;
    }
    if (!grant_test_mechanism_unmapped_local_fault(&work)) {
        goto done;
    }
    if (!grant_test_mechanism_authority_range_faults(&work)) {
        goto done;
    }
    if (!grant_test_mechanism_phase_and_stale_grant(&work)) {
        goto done;
    }
    if (!grant_test_mechanism_stale_endpoint(&work)) {
        goto done;
    }
    if (!grant_test_mechanism_cancel_close_reuse(&work)) {
        goto done;
    }
    if (!grant_test_mechanism_cleanup_baseline(&work)) {
        goto done;
    }
    passed = true;

done:
    if (!passed) {
        uart_write("MICROS_TEST_FAILURE grant-stage=");
        uart_write_hex64(work.failure_stage);
        uart_write("\n");
        uart_flush();
    }
    riscv_irq_restore(saved_status);
    return passed;
}
