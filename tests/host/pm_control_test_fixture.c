#include "tests/host/pm_control_test_internal.h"

#include <stdint.h>
#include <string.h>

#include "micros/endpoint.h"

static struct pm_control_test_context test_context;

struct pm_control_test_context *pm_control_test_context_get(void)
{
    return &test_context;
}

uint32_t pm_control_test_process_endpoint(struct micros_process_handle process)
{
    return (
        process.generation << MICROS_ENDPOINT_SLOT_BITS
    ) | process.slot;
}

bool pm_control_test_initialize_fixture(
    struct micros_kernel_objects *objects,
    struct micros_pm_control_state *state
)
{
    struct micros_process_handle process;
    struct micros_process_handle pm_process;
    struct micros_thread_handle pm_thread;

    memset(objects, 0, sizeof(*objects));
    memset(state, 0, sizeof(*state));
    return (
        micros_kernel_objects_initialize(objects, 1, 1)
            == MICROS_KERNEL_OBJECT_OK
        && micros_process_create_at(objects, 0, &process)
            == MICROS_KERNEL_OBJECT_OK
        && micros_process_create_at(objects, 1, &process)
            == MICROS_KERNEL_OBJECT_OK
        && micros_process_create_at(objects, 2, &pm_process)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_create(objects, pm_process, &pm_thread)
            == MICROS_KERNEL_OBJECT_OK
        && micros_pm_control_state_prepare(
            state,
            3,
            pm_control_test_process_endpoint(pm_process),
            MICROS_PRIVILEGE_PROFILE_PM,
            pm_process,
            pm_thread
        ) == MICROS_PM_CONTROL_OK
        && micros_pm_control_state_validate(state)
            == MICROS_PM_CONTROL_OK
        && micros_pm_control_state_validate_objects(state, objects)
            == MICROS_PM_CONTROL_OK
    );
}

enum micros_pm_control_output_error pm_control_test_fake_output_translate(
    void *context,
    uint64_t user_address,
    size_t requested_size,
    uintptr_t *physical_address,
    size_t *contiguous_size
)
{
    struct pm_control_output_translation_fixture *fixture =
        context;
    size_t call;
    size_t offset = 0;
    size_t index;

    if (
        fixture == NULL
        || physical_address == NULL
        || contiguous_size == NULL
    ) {
        return MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT;
    }
    call = fixture->call_count++;
    if (call == fixture->fault_call) {
        return MICROS_PM_CONTROL_OUTPUT_ERROR_MEMORY_FAULT;
    }
    if (call >= fixture->chunk_count) {
        return MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT;
    }
    for (index = 0; index < call; ++index) {
        offset += fixture->chunk_sizes[index];
    }
    if (
        user_address != fixture->base + offset
        || requested_size
            != sizeof(struct micros_pm_reservation_result) - offset
    ) {
        return MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT;
    }
    *physical_address = (uintptr_t)&fixture->chunks[call][0];
    *contiguous_size = fixture->chunk_sizes[call];
    return MICROS_PM_CONTROL_OUTPUT_OK;
}

bool pm_control_test_output_chunks_are(
    const struct pm_control_output_translation_fixture *fixture,
    unsigned char value
)
{
    size_t chunk;
    size_t index;

    for (chunk = 0; chunk < 3; ++chunk) {
        for (
            index = 0;
            index < sizeof(fixture->chunks[chunk]);
            ++index
        ) {
            if (fixture->chunks[chunk][index] != value) {
                return false;
            }
        }
    }
    return true;
}

void pm_control_test_initialize_output_fixture(
    struct pm_control_output_translation_fixture *fixture
)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->base = UINT64_C(0x4000);
    fixture->fault_call = SIZE_MAX;
    memset(fixture->chunks, 0xa5, sizeof(fixture->chunks));
}
