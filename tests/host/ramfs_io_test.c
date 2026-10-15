#include "tests/host/ramfs_test_internal.h"

#include <string.h>

struct ramfs_io_case {
    struct micros_ramfs_reply_action action;
    struct micros_ipc_message message;
    uint64_t root;
    uint64_t file;
    uint64_t second_file;
    size_t index;
};

static bool io_initial_write_read_eof(
    struct ramfs_test_fixture *fixture,
    struct ramfs_io_case *work
)
{
    EXPECT_TRUE(ramfs_test_initialize_mounted(fixture));
    EXPECT_TRUE(
        ramfs_test_create_node(
            fixture,
            MICROS_RAMFS_MESSAGE_CREATE,
            work->root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            "data",
            &work->action
        )
    );
    work->file = ramfs_test_read_u64_le(&work->action.message.payload[16]);

    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, "abc", 3);
    work->message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        work->file,
        0,
        3,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &work->message, MICROS_RAMFS_RESULT_OK, &work->action)
    );
    EXPECT_TRUE(ramfs_test_read_u64_le(&work->action.message.payload[24]) == 3);
    EXPECT_TRUE(ramfs_test_read_u64_le(&work->action.message.payload[32]) == 3);
    EXPECT_TRUE(ramfs_test_read_u32_le(&work->action.message.payload[40]) == 3);

    ramfs_test_reset_bus(fixture);
    work->message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        work->file,
        0,
        5,
        16
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &work->message, MICROS_RAMFS_RESULT_OK, &work->action)
    );
    EXPECT_TRUE(ramfs_test_read_u32_le(&work->action.message.payload[40]) == 3);
    EXPECT_TRUE(memcmp(&fixture->bus.storage[16], "abc", 3) == 0);

    ramfs_test_reset_bus(fixture);
    memset(fixture->bus.storage, 0xa5, sizeof(fixture->bus.storage));
    work->message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        work->file,
        3,
        4,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &work->message, MICROS_RAMFS_RESULT_OK, &work->action)
    );
    EXPECT_TRUE(ramfs_test_read_u32_le(&work->action.message.payload[40]) == 0);
    EXPECT_TRUE(ramfs_test_read_u64_le(&work->action.message.payload[32]) == 3);
    EXPECT_TRUE(fixture->bus.calls == 0);
    EXPECT_TRUE(fixture->bus.storage[0] == 0xa5);
    return true;
}

static bool io_sparse_and_cross_block(
    struct ramfs_test_fixture *fixture,
    struct ramfs_io_case *work
)
{
    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, "XY", 2);
    work->message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        work->file,
        8194,
        2,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &work->message, MICROS_RAMFS_RESULT_OK, &work->action)
    );
    EXPECT_TRUE(fixture->state.nodes[3].size == 8196);
    EXPECT_TRUE(
        fixture->state.nodes[3].blocks[1] == MICROS_RAMFS_BLOCK_NONE
    );

    ramfs_test_reset_bus(fixture);
    memset(fixture->bus.storage, 0xa5, sizeof(fixture->bus.storage));
    work->message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        work->file,
        4090,
        4106 > MICROS_RAMFS_TRANSFER_MAX
            ? MICROS_RAMFS_TRANSFER_MAX
            : 4106,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &work->message, MICROS_RAMFS_RESULT_OK, &work->action)
    );
    for (work->index = 0; work->index < MICROS_RAMFS_TRANSFER_MAX; ++work->index) {
        EXPECT_TRUE(fixture->bus.storage[work->index] == 0);
    }
    ramfs_test_reset_bus(fixture);
    work->message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        work->file,
        8188,
        8,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &work->message, MICROS_RAMFS_RESULT_OK, &work->action)
    );
    EXPECT_TRUE(memcmp(fixture->bus.storage, "\0\0\0\0\0\0XY", 8) == 0);

    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, "WXYZ", 4);
    work->message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        work->file,
        4094,
        4,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &work->message, MICROS_RAMFS_RESULT_OK, &work->action)
    );
    ramfs_test_reset_bus(fixture);
    work->message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        work->file,
        4094,
        4,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &work->message, MICROS_RAMFS_RESULT_OK, &work->action)
    );
    EXPECT_TRUE(memcmp(fixture->bus.storage, "WXYZ", 4) == 0);
    return true;
}

static bool io_copy_and_range_rejections(
    struct ramfs_test_fixture *fixture,
    struct ramfs_io_case *work
)
{
    ramfs_test_reset_bus(fixture);
    memcpy(fixture->bus.storage, "fail", 4);
    fixture->bus.reject = true;
    work->message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        work->file,
        100,
        4,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_GRANT
        )
    );
    ramfs_test_reset_bus(fixture);
    memset(fixture->bus.storage, 0x5a, sizeof(fixture->bus.storage));
    fixture->bus.reject = true;
    work->message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        work->file,
        0,
        3,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_GRANT
        )
    );
    EXPECT_TRUE(fixture->bus.storage[0] == 0x5a);

    ramfs_test_reset_bus(fixture);
    work->message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        work->root,
        0,
        1,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_IS_DIRECTORY
        )
    );
    work->message.type = MICROS_RAMFS_MESSAGE_WRITE;
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_IS_DIRECTORY
        )
    );
    work->message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_READ,
        work->file,
        0,
        0,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    work->message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        work->file,
        MICROS_RAMFS_FILE_SIZE_MAX,
        1,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    return true;
}

static bool io_maximum_file_boundary(
    struct ramfs_test_fixture *fixture,
    struct ramfs_io_case *work
)
{
    EXPECT_TRUE(ramfs_test_initialize_mounted(fixture));
    EXPECT_TRUE(
        ramfs_test_create_node(
            fixture,
            MICROS_RAMFS_MESSAGE_CREATE,
            work->root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            "maximum",
            &work->action
        )
    );
    work->file = ramfs_test_read_u64_le(&work->action.message.payload[16]);
    ramfs_test_reset_bus(fixture);
    fixture->bus.storage[0] = 0x7f;
    work->message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        work->file,
        MICROS_RAMFS_FILE_SIZE_MAX - 1,
        1,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &work->message, MICROS_RAMFS_RESULT_OK, &work->action)
    );
    EXPECT_TRUE(fixture->state.nodes[3].size == MICROS_RAMFS_FILE_SIZE_MAX);
    return true;
}

static bool io_block_capacity_exhaustion(
    struct ramfs_test_fixture *fixture,
    struct ramfs_io_case *work
)
{
    EXPECT_TRUE(ramfs_test_initialize_mounted(fixture));
    EXPECT_TRUE(
        ramfs_test_create_node(
            fixture,
            MICROS_RAMFS_MESSAGE_CREATE,
            work->root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            "blocks",
            &work->action
        )
    );
    work->file = ramfs_test_read_u64_le(&work->action.message.payload[16]);
    for (work->index = 0; work->index < 63; ++work->index) {
        ramfs_test_reset_bus(fixture);
        fixture->bus.storage[0] = (uint8_t)work->index;
        work->message = ramfs_test_transfer_request(
            MICROS_RAMFS_MESSAGE_WRITE,
            work->file,
            work->index * MICROS_RAMFS_BLOCK_SIZE,
            1,
            0
        );
        EXPECT_TRUE(
            ramfs_test_call_expect(
                fixture,
                &work->message,
                MICROS_RAMFS_RESULT_OK,
                &work->action
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
            work->root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            "second",
            &work->action
        )
    );
    work->second_file = ramfs_test_read_u64_le(&work->action.message.payload[16]);
    ramfs_test_reset_bus(fixture);
    fixture->bus.storage[0] = 1;
    work->message = ramfs_test_transfer_request(
        MICROS_RAMFS_MESSAGE_WRITE,
        work->second_file,
        0,
        1,
        0
    );
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_NO_SPACE
        )
    );
    EXPECT_TRUE(fixture->bus.calls == 0);
    return true;
}

bool ramfs_test_read_write_sparse_and_block_capacity_are_exact(
    struct ramfs_test_fixture *fixture
)
{
    struct ramfs_io_case work;

    work.root = micros_ramfs_handle(0, 1);
    return io_initial_write_read_eof(fixture, &work)
        && io_sparse_and_cross_block(fixture, &work)
        && io_copy_and_range_rejections(fixture, &work)
        && io_maximum_file_boundary(fixture, &work)
        && io_block_capacity_exhaustion(fixture, &work);
}
