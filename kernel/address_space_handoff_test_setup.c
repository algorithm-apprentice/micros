#include "kernel/address_space_handoff_test_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "kernel/grant_runtime_internal.h"
#include "kernel/grant_syscall.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/memory/user_address_space_internal.h"
#include "kernel/vm_snapshot.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_copy.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_buffer.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/sv39.h"
#include "micros/syscall_abi.h"
#include "micros/tty.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

static const struct micros_privilege_profile profiles[] = {
    {
        .id = 1,
        .name = "HANDOFF_TEST",
        .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_TTY,
        .name = "TTY",
        .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        .kernel_operations = MICROS_KERNEL_OPERATION_TTY_CONTROL,
    },
};

static const uint32_t page_permissions[HANDOFF_PAGE_COUNT] = {
    MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE,
    MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE,
    MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE,
    MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE,
    MICROS_SV39_PERMISSION_READ,
};

bool handoff_test_setup_runtime(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = 1;
    fixture->objects = micros_kernel_object_runtime_test_registry();
    fixture->ledger = micros_frame_ownership_runtime_ledger();
    if (
        fixture->objects == NULL
        || fixture->ledger == NULL
        || micros_ipc_runtime_registry() != NULL
        || micros_grant_runtime_registry() != NULL
        || micros_ipc_runtime_initialize(
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        )
            != MICROS_ENDPOINT_OK
        || micros_grant_runtime_initialize() != MICROS_GRANT_OK
        || (
            fixture->endpoint_registry =
                micros_ipc_runtime_authoritative_registry()
        ) == NULL
        || (
            fixture->grant_registry =
                micros_grant_runtime_authoritative_registry()
        ) == NULL
    ) {
        return false;
    }


    return true;
}
static bool create_reserved_processes(
    struct micros_address_space_handoff_test_context *fixture
)
{
    size_t process_index;

    for (
        process_index = 0;
        process_index < HANDOFF_RESERVED_PROCESS_COUNT;
        ++process_index
    ) {
        if (
            micros_process_create(
                fixture->objects,
                &fixture->reserved_processes[process_index]
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
    }
    return true;
}

static bool create_live_processes(
    struct micros_address_space_handoff_test_context *fixture
)
{
    size_t process_index;
    size_t page_index;

    for (
        process_index = 0;
        process_index < HANDOFF_PROCESS_COUNT;
        ++process_index
    ) {
        if (
            micros_process_create(
                fixture->objects,
                &fixture->processes[process_index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_user_address_space_create(
                fixture->processes[process_index]
            ) != MICROS_USER_ADDRESS_SPACE_OK
        ) {
            return false;
        }
        for (
            page_index = 0;
            page_index < HANDOFF_PAGE_COUNT;
            ++page_index
        ) {
            if (
                micros_user_address_space_allocate_page(
                    fixture->processes[process_index],
                    TEST_CODE_ADDRESS
                        + page_index * MICROS_SV39_PAGE_SIZE,
                    page_permissions[page_index],
                    &fixture->user_physical[process_index][page_index]
                ) != MICROS_USER_ADDRESS_SPACE_OK
            ) {
                return false;
            }
            handoff_test_fill_bytes(fixture,
                (void *)(uintptr_t)
                    fixture->user_physical[process_index][page_index],
                (unsigned char)(UINT8_C(0xb0) + process_index),
                MICROS_SV39_PAGE_SIZE
            );
        }
        if (
            micros_thread_create(
                fixture->objects,
                fixture->processes[process_index],
                &fixture->threads[process_index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                fixture->endpoint_registry,
                fixture->objects,
                fixture->processes[process_index],
                &fixture->endpoints[process_index]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                fixture->endpoint_registry,
                fixture->objects,
                fixture->processes[process_index],
                1
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                fixture->endpoint_registry,
                fixture->objects,
                fixture->endpoints[process_index]
            ) != MICROS_ENDPOINT_OK
        ) {
            return false;
        }
    }
    return true;
}

static bool prepare_tty_bootstrap_fixture(
    struct micros_address_space_handoff_test_context *fixture
)
{
    if (
        fixture->processes[HANDOFF_GRANTEE].slot != MICROS_TTY_PROCESS_SLOT
        || micros_user_address_space_allocate_page(
            fixture->processes[HANDOFF_GRANTEE],
            TEST_TTY_SPARE_ADDRESS,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE,
            &fixture->tty_spare_physical
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            fixture->processes[HANDOFF_GRANTEE],
            TEST_TTY_STACK_ADDRESS,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE,
            &fixture->tty_stack_physical
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return false;
    }
    handoff_test_fill_bytes(fixture,
        (void *)(uintptr_t)fixture->tty_spare_physical,
        UINT8_C(0xc3),
        MICROS_SV39_PAGE_SIZE
    );
    handoff_test_fill_bytes(fixture,
        (void *)(uintptr_t)fixture->tty_stack_physical,
        UINT8_C(0x3c),
        MICROS_SV39_PAGE_SIZE
    );
    handoff_test_fill_bytes(fixture, &fixture->tty_binding, 0, sizeof(fixture->tty_binding));
    handoff_test_fill_bytes(fixture, &fixture->tty_entry, 0, sizeof(fixture->tty_entry));
    fixture->tty_binding.service_id = MICROS_TTY_SERVICE_ID;
    fixture->tty_binding.process = fixture->processes[HANDOFF_GRANTEE];
    fixture->tty_binding.thread = fixture->threads[HANDOFF_GRANTEE];
    fixture->tty_binding.root = fixture->objects->processes[
        fixture->processes[HANDOFF_GRANTEE].slot
    ].address_space_root;
    fixture->tty_binding.endpoint = fixture->endpoints[HANDOFF_GRANTEE];
    fixture->tty_binding.scheduler_priority =
        MICROS_SCHEDULER_PRIORITY_DEFAULT_USER;
    fixture->tty_binding.scheduler_preemptible = true;
    fixture->tty_binding.scheduler_quantum_counter_ticks = UINT64_MAX;
    fixture->tty_entry.service_id = MICROS_TTY_SERVICE_ID;
    fixture->tty_entry.image_id = 104;
    fixture->tty_entry.process_slot = MICROS_TTY_PROCESS_SLOT;
    fixture->tty_entry.profile_id = MICROS_PRIVILEGE_PROFILE_TTY;
    fixture->tty_entry.role_flags = MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER;
    fixture->tty_entry.irq_source = MICROS_TTY_UART_IRQ_SOURCE;
    fixture->tty_entry.device_base = MICROS_TTY_UART_PHYSICAL_BASE;
    fixture->tty_entry.device_length = MICROS_TTY_UART_MAPPED_LENGTH;
    fixture->tty_bootstrap.entry_count = 1;
    fixture->tty_bootstrap.plan.console_service_id = MICROS_TTY_SERVICE_ID;
    handoff_test_copy_bytes(fixture,
        &fixture->tty_bootstrap.bindings[0],
        &fixture->tty_binding,
        sizeof(fixture->tty_binding)
    );
    handoff_test_copy_bytes(fixture,
        &fixture->tty_bootstrap.manifest.entries[0],
        &fixture->tty_entry,
        sizeof(fixture->tty_entry)
    );
    fixture->tty_bootstrap.transitions.entry_count = 1;
    fixture->tty_bootstrap.transitions.entries[0] =
        (struct micros_bootstrap_runtime_entry){
            .service_id = MICROS_TTY_SERVICE_ID,
            .state = MICROS_BOOTSTRAP_SERVICE_PREPARED,
            .endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_RESERVED,
        };
    if (
        micros_tty_handoff_runtime_reset()
            != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_runtime_prepare(
            &fixture->tty_binding,
            &fixture->tty_entry
        ) != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_runtime_bind_bootstrap(
            &fixture->tty_bootstrap
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        return false;
    }
    return true;
}

static bool create_empty_process(
    struct micros_address_space_handoff_test_context *fixture
)
{
    if (
        micros_process_create(fixture->objects, &fixture->empty_process)
            != MICROS_KERNEL_OBJECT_OK
        || fixture->objects->processes[fixture->empty_process.slot].address_space_root != 0
        || fixture->objects->processes[fixture->empty_process.slot].live_thread_count != 0
        || fixture->objects->processes[fixture->empty_process.slot].primary_endpoint
            != MICROS_PROCESS_ENDPOINT_NONE
    ) {
        return false;
    }
    return true;
}

bool handoff_test_setup_processes(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = 2;

    return (
        create_reserved_processes(fixture)
        && create_live_processes(fixture)
        && prepare_tty_bootstrap_fixture(fixture)
        && create_empty_process(fixture)
    );
}
bool handoff_test_setup_execution_and_grants(
    struct micros_address_space_handoff_test_context *fixture
)
{
    size_t process_index;
    fixture->failure_stage = 3;
    fixture->payload_size =
        (uintptr_t)micros_address_space_handoff_payload_end
        - (uintptr_t)micros_address_space_handoff_payload_start;
    if (fixture->payload_size == 0 || fixture->payload_size > MICROS_SV39_PAGE_SIZE) {
        return false;
    }
    for (
        process_index = 0;
        process_index < HANDOFF_PROCESS_COUNT;
        ++process_index
    ) {
        handoff_test_prepare_context(fixture, &fixture->contexts[process_index], process_index);
        handoff_test_copy_bytes(fixture,
            (void *)(uintptr_t)fixture->user_physical[process_index][0],
            micros_address_space_handoff_payload_start,
            fixture->payload_size
        );
        if (
            micros_user_execution_prepare(
                fixture->threads[process_index],
                &fixture->contexts[process_index]
            ) != MICROS_USER_EXECUTION_OK
        ) {
            return false;
        }
    }
    __asm__ volatile("fence.i" : : : "memory");

    if (
        micros_grant_create(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTOR],
            fixture->endpoints[HANDOFF_GRANTEE],
            TEST_DATA_SECOND_ADDRESS - 64,
            256,
            MICROS_GRANT_PERMISSION_READ,
            &fixture->read_grant
        ) != MICROS_GRANT_OK
        || micros_grant_create(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTOR],
            fixture->endpoints[HANDOFF_GRANTEE],
            TEST_DATA_SECOND_ADDRESS - 64,
            256,
            MICROS_GRANT_PERMISSION_WRITE,
            &fixture->write_grant
        ) != MICROS_GRANT_OK
    ) {
        return false;
    }


    return true;
}
