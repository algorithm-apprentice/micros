#include "tests/qemu/ramfs_service_vfs_internal.h"

struct ramfs_filesystem_case {
    struct vfs_ramfs_result result;
    uint64_t root;
    uint64_t motd_node;
    uint64_t tmp_node;
    uint64_t note_node;
    uint64_t stale_node;
};

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

static bool filesystem_mount_seed(
    struct vfs_service_fixture *fixture,
    struct ramfs_filesystem_case *work
)
{
    if (
        !vfs_mount(fixture, &work->root)
        || !vfs_malformed_version_is_exact(fixture, work->root)
        || !vfs_lookup(
            fixture,
            work->root,
            work->root,
            motd_path,
            sizeof(motd_path),
            &work->result
        )
        || work->result.result != MICROS_RAMFS_RESULT_OK
        || work->result.file_size != sizeof(motd) - 1
        || work->result.mode
            != (MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644))
    ) {
        return false;
    }
    work->motd_node = work->result.node;
    return true;
}

static bool filesystem_seed_rights_eof(
    struct vfs_service_fixture *fixture,
    struct ramfs_filesystem_case *work
)
{
    vfs_fill_bytes(
        fixture->vfs_ramfs_buffer,
        UINT8_C(0xa5),
        sizeof(fixture->vfs_ramfs_buffer)
    );
    if (
        !vfs_transfer(
            fixture,
            MICROS_RAMFS_MESSAGE_READ,
            work->motd_node,
            0,
            sizeof(motd) - 1,
            MICROS_GRANT_PERMISSION_READ,
            &work->result
        )
        || work->result.result != MICROS_RAMFS_RESULT_GRANT
        || !vfs_result_has_no_fields(&work->result)
        || !vfs_transfer(
            fixture,
            MICROS_RAMFS_MESSAGE_READ,
            work->motd_node,
            0,
            sizeof(motd),
            MICROS_GRANT_PERMISSION_WRITE,
            &work->result
        )
        || work->result.result != MICROS_RAMFS_RESULT_OK
        || work->result.file_size != sizeof(motd) - 1
        || work->result.position != sizeof(motd) - 1
        || work->result.count != sizeof(motd) - 1
        || !vfs_bytes_equal(
            fixture->vfs_ramfs_buffer,
            motd,
            sizeof(motd) - 1
        )
        || !vfs_transfer(
            fixture,
            MICROS_RAMFS_MESSAGE_READ,
            work->motd_node,
            sizeof(motd) - 1,
            1,
            MICROS_GRANT_PERMISSION_WRITE,
            &work->result
        )
        || work->result.result != MICROS_RAMFS_RESULT_OK
        || work->result.file_size != sizeof(motd) - 1
        || work->result.position != sizeof(motd) - 1
        || work->result.count != 0
    ) {
        return false;
    }
    return true;
}

static bool filesystem_stale_and_directory(
    struct vfs_service_fixture *fixture,
    struct ramfs_filesystem_case *work
)
{
    work->stale_node = work->motd_node + (UINT64_C(1) << 32);
    if (
        !vfs_transfer_without_grant(
            fixture,
            MICROS_RAMFS_MESSAGE_READ,
            work->stale_node,
            0,
            1,
            &work->result
        )
        || work->result.result != MICROS_RAMFS_RESULT_NODE
        || !vfs_result_has_no_fields(&work->result)
        || !vfs_putnode(fixture, work->motd_node, 1, MICROS_RAMFS_RESULT_OK)
        || !vfs_create_node(
            fixture,
            MICROS_RAMFS_MESSAGE_MKDIR,
            work->root,
            MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
            tmp_name,
            sizeof(tmp_name),
            &work->result
        )
        || work->result.result != MICROS_RAMFS_RESULT_OK
        || !vfs_result_has_no_fields(&work->result)
        || !vfs_lookup(
            fixture,
            work->root,
            work->root,
            tmp_name,
            sizeof(tmp_name),
            &work->result
        )
        || work->result.result != MICROS_RAMFS_RESULT_OK
        || work->result.file_size != 0
        || work->result.mode
            != (MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755))
    ) {
        return false;
    }
    work->tmp_node = work->result.node;
    return true;
}

static bool filesystem_file_reference(
    struct vfs_service_fixture *fixture,
    struct ramfs_filesystem_case *work
)
{
    if (
        !vfs_create_node(
            fixture,
            MICROS_RAMFS_MESSAGE_CREATE,
            work->tmp_node,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            note_name,
            sizeof(note_name),
            &work->result
        )
        || work->result.result != MICROS_RAMFS_RESULT_OK
        || work->result.file_size != 0
        || work->result.position != 0
        || work->result.count != 0
        || work->result.mode
            != (MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644))
    ) {
        return false;
    }
    work->note_node = work->result.node;
    if (
        !vfs_putnode(fixture, work->note_node, 2, MICROS_RAMFS_RESULT_REFERENCE)
    ) {
        return false;
    }
    return true;
}

static bool filesystem_sparse_directory_release(
    struct vfs_service_fixture *fixture,
    struct ramfs_filesystem_case *work
)
{
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
            work->note_node,
            MICROS_RAMFS_TEST_SPARSE_OFFSET,
            sizeof(note_data) - 1,
            MICROS_GRANT_PERMISSION_READ,
            &work->result
        )
        || work->result.result != MICROS_RAMFS_RESULT_OK
        || work->result.file_size
            != MICROS_RAMFS_TEST_SPARSE_OFFSET
                + sizeof(note_data) - 1
        || work->result.position != work->result.file_size
        || work->result.count != sizeof(note_data) - 1
        || !vfs_transfer(
            fixture,
            MICROS_RAMFS_MESSAGE_READ,
            work->note_node,
            MICROS_RAMFS_BLOCK_SIZE,
            sizeof(sparse_read),
            MICROS_GRANT_PERMISSION_WRITE,
            &work->result
        )
        || work->result.result != MICROS_RAMFS_RESULT_OK
        || work->result.file_size
            != MICROS_RAMFS_TEST_SPARSE_OFFSET
                + sizeof(note_data) - 1
        || work->result.position
            != MICROS_RAMFS_BLOCK_SIZE + sizeof(sparse_read)
        || work->result.count != sizeof(sparse_read)
        || !vfs_bytes_equal(
            fixture->vfs_ramfs_buffer,
            sparse_read,
            sizeof(sparse_read)
        )
        || !vfs_enumerate_root(fixture, work->root)
        || !vfs_putnode(fixture, work->tmp_node, 1, MICROS_RAMFS_RESULT_OK)
        || !vfs_putnode(fixture, work->note_node, 1, MICROS_RAMFS_RESULT_OK)
    ) {
        return false;
    }
    return true;
}

bool vfs_run_filesystem_scenario(
    struct vfs_service_fixture *fixture
)
{
    struct ramfs_filesystem_case work;

    return filesystem_mount_seed(fixture, &work)
        && filesystem_seed_rights_eof(fixture, &work)
        && filesystem_stale_and_directory(fixture, &work)
        && filesystem_file_reference(fixture, &work)
        && filesystem_sparse_directory_release(fixture, &work);
}
