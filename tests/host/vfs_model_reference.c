#include "tests/host/vfs_model_internal.h"

#include <string.h>

uint64_t reference_node_handle(size_t slot) {
    return UINT64_C(0x0000000100000000) | slot;
}

bool reference_path_metadata(const struct vfs_model *selected, const char *path, uint64_t *node,
                             uint32_t *mode, uint64_t *size) {
    if (strcmp(path, "/") == 0) {
        *node = reference_node_handle(0);
        *mode = MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755);
        *size = 0;
        return true;
    }
    if (strcmp(path, "/etc/motd") == 0) {
        *node = reference_node_handle(2);
        *mode = MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644);
        *size = MODEL_MOTD_SIZE;
        return true;
    }
    if (strcmp(path, "/tmp") == 0 && selected->tmp_exists) {
        *node = reference_node_handle(3);
        *mode = MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755);
        *size = 0;
        return true;
    }
    if ((strcmp(path, "tmp/note") == 0 || strcmp(path, "/tmp/note") == 0 ||
         strcmp(path, "note") == 0) &&
        selected->note_exists) {
        *node = reference_node_handle(4);
        *mode = MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644);
        *size = selected->note_size;
        return true;
    }
    return false;
}

const char *absolute_model_path(const char *path) {
    if (strcmp(path, "tmp/note") == 0 || strcmp(path, "note") == 0) {
        return "/tmp/note";
    }
    return path;
}

bool reference_find_process(const struct reference_state *reference, micros_endpoint_t endpoint,
                            size_t *slot) {
    size_t index;

    for (index = 0; index < MICROS_VFS_PROCESS_CAPACITY; ++index) {
        if (reference->processes[index].state == MICROS_VFS_PROCESS_ACTIVE &&
            reference->processes[index].endpoint == endpoint) {
            *slot = index;
            return true;
        }
    }
    return false;
}

static bool reference_find_free_process(const struct reference_state *reference, size_t *slot) {
    size_t index;

    for (index = 0; index < MICROS_VFS_PROCESS_CAPACITY; ++index) {
        if (reference->processes[index].state == MICROS_VFS_PROCESS_FREE) {
            *slot = index;
            return true;
        }
    }
    return false;
}

static bool reference_find_free_open_file(const struct reference_state *reference, size_t *slot) {
    size_t index;

    for (index = 0; index < MICROS_VFS_OPEN_FILE_CAPACITY; ++index) {
        if (reference->open_files[index].state == MICROS_VFS_OPEN_FILE_FREE) {
            *slot = index;
            return true;
        }
    }
    return false;
}

static bool reference_find_free_descriptor(const struct micros_vfs_process_record *process,
                                           size_t *descriptor) {
    size_t index;

    for (index = 0; index < MICROS_VFS_DESCRIPTOR_CAPACITY; ++index) {
        if (process->descriptors[index] == 0) {
            *descriptor = index;
            return true;
        }
    }
    return false;
}

static bool reference_find_vnode(const struct reference_state *reference, uint64_t node,
                                 size_t *slot) {
    size_t index;

    for (index = 0; index < MICROS_VFS_VNODE_CAPACITY; ++index) {
        if (reference->vnodes[index].state == MICROS_VFS_VNODE_ACTIVE &&
            reference->vnodes[index].node == node) {
            *slot = index;
            return true;
        }
    }
    return false;
}

static bool reference_find_free_vnode(const struct reference_state *reference, size_t *slot) {
    size_t index;

    for (index = 1; index < MICROS_VFS_VNODE_CAPACITY; ++index) {
        if (reference->vnodes[index].state == MICROS_VFS_VNODE_FREE) {
            *slot = index;
            return true;
        }
    }
    return false;
}

static bool reference_integrate_vnode(struct vfs_model *selected, uint64_t node, uint32_t mode,
                                      uint64_t size, size_t *slot) {
    struct micros_vfs_vnode *vnode;

    if (reference_find_vnode(&selected->reference, node, slot)) {
        vnode = &selected->reference.vnodes[*slot];
        if (vnode->mode != mode || vnode->size != size) {
            return false;
        }
        ++vnode->local_reference_count;
        ++vnode->backend_reference_count;
        if (vnode->backend_reference_count == MICROS_VFS_BACKEND_REFERENCE_THRESHOLD) {
            vnode->backend_reference_count = 1;
            ++selected->coverage.threshold_cleanup;
        }
        return true;
    }
    if (!reference_find_free_vnode(&selected->reference, slot)) {
        return false;
    }
    selected->reference.vnodes[*slot] = (struct micros_vfs_vnode){
        .state = MICROS_VFS_VNODE_ACTIVE,
        .node = node,
        .mode = mode,
        .local_reference_count = 1,
        .backend_reference_count = 1,
        .size = size,
    };
    return true;
}

static bool reference_release_vnode(struct reference_state *reference, size_t slot) {
    struct micros_vfs_vnode *vnode;

    if (slot >= MICROS_VFS_VNODE_CAPACITY ||
        reference->vnodes[slot].state != MICROS_VFS_VNODE_ACTIVE ||
        reference->vnodes[slot].local_reference_count == 0) {
        return false;
    }
    vnode = &reference->vnodes[slot];
    --vnode->local_reference_count;
    if (vnode->local_reference_count == 0) {
        if (slot == 0 || vnode->mount_root) {
            return false;
        }
        memset(vnode, 0, sizeof(*vnode));
    }
    return true;
}

bool reference_close(struct reference_state *reference, micros_endpoint_t endpoint,
                     uint32_t descriptor) {
    size_t process_slot;
    uint16_t open_file_reference;
    struct micros_vfs_open_file *open_file;
    size_t vnode_slot = 0;
    bool release_vnode = false;

    if (!reference_find_process(reference, endpoint, &process_slot) ||
        descriptor >= MICROS_VFS_DESCRIPTOR_CAPACITY) {
        return false;
    }
    open_file_reference = reference->processes[process_slot].descriptors[descriptor];
    if (open_file_reference == 0) {
        return false;
    }
    reference->processes[process_slot].descriptors[descriptor] = 0;
    open_file = &reference->open_files[open_file_reference - 1];
    if (open_file->reference_count == 0) {
        return false;
    }
    --open_file->reference_count;
    if (open_file->reference_count != 0) {
        return true;
    }
    if (open_file->kind == MICROS_VFS_OBJECT_RAMFS) {
        vnode_slot = open_file->vnode - 1;
        release_vnode = true;
    }
    memset(open_file, 0, sizeof(*open_file));
    return !release_vnode || reference_release_vnode(reference, vnode_slot);
}

bool reference_attach(struct reference_state *reference, micros_endpoint_t endpoint) {
    size_t process_slot;
    size_t open_file_slots[3];
    size_t index;

    if (!reference_find_free_process(reference, &process_slot) ||
        reference_find_process(reference, endpoint, &index)) {
        return false;
    }
    for (index = 0; index < 3; ++index) {
        if (!reference_find_free_open_file(reference, &open_file_slots[index])) {
            return false;
        }
        reference->open_files[open_file_slots[index]].state = MICROS_VFS_OPEN_FILE_ACTIVE;
    }
    memset(&reference->open_files[open_file_slots[0]], 0,
           sizeof(reference->open_files[open_file_slots[0]]));
    reference->open_files[open_file_slots[0]] = (struct micros_vfs_open_file){
        .state = MICROS_VFS_OPEN_FILE_ACTIVE,
        .kind = MICROS_VFS_OBJECT_CONSOLE,
        .access = MICROS_VFS_ACCESS_READ,
        .open_flags = MICROS_VFS_OPEN_READ,
        .reference_count = 1,
    };
    reference->open_files[open_file_slots[1]] = (struct micros_vfs_open_file){
        .state = MICROS_VFS_OPEN_FILE_ACTIVE,
        .kind = MICROS_VFS_OBJECT_CONSOLE,
        .access = MICROS_VFS_ACCESS_WRITE,
        .open_flags = MICROS_VFS_OPEN_WRITE,
        .reference_count = 1,
    };
    reference->open_files[open_file_slots[2]] = reference->open_files[open_file_slots[1]];
    reference->processes[process_slot] = (struct micros_vfs_process_record){
        .state = MICROS_VFS_PROCESS_ACTIVE,
        .endpoint = endpoint,
        .root_vnode = 1,
        .working_directory_vnode = 1,
    };
    for (index = 0; index < 3; ++index) {
        reference->processes[process_slot].descriptors[index] =
            (uint16_t)(open_file_slots[index] + 1);
    }
    reference->vnodes[0].local_reference_count += 2;
    return true;
}

bool reference_detach(struct reference_state *reference, micros_endpoint_t endpoint) {
    size_t process_slot;
    size_t descriptor;
    size_t root_slot;
    size_t cwd_slot;

    if (!reference_find_process(reference, endpoint, &process_slot)) {
        return false;
    }
    root_slot = reference->processes[process_slot].root_vnode - 1;
    cwd_slot = reference->processes[process_slot].working_directory_vnode - 1;
    for (descriptor = 0; descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY; ++descriptor) {
        if (reference->processes[process_slot].descriptors[descriptor] != 0 &&
            !reference_close(reference, endpoint, (uint32_t)descriptor)) {
            return false;
        }
    }
    if (!reference_release_vnode(reference, cwd_slot) ||
        !reference_release_vnode(reference, root_slot)) {
        return false;
    }
    memset(&reference->processes[process_slot], 0, sizeof(reference->processes[process_slot]));
    return true;
}

bool reference_open(struct vfs_model *selected, micros_endpoint_t endpoint, uint64_t node,
                    uint32_t mode, uint64_t size, uint32_t open_flags,
                    uint32_t expected_descriptor) {
    size_t process_slot;
    size_t descriptor;
    size_t open_file_slot;
    size_t vnode_slot;
    uint32_t access = open_flags & MICROS_VFS_ACCESS_DEFINED_MASK;

    if (!reference_find_process(&selected->reference, endpoint, &process_slot) ||
        !reference_find_free_descriptor(&selected->reference.processes[process_slot],
                                        &descriptor) ||
        descriptor != expected_descriptor ||
        !reference_find_free_open_file(&selected->reference, &open_file_slot) ||
        !reference_integrate_vnode(selected, node, mode, size, &vnode_slot)) {
        return false;
    }
    selected->reference.open_files[open_file_slot] = (struct micros_vfs_open_file){
        .state = MICROS_VFS_OPEN_FILE_ACTIVE,
        .kind = MICROS_VFS_OBJECT_RAMFS,
        .access = access,
        .open_flags = open_flags,
        .reference_count = 1,
        .vnode = (uint16_t)(vnode_slot + 1),
    };
    selected->reference.processes[process_slot].descriptors[descriptor] =
        (uint16_t)(open_file_slot + 1);
    return true;
}

bool reference_share(struct reference_state *reference, micros_endpoint_t source,
                     uint32_t source_descriptor, micros_endpoint_t destination,
                     uint32_t destination_descriptor) {
    size_t source_slot;
    size_t destination_slot;
    uint16_t open_file;

    if (!reference_find_process(reference, source, &source_slot) ||
        !reference_find_process(reference, destination, &destination_slot) ||
        destination_descriptor >= MICROS_VFS_DESCRIPTOR_CAPACITY ||
        source_descriptor >= MICROS_VFS_DESCRIPTOR_CAPACITY ||
        reference->processes[destination_slot].descriptors[destination_descriptor] != 0) {
        return false;
    }
    open_file = reference->processes[source_slot].descriptors[source_descriptor];
    if (open_file == 0) {
        return false;
    }
    reference->processes[destination_slot].descriptors[destination_descriptor] = open_file;
    ++reference->open_files[open_file - 1].reference_count;
    return true;
}

struct micros_vfs_open_file *reference_open_file(struct reference_state *reference,
                                                 micros_endpoint_t endpoint, uint32_t descriptor) {
    size_t process_slot;
    uint16_t open_file;

    if (!reference_find_process(reference, endpoint, &process_slot) ||
        descriptor >= MICROS_VFS_DESCRIPTOR_CAPACITY) {
        return NULL;
    }
    open_file = reference->processes[process_slot].descriptors[descriptor];
    return open_file == 0 ? NULL : &reference->open_files[open_file - 1];
}

bool reference_chdir(struct vfs_model *selected, micros_endpoint_t endpoint, uint64_t node,
                     uint32_t mode) {
    size_t process_slot;
    size_t vnode_slot;
    size_t old_slot;

    if (!reference_find_process(&selected->reference, endpoint, &process_slot) ||
        !reference_integrate_vnode(selected, node, mode, 0, &vnode_slot)) {
        return false;
    }
    old_slot = selected->reference.processes[process_slot].working_directory_vnode - 1;
    selected->reference.processes[process_slot].working_directory_vnode =
        (uint16_t)(vnode_slot + 1);
    return reference_release_vnode(&selected->reference, old_slot);
}

void reference_clear_pending(struct reference_state *reference) {
    memset(&reference->pending, 0, sizeof(reference->pending));
    reference->backend_page_owner = MICROS_VFS_PAGE_FREE;
}

void reference_advance_request_id(struct reference_state *reference, uint64_t request_id) {
    reference->next_tty_request_id = request_id == UINT64_MAX ? 0 : request_id + 1;
}

uint32_t reference_backend_references(const struct reference_state *reference, size_t node_slot) {
    uint64_t node = reference_node_handle(node_slot);
    size_t vnode_slot;

    for (vnode_slot = 0; vnode_slot < MICROS_VFS_VNODE_CAPACITY; ++vnode_slot) {
        if (reference->vnodes[vnode_slot].state == MICROS_VFS_VNODE_ACTIVE &&
            reference->vnodes[vnode_slot].node == node) {
            return reference->vnodes[vnode_slot].backend_reference_count;
        }
    }
    return 0;
}
