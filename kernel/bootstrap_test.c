#include "kernel/bootstrap_test.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/bootstrap_test_fixture.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler_core.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "tests/qemu/bootstrap_protocol.h"

#define MICROS_TEST_SCAUSE_INTERRUPT (UINT64_C(1) << 63)
#define MICROS_TEST_EXCEPTION_BREAKPOINT UINT64_C(3)

static const struct micros_bootstrap_expected_service expected_services[] = {
    {
        .service_id = MICROS_BOOTSTRAP_TEST_LAUNCHER_SERVICE_ID,
        .image_id = 101,
        .process_slot = 0,
        .profile_id = 1,
        .service_name = "bootstrap-launcher",
        .profile_name = "BOOTSTRAP_LAUNCHER",
        .role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER,
    },
    {
        .service_id = MICROS_BOOTSTRAP_TEST_FIRST_SERVICE_ID,
        .image_id = 102,
        .process_slot = 1,
        .profile_id = 2,
        .service_name = "bootstrap-probe-a",
        .profile_name = "BOOTSTRAP_PROBE_A",
        .prerequisites = UINT64_C(1),
    },
    {
        .service_id = MICROS_BOOTSTRAP_TEST_LAST_SERVICE_ID,
        .image_id = 103,
        .process_slot = 2,
        .profile_id = 3,
        .service_name = "bootstrap-probe-b",
        .profile_name = "BOOTSTRAP_PROBE_B",
        .prerequisites = UINT64_C(1) << 1,
    },
};

static const struct micros_privilege_profile profiles[] = {
    {
        .id = 1,
        .name = "BOOTSTRAP_LAUNCHER",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_REPLY,
        .kernel_operations =
            MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL,
    },
    {
        .id = 2,
        .name = "BOOTSTRAP_PROBE_A",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL,
        .call_targets = UINT32_C(1) << 1,
    },
    {
        .id = 3,
        .name = "BOOTSTRAP_PROBE_B",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL,
        .call_targets = UINT32_C(1) << 1,
    },
};

static const struct micros_bootstrap_scheduler_policy policies[] = {
    {
        .service_id = MICROS_BOOTSTRAP_TEST_LAUNCHER_SERVICE_ID,
        .priority = 4,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_BOOTSTRAP_TEST_FIRST_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_BOOTSTRAP_TEST_LAST_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
};

static const struct micros_bootstrap_runtime_config runtime_config = {
    .manifest = &micros_bootstrap_test_manifest,
    .expected_services = expected_services,
    .expected_service_count =
        sizeof(expected_services) / sizeof(expected_services[0]),
    .images = micros_bootstrap_test_images,
    .image_count =
        sizeof(micros_bootstrap_test_images)
            / sizeof(micros_bootstrap_test_images[0]),
    .profiles = profiles,
    .profile_count = sizeof(profiles) / sizeof(profiles[0]),
    .policies = policies,
    .policy_count = sizeof(policies) / sizeof(policies[0]),
    .scheduler_preemption_interval =
        MICROS_BOOTSTRAP_TEST_SCHEDULER_INTERVAL,
};

#ifdef MICROS_BUILD_BOOTSTRAP_MANIFEST_PANIC_TEST
static struct micros_bootstrap_manifest cycle_manifest;
static struct micros_bootstrap_expected_service
        cycle_expected_services[MICROS_BOOTSTRAP_TEST_SERVICE_COUNT];
static struct micros_bootstrap_runtime_config cycle_runtime_config;

static void copy_bytes(void *destination, const void *source, size_t size)
{
        uint8_t *output = destination;
        const uint8_t *input = source;
        size_t index;

        for (index = 0; index < size; ++index) {
            output[index] = input[index];
        }
}
#endif

static _Noreturn void test_failure(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
)
{
    MICROS_TRAP_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        reason,
        frame
    );
}

static bool copy_user_bytes(
    struct micros_process_handle process,
    uint64_t address,
    void *output,
    size_t size
)
{
    uint8_t *bytes = output;
    size_t copied = 0;

    while (copied < size) {
        uint64_t physical;
        uint32_t permissions;
        size_t contiguous;
        size_t chunk;
        size_t index;

        if (
            micros_user_address_space_translate(
                process,
                address + copied,
                &physical,
                &permissions,
                &contiguous
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || (
                permissions & MICROS_SV39_PERMISSION_READ
            ) == 0
            || contiguous == 0
        ) {
            return false;
        }
        chunk = size - copied < contiguous
            ? size - copied
            : contiguous;
        for (index = 0; index < chunk; ++index) {
            bytes[copied + index] =
                ((const uint8_t *)(uintptr_t)physical)[index];
        }
        copied += chunk;
    }
    return true;
}

static const struct micros_bootstrap_image *image_for_service(
    uint32_t service_id
)
{
    if (
        service_id < MICROS_BOOTSTRAP_TEST_LAUNCHER_SERVICE_ID
        || service_id > MICROS_BOOTSTRAP_TEST_LAST_SERVICE_ID
    ) {
        return NULL;
    }
    return &micros_bootstrap_test_images[service_id - 1];
}

static bool configuration_is_exact(
    const struct micros_bootstrap_control_state *state,
    const struct micros_bootstrap_binding *binding
)
{
    const struct micros_bootstrap_image *image =
        image_for_service(binding->service_id);
    const struct micros_bootstrap_binding *launcher =
        micros_bootstrap_control_find_binding(
            state,
            MICROS_BOOTSTRAP_TEST_LAUNCHER_SERVICE_ID
        );
    struct micros_bootstrap_service_config config;
    size_t index;

    if (
        image == NULL
        || launcher == NULL
        || !copy_user_bytes(
            binding->process,
            image->config_address,
            &config,
            sizeof(config)
        )
        || config.version != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || config.service_id != binding->service_id
        || config.self_endpoint != binding->endpoint
        || config.launcher_endpoint
            != launcher->endpoint
        || config.service_count != state->entry_count
        || config.manifest_view_address
            != (
                binding->service_id
                    == MICROS_BOOTSTRAP_TEST_LAUNCHER_SERVICE_ID
                ? MICROS_BOOTSTRAP_MANIFEST_VIEW
                : 0
            )
    ) {
        return false;
    }
    for (index = 0; index < config.service_count; ++index) {
        const struct micros_bootstrap_binding *listed =
            micros_bootstrap_control_find_binding(
                state,
                config.services[index].service_id
            );

        if (
            config.services[index].service_id != index + 1
            || listed == NULL
            || config.services[index].endpoint != listed->endpoint
        ) {
            return false;
        }
    }
    for (
        index = config.service_count;
        index < MICROS_BOOTSTRAP_SERVICE_CAPACITY;
        ++index
    ) {
        if (
            config.services[index].service_id != 0
            || config.services[index].endpoint != 0
        ) {
            return false;
        }
    }
    for (
        index = 0;
        index < sizeof(config.reserved) / sizeof(config.reserved[0]);
        ++index
    ) {
        if (config.reserved[index] != 0) {
            return false;
        }
    }
    return true;
}

_Noreturn void micros_bootstrap_test_launch(void)
{
#ifdef MICROS_BUILD_BOOTSTRAP_MANIFEST_PANIC_TEST
    copy_bytes(
        &cycle_manifest,
        &micros_bootstrap_test_manifest,
        sizeof(cycle_manifest)
    );
    copy_bytes(
        cycle_expected_services,
        expected_services,
        sizeof(cycle_expected_services)
    );
    copy_bytes(
        &cycle_runtime_config,
        &runtime_config,
        sizeof(cycle_runtime_config)
    );
    cycle_manifest.entries[2].prerequisites = UINT64_C(1) << 2;
    cycle_expected_services[1].prerequisites = UINT64_C(1) << 2;
    cycle_runtime_config.manifest = &cycle_manifest;
    cycle_runtime_config.expected_services =
        cycle_expected_services;
    micros_bootstrap_runtime_launch(&cycle_runtime_config);
#else
    micros_bootstrap_runtime_launch(&runtime_config);
#endif
}

_Noreturn void micros_bootstrap_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    const struct micros_bootstrap_control_state *state =
        micros_bootstrap_runtime_state();
    const struct micros_endpoint_registry *registry =
        micros_ipc_runtime_registry();
    const struct micros_bootstrap_binding *launcher;
    const struct micros_bootstrap_binding *last;
    const struct micros_endpoint_record *endpoint;
    const struct micros_thread *launcher_thread;
    struct micros_thread_handle current;
    size_t index;

    if (
        hart == NULL
        || frame == NULL
        || state == NULL
        || registry == NULL
        || (
            frame->scause & MICROS_TEST_SCAUSE_INTERRUPT
        ) != 0
        || frame->scause != MICROS_TEST_EXCEPTION_BREAKPOINT
        || frame->sepc
            != micros_bootstrap_test_probe_report_address
        || frame->a0
            != (
                uint64_t
            )(int64_t)MICROS_SYSCALL_ABI_ENDPOINT_CLOSING
        || state->phase != MICROS_BOOTSTRAP_PHASE_SEALED
        || state->controller_process.generation != 0
        || state->controller_thread.generation != 0
        || state->controller_endpoint != 0
        || micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
        || micros_hart_current_thread(
            micros_kernel_object_runtime_registry(),
            micros_kernel_object_runtime_boot_hart_handle(),
            &current
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        test_failure(hart, frame, "bootstrap-test-state");
    }
    launcher = micros_bootstrap_control_find_binding(
        state,
        MICROS_BOOTSTRAP_TEST_LAUNCHER_SERVICE_ID
    );
    last = micros_bootstrap_control_find_binding(
        state,
        MICROS_BOOTSTRAP_TEST_LAST_SERVICE_ID
    );
    if (
        launcher == NULL
        || last == NULL
        || current.slot != last->thread.slot
        || current.generation != last->thread.generation
        || micros_endpoint_resolve_internal(
            registry,
            micros_kernel_object_runtime_registry(),
            launcher->endpoint,
            &endpoint
        ) != MICROS_ENDPOINT_OK
        || endpoint->state != MICROS_ENDPOINT_STATE_SOURCE_ONLY
        || micros_endpoint_resolve_active(
            registry,
            micros_kernel_object_runtime_registry(),
            launcher->endpoint,
            &endpoint
        ) != MICROS_ENDPOINT_ERROR_CLOSING
        || micros_thread_resolve(
            micros_kernel_object_runtime_registry(),
            launcher->thread,
            &launcher_thread
        ) != MICROS_KERNEL_OBJECT_OK
        || launcher_thread->scheduler_assigned
        || launcher_thread->runtime_flags
            != MICROS_THREAD_RTS_INACTIVE
    ) {
        test_failure(hart, frame, "bootstrap-test-seal");
    }
    for (index = 0; index < state->entry_count; ++index) {
        if (
            state->transitions.entries[index].state
                != MICROS_BOOTSTRAP_SERVICE_READY
            || !configuration_is_exact(
                state,
                &state->bindings[index]
            )
        ) {
            test_failure(hart, frame, "bootstrap-test-binding");
        }
    }
    uart_write(
        "MICROS_BOOTSTRAP_TEST_PASS "
        "manifest=immutable order=topological profiles=exact "
        "endpoints=staged readiness=acknowledged authority=revoked\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
