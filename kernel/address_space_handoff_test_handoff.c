#include "kernel/address_space_handoff_test_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "kernel/grant_runtime_internal.h"
#include "kernel/grant_syscall.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/tty_handoff_runtime.h"
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

bool handoff_test_verify_pre_handoff_access(
    struct micros_address_space_handoff_test_context *fixture
)
{
    size_t payload_index;
    fixture->failure_stage = 4;
    for (payload_index = 0; payload_index < sizeof(fixture->message.payload); ++payload_index) {
        fixture->message.payload[payload_index] =
            (unsigned char)(UINT8_C(0x30) + payload_index);
    }
    fixture->message.type = UINT32_C(0x12345678);
    fixture->message.source = UINT32_C(0xabcdef01);
    fixture->message.reply_token = UINT64_C(0x1122334455667788);
    handoff_test_write_virtual(fixture,
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 32,
        &fixture->message,
        sizeof(fixture->message)
    );
    if (
        micros_user_address_space_validate(fixture->processes[HANDOFF_GRANTOR])
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_translate(
            fixture->processes[HANDOFF_GRANTOR],
            TEST_DATA_SECOND_ADDRESS - 32,
            &fixture->physical_address,
            &fixture->permissions,
            &fixture->contiguous
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || fixture->physical_address
            != handoff_test_physical_for(fixture,
                HANDOFF_GRANTOR,
                TEST_DATA_SECOND_ADDRESS - 32
            )
        || fixture->contiguous != 32
        || micros_ipc_buffer_snapshot(
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
    ) {
        return false;
    }
    handoff_test_write_pattern(fixture,
        HANDOFF_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 64,
        128,
        UINT8_C(0x50)
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
            UINT8_C(0x50)
        )
    ) {
        return false;
    }


    return true;
}
static bool handoff_without_execution_is_rejected(
    struct micros_address_space_handoff_test_context *fixture
)
{
    return (
        micros_user_execution_detach(fixture->threads[HANDOFF_GRANTEE])
            == MICROS_USER_EXECUTION_OK
        && handoff_test_snapshot_state(
            fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
        && micros_user_address_space_prepare_wired_handoff()
            == MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        && handoff_test_state_matches(
            fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
        && fixture->ledger->phase
            == MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        && micros_user_execution_prepare(
            fixture->threads[HANDOFF_GRANTEE],
            &fixture->contexts[HANDOFF_GRANTEE]
        ) == MICROS_USER_EXECUTION_OK
    );
}

static bool orphan_process_frame_is_rejected(
    struct micros_address_space_handoff_test_context *fixture
)
{
    if (
        micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_USER,
            fixture->processes[HANDOFF_GRANTEE],
            &fixture->process_user_owner
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_allocate(
            fixture->process_user_owner,
            &fixture->orphan_physical
        ) != MICROS_FRAME_OWNERSHIP_OK
        || !handoff_test_snapshot_state(
            fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
        || micros_user_address_space_prepare_wired_handoff()
            != MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        || !handoff_test_state_matches(
            fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
    ) {
        return false;
    }
    return micros_frame_ownership_runtime_release(
        fixture->process_user_owner,
        fixture->orphan_physical
    ) == MICROS_FRAME_OWNERSHIP_OK;
}

static bool wired_snapshot_tracks_authority(
    struct micros_address_space_handoff_test_context *fixture
)
{
    return (
        handoff_test_stage_wired_pages(fixture)
        && handoff_test_validate_wired_snapshot(fixture)
        && micros_frame_owner_make_kernel(
            MICROS_FRAME_OWNER_KERNEL_RETAINED,
            &fixture->retained_owner
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_runtime_allocate(
            fixture->retained_owner,
            &fixture->stale_snapshot_frame
        ) == MICROS_FRAME_OWNERSHIP_OK
        && !handoff_test_validate_wired_snapshot(fixture)
        && micros_frame_ownership_runtime_release(
            fixture->retained_owner,
            fixture->stale_snapshot_frame
        ) == MICROS_FRAME_OWNERSHIP_OK
        && handoff_test_validate_wired_snapshot(fixture)
    );
}

static bool completion_without_execution_is_rejected(
    struct micros_address_space_handoff_test_context *fixture
)
{
    return (
        micros_user_execution_detach(fixture->threads[HANDOFF_GRANTEE])
            == MICROS_USER_EXECUTION_OK
        && handoff_test_snapshot_state(
            fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
        && micros_user_address_space_complete_wired_handoff()
            == MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        && handoff_test_state_matches(
            fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
        && micros_user_execution_prepare(
            fixture->threads[HANDOFF_GRANTEE],
            &fixture->contexts[HANDOFF_GRANTEE]
        ) == MICROS_USER_EXECUTION_OK
    );
}

static bool transferable_leaf_is_rejected_before_commit(
    struct micros_address_space_handoff_test_context *fixture
)
{
    return (
        micros_frame_ownership_runtime_prepare_handoff(
            fixture->user_physical[HANDOFF_GRANTEE][2],
            fixture->process_user_owner,
            MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        ) == MICROS_FRAME_OWNERSHIP_OK
        && handoff_test_snapshot_state(
            fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
        && micros_user_address_space_complete_wired_handoff()
            == MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        && handoff_test_state_matches(
            fixture,
            fixture->ledger,
            fixture->objects,
            fixture->endpoint_registry,
            fixture->grant_registry
        )
        && fixture->ledger->phase
            == MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        && micros_frame_ownership_runtime_prepare_handoff(
            fixture->user_physical[HANDOFF_GRANTEE][2],
            fixture->process_user_owner,
            MICROS_FRAME_HANDOFF_VM_WIRED
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_user_address_space_complete_wired_handoff()
            == MICROS_USER_ADDRESS_SPACE_OK
        && fixture->ledger->phase
            == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    );
}

bool handoff_test_prepare_and_commit_handoff(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = 5;
    return (
        handoff_without_execution_is_rejected(fixture)
        && orphan_process_frame_is_rejected(fixture)
        && wired_snapshot_tracks_authority(fixture)
        && completion_without_execution_is_rejected(fixture)
        && transferable_leaf_is_rejected_before_commit(fixture)
    );
}
