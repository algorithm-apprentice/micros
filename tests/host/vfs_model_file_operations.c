#include "tests/host/vfs_model_internal.h"

#include <string.h>

bool model_malformed_or_foreign(struct vfs_model *selected, uint64_t random) {
    struct micros_vfs_request request;
    struct micros_vfs_result_action action;

    if ((random & 1) == 0) {
        request =
            model_request(MICROS_VFS_MESSAGE_OPEN, VFS_MODEL_FIRST_ENDPOINT, selected->step + 1);
        request.path_length = 2;
        MODEL_EXPECT(selected, micros_vfs_handle_request(&selected->production, &request,
                                                         &selected->fixture.io,
                                                         &action) == MICROS_VFS_CORE_OK &&
                                   action.result == MICROS_VFS_RESULT_MALFORMED);
        ++selected->coverage.malformed;
    } else {
        request = model_request(MICROS_VFS_MESSAGE_CLOSE, UINT32_C(0x00003009), selected->step + 1);
        MODEL_EXPECT(selected, micros_vfs_handle_request(&selected->production, &request,
                                                         &selected->fixture.io,
                                                         &action) == MICROS_VFS_CORE_OK &&
                                   action.result == MICROS_VFS_RESULT_CALLER);
        ++selected->coverage.foreign;
    }
    return compare_transition(selected);
}

bool model_mkdir(struct vfs_model *selected) {
    struct micros_vfs_request request =
        model_path_request(selected, MICROS_VFS_MESSAGE_MKDIR, VFS_MODEL_FIRST_ENDPOINT,
                           selected->step + 1, "/tmp", 0, UINT32_C(0755));
    struct micros_vfs_result_action action;
    bool existed = selected->tmp_exists;

    MODEL_EXPECT(selected,
                 request.grant != MICROS_GRANT_NONE &&
                     micros_vfs_handle_request(&selected->production, &request,
                                               &selected->fixture.io,
                                               &action) == MICROS_VFS_CORE_OK &&
                     action.result == (existed ? MICROS_VFS_RESULT_EXISTS : MICROS_VFS_RESULT_OK) &&
                     revoke_request_grant(selected, &request));
    if (!existed) {
        selected->tmp_exists = true;
        MODEL_EXPECT(selected, vfs_test_fixture_find_path(&selected->fixture, "/tmp") ==
                                       reference_node_handle(3) &&
                                   selected->fixture.nodes[3].mode ==
                                       (MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755)));
    }
    ++selected->coverage.mkdir;
    return compare_transition(selected);
}

bool model_open_path(struct vfs_model *selected, micros_endpoint_t endpoint, const char *path,
                     uint32_t flags, uint32_t mode, uint32_t expected_descriptor, bool creation) {
    struct micros_vfs_request request = model_path_request(
        selected, MICROS_VFS_MESSAGE_OPEN, endpoint, selected->step + 1, path, flags, mode);
    struct micros_vfs_result_action action;
    uint64_t node;
    uint32_t expected_mode;
    uint64_t expected_size;
    bool created = false;

    if (!reference_path_metadata(selected, path, &node, &expected_mode, &expected_size)) {
        MODEL_EXPECT(selected, creation && selected->tmp_exists && strcmp(path, "tmp/note") == 0);
        node = reference_node_handle(4);
        expected_mode = MICROS_RAMFS_MODE_REGULAR | mode;
        expected_size = 0;
        created = true;
    }

    MODEL_EXPECT(
        selected,
        request.grant != MICROS_GRANT_NONE &&
            micros_vfs_handle_request(&selected->production, &request, &selected->fixture.io,
                                      &action) == MICROS_VFS_CORE_OK &&
            action.result == MICROS_VFS_RESULT_OK && action.descriptor == expected_descriptor &&
            action.mode == expected_mode && revoke_request_grant(selected, &request));
    if (created) {
        selected->note_exists = true;
        selected->note_size = 0;
    }
    MODEL_EXPECT(selected,
                 vfs_test_fixture_find_path(&selected->fixture, absolute_model_path(path)) ==
                         node &&
                     selected->fixture.nodes[(size_t)(uint16_t)node].mode == expected_mode &&
                     selected->fixture.nodes[(size_t)(uint16_t)node].size == expected_size &&
                     reference_open(selected, endpoint, node, expected_mode, expected_size, flags,
                                    expected_descriptor));
    ++selected->coverage.open;
    if (creation) {
        ++selected->coverage.create;
    }
    return compare_transition(selected);
}

bool model_share_motd(struct vfs_model *selected) {
    enum micros_vfs_trusted_result result;

    MODEL_EXPECT(selected, micros_vfs_share_descriptor(
                               &selected->production, VFS_MODEL_FIRST_ENDPOINT, 3,
                               VFS_MODEL_SECOND_ENDPOINT, 3, &result) == MICROS_VFS_CORE_OK &&
                               result == MICROS_VFS_TRUSTED_OK &&
                               reference_share(&selected->reference, VFS_MODEL_FIRST_ENDPOINT, 3,
                                               VFS_MODEL_SECOND_ENDPOINT, 3));
    ++selected->coverage.shared;
    return compare_transition(selected);
}

bool model_read(struct vfs_model *selected, micros_endpoint_t endpoint, uint32_t descriptor,
                uint32_t count, bool valid) {
    struct micros_vfs_request request =
        model_request(MICROS_VFS_MESSAGE_READ, endpoint, selected->step + 1);
    struct micros_vfs_result_action action;
    struct micros_vfs_open_file *open_file =
        reference_open_file(&selected->reference, endpoint, descriptor);
    micros_grant_t grant;
    uint64_t expected_count;
    uint64_t expected_position;
    uint64_t size;

    MODEL_EXPECT(selected, open_file != NULL);
    size = selected->reference.vnodes[open_file->vnode - 1].size;
    grant = vfs_test_fixture_add_application_grant(
        &selected->fixture, endpoint,
        valid ? MICROS_GRANT_PERMISSION_WRITE : MICROS_GRANT_PERMISSION_READ, NULL, count);
    request.descriptor = descriptor;
    request.grant = grant;
    request.count = count;
    expected_count =
        open_file->position >= size
            ? 0
            : (count < size - open_file->position ? count : size - open_file->position);
    expected_position = open_file->position + expected_count;
    MODEL_EXPECT(selected,
                 grant != MICROS_GRANT_NONE &&
                     micros_vfs_handle_request(&selected->production, &request,
                                               &selected->fixture.io,
                                               &action) == MICROS_VFS_CORE_OK &&
                     action.result == (valid ? MICROS_VFS_RESULT_OK : MICROS_VFS_RESULT_GRANT) &&
                     (!valid || (action.transferred_count == expected_count &&
                                 action.position == expected_position)) &&
                     vfs_test_fixture_revoke_application_grant(&selected->fixture, grant));
    if (valid) {
        open_file->position = expected_position;
        ++selected->coverage.read;
    } else {
        ++selected->coverage.grant_failure;
    }
    return compare_transition(selected);
}

bool model_write_note(struct vfs_model *selected) {
    static const uint8_t bytes[] = "hello";
    struct micros_vfs_request request =
        model_request(MICROS_VFS_MESSAGE_WRITE, VFS_MODEL_FIRST_ENDPOINT, selected->step + 1);
    struct micros_vfs_result_action action;
    struct micros_vfs_open_file *open_file =
        reference_open_file(&selected->reference, VFS_MODEL_FIRST_ENDPOINT, 3);
    struct micros_vfs_vnode *vnode;
    micros_grant_t grant;
    uint64_t expected_position;

    MODEL_EXPECT(selected, open_file != NULL);
    vnode = &selected->reference.vnodes[open_file->vnode - 1];
    grant = vfs_test_fixture_add_application_grant(&selected->fixture, VFS_MODEL_FIRST_ENDPOINT,
                                                   MICROS_GRANT_PERMISSION_READ, bytes,
                                                   sizeof(bytes) - 1);
    request.descriptor = 3;
    request.grant = grant;
    request.count = sizeof(bytes) - 1;
    expected_position = open_file->position + sizeof(bytes) - 1;
    MODEL_EXPECT(
        selected,
        grant != MICROS_GRANT_NONE &&
            micros_vfs_handle_request(&selected->production, &request, &selected->fixture.io,
                                      &action) == MICROS_VFS_CORE_OK &&
            action.result == MICROS_VFS_RESULT_OK && action.position == expected_position &&
            action.transferred_count == sizeof(bytes) - 1 &&
            vfs_test_fixture_revoke_application_grant(&selected->fixture, grant));
    open_file->position = expected_position;
    if (vnode->size < expected_position) {
        vnode->size = expected_position;
    }
    if (selected->note_size < expected_position) {
        selected->note_size = expected_position;
    }
    ++selected->coverage.write;
    MODEL_EXPECT(selected, compare_transition(selected));

    grant = vfs_test_fixture_add_application_grant(&selected->fixture, VFS_MODEL_FIRST_ENDPOINT,
                                                   MICROS_GRANT_PERMISSION_WRITE, bytes,
                                                   sizeof(bytes) - 1);
    request.grant = grant;
    MODEL_EXPECT(selected,
                 grant != MICROS_GRANT_NONE &&
                     micros_vfs_handle_request(&selected->production, &request,
                                               &selected->fixture.io,
                                               &action) == MICROS_VFS_CORE_OK &&
                     action.result == MICROS_VFS_RESULT_GRANT &&
                     vfs_test_fixture_revoke_application_grant(&selected->fixture, grant));
    ++selected->coverage.grant_failure;
    return compare_transition(selected);
}

bool model_close(struct vfs_model *selected, micros_endpoint_t endpoint, uint32_t descriptor) {
    struct micros_vfs_request request =
        model_request(MICROS_VFS_MESSAGE_CLOSE, endpoint, selected->step + 1);
    struct micros_vfs_result_action action;

    request.descriptor = descriptor;
    MODEL_EXPECT(selected,
                 micros_vfs_handle_request(&selected->production, &request, &selected->fixture.io,
                                           &action) == MICROS_VFS_CORE_OK &&
                     action.result == MICROS_VFS_RESULT_OK &&
                     reference_close(&selected->reference, endpoint, descriptor));
    ++selected->coverage.close;
    return compare_transition(selected);
}

bool model_chdir_path(struct vfs_model *selected, const char *path) {
    struct micros_vfs_request request =
        model_path_request(selected, MICROS_VFS_MESSAGE_CHDIR, VFS_MODEL_FIRST_ENDPOINT,
                           selected->step + 1, path, 0, 0);
    struct micros_vfs_result_action action;
    uint64_t node;
    uint32_t mode;
    uint64_t size;

    MODEL_EXPECT(selected,
                 reference_path_metadata(selected, path, &node, &mode, &size) &&
                     (mode & MICROS_RAMFS_MODE_TYPE_MASK) == MICROS_RAMFS_MODE_DIRECTORY &&
                     size == 0);

    MODEL_EXPECT(selected, request.grant != MICROS_GRANT_NONE &&
                               micros_vfs_handle_request(&selected->production, &request,
                                                         &selected->fixture.io,
                                                         &action) == MICROS_VFS_CORE_OK &&
                               action.result == MICROS_VFS_RESULT_OK &&
                               revoke_request_grant(selected, &request));
    MODEL_EXPECT(selected, vfs_test_fixture_find_path(&selected->fixture, path) == node &&
                               reference_chdir(selected, VFS_MODEL_FIRST_ENDPOINT, node, mode));
    ++selected->coverage.chdir;
    return compare_transition(selected);
}

bool model_getdents(struct vfs_model *selected, bool reject_copy, uint64_t expected_position,
                    uint64_t expected_count) {
    struct micros_vfs_request request =
        model_request(MICROS_VFS_MESSAGE_GETDENTS, VFS_MODEL_FIRST_ENDPOINT, selected->step + 1);
    struct micros_vfs_result_action action;
    struct micros_vfs_open_file *open_file =
        reference_open_file(&selected->reference, VFS_MODEL_FIRST_ENDPOINT, 3);
    micros_grant_t grant;

    MODEL_EXPECT(selected, open_file != NULL);
    grant = vfs_test_fixture_add_application_grant(&selected->fixture, VFS_MODEL_FIRST_ENDPOINT,
                                                   MICROS_GRANT_PERMISSION_WRITE, NULL, 160);
    request.descriptor = 3;
    request.grant = grant;
    request.count = 160;
    selected->fixture.reject_client_copy = reject_copy;
    MODEL_EXPECT(
        selected,
        grant != MICROS_GRANT_NONE &&
            micros_vfs_handle_request(&selected->production, &request, &selected->fixture.io,
                                      &action) == MICROS_VFS_CORE_OK &&
            action.result == (reject_copy ? MICROS_VFS_RESULT_GRANT : MICROS_VFS_RESULT_OK) &&
            (reject_copy || (action.position == expected_position &&
                             action.transferred_count == expected_count)) &&
            vfs_test_fixture_revoke_application_grant(&selected->fixture, grant));
    selected->fixture.reject_client_copy = false;
    if (!reject_copy) {
        open_file->position = expected_position;
        ++selected->coverage.getdents;
    } else {
        ++selected->coverage.grant_failure;
    }
    return compare_transition(selected);
}
