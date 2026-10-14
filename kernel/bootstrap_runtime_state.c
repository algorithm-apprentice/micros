#include "kernel/bootstrap_runtime_internal.h"

#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"

static struct bootstrap_runtime_context runtime_context;

struct bootstrap_runtime_context *bootstrap_runtime_context(void)
{
    return &runtime_context;
}

void bootstrap_runtime_set_preparation_diagnostic(
    struct bootstrap_runtime_context *runtime,
    enum micros_bootstrap_diagnostic_reason reason,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t detail
)
{
    runtime->preparation_diagnostic.reason = reason;
    runtime->preparation_diagnostic.service_id = service_id;
    runtime->preparation_diagnostic.endpoint = endpoint;
    runtime->preparation_diagnostic.detail = detail;
}

void bootstrap_runtime_clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void bootstrap_runtime_copy_bytes(
    void *destination,
    const void *source,
    size_t size
)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

bool bootstrap_runtime_bytes_equal(
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

_Noreturn void bootstrap_runtime_panic_runtime(const char *reason)
{
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();

    MICROS_PANIC(hart == NULL ? 0 : hart->hardware_id, reason);
}

const struct micros_bootstrap_control_state *
micros_bootstrap_runtime_state(void)
{
    return runtime_context.bootstrap_state.phase
            == MICROS_BOOTSTRAP_PHASE_UNINITIALIZED
        ? NULL
        : &runtime_context.bootstrap_state;
}

struct micros_bootstrap_control_state *
micros_bootstrap_runtime_authoritative_state(void)
{
    return runtime_context.bootstrap_state.phase
            == MICROS_BOOTSTRAP_PHASE_UNINITIALIZED
        ? NULL
        : &runtime_context.bootstrap_state;
}

bool micros_bootstrap_runtime_is_active_controller(
    struct micros_process_handle process
)
{
    return (
        runtime_context.bootstrap_state.phase
            == MICROS_BOOTSTRAP_PHASE_RUNNING
        && runtime_context.bootstrap_state.controller_process.slot
            == process.slot
        && runtime_context.bootstrap_state.controller_process.generation
            == process.generation
    );
}
