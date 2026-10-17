#include "kernel/address_space_handoff_test_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "kernel/grant/grant_runtime_internal.h"
#include "kernel/grant/grant_syscall.h"
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

static bool verify_tty_stage_1101(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = UINT64_C(0x1101);
    fixture->tty_root = fixture->objects->processes[
        fixture->processes[HANDOFF_GRANTEE].slot
    ].address_space_root;
    handoff_test_fill_bytes(fixture,
        &fixture->tty_map_sentinel,
        UINT8_C(0xa5),
        sizeof(fixture->tty_map_sentinel)
    );
    fixture->tty_map_plan = fixture->tty_map_sentinel;
    if (
        micros_user_address_space_prepare_tty_uart_mapping(
            fixture->processes[HANDOFF_GRANTOR],
            fixture->objects->processes[
                fixture->processes[HANDOFF_GRANTOR].slot
            ].address_space_root,
            &fixture->tty_map_plan
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED
        || !handoff_test_bytes_equal(fixture,
            &fixture->tty_map_plan,
            &fixture->tty_map_sentinel,
            sizeof(fixture->tty_map_plan)
        )
    ) {
        return false;
    }
    fixture->tty_spare_pte = handoff_test_leaf_pte_for(fixture,
        fixture->objects,
        fixture->processes[HANDOFF_GRANTEE],
        TEST_TTY_SPARE_ADDRESS
    );
    fixture->tty_uart_pte = handoff_test_leaf_pte_for(fixture,
        fixture->objects,
        fixture->processes[HANDOFF_GRANTEE],
        MICROS_TTY_UART_VIRTUAL_BASE
    );
    if (
        fixture->tty_spare_pte == NULL
        || fixture->tty_uart_pte == NULL
        || *fixture->tty_spare_pte == 0
        || *fixture->tty_uart_pte != 0
    ) {
        return false;
    }
    fixture->saved_spare_pte = *fixture->tty_spare_pte;

    return true;
}

static bool verify_tty_stage_1102(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = UINT64_C(0x1102);
    *fixture->tty_spare_pte = 0;
    *fixture->tty_uart_pte = fixture->saved_spare_pte;
    fixture->tty_map_plan = fixture->tty_map_sentinel;
    if (
        micros_user_address_space_validate(
            fixture->processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_prepare_tty_uart_mapping(
            fixture->processes[HANDOFF_GRANTEE],
            fixture->tty_root,
            &fixture->tty_map_plan
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT
        || !handoff_test_bytes_equal(fixture,
            &fixture->tty_map_plan,
            &fixture->tty_map_sentinel,
            sizeof(fixture->tty_map_plan)
        )
    ) {
        return false;
    }
    *fixture->tty_uart_pte = 0;
    *fixture->tty_spare_pte = fixture->saved_spare_pte;

    return true;
}

static bool verify_tty_stage_1103(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = UINT64_C(0x1103);
    if (
        micros_user_address_space_activate(
            fixture->processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return false;
    }
    fixture->tty_map_plan = fixture->tty_map_sentinel;
    if (
        micros_user_address_space_prepare_tty_uart_mapping(
            fixture->processes[HANDOFF_GRANTEE],
            fixture->tty_root,
            &fixture->tty_map_plan
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        || !handoff_test_bytes_equal(fixture,
            &fixture->tty_map_plan,
            &fixture->tty_map_sentinel,
            sizeof(fixture->tty_map_plan)
        )
        || micros_user_address_space_activate_kernel()
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_prepare_tty_uart_mapping(
            fixture->processes[HANDOFF_GRANTEE],
            fixture->tty_root,
            &fixture->tty_map_plan
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_tty_handoff_runtime_prepare_mapped(
            &fixture->mapped_handoff
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        return false;
    }
    micros_user_address_space_commit_tty_uart_mapping(
        &fixture->tty_map_plan
    );
    micros_tty_handoff_runtime_commit_mapped_prevalidated(
        &fixture->mapped_handoff
    );
    handoff_test_fill_bytes(fixture,
        &fixture->mapped_handoff_sentinel,
        UINT8_C(0xa5),
        sizeof(fixture->mapped_handoff_sentinel)
    );
    fixture->mapped_handoff = fixture->mapped_handoff_sentinel;

    return true;
}

static bool verify_tty_stage_1104(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = UINT64_C(0x1104);
    if (
        micros_tty_handoff_runtime_prepare_mapped(
            &fixture->mapped_handoff
        ) != MICROS_TTY_HANDOFF_ERROR_STATE
        || !handoff_test_bytes_equal(fixture,
            &fixture->mapped_handoff,
            &fixture->mapped_handoff_sentinel,
            sizeof(fixture->mapped_handoff)
        )
        || micros_user_address_space_validate(
            fixture->processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_validate_tty_uart_mapping(
            fixture->processes[HANDOFF_GRANTEE],
            fixture->tty_root
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return false;
    }

    fixture->physical_address = UINT64_C(0xfeedfacefeedface);

    return true;
}

static bool verify_tty_stage_1105(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = UINT64_C(0x1105);
    fixture->permissions = UINT32_MAX;
    if (
        micros_user_address_space_lookup(
            fixture->processes[HANDOFF_GRANTEE],
            MICROS_TTY_UART_VIRTUAL_BASE,
            &fixture->physical_address,
            &fixture->permissions
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        || fixture->physical_address != UINT64_C(0xfeedfacefeedface)
        || fixture->permissions != UINT32_MAX
    ) {
        return false;
    }
    fixture->physical_address = UINT64_C(0xfacefeedfacefeed);

    return true;
}

static bool verify_tty_stage_1106(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = UINT64_C(0x1106);
    fixture->permissions = UINT32_MAX;
    fixture->contiguous = SIZE_MAX;
    if (
        micros_user_address_space_translate(
            fixture->processes[HANDOFF_GRANTEE],
            MICROS_TTY_UART_VIRTUAL_BASE + 17,
            &fixture->physical_address,
            &fixture->permissions,
            &fixture->contiguous
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        || fixture->physical_address != UINT64_C(0xfacefeedfacefeed)
        || fixture->permissions != UINT32_MAX
        || fixture->contiguous != SIZE_MAX
    ) {
        return false;
    }
    fixture->inventory = (struct managed_inventory_context){0};

    return true;
}

static bool verify_tty_stage_1107(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = UINT64_C(0x1107);
    fixture->inventory_root = 0;
    fixture->inventory_count = 0;
    if (
        micros_user_address_space_inventory(
            fixture->processes[HANDOFF_GRANTEE],
            handoff_test_record_managed_mapping,
            &fixture->inventory,
            &fixture->inventory_root,
            &fixture->inventory_count
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || fixture->inventory_root != fixture->tty_root
        || fixture->inventory_count != HANDOFF_GRANTEE_MAPPING_COUNT
        || fixture->inventory.count != fixture->inventory_count
        || fixture->inventory.saw_uart
    ) {
        return false;
    }
    handoff_test_fill_bytes(fixture,
        &fixture->observed_message,
        UINT8_C(0xa5),
        sizeof(fixture->observed_message)
    );
    fixture->message = fixture->observed_message;

    return true;
}

static bool verify_tty_stage_1108(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = UINT64_C(0x1108);
    if (
        micros_ipc_buffer_snapshot(
            fixture->processes[HANDOFF_GRANTEE],
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_IPC_BUFFER_READ,
            &fixture->observed_message
        ) != MICROS_IPC_BUFFER_ERROR_INVARIANT
        || !handoff_test_bytes_equal(fixture,
            &fixture->observed_message,
            &fixture->message,
            sizeof(fixture->observed_message)
        )
    ) {
        return false;
    }

    fixture->saved_uart_pte = *fixture->tty_uart_pte;

    return true;
}

static bool verify_tty_stage_1109(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = UINT64_C(0x1109);
    if (
        micros_sv39_make_leaf_pte(
            MICROS_TTY_UART_PHYSICAL_BASE,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE
                | MICROS_SV39_PERMISSION_USER,
            &fixture->saved_pte
        ) != MICROS_SV39_OK
    ) {
        return false;
    }
    *fixture->tty_uart_pte = fixture->saved_pte;
    if (
        micros_user_address_space_validate(
            fixture->processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
    ) {
        return false;
    }
    if (
        micros_sv39_make_leaf_pte(
            MICROS_TTY_UART_PHYSICAL_BASE
                + MICROS_SV39_PAGE_SIZE,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_USER,
            &fixture->saved_pte
        ) != MICROS_SV39_OK
    ) {
        return false;
    }
    *fixture->tty_uart_pte = fixture->saved_pte;
    if (
        micros_user_address_space_validate(
            fixture->processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
    ) {
        return false;
    }
    *fixture->tty_uart_pte = 0;
    if (
        micros_user_address_space_validate(
            fixture->processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
    ) {
        return false;
    }
    *fixture->tty_uart_pte = fixture->saved_uart_pte;
    fixture->saved_spare_pte = *fixture->tty_spare_pte;
    *fixture->tty_spare_pte = fixture->saved_uart_pte;
    if (
        micros_user_address_space_validate(
            fixture->processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
    ) {
        return false;
    }
    *fixture->tty_spare_pte = fixture->saved_spare_pte;
    fixture->physical_address = UINT64_C(0x1122334455667788);
    if (
        micros_user_address_space_release_page(
            fixture->processes[HANDOFF_GRANTEE],
            MICROS_TTY_UART_VIRTUAL_BASE,
            &fixture->physical_address
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || fixture->physical_address != UINT64_C(0x1122334455667788)
        || micros_user_address_space_destroy(
            fixture->processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || *fixture->tty_uart_pte != fixture->saved_uart_pte
        || micros_user_address_space_validate_tty_uart_mapping(
            fixture->processes[HANDOFF_GRANTEE],
            fixture->tty_root
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return false;
    }


    return true;
}

static bool verify_tty_stage_1110(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = UINT64_C(0x1110);
    fixture->tty_runtime =
        micros_tty_handoff_runtime_authoritative_state();
    if (
        fixture->tty_runtime == NULL
        || micros_endpoint_install_profile(
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTEE],
            MICROS_PRIVILEGE_PROFILE_TTY
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            fixture->endpoint_registry,
            fixture->objects,
            fixture->endpoints[HANDOFF_GRANTEE]
        ) != MICROS_ENDPOINT_OK
        || micros_thread_scheduler_admit(
            fixture->objects,
            micros_kernel_object_runtime_boot_hart_handle(),
            fixture->threads[HANDOFF_GRANTEE],
            fixture->tty_binding.scheduler_priority,
            fixture->tty_binding.scheduler_quantum_counter_ticks,
            fixture->tty_binding.scheduler_preemptible
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_tty_handoff_release(&fixture->tty_runtime->handoff)
            != MICROS_TTY_HANDOFF_OK
    ) {
        return false;
    }
    fixture->tty_bootstrap.transitions.entries[0].state =
        MICROS_BOOTSTRAP_SERVICE_STARTING;
    fixture->tty_bootstrap.transitions.entries[0].endpoint_state =
        MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
    fixture->tty_bootstrap.transitions.entries[0].profile_installed = true;
    fixture->tty_bootstrap.transitions.entries[0].scheduler_assigned = true;
    fixture->tty_bootstrap.transitions.entries[0].ready_deadline =
        fixture->tty_runtime->handoff.deadline;
    if (
        micros_tty_handoff_runtime_validate(
            &fixture->tty_bootstrap,
            fixture->endpoint_registry,
            fixture->objects
        ) != MICROS_TTY_HANDOFF_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
    ) {
        return false;
    }

    handoff_test_fill_bytes(fixture, &fixture->grant_hart, 0, sizeof(fixture->grant_hart));
    fixture->grant_hart.hardware_id = 0;
    handoff_test_fill_bytes(fixture, &fixture->grant_context, 0, sizeof(fixture->grant_context));
    fixture->grant_context.objects = fixture->objects;
    fixture->grant_context.process = fixture->processes[HANDOFF_GRANTEE];
    fixture->grant_context.current = fixture->threads[HANDOFF_GRANTEE];
    handoff_test_fill_bytes(fixture, &fixture->grant_frame, 0, sizeof(fixture->grant_frame));
    fixture->grant_frame.a0 = UINT64_C(0x1122334455667788);
    handoff_test_fill_bytes(fixture, &fixture->grant_arguments, 0, sizeof(fixture->grant_arguments));
    fixture->grant_arguments.a0 = fixture->endpoints[HANDOFF_GRANTOR];
    fixture->grant_arguments.a1 = MICROS_TTY_UART_VIRTUAL_BASE;
    fixture->grant_arguments.a2 = MICROS_TTY_UART_MAPPED_LENGTH;
    fixture->grant_arguments.a3 = MICROS_GRANT_PERMISSION_READ;
    fixture->grant_arguments.a7 = MICROS_SYSCALL_ABI_GRANT_CREATE;
    if (
        !handoff_test_snapshot_state(fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
        || micros_grant_handle_captured_user_ecall(
            &fixture->grant_hart,
            &fixture->grant_frame,
            &fixture->grant_context,
            &fixture->grant_arguments
        ) != MICROS_SYSCALL_RETURN_NORMAL
        || fixture->grant_frame.a0
            != (uint64_t)(int64_t)
                MICROS_SYSCALL_ABI_MEMORY_FAULT
        || !handoff_test_state_matches(fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
    ) {
        return false;
    }


    return true;
}

static bool verify_tty_stage_1111(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = UINT64_C(0x1111);
    if (
        micros_grant_create(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTOR],
            fixture->endpoints[HANDOFF_GRANTEE],
            TEST_DATA_SECOND_ADDRESS,
            32,
            MICROS_GRANT_PERMISSION_READ,
            &fixture->device_copy_grant
        ) != MICROS_GRANT_OK
    ) {
        return false;
    }
    *fixture->tty_uart_pte = 0;
    if (
        micros_grant_copy_from(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTEE],
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->device_copy_grant,
            0,
            MICROS_TTY_UART_VIRTUAL_BASE,
            16
        ) != MICROS_GRANT_ERROR_INVARIANT
    ) {
        return false;
    }
    *fixture->tty_uart_pte = fixture->saved_uart_pte;
    fixture->corrupt_pte = handoff_test_leaf_pte_for(fixture,
        fixture->objects,
        fixture->processes[HANDOFF_GRANTOR],
        TEST_DATA_SECOND_ADDRESS
    );
    if (fixture->corrupt_pte == NULL) {
        return false;
    }
    fixture->saved_pte = *fixture->corrupt_pte;
    *fixture->corrupt_pte = UINT64_C(1);
    if (
        micros_grant_copy_from(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTEE],
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->device_copy_grant,
            0,
            MICROS_TTY_UART_VIRTUAL_BASE,
            16
        ) != MICROS_GRANT_ERROR_INVARIANT
    ) {
        return false;
    }
    *fixture->corrupt_pte = fixture->saved_pte;
    if (
        !handoff_test_snapshot_state(fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
    ) {
        return false;
    }
    handoff_test_fill_bytes(fixture, &fixture->grant_frame, 0, sizeof(fixture->grant_frame));
    fixture->grant_frame.a0 = UINT64_C(0x8877665544332211);
    handoff_test_fill_bytes(fixture, &fixture->grant_arguments, 0, sizeof(fixture->grant_arguments));
    fixture->grant_arguments.a0 = fixture->endpoints[HANDOFF_GRANTOR];
    fixture->grant_arguments.a1 = fixture->device_copy_grant;
    fixture->grant_arguments.a3 = MICROS_TTY_UART_VIRTUAL_BASE;
    fixture->grant_arguments.a4 = 16;
    fixture->grant_arguments.a7 = MICROS_SYSCALL_ABI_GRANT_COPY_FROM;
    if (
        micros_grant_handle_captured_user_ecall(
            &fixture->grant_hart,
            &fixture->grant_frame,
            &fixture->grant_context,
            &fixture->grant_arguments
        ) != MICROS_SYSCALL_RETURN_NORMAL
        || fixture->grant_frame.a0
            != (uint64_t)(int64_t)
                MICROS_SYSCALL_ABI_MEMORY_FAULT
        || !handoff_test_state_matches(fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
        || micros_grant_revoke(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTOR],
            fixture->device_copy_grant
        ) != MICROS_GRANT_OK
    ) {
        return false;
    }


    return true;
}

bool handoff_test_verify_tty_mapping(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = 11;
    return (
        verify_tty_stage_1101(fixture)
        && verify_tty_stage_1102(fixture)
        && verify_tty_stage_1103(fixture)
        && verify_tty_stage_1104(fixture)
        && verify_tty_stage_1105(fixture)
        && verify_tty_stage_1106(fixture)
        && verify_tty_stage_1107(fixture)
        && verify_tty_stage_1108(fixture)
        && verify_tty_stage_1109(fixture)
        && verify_tty_stage_1110(fixture)
        && verify_tty_stage_1111(fixture)
    );
}
