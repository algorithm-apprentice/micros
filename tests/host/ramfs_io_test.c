#include "tests/host/ramfs_test_internal.h"

#include <string.h>

bool ramfs_test_read_write_sparse_and_block_capacity_are_exact(
    struct ramfs_test_fixture *fixture
)
{
    struct micros_ramfs_reply_action action;
    struct micros_ipc_message message;
    uint64_t root = micros_ramfs_handle(0, 1);
    uint64_t file;
    uint64_t second_file;
    size_t index;

    EXPECT_TRUE(ramfs_test_initialize_mounted(fixture));
    EXPECT_TRUE(
        ramfs_test_create_node(
            fixture,
            MICROS_RAMFS_MESSAGE_CREATE,
            root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            "data",
            &action
        )
    );
    file = ramfs_test_read_u64_le(&action.message.payload[16]);

    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, "abc", 3);
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        file,
        0,
        3,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(ramfs_test_read_u64_le(&action.message.payload[24]) == 3);
    EXPECT_TRUE(ramfs_test_read_u64_le(&action.message.payload[32]) == 3);
    EXPECT_TRUE(ramfs_test_read_u32_le(&action.message.payload[40]) == 3);

    ramfs_test_reset_bus(fixture);
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        file,
        0,
        5,
        16
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(ramfs_test_read_u32_le(&action.message.payload[40]) == 3);
    EXPECT_TRUE(memcmp(&fixture->bus.storage[16], "abc", 3) == 0);

    ramfs_test_reset_bus(fixture);
    memset(fixture->bus.storage, 0xa5, sizeof(fixture->bus.storage));
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        file,
        3,
        4,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(ramfs_test_read_u32_le(&action.message.payload[40]) == 0);
    EXPECT_TRUE(ramfs_test_read_u64_le(&action.message.payload[32]) == 3);
    EXPECT_TRUE(fixture->bus.calls == 0);
    EXPECT_TRUE(fixture->bus.storage[0] == 0xa5);

    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, "XY", 2);
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        file,
        8194,
        2,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(fixture->state.nodes[3].size == 8196);
    EXPECT_TRUE(
        fixture->state.nodes[3].blocks[1] == MICROS_RAMFS_BLOCK_NONE
    );

    ramfs_test_reset_bus(fixture);
    memset(fixture->bus.storage, 0xa5, sizeof(fixture->bus.storage));
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        file,
        4090,
        4106 > MICROS_RAMFS_TRANSFER_MAX
            ? MICROS_RAMFS_TRANSFER_MAX
            : 4106,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &message, MICROS_RAMFS_RESULT_OK, &action)
    );
    for (index = 0; index < MICROS_RAMFS_TRANSFER_MAX; ++index) {
        EXPECT_TRUE(fixture->bus.storage[index] == 0);
    }
    ramfs_test_reset_bus(fixture);
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        file,
        8188,
        8,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(memcmp(fixture->bus.storage, "\0\0\0\0\0\0XY", 8) == 0);

    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, "WXYZ", 4);
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        file,
        4094,
        4,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &message, MICROS_RAMFS_RESULT_OK, &action)
    );
    ramfs_test_reset_bus(fixture);
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        file,
        4094,
        4,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(memcmp(fixture->bus.storage, "WXYZ", 4) == 0);

    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, "fail", 4);
    fixture->bus.reject = true;
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        file,
        100,
        4,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_GRANT
        )
    );
    ramfs_test_reset_bus(fixture);
    memset(fixture->bus.storage, 0x5a, sizeof(fixture->bus.storage));
    fixture->bus.reject = true;
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        file,
        0,
        3,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_GRANT
        )
    );
    EXPECT_TRUE(fixture->bus.storage[0] == 0x5a);

    ramfs_test_reset_bus(fixture);
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        root,
        0,
        1,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_IS_DIRECTORY
        )
    );
    message.type = MICROS_RAMFS_MESSAGE_WRITE;
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_IS_DIRECTORY
        )
    );
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        file,
        0,
        0,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        file,
        MICROS_RAMFS_FILE_SIZE_MAX,
        1,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );

    EXPECT_TRUE(ramfs_test_initialize_mounted(fixture));
    EXPECT_TRUE(
        ramfs_test_create_node(
            fixture,
            MICROS_RAMFS_MESSAGE_CREATE,
            root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            "maximum",
            &action
        )
    );
    file = ramfs_test_read_u64_le(&action.message.payload[16]);
    ramfs_test_reset_bus(fixture);
    fixture->bus.storage[0] = 0x7f;
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        file,
        MICROS_RAMFS_FILE_SIZE_MAX - 1,
        1,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &message, MICROS_RAMFS_RESULT_OK, &action)
    );
    EXPECT_TRUE(fixture->state.nodes[3].size == MICROS_RAMFS_FILE_SIZE_MAX);

    EXPECT_TRUE(ramfs_test_initialize_mounted(fixture));
    EXPECT_TRUE(
        ramfs_test_create_node(
            fixture,
            MICROS_RAMFS_MESSAGE_CREATE,
            root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            "blocks",
            &action
        )
    );
    file = ramfs_test_read_u64_le(&action.message.payload[16]);
    for (index = 0; index < 63; ++index) {
        ramfs_test_reset_bus(fixture);
        fixture->bus.storage[0] = (uint8_t)index;
        message = ramfs_test_transfer_request(
            MICROS_RAMFS_MESSAGE_WRITE,
            file,
            index * MICROS_RAMFS_BLOCK_SIZE,
            1,
            0
        );
        EXPECT_TRUE(
            ramfs_test_call_expect(
                fixture,
                &message,
                MICROS_RAMFS_RESULT_OK,
                &action
            )
        );
    }
    EXPECT_TRUE(
        fixture->state.allocated_block_count == MICROS_RAMFS_BLOCK_CAPACITY
    );
    EXPECT_TRUE(
        ramfs_test_create_node(
            fixture,
            MICROS_RAMFS_MESSAGE_CREATE,
            root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            "second",
            &action
        )
    );
    second_file = ramfs_test_read_u64_le(&action.message.payload[16]);
    ramfs_test_reset_bus(fixture);
    fixture->bus.storage[0] = 1;
    message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        second_file,
        0,
        1,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &message,
            MICROS_RAMFS_RESULT_NO_SPACE
        )
    );
    EXPECT_TRUE(fixture->bus.calls == 0);
    return true;
}
