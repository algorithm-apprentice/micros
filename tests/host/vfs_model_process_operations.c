#include "tests/host/vfs_model_internal.h"

bool model_attach_second(struct vfs_model *selected) {
    enum micros_vfs_trusted_result result;

    MODEL_EXPECT(selected,
                 micros_vfs_attach_console(&selected->production, VFS_MODEL_SECOND_ENDPOINT, 0,
                                           &result) == MICROS_VFS_CORE_OK &&
                     result == MICROS_VFS_TRUSTED_OK &&
                     reference_attach(&selected->reference, VFS_MODEL_SECOND_ENDPOINT));
    ++selected->coverage.attach;
    return compare_transition(selected);
}

bool model_detach_and_finish_cycle(struct vfs_model *selected) {
    enum micros_vfs_trusted_result result;
    size_t process_slot;
    struct micros_vfs_request request;
    struct micros_vfs_result_action action;

    if (reference_find_process(&selected->reference, VFS_MODEL_SECOND_ENDPOINT, &process_slot)) {
        MODEL_EXPECT(selected,
                     micros_vfs_detach(&selected->production, VFS_MODEL_SECOND_ENDPOINT,
                                       &selected->fixture.io, &result) == MICROS_VFS_CORE_OK &&
                         result == MICROS_VFS_TRUSTED_OK &&
                         reference_detach(&selected->reference, VFS_MODEL_SECOND_ENDPOINT));
        ++selected->coverage.detach;
    } else {
        MODEL_EXPECT(selected,
                     micros_vfs_detach(&selected->production, VFS_MODEL_SECOND_ENDPOINT,
                                       &selected->fixture.io, &result) == MICROS_VFS_CORE_OK &&
                         result == MICROS_VFS_TRUSTED_NOT_FOUND);
    }
    MODEL_EXPECT(selected, compare_transition(selected));
    MODEL_EXPECT(selected, vfs_test_fixture_drain_output(&selected->fixture) == 0);
    request = model_request(MICROS_VFS_MESSAGE_CLOSE, VFS_MODEL_FIRST_ENDPOINT, selected->step + 1);
    request.version = 2;
    MODEL_EXPECT(selected,
                 micros_vfs_handle_request(&selected->production, &request, &selected->fixture.io,
                                           &action) == MICROS_VFS_CORE_OK &&
                     action.result == MICROS_VFS_RESULT_BAD_VERSION);
    ++selected->coverage.malformed;
    return compare_transition(selected);
}
