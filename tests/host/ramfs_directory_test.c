#include "tests/host/ramfs_test_internal.h"

#include <string.h>

struct ramfs_directory_case {
    struct micros_ramfs_reply_action action;
    struct micros_ipc_message message;
    uint64_t root;
    uint64_t file;
};

static bool directory_seed_records(
    struct ramfs_test_fixture *fixture,
    struct ramfs_directory_case *work
)
{
    EXPECT_TRUE(ramfs_test_initialize_mounted(fixture));
    ramfs_test_reset_bus(fixture);
    work->message = ramfs_test_getdents_request(work->root, 0, 384, 0);
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &work->message, MICROS_RAMFS_RESULT_OK, &work->action)
    );
    EXPECT_TRUE(ramfs_test_read_u32_le(&work->action.message.payload[40]) == 384);
    EXPECT_TRUE(
        ramfs_test_read_u64_le(&work->action.message.payload[32])
            == MICROS_RAMFS_DIRECTORY_CURSOR_END
    );
    EXPECT_TRUE(ramfs_test_read_u64_le(&fixture->bus.storage[0]) == work->root);
    EXPECT_TRUE(ramfs_test_read_u32_le(&fixture->bus.storage[12]) == 1);
    EXPECT_TRUE(fixture->bus.storage[16] == '.');
    EXPECT_TRUE(ramfs_test_read_u64_le(&fixture->bus.storage[128]) == work->root);
    EXPECT_TRUE(ramfs_test_read_u32_le(&fixture->bus.storage[128 + 12]) == 2);
    EXPECT_TRUE(memcmp(&fixture->bus.storage[128 + 16], "..", 2) == 0);
    EXPECT_TRUE(
        ramfs_test_read_u64_le(&fixture->bus.storage[256])
            == micros_ramfs_handle(1, 1)
    );
    EXPECT_TRUE(ramfs_test_read_u32_le(&fixture->bus.storage[256 + 12]) == 3);
    EXPECT_TRUE(memcmp(&fixture->bus.storage[256 + 16], "etc", 3) == 0);
    EXPECT_TRUE(
        fixture->bus.storage[256 + MICROS_RAMFS_DIRECTORY_RECORD_SIZE - 1]
            == 0
    );
    return true;
}

static bool directory_cursor_steps(
    struct ramfs_test_fixture *fixture,
    struct ramfs_directory_case *work
)
{
    ramfs_test_reset_bus(fixture);
    work->message = ramfs_test_getdents_request(work->root, 0, 128, 0);
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &work->message, MICROS_RAMFS_RESULT_OK, &work->action)
    );
    EXPECT_TRUE(ramfs_test_read_u64_le(&work->action.message.payload[32]) == 1);
    ramfs_test_reset_bus(fixture);
    work->message = ramfs_test_getdents_request(work->root, 1, 128, 0);
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &work->message, MICROS_RAMFS_RESULT_OK, &work->action)
    );
    EXPECT_TRUE(ramfs_test_read_u64_le(&work->action.message.payload[32]) == 3);
    ramfs_test_reset_bus(fixture);
    work->message = ramfs_test_getdents_request(work->root, 2, 128, 0);
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &work->message, MICROS_RAMFS_RESULT_OK, &work->action)
    );
    EXPECT_TRUE(
        ramfs_test_read_u64_le(&work->action.message.payload[32])
            == MICROS_RAMFS_DIRECTORY_CURSOR_END
    );
    return true;
}

static bool directory_namespace_continuation(
    struct ramfs_test_fixture *fixture,
    struct ramfs_directory_case *work
)
{
    EXPECT_TRUE(
        ramfs_test_create_node(
            fixture,
            MICROS_RAMFS_MESSAGE_CREATE,
            work->root,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            "file",
            &work->action
        )
    );
    work->file = ramfs_test_read_u64_le(&work->action.message.payload[16]);
    EXPECT_TRUE(
        ramfs_test_create_node(
            fixture,
            MICROS_RAMFS_MESSAGE_MKDIR,
            work->root,
            MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
            "dir",
            &work->action
        )
    );
    ramfs_test_reset_bus(fixture);
    work->message = ramfs_test_getdents_request(work->root, 4, 128, 0);
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &work->message, MICROS_RAMFS_RESULT_OK, &work->action)
    );
    EXPECT_TRUE(ramfs_test_read_u64_le(&fixture->bus.storage[0]) == work->file);
    EXPECT_TRUE(ramfs_test_read_u64_le(&work->action.message.payload[32]) == 6);
    return true;
}

static bool directory_eof_and_errors(
    struct ramfs_test_fixture *fixture,
    struct ramfs_directory_case *work
)
{
    ramfs_test_reset_bus(fixture);
    memset(fixture->bus.storage, 0xa5, sizeof(fixture->bus.storage));
    work->message = ramfs_test_getdents_request(
        work->root,
        MICROS_RAMFS_DIRECTORY_CURSOR_END,
        128,
        0
    );
    EXPECT_TRUE(
        ramfs_test_call_expect(fixture, &work->message, MICROS_RAMFS_RESULT_OK, &work->action)
    );
    EXPECT_TRUE(ramfs_test_read_u32_le(&work->action.message.payload[40]) == 0);
    EXPECT_TRUE(fixture->bus.calls == 0);
    EXPECT_TRUE(fixture->bus.storage[0] == 0xa5);

    work->message = ramfs_test_getdents_request(work->file, 0, 128, 0);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_NOT_DIRECTORY
        )
    );
    work->message = ramfs_test_getdents_request(work->root, 67, 128, 0);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    work->message = ramfs_test_getdents_request(work->root, 0, 127, 0);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    work->message = ramfs_test_getdents_request(work->root, 0, 129, 0);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_RANGE
        )
    );
    ramfs_test_reset_bus(fixture);
    memset(fixture->bus.storage, 0x5a, sizeof(fixture->bus.storage));
    fixture->bus.reject = true;
    work->message = ramfs_test_getdents_request(work->root, 0, 128, 0);
    EXPECT_TRUE(
        ramfs_test_failure_preserves_state(
            fixture,
            &work->message,
            MICROS_RAMFS_RESULT_GRANT
        )
    );
    EXPECT_TRUE(fixture->bus.storage[0] == 0x5a);
    return true;
}

bool ramfs_test_getdents_cursor_and_records_are_exact(
    struct ramfs_test_fixture *fixture
)
{
    struct ramfs_directory_case work;

    work.root = micros_ramfs_handle(0, 1);
    return directory_seed_records(fixture, &work)
        && directory_cursor_steps(fixture, &work)
        && directory_namespace_continuation(fixture, &work)
        && directory_eof_and_errors(fixture, &work);
}
