#include "tty_handoff_test_internal.h"

static struct tty_handoff_test_fixture fixture_state;

struct tty_handoff_test_fixture *tty_handoff_test_fixture(void)
{
    return &fixture_state;
}

const struct micros_bootstrap_binding *
micros_bootstrap_control_find_binding(
    const struct micros_bootstrap_control_state *state,
    uint32_t service_id
)
{
    return micros_bootstrap_control_find_binding_bounded(
        state,
        service_id
    );
}

enum micros_kernel_object_error micros_process_resolve(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle handle,
    const struct micros_process **process
)
{
    struct tty_handoff_test_fixture *fixture = tty_handoff_test_fixture();

    if (
        objects == NULL
        || process == NULL
        || handle.slot != fixture->runtime_process_handle.slot
        || handle.generation != fixture->runtime_process_handle.generation
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STALE;
    }
    *process = &fixture->runtime_process;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_thread_resolve(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    const struct micros_thread **thread
)
{
    struct tty_handoff_test_fixture *fixture = tty_handoff_test_fixture();

    if (
        objects == NULL
        || thread == NULL
        || handle.slot != fixture->runtime_thread_handle.slot
        || handle.generation != fixture->runtime_thread_handle.generation
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STALE;
    }
    *thread = &fixture->runtime_thread;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_endpoint_error micros_endpoint_resolve_internal(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    const struct micros_endpoint_record **record
)
{
    struct tty_handoff_test_fixture *fixture = tty_handoff_test_fixture();

    if (
        registry == NULL
        || objects == NULL
        || record == NULL
        || endpoint != fixture->runtime_endpoint.value
    ) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    *record = &fixture->runtime_endpoint;
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error
micros_privilege_profile_allows_kernel_operation(
    const struct micros_endpoint_registry *registry,
    uint8_t profile_id,
    uint8_t operation
)
{
    return (
        registry != NULL
        && profile_id == MICROS_PRIVILEGE_PROFILE_TTY
        && operation == 3
    )
        ? MICROS_ENDPOINT_OK
        : MICROS_ENDPOINT_ERROR_UNAUTHORIZED;
}
