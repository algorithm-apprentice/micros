#include "tests/qemu/ramfs_service_vfs_internal.h"

static bool vfs_directory_record_matches(
    struct vfs_service_fixture *fixture,
    const uint8_t *name,
    size_t name_length
)
{
    if (
        vfs_read_u64_le(&fixture->vfs_ramfs_buffer[0]) == 0
        || (
            vfs_read_u32_le(&fixture->vfs_ramfs_buffer[8])
            & MICROS_RAMFS_MODE_TYPE_MASK
        ) != MICROS_RAMFS_MODE_DIRECTORY
        || vfs_read_u32_le(&fixture->vfs_ramfs_buffer[12]) != name_length
        || !vfs_bytes_equal(
            &fixture->vfs_ramfs_buffer[16],
            name,
            name_length
        )
    ) {
        return false;
    }
    return vfs_bytes_are_zero(
        &fixture->vfs_ramfs_buffer[16 + name_length],
        MICROS_RAMFS_DIRECTORY_RECORD_SIZE - 16 - name_length
    );
}

static bool vfs_enumerate_root(
    struct vfs_service_fixture *fixture,
    uint64_t root
)
{
    static const uint8_t *const names[] = {
        (const uint8_t *)".",
        (const uint8_t *)"..",
        (const uint8_t *)"etc",
        (const uint8_t *)"tmp",
    };
    static const size_t lengths[] = {1, 2, 3, 3};
    struct vfs_ramfs_result result;
    uint64_t cursor = 0;
    size_t index;

    for (index = 0; index < sizeof(lengths) / sizeof(lengths[0]); ++index) {
        if (
            !vfs_getdent(fixture, root, cursor, &result)
            || result.result != MICROS_RAMFS_RESULT_OK
            || result.node != 0
            || result.file_size != 0
            || result.count != MICROS_RAMFS_DIRECTORY_RECORD_SIZE
            || result.mode != 0
            || !vfs_directory_record_matches(fixture, names[index], lengths[index])
        ) {
            return false;
        }
        cursor = result.position;
    }
    return cursor == MICROS_RAMFS_DIRECTORY_CURSOR_END;
}

bool vfs_run_filesystem_scenario(
    struct vfs_service_fixture *fixture
)
{
    static const uint8_t motd_path[] = "/etc/motd";
    static const uint8_t motd[] = MICROS_RAMFS_TEST_MOTD;
    static const uint8_t tmp_name[] = "tmp";
    static const uint8_t note_name[] = "note";
    static const uint8_t note_data[] = MICROS_RAMFS_TEST_NOTE_DATA;
    static const uint8_t sparse_read[] = {
        0,
        0,
        0,
        'n',
        'o',
        't',
        'e',
    };
    struct vfs_ramfs_result result;
    uint64_t root;
    uint64_t motd_node;
    uint64_t tmp_node;
    uint64_t note_node;
    uint64_t stale_node;

    if (
        !vfs_mount(fixture, &root)
        || !vfs_malformed_version_is_exact(fixture, root)
        || !vfs_lookup(
            fixture,
            root,
            root,
            motd_path,
            sizeof(motd_path),
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.file_size != sizeof(motd) - 1
        || result.mode
            != (MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644))
    ) {
        return false;
    }
    motd_node = result.node;
    vfs_fill_bytes(
        fixture->vfs_ramfs_buffer,
        UINT8_C(0xa5),
        sizeof(fixture->vfs_ramfs_buffer)
    );
    if (
        !vfs_transfer(
            fixture,
            MICROS_RAMFS_MESSAGE_READ,
            motd_node,
            0,
            sizeof(motd) - 1,
            MICROS_GRANT_PERMISSION_READ,
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_GRANT
        || !vfs_result_has_no_fields(&result)
        || !vfs_transfer(
            fixture,
            MICROS_RAMFS_MESSAGE_READ,
            motd_node,
            0,
            sizeof(motd),
            MICROS_GRANT_PERMISSION_WRITE,
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.file_size != sizeof(motd) - 1
        || result.position != sizeof(motd) - 1
        || result.count != sizeof(motd) - 1
        || !vfs_bytes_equal(
            fixture->vfs_ramfs_buffer,
            motd,
            sizeof(motd) - 1
        )
        || !vfs_transfer(
            fixture,
            MICROS_RAMFS_MESSAGE_READ,
            motd_node,
            sizeof(motd) - 1,
            1,
            MICROS_GRANT_PERMISSION_WRITE,
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.file_size != sizeof(motd) - 1
        || result.position != sizeof(motd) - 1
        || result.count != 0
    ) {
        return false;
    }
    stale_node = motd_node + (UINT64_C(1) << 32);
    if (
        !vfs_transfer_without_grant(
            fixture,
            MICROS_RAMFS_MESSAGE_READ,
            stale_node,
            0,
            1,
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_NODE
        || !vfs_result_has_no_fields(&result)
        || !vfs_putnode(fixture, motd_node, 1, MICROS_RAMFS_RESULT_OK)
        || !vfs_create_node(
            fixture,
            MICROS_RAMFS_MESSAGE_MKDIR,
            root,
            MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
            tmp_name,
            sizeof(tmp_name),
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || !vfs_result_has_no_fields(&result)
        || !vfs_lookup(
            fixture,
            root,
            root,
            tmp_name,
            sizeof(tmp_name),
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.file_size != 0
        || result.mode
            != (MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755))
    ) {
        return false;
    }
    tmp_node = result.node;
    if (
        !vfs_create_node(
            fixture,
            MICROS_RAMFS_MESSAGE_CREATE,
            tmp_node,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            note_name,
            sizeof(note_name),
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.file_size != 0
        || result.position != 0
        || result.count != 0
        || result.mode
            != (MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644))
    ) {
        return false;
    }
    note_node = result.node;
    if (
        !vfs_putnode(fixture, note_node, 2, MICROS_RAMFS_RESULT_REFERENCE)
    ) {
        return false;
    }
    vfs_clear_bytes(fixture->vfs_ramfs_buffer, sizeof(fixture->vfs_ramfs_buffer));
    vfs_copy_bytes(
        fixture->vfs_ramfs_buffer,
        note_data,
        sizeof(note_data) - 1
    );
    if (
        !vfs_transfer(
            fixture,
            MICROS_RAMFS_MESSAGE_WRITE,
            note_node,
            MICROS_RAMFS_TEST_SPARSE_OFFSET,
            sizeof(note_data) - 1,
            MICROS_GRANT_PERMISSION_READ,
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.file_size
            != MICROS_RAMFS_TEST_SPARSE_OFFSET
                + sizeof(note_data) - 1
        || result.position != result.file_size
        || result.count != sizeof(note_data) - 1
        || !vfs_transfer(
            fixture,
            MICROS_RAMFS_MESSAGE_READ,
            note_node,
            MICROS_RAMFS_BLOCK_SIZE,
            sizeof(sparse_read),
            MICROS_GRANT_PERMISSION_WRITE,
            &result
        )
        || result.result != MICROS_RAMFS_RESULT_OK
        || result.file_size
            != MICROS_RAMFS_TEST_SPARSE_OFFSET
                + sizeof(note_data) - 1
        || result.position
            != MICROS_RAMFS_BLOCK_SIZE + sizeof(sparse_read)
        || result.count != sizeof(sparse_read)
        || !vfs_bytes_equal(
            fixture->vfs_ramfs_buffer,
            sparse_read,
            sizeof(sparse_read)
        )
        || !vfs_enumerate_root(fixture, root)
        || !vfs_putnode(fixture, tmp_node, 1, MICROS_RAMFS_RESULT_OK)
        || !vfs_putnode(fixture, note_node, 1, MICROS_RAMFS_RESULT_OK)
    ) {
        return false;
    }
    return true;
}
