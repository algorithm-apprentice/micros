#include "kernel/elf_spawn_test_internal.h"

static const struct micros_vfs_process_record *elf_spawn_vfs_process(
    const struct micros_vfs_state *state, micros_endpoint_t endpoint
)
{
    size_t index;

    for (index = 0; index < MICROS_VFS_PROCESS_CAPACITY; ++index) {
        if (state->processes[index].state == MICROS_VFS_PROCESS_ACTIVE
            && state->processes[index].endpoint == endpoint) {
            return &state->processes[index];
        }
    }
    return NULL;
}

static bool elf_spawn_only_init_pm(
    const struct micros_pm_table *table, micros_endpoint_t endpoint
)
{
    size_t index;
    size_t running = 0;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct micros_pm_process_record *record = &table->records[index];

        if (record->state == MICROS_PM_PROCESS_RUNNING) {
            if (record->pid != MICROS_ELF_SPAWN_INIT_PID || !record->reaper
                || record->endpoint != endpoint || record->wait.active || record->reap_pending) {
                return false;
            }
            ++running;
        } else if (record->state != MICROS_PM_PROCESS_FREE) {
            return false;
        }
    }
    return running == 1 && table->runtime_enabled && table->init_attempted;
}

static bool elf_spawn_only_init_vfs(
    const struct micros_vfs_state *state, micros_endpoint_t endpoint
)
{
    const struct micros_vfs_process_record *init = elf_spawn_vfs_process(state, endpoint);
    size_t processes = 0;
    uint64_t references = 0;
    size_t vnodes = 0;
    size_t index;

    if (init == NULL || state->spawn.phase != MICROS_VFS_SPAWN_IDLE
        || state->pending.state != MICROS_VFS_PENDING_NONE
        || state->client_page_owner != MICROS_VFS_PAGE_FREE
        || state->backend_page_owner != MICROS_VFS_PAGE_FREE) {
        return false;
    }
    for (index = 0; index < MICROS_VFS_PROCESS_CAPACITY; ++index) {
        processes += state->processes[index].state == MICROS_VFS_PROCESS_ACTIVE;
    }
    for (index = 0; index < MICROS_VFS_DESCRIPTOR_CAPACITY; ++index) {
        uint16_t open_file = init->descriptors[index];

        if (index < 3) {
            if (open_file == 0 || open_file > MICROS_VFS_OPEN_FILE_CAPACITY
                || state->open_files[open_file - 1].state != MICROS_VFS_OPEN_FILE_ACTIVE
                || state->open_files[open_file - 1].kind != MICROS_VFS_OBJECT_CONSOLE) {
                return false;
            }
        } else if (open_file != 0) {
            return false;
        }
    }
    for (index = 0; index < MICROS_VFS_OPEN_FILE_CAPACITY; ++index) {
        if (state->open_files[index].state == MICROS_VFS_OPEN_FILE_ACTIVE) {
            references += state->open_files[index].reference_count;
        }
    }
    for (index = 0; index < MICROS_VFS_VNODE_CAPACITY; ++index) {
        if (state->vnodes[index].state == MICROS_VFS_VNODE_ACTIVE) {
            if (!state->vnodes[index].mount_root) {
                return false;
            }
            ++vnodes;
        }
    }
    return processes == 1 && references == 3 && vnodes == 1;
}

static bool elf_spawn_only_init_vm(const struct micros_vm_state *state)
{
    size_t mappings = 0;
    size_t index;

    if (state->phase != MICROS_VM_LOADER_IDLE || state->transaction.load_token != 0) {
        return false;
    }
    for (index = 0; index < MICROS_VM_MAPPING_RECORD_CAPACITY; ++index) {
        const struct micros_vm_mapping_record *record = &state->mapping_records[index];

        if (record->phase != MICROS_VM_MAPPING_RECORD_UNUSED) {
            if (record->phase != MICROS_VM_MAPPING_RECORD_ACTIVE
                || record->semantic_pid != MICROS_ELF_SPAWN_INIT_PID) {
                return false;
            }
            ++mappings;
        }
    }
    return mappings == 1;
}

static bool elf_spawn_terminal_empty(const struct micros_tty_service_state *tty)
{
    return tty->owned && !tty->pending_read.active && !tty->read_completion.active
        && !tty->write_completion.active && tty->terminal.input_count == 0
        && tty->terminal.echo_count == 0 && !tty->terminal.write_resident
        && !tty->terminal.transmit_interrupt_enabled && !tty->terminal.write_retry_armed;
}

bool elf_spawn_test_services_idle(
    const struct micros_elf_spawn_service_resources *snapshot, micros_endpoint_t endpoint
)
{
    size_t index;

    if (
        !elf_spawn_only_init_pm(&snapshot->pm, endpoint)
        || !elf_spawn_only_init_vfs(&snapshot->vfs, endpoint)
        || !elf_spawn_only_init_vm(&snapshot->vm)
        || snapshot->coordinator.spawn_pending || snapshot->coordinator.parent_lost
        || snapshot->coordinator.pending_parent_reply != 0
        || !elf_spawn_terminal_empty(&snapshot->tty)
    ) {
        return false;
    }
    for (index = 0; index < MICROS_RAMFS_NODE_CAPACITY; ++index) {
        if (snapshot->ramfs.nodes[index].live
            && snapshot->ramfs.nodes[index].reference_count != (index == 0 ? 1U : 0U)) {
            return false;
        }
    }
    return true;
}

bool elf_spawn_test_child_descriptors(
    const struct micros_elf_spawn_service_resources *snapshot,
    micros_endpoint_t parent_endpoint, micros_endpoint_t child_endpoint
)
{
    const struct micros_vfs_process_record *parent =
        elf_spawn_vfs_process(&snapshot->vfs, parent_endpoint);
    const struct micros_vfs_process_record *child =
        elf_spawn_vfs_process(&snapshot->vfs, child_endpoint);
    size_t index;

    if (parent == NULL || child == NULL || parent == child
        || parent->root_vnode != child->root_vnode
        || parent->working_directory_vnode != child->working_directory_vnode
        || snapshot->vfs.spawn.phase != MICROS_VFS_SPAWN_IDLE) {
        return false;
    }
    for (index = 0; index < MICROS_VFS_DESCRIPTOR_CAPACITY; ++index) {
        if (index < 3) {
            if (parent->descriptors[index] == 0
                || child->descriptors[index] != parent->descriptors[index]) {
                return false;
            }
        } else if (child->descriptors[index] != 0) {
            return false;
        }
    }
    return true;
}
