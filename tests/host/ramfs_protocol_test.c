#include "tests/host/ramfs_test_internal.h"

#include <string.h>

_Static_assert(
    MICROS_RAMFS_PROTOCOL_VERSION == 1,
    "RAMFS protocol version drift"
);
_Static_assert(
    MICROS_RAMFS_MESSAGE_MOUNT == UINT32_C(0x00030001)
        && MICROS_RAMFS_MESSAGE_LOOKUP == UINT32_C(0x00030002)
        && MICROS_RAMFS_MESSAGE_CREATE == UINT32_C(0x00030003)
        && MICROS_RAMFS_MESSAGE_MKDIR == UINT32_C(0x00030004)
        && MICROS_RAMFS_MESSAGE_READ == UINT32_C(0x00030005)
        && MICROS_RAMFS_MESSAGE_WRITE == UINT32_C(0x00030006)
        && MICROS_RAMFS_MESSAGE_GETDENTS == UINT32_C(0x00030007)
        && MICROS_RAMFS_MESSAGE_PUTNODE == UINT32_C(0x00030008)
        && MICROS_RAMFS_MESSAGE_RESULT == UINT32_C(0x00030009),
    "RAMFS message identity drift"
);
_Static_assert(
    MICROS_RAMFS_DIRECTORY_RECORD_SIZE == 128,
    "RAMFS directory record size drift"
);

bool ramfs_test_seed_state_is_exact_and_invariants_fail_closed(
    struct ramfs_test_fixture *fixture
)
{
    uint64_t root = micros_ramfs_handle(0, 1);

    EXPECT_TRUE(
        micros_ramfs_state_initialize(
            &fixture->corrupt_state,
            5,
            fixture->seed_image,
            fixture->seed_image_size
        ) == MICROS_RAMFS_CORE_ERROR_ARGUMENT
    );
    EXPECT_TRUE(ramfs_test_initialize_unmounted(fixture));
    EXPECT_TRUE(root == UINT64_C(0x0000000100000000));
    EXPECT_TRUE(
        micros_ramfs_handle(63, 7)
            == UINT64_C(0x000000070000003f)
    );
    EXPECT_TRUE(fixture->state.phase == MICROS_RAMFS_PHASE_READY_UNMOUNTED);
    EXPECT_TRUE(fixture->state.node_count == 3);
    EXPECT_TRUE(fixture->state.allocated_block_count == 1);
    EXPECT_TRUE(fixture->state.nodes[0].parent == root);
    EXPECT_TRUE(fixture->state.nodes[0].link_count == 3);
    EXPECT_TRUE(fixture->state.nodes[1].parent == root);
    EXPECT_TRUE(fixture->state.nodes[1].link_count == 2);
    EXPECT_TRUE(fixture->state.nodes[2].parent == micros_ramfs_handle(1, 1));
    EXPECT_TRUE(fixture->state.nodes[2].link_count == 1);
    EXPECT_TRUE(fixture->state.nodes[2].size == 13);
    EXPECT_TRUE(fixture->state.nodes[2].blocks[0] == 0);
    EXPECT_TRUE(
        memcmp(fixture->state.data[0], "micros ramfs\n", 13) == 0
    );
    EXPECT_TRUE(micros_ramfs_handle(64, 1) == 0);
    EXPECT_TRUE(micros_ramfs_handle(0, 0) == 0);
    EXPECT_TRUE(
        micros_ramfs_state_validate(&fixture->state) == MICROS_RAMFS_CORE_OK
    );

    fixture->corrupt_state = fixture->state;
    fixture->corrupt_state.scratch[0] = 1;
    EXPECT_TRUE(
        micros_ramfs_state_validate(&fixture->corrupt_state)
            == MICROS_RAMFS_CORE_ERROR_INVARIANT
    );
    fixture->corrupt_state = fixture->state;
    fixture->corrupt_state.vfs_endpoint = 5;
    EXPECT_TRUE(
        micros_ramfs_state_validate(&fixture->corrupt_state)
            == MICROS_RAMFS_CORE_ERROR_INVARIANT
    );
    fixture->corrupt_state = fixture->state;
    fixture->corrupt_state.nodes[2].blocks[0] = 1;
    EXPECT_TRUE(
        micros_ramfs_state_validate(&fixture->corrupt_state)
            == MICROS_RAMFS_CORE_ERROR_INVARIANT
    );
    fixture->corrupt_state = fixture->state;
    fixture->corrupt_state.nodes[1].link_count = 3;
    EXPECT_TRUE(
        micros_ramfs_state_validate(&fixture->corrupt_state)
            == MICROS_RAMFS_CORE_ERROR_INVARIANT
    );
    return true;
}

bool ramfs_test_protocol_mount_and_result_layout_are_exact(
    struct ramfs_test_fixture *fixture
)
{
    struct micros_ramfs_reply_action action;
    struct micros_ipc_message message;
    struct micros_ipc_message built;
    enum micros_ramfs_result result;
    uint64_t root = micros_ramfs_handle(0, 1);

    EXPECT_TRUE(ramfs_test_initialize_unmounted(fixture));
    message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        root,
        MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
        2,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_STATE
        )
    );
    EXPECT_TRUE(fixture->bus.calls == 0);

    message = ramfs_test_lookup_request(0, 0, 1, 0);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_STATE
        )
    );
    EXPECT_TRUE(fixture->bus.calls == 0);

    message = ramfs_test_request(UINT32_C(0x000300ff));
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_BAD_TYPE
        )
    );
    message = ramfs_test_request(MICROS_RAMFS_MESSAGE_MOUNT);
    ramfs_test_write_u32_le(&message.payload[0], 2);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_BAD_VERSION
        )
    );
    message = ramfs_test_request(MICROS_RAMFS_MESSAGE_MOUNT);
    ramfs_test_write_u32_le(&message.payload[4], 1);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    message = ramfs_test_request(MICROS_RAMFS_MESSAGE_MOUNT);
    message.payload[8] = 1;
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    message = ramfs_test_request(MICROS_RAMFS_MESSAGE_MOUNT);
    message.source ^= 1;
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_CALLER
        )
    );
    message = ramfs_test_request(MICROS_RAMFS_MESSAGE_MOUNT);
    message.reply_token = 0;
    fixture->state_snapshot = fixture->state;
    memset(&action, 0xa5, sizeof(action));
    EXPECT_TRUE(
        micros_ramfs_handle_call(
            &fixture->state,
            &message,
            &ramfs_test_io,
            &action
        ) == MICROS_RAMFS_CORE_ERROR_INVARIANT
    );
    EXPECT_TRUE(memcmp(&fixture->state, &fixture->state_snapshot, sizeof(fixture->state)) == 0);

    message = ramfs_test_request(MICROS_RAMFS_MESSAGE_MOUNT);
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(fixture->state.phase == MICROS_RAMFS_PHASE_MOUNTED);
    EXPECT_TRUE(fixture->state.nodes[0].reference_count == 1);
    EXPECT_TRUE(
        ramfs_test_read_u64_le(&action.message.payload[16])
            == micros_ramfs_handle(0, 1)
    );
    EXPECT_TRUE(ramfs_test_read_u64_le(&action.message.payload[24]) == 0);
    EXPECT_TRUE(ramfs_test_read_u64_le(&action.message.payload[32]) == 0);
    EXPECT_TRUE(ramfs_test_read_u32_le(&action.message.payload[40]) == 0);
    EXPECT_TRUE(
        ramfs_test_read_u32_le(&action.message.payload[44])
            == (MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755))
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_STATE
        )
    );

    message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        root,
        MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
        2,
        0
    );
    message.source ^= 1;
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_CALLER
        )
    );
    message.source = ramfs_test_vfs_endpoint;
    ramfs_test_write_u64_le(
        &message.payload[8],
        micros_ramfs_handle(63, 1)
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_NODE
        )
    );
    ramfs_test_write_u64_le(
        &message.payload[8],
        micros_ramfs_handle(1, 1)
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_REFERENCE
        )
    );
    ramfs_test_write_u64_le(&message.payload[8], root);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    EXPECT_TRUE(fixture->bus.calls == 0);

    message = ramfs_test_lookup_request(0, 0, 1, 0);
    message.payload[40] = 1;
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    message = ramfs_test_create_request(
        MICROS_RAMFS_MESSAGE_CREATE,
        0,
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        2,
        0
    );
    message.payload[36] = 1;
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        0,
        0,
        1,
        0
    );
    message.payload[40] = 1;
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    message = ramfs_test_getdents_request(0, 0, 128, 0);
    message.payload[40] = 1;
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );
    message = ramfs_test_putnode_request(0, 1);
    message.payload[20] = 1;
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_MALFORMED
        )
    );

    for (
        result = MICROS_RAMFS_RESULT_REFERENCE;
        result <= MICROS_RAMFS_RESULT_OK;
        result = (enum micros_ramfs_result)((int)result + 1)
    ) {
        EXPECT_TRUE(
            micros_ramfs_build_result(
                MICROS_RAMFS_MESSAGE_READ,
                result,
                0,
                0,
                0,
                0,
                0,
                &built
            ) == MICROS_RAMFS_CORE_OK
        );
        EXPECT_TRUE(
            (int32_t)ramfs_test_read_u32_le(&built.payload[8]) == result
        );
    }
    return true;
}
