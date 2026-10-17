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

static bool validate_handed_off_roots(
    struct micros_address_space_handoff_test_context *fixture
)
{
    size_t process_index;

    for (
        process_index = 0;
        process_index < HANDOFF_PROCESS_COUNT;
        ++process_index
    ) {
        if (
            micros_user_address_space_validate(
                fixture->processes[process_index]
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || micros_user_address_space_lookup(
                fixture->processes[process_index],
                TEST_DATA_ADDRESS,
                &fixture->physical_address,
                &fixture->permissions
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || fixture->physical_address
                != fixture->user_physical[process_index][2]
            || micros_user_address_space_translate(
                fixture->processes[process_index],
                TEST_DATA_ADDRESS + 7,
                &fixture->physical_address,
                &fixture->permissions,
                &fixture->contiguous
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || fixture->physical_address
                != fixture->user_physical[process_index][2] + 7
            || fixture->contiguous != MICROS_SV39_PAGE_SIZE - 7
            || (
                fixture->permissions
                & (
                    MICROS_SV39_PERMISSION_READ
                    | MICROS_SV39_PERMISSION_WRITE
                )
            ) != (
                MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
            )
            || micros_user_address_space_activate(
                fixture->processes[process_index]
            ) != MICROS_USER_ADDRESS_SPACE_OK
        ) {
            return false;
        }
    }
    if (
        micros_user_address_space_activate_kernel()
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return false;
    }

    return true;
}

static bool verify_resident_ipc_buffers(
    struct micros_address_space_handoff_test_context *fixture
)
{
    handoff_test_write_virtual(fixture,
        HANDOFF_GRANTOR,
        TEST_DATA_ADDRESS + UINT64_C(0x100),
        &fixture->message,
        sizeof(fixture->message)
    );
    if (
        micros_ipc_buffer_snapshot(
            fixture->processes[HANDOFF_GRANTOR],
            TEST_DATA_ADDRESS + UINT64_C(0x100),
            MICROS_IPC_BUFFER_READ,
            &fixture->observed_message
        ) != MICROS_IPC_BUFFER_OK
        || !handoff_test_bytes_equal(fixture,
            &fixture->observed_message,
            &fixture->message,
            sizeof(fixture->message)
        )
        || micros_ipc_buffer_write(
            fixture->processes[HANDOFF_GRANTEE],
            TEST_DATA_ADDRESS + UINT64_C(0x180),
            &fixture->message
        ) != MICROS_IPC_BUFFER_OK
        || !handoff_test_virtual_matches(fixture,
            HANDOFF_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x180),
            &fixture->message,
            sizeof(fixture->message)
        )
    ) {
        return false;
    }
    handoff_test_write_virtual(fixture,
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 32,
        &fixture->message,
        sizeof(fixture->message)
    );
    if (
        micros_ipc_buffer_snapshot(
            fixture->processes[HANDOFF_GRANTOR],
            TEST_DATA_SECOND_ADDRESS - 32,
            MICROS_IPC_BUFFER_READ,
            &fixture->observed_message
        ) != MICROS_IPC_BUFFER_OK
        || !handoff_test_bytes_equal(fixture,
            &fixture->observed_message,
            &fixture->message,
            sizeof(fixture->message)
        )
        || micros_ipc_buffer_write(
            fixture->processes[HANDOFF_GRANTEE],
            TEST_DATA_SECOND_ADDRESS - 32,
            &fixture->message
        ) != MICROS_IPC_BUFFER_OK
        || !handoff_test_virtual_matches(fixture,
            HANDOFF_GRANTEE,
            TEST_DATA_SECOND_ADDRESS - 32,
            &fixture->message,
            sizeof(fixture->message)
        )
    ) {
        return false;
    }


    return true;
}

static bool verify_cross_page_grants(
    struct micros_address_space_handoff_test_context *fixture
)
{
    handoff_test_write_pattern(fixture,
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 64,
        128,
        UINT8_C(0x70)
    );
    if (
        micros_grant_copy_from(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTEE],
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->read_grant,
            0,
            TEST_DATA_SECOND_ADDRESS - 32,
            128
        ) != MICROS_GRANT_OK
        || !handoff_test_pattern_matches(fixture,
            HANDOFF_GRANTEE,
            TEST_DATA_SECOND_ADDRESS - 32,
            128,
            UINT8_C(0x70)
        )
    ) {
        return false;
    }
    handoff_test_write_pattern(fixture,
        HANDOFF_GRANTEE,
        TEST_DATA_SECOND_ADDRESS - 48,
        96,
        UINT8_C(0x90)
    );
    if (
        micros_grant_copy_to(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTEE],
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->write_grant,
            16,
            TEST_DATA_SECOND_ADDRESS - 48,
            96
        ) != MICROS_GRANT_OK
        || !handoff_test_pattern_matches(fixture,
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 48,
            96,
            UINT8_C(0x90)
        )
    ) {
        return false;
    }

    return true;
}

static bool verify_grant_boundaries(
    struct micros_address_space_handoff_test_context *fixture
)
{

    handoff_test_write_pattern(fixture,
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS,
        32,
        UINT8_C(0x11)
    );
    *(unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
        HANDOFF_GRANTEE,
        TEST_DATA_ADDRESS + UINT64_C(0x2ff)
    ) = UINT8_C(0x5a);
    *(unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
        HANDOFF_GRANTEE,
        TEST_DATA_ADDRESS + UINT64_C(0x320)
    ) = UINT8_C(0xa5);

    if (
        micros_grant_copy_from(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTEE],
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->read_grant,
            64,
            TEST_DATA_ADDRESS + UINT64_C(0x300),
            32
        ) != MICROS_GRANT_OK
        || !handoff_test_pattern_matches(fixture,
            HANDOFF_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x300),
            32,
            UINT8_C(0x11)
        )
        || *(const unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
            HANDOFF_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x2ff)
        ) != UINT8_C(0x5a)
        || *(const unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
            HANDOFF_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x320)
        ) != UINT8_C(0xa5)
    ) {
        return false;
    }
    handoff_test_write_pattern(fixture,
        HANDOFF_GRANTEE,
        TEST_DATA_ADDRESS + UINT64_C(0x380),
        32,
        UINT8_C(0x22)
    );
    *(unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS + UINT64_C(0x3f)
    ) = UINT8_C(0x6b);
    *(unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS + UINT64_C(0x60)
    ) = UINT8_C(0xb6);
    if (
        micros_grant_copy_to(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTEE],
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->write_grant,
            128,
            TEST_DATA_ADDRESS + UINT64_C(0x380),
            32
        ) != MICROS_GRANT_OK
        || !handoff_test_pattern_matches(fixture,
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS + UINT64_C(0x40),
            32,
            UINT8_C(0x22)
        )
        || *(const unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS + UINT64_C(0x3f)
        ) != UINT8_C(0x6b)
        || *(const unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS + UINT64_C(0x60)
        ) != UINT8_C(0xb6)
    ) {
        return false;
    }

    return true;
}

static bool verify_zero_length_grants(
    struct micros_address_space_handoff_test_context *fixture
)
{
    if (
        micros_grant_copy_from(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTEE],
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->read_grant,
            0,
            0,
            0
        ) != MICROS_GRANT_OK
        || micros_grant_copy_to(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTEE],
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->write_grant,
            0,
            0,
            0
        ) != MICROS_GRANT_OK
    ) {
        return false;
    }




    return true;
}

bool handoff_test_verify_post_handoff_runtime(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = 6;
    return (
        validate_handed_off_roots(fixture)
        && verify_resident_ipc_buffers(fixture)
        && verify_cross_page_grants(fixture)
        && verify_grant_boundaries(fixture)
        && verify_zero_length_grants(fixture)
    );
}

bool handoff_test_verify_post_handoff_failures(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = 7;
    handoff_test_write_pattern(fixture,
        HANDOFF_GRANTEE,
        TEST_READ_ONLY_ADDRESS,
        64,
        UINT8_C(0xa0)
    );
    handoff_test_write_pattern(fixture,
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 64,
        64,
        UINT8_C(0xb0)
    );
    if (
        !handoff_test_snapshot_state(fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
        || micros_ipc_buffer_write(
            fixture->processes[HANDOFF_GRANTEE],
            TEST_READ_ONLY_ADDRESS,
            &fixture->message
        ) != MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT
        || micros_grant_copy_from(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTEE],
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->read_grant,
            0,
            TEST_READ_ONLY_ADDRESS,
            64
        ) != MICROS_GRANT_ERROR_FAULT
        || micros_grant_copy_from(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTEE],
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->read_grant,
            0,
            TEST_UNMAPPED_ADDRESS,
            64
        ) != MICROS_GRANT_ERROR_FAULT
        || !handoff_test_pattern_matches(fixture,
            HANDOFF_GRANTEE,
            TEST_READ_ONLY_ADDRESS,
            64,
            UINT8_C(0xa0)
        )
        || !handoff_test_pattern_matches(fixture,
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 64,
            64,
            UINT8_C(0xb0)
        )
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
static bool reject_corrupt_frame_owners(
    struct micros_address_space_handoff_test_context *fixture
)
{

    if (
        !handoff_test_frame_index_for_physical(fixture,
            fixture->ledger,
            fixture->user_physical[HANDOFF_GRANTEE][2],
            &fixture->frame_index
        )
    ) {
        return false;
    }
    fixture->saved_owner = fixture->ledger->owners[fixture->frame_index];
    handoff_test_write_pattern(fixture,
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 64,
        64,
        UINT8_C(0xc0)
    );
    handoff_test_write_pattern(fixture,
        HANDOFF_GRANTEE,
        TEST_DATA_ADDRESS,
        64,
        UINT8_C(0xd0)
    );
    ((struct micros_frame_ownership *)(uintptr_t)fixture->ledger)
        ->owners[fixture->frame_index] =
        (struct micros_frame_owner){
            .generation = fixture->processes[HANDOFF_GRANTOR].generation,
            .slot = fixture->processes[HANDOFF_GRANTOR].slot,
            .kind = MICROS_FRAME_OWNER_VM_WIRED,
            .reserved = 0,
        };
    if (
        !handoff_test_snapshot_state(fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
        || micros_user_address_space_validate(
            fixture->processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        || micros_grant_copy_from(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTEE],
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->read_grant,
            0,
            TEST_DATA_ADDRESS,
            64
        ) != MICROS_GRANT_ERROR_INVARIANT
        || !handoff_test_pattern_matches(fixture,
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 64,
            64,
            UINT8_C(0xc0)
        )
        || !handoff_test_pattern_matches(fixture,
            HANDOFF_GRANTEE,
            TEST_DATA_ADDRESS,
            64,
            UINT8_C(0xd0)
        )
        || !handoff_test_state_matches(fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
    ) {
        return false;
    }
    ((struct micros_frame_ownership *)(uintptr_t)fixture->ledger)
        ->owners[fixture->frame_index] = fixture->saved_owner;

    ((struct micros_frame_ownership *)(uintptr_t)fixture->ledger)
        ->owners[fixture->frame_index] =
        (struct micros_frame_owner){
            .generation = 0,
            .slot = 0,
            .kind = MICROS_FRAME_OWNER_VM_TRANSFERABLE,
            .reserved = 0,
        };
    --((struct micros_frame_ownership *)(uintptr_t)fixture->ledger)
        ->owner_counts[MICROS_FRAME_OWNER_VM_WIRED];
    ++((struct micros_frame_ownership *)(uintptr_t)fixture->ledger)
        ->owner_counts[MICROS_FRAME_OWNER_VM_TRANSFERABLE];
    if (
        micros_frame_ownership_runtime_validate(fixture->objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_user_address_space_validate(
            fixture->processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
    ) {
        return false;
    }
    ((struct micros_frame_ownership *)(uintptr_t)fixture->ledger)
        ->owners[fixture->frame_index] = fixture->saved_owner;
    ++((struct micros_frame_ownership *)(uintptr_t)fixture->ledger)
        ->owner_counts[MICROS_FRAME_OWNER_VM_WIRED];
    --((struct micros_frame_ownership *)(uintptr_t)fixture->ledger)
        ->owner_counts[MICROS_FRAME_OWNER_VM_TRANSFERABLE];


    return true;
}

static bool reject_corrupt_leaf_pte(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->corrupt_pte = handoff_test_leaf_pte_for(fixture,
        fixture->objects,
        fixture->processes[HANDOFF_GRANTEE],
        TEST_DATA_ADDRESS
    );
    if (fixture->corrupt_pte == NULL) {
        return false;
    }
    fixture->saved_pte = *fixture->corrupt_pte;
    *fixture->corrupt_pte = UINT64_C(1);
    fixture->physical_address = UINT64_C(0xfeedfacefeedface);
    fixture->permissions = UINT32_MAX;
    fixture->contiguous = SIZE_MAX;
    if (
        micros_user_address_space_translate(
            fixture->processes[HANDOFF_GRANTEE],
            TEST_UNMAPPED_ADDRESS,
            &fixture->physical_address,
            &fixture->permissions,
            &fixture->contiguous
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PTE
        || fixture->physical_address != UINT64_C(0xfeedfacefeedface)
        || fixture->permissions != UINT32_MAX
        || fixture->contiguous != SIZE_MAX
    ) {
        return false;
    }
    *fixture->corrupt_pte = fixture->saved_pte;



    return true;
}

bool handoff_test_verify_corruption_rejection(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = 8;
    return (
        reject_corrupt_frame_owners(fixture)
        && reject_corrupt_leaf_pte(fixture)
    );
}

bool handoff_test_verify_mutation_revocation(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = 9;
    fixture->physical_address = UINT64_C(0xfeedfacefeedface);
    if (
        !handoff_test_snapshot_state(fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
        || micros_user_address_space_create(
            fixture->processes[HANDOFF_GRANTOR]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || micros_user_address_space_allocate_page(
            fixture->processes[HANDOFF_GRANTOR],
            TEST_UNMAPPED_ADDRESS,
            MICROS_SV39_PERMISSION_READ,
            &fixture->physical_address
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || fixture->physical_address != UINT64_C(0xfeedfacefeedface)
    ) {
        return false;
    }
    fixture->physical_address = UINT64_C(0xfacefeedfacefeed);
    if (
        micros_user_address_space_release_page(
            fixture->processes[HANDOFF_GRANTOR],
            TEST_DATA_ADDRESS,
            &fixture->physical_address
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || fixture->physical_address != UINT64_C(0xfacefeedfacefeed)
        || micros_user_address_space_destroy(
            fixture->processes[HANDOFF_GRANTOR]
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || !handoff_test_state_matches(fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
    ) {
        return false;
    }

    if (
        !micros_user_execution_test_stack_bounds(
            fixture->threads[HANDOFF_GRANTOR],
            &fixture->kernel_stack_bottom,
            &fixture->kernel_stack_top
        )
        || fixture->kernel_stack_top - fixture->kernel_stack_bottom
            != MICROS_THREAD_KERNEL_STACK_SIZE
    ) {
        return false;
    }
    handoff_test_copy_bytes(fixture,
        &fixture->thread_snapshot,
        &fixture->objects->threads[fixture->threads[HANDOFF_GRANTOR].slot],
        sizeof(fixture->thread_snapshot)
    );
    handoff_test_copy_bytes(fixture,
        fixture->kernel_stack_snapshot,
        (const void *)fixture->kernel_stack_bottom,
        sizeof(fixture->kernel_stack_snapshot)
    );
    handoff_test_copy_bytes(fixture,
        &fixture->observed_context,
        &fixture->contexts[HANDOFF_GRANTOR],
        sizeof(fixture->observed_context)
    );
    if (
        !handoff_test_snapshot_state(fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
        || micros_user_execution_prepare(
            fixture->threads[HANDOFF_GRANTOR],
            &fixture->observed_context
        ) != MICROS_USER_EXECUTION_ERROR_PHASE
        || !handoff_test_bytes_equal(fixture,
            &fixture->observed_context,
            &fixture->contexts[HANDOFF_GRANTOR],
            sizeof(fixture->observed_context)
        )
        || !handoff_test_bytes_equal(fixture,
            &fixture->thread_snapshot,
            &fixture->objects->threads[fixture->threads[HANDOFF_GRANTOR].slot],
            sizeof(fixture->thread_snapshot)
        )
        || !handoff_test_bytes_equal(fixture,
            fixture->kernel_stack_snapshot,
            (const void *)fixture->kernel_stack_bottom,
            sizeof(fixture->kernel_stack_snapshot)
        )
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
