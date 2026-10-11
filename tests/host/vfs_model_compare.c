#include "tests/host/vfs_model_internal.h"

#include <string.h>

bool compare_complete_state(struct vfs_model *selected) {
    size_t expected_backend_grants = 0;
    size_t node_slot;

    if (selected->reference.pending.state == MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION ||
        selected->reference.pending.state == MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE) {
        expected_backend_grants = 1;
    }
    MODEL_EXPECT(selected, micros_vfs_state_validate(&selected->production) == MICROS_VFS_CORE_OK);
    MODEL_EXPECT(
        selected,
        selected->production.phase == MICROS_VFS_PHASE_MOUNTED &&
            selected->production.self_endpoint == VFS_MODEL_VFS_ENDPOINT &&
            selected->production.ramfs_endpoint == VFS_MODEL_RAMFS_ENDPOINT &&
            selected->production.tty_endpoint == VFS_MODEL_TTY_ENDPOINT &&
            selected->production.next_tty_request_id == selected->reference.next_tty_request_id &&
            selected->production.client_page_owner == selected->reference.client_page_owner &&
            selected->production.backend_page_owner == selected->reference.backend_page_owner &&
            memcmp(&selected->production.pending, &selected->reference.pending,
                   sizeof(selected->reference.pending)) == 0 &&
            memcmp(selected->production.processes, selected->reference.processes,
                   sizeof(selected->reference.processes)) == 0 &&
            memcmp(selected->production.open_files, selected->reference.open_files,
                   sizeof(selected->reference.open_files)) == 0 &&
            memcmp(selected->production.vnodes, selected->reference.vnodes,
                   sizeof(selected->reference.vnodes)) == 0 &&
            selected->fixture.backend_grant_count == expected_backend_grants);
    if (selected->reference.pending.state == MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE) {
        MODEL_EXPECT(selected,
                     selected->pending_write_length == selected->reference.pending.count &&
                         memcmp(selected->production.backend_page, selected->pending_write_bytes,
                                selected->pending_write_length) == 0);
    } else {
        MODEL_EXPECT(selected, selected->pending_write_length == 0);
    }
    for (node_slot = 0; node_slot < MICROS_RAMFS_NODE_CAPACITY; ++node_slot) {
        MODEL_EXPECT(selected, selected->fixture.nodes[node_slot].backend_references ==
                                   reference_backend_references(&selected->reference, node_slot));
    }
    return true;
}

bool compare_transition(struct vfs_model *selected) {
    if (!compare_complete_state(selected)) {
        return false;
    }
    ++selected->transition_checks;
    return true;
}
