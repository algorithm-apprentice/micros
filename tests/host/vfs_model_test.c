#include "servers/vfs/vfs_core.h"
#include "tests/host/vfs_test_fixture.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    MODEL_STEP_COUNT = 8192,
    MODEL_CYCLE_LENGTH = 32,
    MODEL_MOTD_SIZE = sizeof("micros test fixture\n") - 1,
};

enum model_operation {
    MODEL_OPERATION_ATTACH = 1,
    MODEL_OPERATION_MALFORMED,
    MODEL_OPERATION_FOREIGN,
    MODEL_OPERATION_MKDIR,
    MODEL_OPERATION_OPEN,
    MODEL_OPERATION_SHARE,
    MODEL_OPERATION_READ,
    MODEL_OPERATION_GRANT_FAILURE,
    MODEL_OPERATION_CLOSE,
    MODEL_OPERATION_CREATE,
    MODEL_OPERATION_WRITE,
    MODEL_OPERATION_CHDIR,
    MODEL_OPERATION_GETDENTS,
    MODEL_OPERATION_TTY_SUBMIT,
    MODEL_OPERATION_TTY_COMPLETION,
    MODEL_OPERATION_TTY_CANCEL,
    MODEL_OPERATION_TTY_WRITABLE,
    MODEL_OPERATION_TTY_COLLECT,
    MODEL_OPERATION_DRAIN,
    MODEL_OPERATION_DESCRIPTOR_PRESSURE,
    MODEL_OPERATION_VNODE_PRESSURE,
    MODEL_OPERATION_PROCESS_PRESSURE,
    MODEL_OPERATION_BACKEND_CAPACITY,
    MODEL_OPERATION_FATAL,
    MODEL_OPERATION_DETACH,
};

struct reference_state {
    uint64_t next_tty_request_id;
    enum micros_vfs_page_owner client_page_owner;
    enum micros_vfs_page_owner backend_page_owner;
    struct micros_vfs_pending_operation pending;
    struct micros_vfs_process_record
        processes[MICROS_VFS_PROCESS_CAPACITY];
    struct micros_vfs_open_file
        open_files[MICROS_VFS_OPEN_FILE_CAPACITY];
    struct micros_vfs_vnode vnodes[MICROS_VFS_VNODE_CAPACITY];
};

struct model_coverage {
    size_t attach;
    size_t detach;
    size_t open;
    size_t create;
    size_t close;
    size_t read;
    size_t write;
    size_t getdents;
    size_t mkdir;
    size_t chdir;
    size_t shared;
    size_t descriptor_pressure;
    size_t vnode_pressure;
    size_t process_pressure;
    size_t grant_failure;
    size_t backend_capacity;
    size_t tty_submit;
    size_t tty_completion;
    size_t tty_writable;
    size_t tty_cancel;
    size_t tty_collect;
    size_t malformed;
    size_t foreign;
    size_t fatal;
    size_t threshold_cleanup;
};

struct vfs_model {
    uint64_t seed;
    uint64_t random_state;
    size_t step;
    uint32_t trace[MODEL_STEP_COUNT];
    struct micros_vfs_state production;
    struct reference_state reference;
    struct vfs_test_fixture fixture;
    struct model_coverage coverage;
    micros_grant_t retained_application_grant;
    bool tmp_exists;
    bool note_exists;
    uint64_t note_size;
    uint8_t pending_write_bytes[MICROS_VFS_TRANSFER_MAX];
    size_t pending_write_length;
    size_t transition_checks;
};

static const micros_endpoint_t vfs_endpoint =
    UINT32_C(0x00001006);
static const micros_endpoint_t ramfs_endpoint =
    UINT32_C(0x00001005);
static const micros_endpoint_t tty_endpoint =
    UINT32_C(0x00001004);
static const micros_endpoint_t first_endpoint =
    UINT32_C(0x00002007);
static const micros_endpoint_t second_endpoint =
    UINT32_C(0x00002008);
static struct vfs_model model;
static struct micros_vfs_state pressure_state;
static struct vfs_test_fixture pressure_fixture;
static struct micros_vfs_state fatal_state;
static struct vfs_test_fixture fatal_fixture;

static uint64_t reference_node_handle(size_t slot)
{
    return UINT64_C(0x0000000100000000) | slot;
}

static bool reference_path_metadata(
    const struct vfs_model *selected,
    const char *path,
    uint64_t *node,
    uint32_t *mode,
    uint64_t *size
)
{
    if (strcmp(path, "/") == 0) {
        *node = reference_node_handle(0);
        *mode =
            MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755);
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
        *mode =
            MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755);
        *size = 0;
        return true;
    }
    if (
        (
            strcmp(path, "tmp/note") == 0
            || strcmp(path, "/tmp/note") == 0
            || strcmp(path, "note") == 0
        )
        && selected->note_exists
    ) {
        *node = reference_node_handle(4);
        *mode = MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644);
        *size = selected->note_size;
        return true;
    }
    return false;
}

static const char *absolute_model_path(const char *path)
{
    if (
        strcmp(path, "tmp/note") == 0
        || strcmp(path, "note") == 0
    ) {
        return "/tmp/note";
    }
    return path;
}

static bool model_failure(
    const struct vfs_model *selected,
    const char *expression,
    int line
)
{
    size_t index;

    fprintf(
        stderr,
        "%s:%d: VFS model failure: %s "
        "seed=0x%016llx step=%zu\n",
        __FILE__,
        line,
        expression,
        (unsigned long long)selected->seed,
        selected->step
    );
    fprintf(stderr, "operation trace:");
    for (index = 0; index <= selected->step; ++index) {
        if ((index % 8) == 0) {
            fputc('\n', stderr);
        }
        fprintf(stderr, " %08x", selected->trace[index]);
    }
    fputc('\n', stderr);
    return false;
}

#define MODEL_EXPECT(selected, expression) \
    do { \
        if (!(expression)) { \
            return model_failure( \
                (selected), \
                #expression, \
                __LINE__ \
            ); \
        } \
    } while (false)

static uint64_t model_random(struct vfs_model *selected)
{
    uint64_t value = selected->random_state;

    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    selected->random_state = value;
    return value;
}

static void record_operation(
    struct vfs_model *selected,
    enum model_operation operation,
    uint32_t detail
)
{
    selected->trace[selected->step] =
        (uint32_t)operation << 24
        | (detail & UINT32_C(0x00ffffff));
}

static bool reference_find_process(
    const struct reference_state *reference,
    micros_endpoint_t endpoint,
    size_t *slot
)
{
    size_t index;

    for (index = 0; index < MICROS_VFS_PROCESS_CAPACITY; ++index) {
        if (
            reference->processes[index].state
                == MICROS_VFS_PROCESS_ACTIVE
            && reference->processes[index].endpoint == endpoint
        ) {
            *slot = index;
            return true;
        }
    }
    return false;
}

static bool reference_find_free_process(
    const struct reference_state *reference,
    size_t *slot
)
{
    size_t index;

    for (index = 0; index < MICROS_VFS_PROCESS_CAPACITY; ++index) {
        if (
            reference->processes[index].state
                == MICROS_VFS_PROCESS_FREE
        ) {
            *slot = index;
            return true;
        }
    }
    return false;
}

static bool reference_find_free_open_file(
    const struct reference_state *reference,
    size_t *slot
)
{
    size_t index;

    for (
        index = 0;
        index < MICROS_VFS_OPEN_FILE_CAPACITY;
        ++index
    ) {
        if (
            reference->open_files[index].state
                == MICROS_VFS_OPEN_FILE_FREE
        ) {
            *slot = index;
            return true;
        }
    }
    return false;
}

static bool reference_find_free_descriptor(
    const struct micros_vfs_process_record *process,
    size_t *descriptor
)
{
    size_t index;

    for (
        index = 0;
        index < MICROS_VFS_DESCRIPTOR_CAPACITY;
        ++index
    ) {
        if (process->descriptors[index] == 0) {
            *descriptor = index;
            return true;
        }
    }
    return false;
}

static bool reference_find_vnode(
    const struct reference_state *reference,
    uint64_t node,
    size_t *slot
)
{
    size_t index;

    for (index = 0; index < MICROS_VFS_VNODE_CAPACITY; ++index) {
        if (
            reference->vnodes[index].state
                == MICROS_VFS_VNODE_ACTIVE
            && reference->vnodes[index].node == node
        ) {
            *slot = index;
            return true;
        }
    }
    return false;
}

static bool reference_find_free_vnode(
    const struct reference_state *reference,
    size_t *slot
)
{
    size_t index;

    for (index = 1; index < MICROS_VFS_VNODE_CAPACITY; ++index) {
        if (
            reference->vnodes[index].state
                == MICROS_VFS_VNODE_FREE
        ) {
            *slot = index;
            return true;
        }
    }
    return false;
}

static bool reference_integrate_vnode(
    struct vfs_model *selected,
    uint64_t node,
    uint32_t mode,
    uint64_t size,
    size_t *slot
)
{
    struct micros_vfs_vnode *vnode;

    if (reference_find_vnode(&selected->reference, node, slot)) {
        vnode = &selected->reference.vnodes[*slot];
        if (vnode->mode != mode || vnode->size != size) {
            return false;
        }
        ++vnode->local_reference_count;
        ++vnode->backend_reference_count;
        if (
            vnode->backend_reference_count
                == MICROS_VFS_BACKEND_REFERENCE_THRESHOLD
        ) {
            vnode->backend_reference_count = 1;
            ++selected->coverage.threshold_cleanup;
        }
        return true;
    }
    if (!reference_find_free_vnode(&selected->reference, slot)) {
        return false;
    }
    selected->reference.vnodes[*slot] =
        (struct micros_vfs_vnode){
            .state = MICROS_VFS_VNODE_ACTIVE,
            .node = node,
            .mode = mode,
            .local_reference_count = 1,
            .backend_reference_count = 1,
            .size = size,
        };
    return true;
}

static bool reference_release_vnode(
    struct reference_state *reference,
    size_t slot
)
{
    struct micros_vfs_vnode *vnode;

    if (
        slot >= MICROS_VFS_VNODE_CAPACITY
        || reference->vnodes[slot].state
            != MICROS_VFS_VNODE_ACTIVE
        || reference->vnodes[slot].local_reference_count == 0
    ) {
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

static bool reference_close(
    struct reference_state *reference,
    micros_endpoint_t endpoint,
    uint32_t descriptor
)
{
    size_t process_slot;
    uint16_t open_file_reference;
    struct micros_vfs_open_file *open_file;
    size_t vnode_slot = 0;
    bool release_vnode = false;

    if (
        !reference_find_process(reference, endpoint, &process_slot)
        || descriptor >= MICROS_VFS_DESCRIPTOR_CAPACITY
    ) {
        return false;
    }
    open_file_reference =
        reference->processes[process_slot].descriptors[descriptor];
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
    return !release_vnode
        || reference_release_vnode(reference, vnode_slot);
}

static bool reference_attach(
    struct reference_state *reference,
    micros_endpoint_t endpoint
)
{
    size_t process_slot;
    size_t open_file_slots[3];
    size_t index;

    if (
        !reference_find_free_process(reference, &process_slot)
        || reference_find_process(reference, endpoint, &index)
    ) {
        return false;
    }
    for (index = 0; index < 3; ++index) {
        if (
            !reference_find_free_open_file(
                reference,
                &open_file_slots[index]
            )
        ) {
            return false;
        }
        reference->open_files[open_file_slots[index]].state =
            MICROS_VFS_OPEN_FILE_ACTIVE;
    }
    memset(
        &reference->open_files[open_file_slots[0]],
        0,
        sizeof(reference->open_files[open_file_slots[0]])
    );
    reference->open_files[open_file_slots[0]] =
        (struct micros_vfs_open_file){
            .state = MICROS_VFS_OPEN_FILE_ACTIVE,
            .kind = MICROS_VFS_OBJECT_CONSOLE,
            .access = MICROS_VFS_ACCESS_READ,
            .open_flags = MICROS_VFS_OPEN_READ,
            .reference_count = 1,
        };
    reference->open_files[open_file_slots[1]] =
        (struct micros_vfs_open_file){
            .state = MICROS_VFS_OPEN_FILE_ACTIVE,
            .kind = MICROS_VFS_OBJECT_CONSOLE,
            .access = MICROS_VFS_ACCESS_WRITE,
            .open_flags = MICROS_VFS_OPEN_WRITE,
            .reference_count = 1,
        };
    reference->open_files[open_file_slots[2]] =
        reference->open_files[open_file_slots[1]];
    reference->processes[process_slot] =
        (struct micros_vfs_process_record){
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

static bool reference_detach(
    struct reference_state *reference,
    micros_endpoint_t endpoint
)
{
    size_t process_slot;
    size_t descriptor;
    size_t root_slot;
    size_t cwd_slot;

    if (!reference_find_process(reference, endpoint, &process_slot)) {
        return false;
    }
    root_slot = reference->processes[process_slot].root_vnode - 1;
    cwd_slot =
        reference->processes[process_slot]
            .working_directory_vnode - 1;
    for (
        descriptor = 0;
        descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY;
        ++descriptor
    ) {
        if (
            reference->processes[process_slot]
                .descriptors[descriptor] != 0
            && !reference_close(
                reference,
                endpoint,
                (uint32_t)descriptor
            )
        ) {
            return false;
        }
    }
    if (
        !reference_release_vnode(reference, cwd_slot)
        || !reference_release_vnode(reference, root_slot)
    ) {
        return false;
    }
    memset(
        &reference->processes[process_slot],
        0,
        sizeof(reference->processes[process_slot])
    );
    return true;
}

static bool reference_open(
    struct vfs_model *selected,
    micros_endpoint_t endpoint,
    uint64_t node,
    uint32_t mode,
    uint64_t size,
    uint32_t open_flags,
    uint32_t expected_descriptor
)
{
    size_t process_slot;
    size_t descriptor;
    size_t open_file_slot;
    size_t vnode_slot;
    uint32_t access =
        open_flags & MICROS_VFS_ACCESS_DEFINED_MASK;

    if (
        !reference_find_process(
            &selected->reference,
            endpoint,
            &process_slot
        )
        || !reference_find_free_descriptor(
            &selected->reference.processes[process_slot],
            &descriptor
        )
        || descriptor != expected_descriptor
        || !reference_find_free_open_file(
            &selected->reference,
            &open_file_slot
        )
        || !reference_integrate_vnode(
            selected,
            node,
            mode,
            size,
            &vnode_slot
        )
    ) {
        return false;
    }
    selected->reference.open_files[open_file_slot] =
        (struct micros_vfs_open_file){
            .state = MICROS_VFS_OPEN_FILE_ACTIVE,
            .kind = MICROS_VFS_OBJECT_RAMFS,
            .access = access,
            .open_flags = open_flags,
            .reference_count = 1,
            .vnode = (uint16_t)(vnode_slot + 1),
        };
    selected->reference.processes[process_slot]
        .descriptors[descriptor] =
            (uint16_t)(open_file_slot + 1);
    return true;
}

static bool reference_share(
    struct reference_state *reference,
    micros_endpoint_t source,
    uint32_t source_descriptor,
    micros_endpoint_t destination,
    uint32_t destination_descriptor
)
{
    size_t source_slot;
    size_t destination_slot;
    uint16_t open_file;

    if (
        !reference_find_process(reference, source, &source_slot)
        || !reference_find_process(
            reference,
            destination,
            &destination_slot
        )
        || destination_descriptor >= MICROS_VFS_DESCRIPTOR_CAPACITY
        || source_descriptor >= MICROS_VFS_DESCRIPTOR_CAPACITY
        || reference->processes[destination_slot]
            .descriptors[destination_descriptor] != 0
    ) {
        return false;
    }
    open_file = reference->processes[source_slot]
        .descriptors[source_descriptor];
    if (open_file == 0) {
        return false;
    }
    reference->processes[destination_slot]
        .descriptors[destination_descriptor] = open_file;
    ++reference->open_files[open_file - 1].reference_count;
    return true;
}

static struct micros_vfs_open_file *reference_open_file(
    struct reference_state *reference,
    micros_endpoint_t endpoint,
    uint32_t descriptor
)
{
    size_t process_slot;
    uint16_t open_file;

    if (
        !reference_find_process(reference, endpoint, &process_slot)
        || descriptor >= MICROS_VFS_DESCRIPTOR_CAPACITY
    ) {
        return NULL;
    }
    open_file =
        reference->processes[process_slot].descriptors[descriptor];
    return open_file == 0
        ? NULL
        : &reference->open_files[open_file - 1];
}

static bool reference_chdir(
    struct vfs_model *selected,
    micros_endpoint_t endpoint,
    uint64_t node,
    uint32_t mode
)
{
    size_t process_slot;
    size_t vnode_slot;
    size_t old_slot;

    if (
        !reference_find_process(
            &selected->reference,
            endpoint,
            &process_slot
        )
        || !reference_integrate_vnode(
            selected,
            node,
            mode,
            0,
            &vnode_slot
        )
    ) {
        return false;
    }
    old_slot = selected->reference.processes[process_slot]
        .working_directory_vnode - 1;
    selected->reference.processes[process_slot]
        .working_directory_vnode =
            (uint16_t)(vnode_slot + 1);
    return reference_release_vnode(
        &selected->reference,
        old_slot
    );
}

static void reference_clear_pending(
    struct reference_state *reference
)
{
    memset(&reference->pending, 0, sizeof(reference->pending));
    reference->backend_page_owner = MICROS_VFS_PAGE_FREE;
}

static void reference_advance_request_id(
    struct reference_state *reference,
    uint64_t request_id
)
{
    reference->next_tty_request_id =
        request_id == UINT64_MAX ? 0 : request_id + 1;
}

static uint32_t reference_backend_references(
    const struct reference_state *reference,
    size_t node_slot
)
{
    uint64_t node = reference_node_handle(node_slot);
    size_t vnode_slot;

    for (
        vnode_slot = 0;
        vnode_slot < MICROS_VFS_VNODE_CAPACITY;
        ++vnode_slot
    ) {
        if (
            reference->vnodes[vnode_slot].state
                == MICROS_VFS_VNODE_ACTIVE
            && reference->vnodes[vnode_slot].node == node
        ) {
            return reference->vnodes[vnode_slot]
                .backend_reference_count;
        }
    }
    return 0;
}

static bool compare_complete_state(struct vfs_model *selected)
{
    size_t expected_backend_grants = 0;
    size_t node_slot;

    if (
        selected->reference.pending.state
            == MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION
        || selected->reference.pending.state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE
    ) {
        expected_backend_grants = 1;
    }
    MODEL_EXPECT(
        selected,
        micros_vfs_state_validate(&selected->production)
            == MICROS_VFS_CORE_OK
    );
    MODEL_EXPECT(
        selected,
        selected->production.phase == MICROS_VFS_PHASE_MOUNTED
        && selected->production.self_endpoint == vfs_endpoint
        && selected->production.ramfs_endpoint == ramfs_endpoint
        && selected->production.tty_endpoint == tty_endpoint
        && selected->production.next_tty_request_id
            == selected->reference.next_tty_request_id
        && selected->production.client_page_owner
            == selected->reference.client_page_owner
        && selected->production.backend_page_owner
            == selected->reference.backend_page_owner
        && memcmp(
            &selected->production.pending,
            &selected->reference.pending,
            sizeof(selected->reference.pending)
        ) == 0
        && memcmp(
            selected->production.processes,
            selected->reference.processes,
            sizeof(selected->reference.processes)
        ) == 0
        && memcmp(
            selected->production.open_files,
            selected->reference.open_files,
            sizeof(selected->reference.open_files)
        ) == 0
        && memcmp(
            selected->production.vnodes,
            selected->reference.vnodes,
            sizeof(selected->reference.vnodes)
        ) == 0
        && selected->fixture.backend_grant_count
            == expected_backend_grants
    );
    if (
        selected->reference.pending.state
            == MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE
    ) {
        MODEL_EXPECT(
            selected,
            selected->pending_write_length
                == selected->reference.pending.count
            && memcmp(
                selected->production.backend_page,
                selected->pending_write_bytes,
                selected->pending_write_length
            ) == 0
        );
    } else {
        MODEL_EXPECT(selected, selected->pending_write_length == 0);
    }
    for (
        node_slot = 0;
        node_slot < MICROS_RAMFS_NODE_CAPACITY;
        ++node_slot
    ) {
        MODEL_EXPECT(
            selected,
            selected->fixture.nodes[node_slot].backend_references
                == reference_backend_references(
                    &selected->reference,
                    node_slot
                )
        );
    }
    return true;
}

static bool compare_transition(struct vfs_model *selected)
{
    if (!compare_complete_state(selected)) {
        return false;
    }
    ++selected->transition_checks;
    return true;
}

static struct micros_vfs_request model_request(
    uint32_t type,
    micros_endpoint_t source,
    uint64_t reply_token
)
{
    struct micros_vfs_request request = {
        .type = type,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = source,
        .reply_token = reply_token,
    };

    return request;
}

static struct micros_vfs_request model_path_request(
    struct vfs_model *selected,
    uint32_t type,
    micros_endpoint_t endpoint,
    uint64_t reply_token,
    const char *path,
    uint32_t open_flags,
    uint32_t mode
)
{
    struct micros_vfs_request request =
        model_request(type, endpoint, reply_token);
    size_t length = strlen(path) + 1;

    request.grant = vfs_test_fixture_add_application_grant(
        &selected->fixture,
        endpoint,
        MICROS_GRANT_PERMISSION_READ,
        (const uint8_t *)path,
        length
    );
    request.path_length = (uint32_t)length;
    request.open_flags = open_flags;
    request.mode = mode;
    return request;
}

static bool revoke_request_grant(
    struct vfs_model *selected,
    const struct micros_vfs_request *request
)
{
    return vfs_test_fixture_revoke_application_grant(
        &selected->fixture,
        request->grant
    );
}

static bool initialize_model(struct vfs_model *selected)
{
    enum micros_vfs_trusted_result result;

    memset(selected, 0, sizeof(*selected));
    selected->seed = UINT64_C(0x6d6963726f735646);
    selected->random_state = selected->seed;
    vfs_test_fixture_initialize(
        &selected->fixture,
        vfs_endpoint,
        ramfs_endpoint,
        tty_endpoint
    );
    if (
        micros_vfs_state_initialize(
            &selected->production,
            vfs_endpoint,
            ramfs_endpoint,
            tty_endpoint
        ) != MICROS_VFS_CORE_OK
        || micros_vfs_mount(
            &selected->production,
            &selected->fixture.io
        ) != MICROS_VFS_CORE_OK
        || micros_vfs_attach_console(
            &selected->production,
            first_endpoint,
            0,
            &result
        ) != MICROS_VFS_CORE_OK
        || result != MICROS_VFS_TRUSTED_OK
    ) {
        return false;
    }
    selected->reference.next_tty_request_id = 1;
    selected->reference.vnodes[0] =
        (struct micros_vfs_vnode){
            .state = MICROS_VFS_VNODE_ACTIVE,
            .mount_root = true,
            .node = reference_node_handle(0),
            .mode =
                MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
            .local_reference_count = 1,
            .backend_reference_count = 1,
        };
    if (
        !reference_attach(
            &selected->reference,
            first_endpoint
        )
    ) {
        return false;
    }
    return compare_complete_state(selected);
}

static bool model_attach_second(struct vfs_model *selected)
{
    enum micros_vfs_trusted_result result;

    MODEL_EXPECT(
        selected,
        micros_vfs_attach_console(
            &selected->production,
            second_endpoint,
            0,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_OK
        && reference_attach(
            &selected->reference,
            second_endpoint
        )
    );
    ++selected->coverage.attach;
    return compare_transition(selected);
}

static bool model_malformed_or_foreign(
    struct vfs_model *selected,
    uint64_t random
)
{
    struct micros_vfs_request request;
    struct micros_vfs_result_action action;

    if ((random & 1) == 0) {
        request = model_request(
            MICROS_VFS_MESSAGE_OPEN,
            first_endpoint,
            selected->step + 1
        );
        request.path_length = 2;
        MODEL_EXPECT(
            selected,
            micros_vfs_handle_request(
                &selected->production,
                &request,
                &selected->fixture.io,
                &action
            ) == MICROS_VFS_CORE_OK
            && action.result == MICROS_VFS_RESULT_MALFORMED
        );
        ++selected->coverage.malformed;
    } else {
        request = model_request(
            MICROS_VFS_MESSAGE_CLOSE,
            UINT32_C(0x00003009),
            selected->step + 1
        );
        MODEL_EXPECT(
            selected,
            micros_vfs_handle_request(
                &selected->production,
                &request,
                &selected->fixture.io,
                &action
            ) == MICROS_VFS_CORE_OK
            && action.result == MICROS_VFS_RESULT_CALLER
        );
        ++selected->coverage.foreign;
    }
    return compare_transition(selected);
}

static bool model_mkdir(struct vfs_model *selected)
{
    struct micros_vfs_request request = model_path_request(
        selected,
        MICROS_VFS_MESSAGE_MKDIR,
        first_endpoint,
        selected->step + 1,
        "/tmp",
        0,
        UINT32_C(0755)
    );
    struct micros_vfs_result_action action;
    bool existed = selected->tmp_exists;

    MODEL_EXPECT(
        selected,
        request.grant != MICROS_GRANT_NONE
        && micros_vfs_handle_request(
            &selected->production,
            &request,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == (
            existed
                ? MICROS_VFS_RESULT_EXISTS
                : MICROS_VFS_RESULT_OK
        )
        && revoke_request_grant(selected, &request)
    );
    if (!existed) {
        selected->tmp_exists = true;
        MODEL_EXPECT(
            selected,
            vfs_test_fixture_find_path(
                &selected->fixture,
                "/tmp"
            ) == reference_node_handle(3)
            && selected->fixture.nodes[3].mode
                == (
                    MICROS_RAMFS_MODE_DIRECTORY
                    | UINT32_C(0755)
                )
        );
    }
    ++selected->coverage.mkdir;
    return compare_transition(selected);
}

static bool model_open_path(
    struct vfs_model *selected,
    micros_endpoint_t endpoint,
    const char *path,
    uint32_t flags,
    uint32_t mode,
    uint32_t expected_descriptor,
    bool creation
)
{
    struct micros_vfs_request request = model_path_request(
        selected,
        MICROS_VFS_MESSAGE_OPEN,
        endpoint,
        selected->step + 1,
        path,
        flags,
        mode
    );
    struct micros_vfs_result_action action;
    uint64_t node;
    uint32_t expected_mode;
    uint64_t expected_size;
    bool created = false;

    if (
        !reference_path_metadata(
            selected,
            path,
            &node,
            &expected_mode,
            &expected_size
        )
    ) {
        MODEL_EXPECT(
            selected,
            creation
            && selected->tmp_exists
            && strcmp(path, "tmp/note") == 0
        );
        node = reference_node_handle(4);
        expected_mode =
            MICROS_RAMFS_MODE_REGULAR | mode;
        expected_size = 0;
        created = true;
    }

    MODEL_EXPECT(
        selected,
        request.grant != MICROS_GRANT_NONE
        && micros_vfs_handle_request(
            &selected->production,
            &request,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.descriptor == expected_descriptor
        && action.mode == expected_mode
        && revoke_request_grant(selected, &request)
    );
    if (created) {
        selected->note_exists = true;
        selected->note_size = 0;
    }
    MODEL_EXPECT(
        selected,
        vfs_test_fixture_find_path(
            &selected->fixture,
            absolute_model_path(path)
        ) == node
        && selected->fixture.nodes[(size_t)(uint16_t)node].mode
            == expected_mode
        && selected->fixture.nodes[(size_t)(uint16_t)node].size
            == expected_size
        && reference_open(
            selected,
            endpoint,
            node,
            expected_mode,
            expected_size,
            flags,
            expected_descriptor
        )
    );
    ++selected->coverage.open;
    if (creation) {
        ++selected->coverage.create;
    }
    return compare_transition(selected);
}

static bool model_share_motd(struct vfs_model *selected)
{
    enum micros_vfs_trusted_result result;

    MODEL_EXPECT(
        selected,
        micros_vfs_share_descriptor(
            &selected->production,
            first_endpoint,
            3,
            second_endpoint,
            3,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_OK
        && reference_share(
            &selected->reference,
            first_endpoint,
            3,
            second_endpoint,
            3
        )
    );
    ++selected->coverage.shared;
    return compare_transition(selected);
}

static bool model_read(
    struct vfs_model *selected,
    micros_endpoint_t endpoint,
    uint32_t descriptor,
    uint32_t count,
    bool valid
)
{
    struct micros_vfs_request request = model_request(
        MICROS_VFS_MESSAGE_READ,
        endpoint,
        selected->step + 1
    );
    struct micros_vfs_result_action action;
    struct micros_vfs_open_file *open_file = reference_open_file(
        &selected->reference,
        endpoint,
        descriptor
    );
    micros_grant_t grant;
    uint64_t expected_count;
    uint64_t expected_position;
    uint64_t size;

    MODEL_EXPECT(selected, open_file != NULL);
    size = selected->reference.vnodes[open_file->vnode - 1].size;
    grant = vfs_test_fixture_add_application_grant(
        &selected->fixture,
        endpoint,
        valid
            ? MICROS_GRANT_PERMISSION_WRITE
            : MICROS_GRANT_PERMISSION_READ,
        NULL,
        count
    );
    request.descriptor = descriptor;
    request.grant = grant;
    request.count = count;
    expected_count = open_file->position >= size
        ? 0
        : (
            count < size - open_file->position
                ? count
                : size - open_file->position
        );
    expected_position = open_file->position + expected_count;
    MODEL_EXPECT(
        selected,
        grant != MICROS_GRANT_NONE
        && micros_vfs_handle_request(
            &selected->production,
            &request,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == (
            valid
                ? MICROS_VFS_RESULT_OK
                : MICROS_VFS_RESULT_GRANT
        )
        && (
            !valid
            || (
                action.transferred_count == expected_count
                && action.position == expected_position
            )
        )
        && vfs_test_fixture_revoke_application_grant(
            &selected->fixture,
            grant
        )
    );
    if (valid) {
        open_file->position = expected_position;
        ++selected->coverage.read;
    } else {
        ++selected->coverage.grant_failure;
    }
    return compare_transition(selected);
}

static bool model_write_note(struct vfs_model *selected)
{
    static const uint8_t bytes[] = "hello";
    struct micros_vfs_request request = model_request(
        MICROS_VFS_MESSAGE_WRITE,
        first_endpoint,
        selected->step + 1
    );
    struct micros_vfs_result_action action;
    struct micros_vfs_open_file *open_file = reference_open_file(
        &selected->reference,
        first_endpoint,
        3
    );
    struct micros_vfs_vnode *vnode;
    micros_grant_t grant;
    uint64_t expected_position;

    MODEL_EXPECT(selected, open_file != NULL);
    vnode = &selected->reference.vnodes[open_file->vnode - 1];
    grant = vfs_test_fixture_add_application_grant(
        &selected->fixture,
        first_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        bytes,
        sizeof(bytes) - 1
    );
    request.descriptor = 3;
    request.grant = grant;
    request.count = sizeof(bytes) - 1;
    expected_position = open_file->position + sizeof(bytes) - 1;
    MODEL_EXPECT(
        selected,
        grant != MICROS_GRANT_NONE
        && micros_vfs_handle_request(
            &selected->production,
            &request,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && action.position == expected_position
        && action.transferred_count == sizeof(bytes) - 1
        && vfs_test_fixture_revoke_application_grant(
            &selected->fixture,
            grant
        )
    );
    open_file->position = expected_position;
    if (vnode->size < expected_position) {
        vnode->size = expected_position;
    }
    if (selected->note_size < expected_position) {
        selected->note_size = expected_position;
    }
    ++selected->coverage.write;
    MODEL_EXPECT(selected, compare_transition(selected));

    grant = vfs_test_fixture_add_application_grant(
        &selected->fixture,
        first_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        bytes,
        sizeof(bytes) - 1
    );
    request.grant = grant;
    MODEL_EXPECT(
        selected,
        grant != MICROS_GRANT_NONE
        && micros_vfs_handle_request(
            &selected->production,
            &request,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_GRANT
        && vfs_test_fixture_revoke_application_grant(
            &selected->fixture,
            grant
        )
    );
    ++selected->coverage.grant_failure;
    return compare_transition(selected);
}

static bool model_close(
    struct vfs_model *selected,
    micros_endpoint_t endpoint,
    uint32_t descriptor
)
{
    struct micros_vfs_request request = model_request(
        MICROS_VFS_MESSAGE_CLOSE,
        endpoint,
        selected->step + 1
    );
    struct micros_vfs_result_action action;

    request.descriptor = descriptor;
    MODEL_EXPECT(
        selected,
        micros_vfs_handle_request(
            &selected->production,
            &request,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && reference_close(
            &selected->reference,
            endpoint,
            descriptor
        )
    );
    ++selected->coverage.close;
    return compare_transition(selected);
}

static bool model_chdir_path(
    struct vfs_model *selected,
    const char *path
)
{
    struct micros_vfs_request request = model_path_request(
        selected,
        MICROS_VFS_MESSAGE_CHDIR,
        first_endpoint,
        selected->step + 1,
        path,
        0,
        0
    );
    struct micros_vfs_result_action action;
    uint64_t node;
    uint32_t mode;
    uint64_t size;

    MODEL_EXPECT(
        selected,
        reference_path_metadata(
            selected,
            path,
            &node,
            &mode,
            &size
        )
        && (
            mode & MICROS_RAMFS_MODE_TYPE_MASK
        ) == MICROS_RAMFS_MODE_DIRECTORY
        && size == 0
    );

    MODEL_EXPECT(
        selected,
        request.grant != MICROS_GRANT_NONE
        && micros_vfs_handle_request(
            &selected->production,
            &request,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_OK
        && revoke_request_grant(selected, &request)
    );
    MODEL_EXPECT(
        selected,
        vfs_test_fixture_find_path(&selected->fixture, path)
            == node
        && reference_chdir(
            selected,
            first_endpoint,
            node,
            mode
        )
    );
    ++selected->coverage.chdir;
    return compare_transition(selected);
}

static bool model_getdents(
    struct vfs_model *selected,
    bool reject_copy,
    uint64_t expected_position,
    uint64_t expected_count
)
{
    struct micros_vfs_request request = model_request(
        MICROS_VFS_MESSAGE_GETDENTS,
        first_endpoint,
        selected->step + 1
    );
    struct micros_vfs_result_action action;
    struct micros_vfs_open_file *open_file = reference_open_file(
        &selected->reference,
        first_endpoint,
        3
    );
    micros_grant_t grant;

    MODEL_EXPECT(selected, open_file != NULL);
    grant = vfs_test_fixture_add_application_grant(
        &selected->fixture,
        first_endpoint,
        MICROS_GRANT_PERMISSION_WRITE,
        NULL,
        160
    );
    request.descriptor = 3;
    request.grant = grant;
    request.count = 160;
    selected->fixture.reject_client_copy = reject_copy;
    MODEL_EXPECT(
        selected,
        grant != MICROS_GRANT_NONE
        && micros_vfs_handle_request(
            &selected->production,
            &request,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == (
            reject_copy
                ? MICROS_VFS_RESULT_GRANT
                : MICROS_VFS_RESULT_OK
        )
        && (
            reject_copy
            || (
                action.position == expected_position
                && action.transferred_count == expected_count
            )
        )
        && vfs_test_fixture_revoke_application_grant(
            &selected->fixture,
            grant
        )
    );
    selected->fixture.reject_client_copy = false;
    if (!reject_copy) {
        open_file->position = expected_position;
        ++selected->coverage.getdents;
    } else {
        ++selected->coverage.grant_failure;
    }
    return compare_transition(selected);
}

static bool model_console_read_submit(struct vfs_model *selected)
{
    struct micros_vfs_request request = model_request(
        MICROS_VFS_MESSAGE_READ,
        second_endpoint,
        selected->step + 1
    );
    struct micros_vfs_result_action action;
    size_t process_slot;
    uint16_t open_file;
    uint64_t request_id =
        selected->reference.next_tty_request_id;

    selected->retained_application_grant =
        vfs_test_fixture_add_application_grant(
            &selected->fixture,
            second_endpoint,
            MICROS_GRANT_PERMISSION_WRITE,
            NULL,
            8
        );
    request.descriptor = 0;
    request.grant = selected->retained_application_grant;
    request.count = 8;
    MODEL_EXPECT(
        selected,
        selected->retained_application_grant
            != MICROS_GRANT_NONE
        && micros_vfs_handle_request(
            &selected->production,
            &request,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && reference_find_process(
            &selected->reference,
            second_endpoint,
            &process_slot
        )
    );
    open_file = selected->reference.processes[process_slot]
        .descriptors[0];
    selected->reference.pending =
        (struct micros_vfs_pending_operation){
            .state =
                MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION,
            .process_slot = (uint16_t)process_slot,
            .descriptor = 0,
            .open_file = (uint16_t)(open_file - 1),
            .endpoint = second_endpoint,
            .reply_token = request.reply_token,
            .application_grant =
                selected->retained_application_grant,
            .tty_grant = UINT32_C(0x100),
            .count = 8,
            .request_id = request_id,
        };
    selected->reference.backend_page_owner =
        MICROS_VFS_PAGE_TTY_READ;
    reference_advance_request_id(
        &selected->reference,
        request_id
    );
    ++selected->coverage.tty_submit;
    return compare_transition(selected);
}

static bool revoke_retained_grant(struct vfs_model *selected)
{
    bool result = vfs_test_fixture_revoke_application_grant(
        &selected->fixture,
        selected->retained_application_grant
    );

    selected->retained_application_grant = MICROS_GRANT_NONE;
    return result;
}

static bool model_console_read_finish(
    struct vfs_model *selected,
    uint32_t variant
)
{
    static const uint8_t input[] = "x\n";
    struct micros_vfs_result_action action;
    enum micros_vfs_trusted_result result;

    if (variant == 0) {
        MODEL_EXPECT(
            selected,
            vfs_test_fixture_complete_read(
                &selected->fixture,
                input,
                sizeof(input) - 1
            )
            && micros_vfs_handle_tty_notification(
                &selected->production,
                MICROS_TTY_EVENT_COMPLETION,
                &selected->fixture.io,
                &action
            ) == MICROS_VFS_CORE_OK
            && action.active
            && action.transferred_count == sizeof(input) - 1
            && revoke_retained_grant(selected)
        );
        reference_clear_pending(&selected->reference);
        ++selected->coverage.tty_completion;
        ++selected->coverage.tty_collect;
        return compare_transition(selected);
    }
    if (variant == 1) {
        MODEL_EXPECT(
            selected,
            micros_vfs_detach(
                &selected->production,
                second_endpoint,
                &selected->fixture.io,
                &result
            ) == MICROS_VFS_CORE_OK
            && result == MICROS_VFS_TRUSTED_OK
            && revoke_retained_grant(selected)
        );
        reference_clear_pending(&selected->reference);
        MODEL_EXPECT(
            selected,
            reference_detach(
                &selected->reference,
                second_endpoint
            )
        );
        ++selected->coverage.tty_cancel;
        ++selected->coverage.detach;
        return compare_transition(selected);
    }
    MODEL_EXPECT(
        selected,
        vfs_test_fixture_complete_read(
            &selected->fixture,
            input,
            sizeof(input) - 1
        )
        && micros_vfs_detach(
            &selected->production,
            second_endpoint,
            &selected->fixture.io,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_PENDING
        && revoke_retained_grant(selected)
    );
    reference_clear_pending(&selected->reference);
    selected->reference.pending.state =
        MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT;
    MODEL_EXPECT(
        selected,
        reference_detach(
            &selected->reference,
            second_endpoint
        )
    );
    ++selected->coverage.tty_cancel;
    ++selected->coverage.tty_collect;
    ++selected->coverage.detach;
    return compare_transition(selected);
}

static bool model_clear_debt_or_bad_version(
    struct vfs_model *selected
)
{
    struct micros_vfs_result_action action;

    if (
        selected->reference.pending.state
            == MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
    ) {
        MODEL_EXPECT(
            selected,
            micros_vfs_handle_tty_notification(
                &selected->production,
                MICROS_TTY_EVENT_COMPLETION,
                &selected->fixture.io,
                &action
            ) == MICROS_VFS_CORE_OK
            && !action.active
        );
        reference_clear_pending(&selected->reference);
        ++selected->coverage.tty_completion;
        return compare_transition(selected);
    }
    {
        struct micros_vfs_request request = model_request(
            MICROS_VFS_MESSAGE_CLOSE,
            first_endpoint,
            selected->step + 1
        );

        request.version = 2;
        MODEL_EXPECT(
            selected,
            micros_vfs_handle_request(
                &selected->production,
                &request,
                &selected->fixture.io,
                &action
            ) == MICROS_VFS_CORE_OK
            && action.result == MICROS_VFS_RESULT_BAD_VERSION
        );
        ++selected->coverage.malformed;
    }
    return compare_transition(selected);
}

static bool model_console_write_submit(
    struct vfs_model *selected,
    bool expect_busy
)
{
    static const uint8_t output[] = "model";
    struct micros_vfs_request request = model_request(
        MICROS_VFS_MESSAGE_WRITE,
        first_endpoint,
        selected->step + 1
    );
    struct micros_vfs_result_action action;
    micros_grant_t grant;
    size_t process_slot;
    uint16_t open_file;
    uint64_t request_id =
        selected->reference.next_tty_request_id;

    grant = vfs_test_fixture_add_application_grant(
        &selected->fixture,
        first_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        output,
        sizeof(output) - 1
    );
    request.descriptor = 1;
    request.grant = grant;
    request.count = sizeof(output) - 1;
    MODEL_EXPECT(
        selected,
        grant != MICROS_GRANT_NONE
        && micros_vfs_handle_request(
            &selected->production,
            &request,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
        && vfs_test_fixture_revoke_application_grant(
            &selected->fixture,
            grant
        )
        && reference_find_process(
            &selected->reference,
            first_endpoint,
            &process_slot
        )
    );
    open_file = selected->reference.processes[process_slot]
        .descriptors[1];
    selected->reference.pending =
        (struct micros_vfs_pending_operation){
            .state = expect_busy
                ? MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE
                : MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION,
            .process_slot = (uint16_t)process_slot,
            .descriptor = 1,
            .open_file = (uint16_t)(open_file - 1),
            .endpoint = first_endpoint,
            .reply_token = request.reply_token,
            .application_grant = MICROS_GRANT_NONE,
            .tty_grant = expect_busy
                ? UINT32_C(0x100)
                : MICROS_GRANT_NONE,
            .count = sizeof(output) - 1,
            .request_id = request_id,
        };
    if (expect_busy) {
        selected->reference.backend_page_owner =
            MICROS_VFS_PAGE_TTY_WRITE_RETRY;
        memcpy(
            selected->pending_write_bytes,
            output,
            sizeof(output) - 1
        );
        selected->pending_write_length = sizeof(output) - 1;
    } else {
        reference_advance_request_id(
            &selected->reference,
            request_id
        );
    }
    ++selected->coverage.tty_submit;
    return compare_transition(selected);
}

static bool model_console_write_completion(
    struct vfs_model *selected
)
{
    struct micros_vfs_result_action action;

    MODEL_EXPECT(
        selected,
        micros_vfs_handle_tty_notification(
            &selected->production,
            MICROS_TTY_EVENT_COMPLETION,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.result == MICROS_VFS_RESULT_OK
        && action.transferred_count
            == selected->reference.pending.count
    );
    reference_clear_pending(&selected->reference);
    ++selected->coverage.tty_completion;
    ++selected->coverage.tty_collect;
    return compare_transition(selected);
}

static bool model_console_write_writable(
    struct vfs_model *selected
)
{
    struct micros_vfs_result_action action;
    uint64_t request_id =
        selected->reference.pending.request_id;
    uint64_t events = vfs_test_fixture_drain_output(
        &selected->fixture
    );

    MODEL_EXPECT(
        selected,
        events == MICROS_TTY_EVENT_WRITABLE
        && micros_vfs_handle_tty_notification(
            &selected->production,
            events,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
    );
    selected->reference.pending.state =
        MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION;
    selected->reference.pending.tty_grant = MICROS_GRANT_NONE;
    selected->reference.backend_page_owner =
        MICROS_VFS_PAGE_FREE;
    reference_advance_request_id(
        &selected->reference,
        request_id
    );
    memset(
        selected->pending_write_bytes,
        0,
        sizeof(selected->pending_write_bytes)
    );
    selected->pending_write_length = 0;
    ++selected->coverage.tty_writable;
    return compare_transition(selected);
}

static bool model_test_drain_begin(struct vfs_model *selected)
{
    struct micros_vfs_result_action action;
    uint64_t request_id =
        selected->reference.next_tty_request_id;

    MODEL_EXPECT(
        selected,
        micros_vfs_begin_test_drain(
            &selected->production,
            first_endpoint,
            selected->step + 1,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && !action.active
    );
    selected->reference.pending =
        (struct micros_vfs_pending_operation){
            .state =
                MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE,
            .reply_token = selected->step + 1,
            .application_grant = MICROS_GRANT_NONE,
            .tty_grant = MICROS_GRANT_NONE,
            .request_id = request_id,
        };
    return compare_transition(selected);
}

static bool model_test_drain_finish(struct vfs_model *selected)
{
    struct micros_vfs_result_action action;
    uint64_t request_id =
        selected->reference.pending.request_id;
    uint64_t events = vfs_test_fixture_drain_output(
        &selected->fixture
    );

    MODEL_EXPECT(
        selected,
        events == MICROS_TTY_EVENT_WRITABLE
        && micros_vfs_handle_tty_notification(
            &selected->production,
            events,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.active
        && action.request_type == MICROS_VFS_TEST_MESSAGE_DRAIN
        && action.result == MICROS_VFS_RESULT_OK
    );
    reference_clear_pending(&selected->reference);
    reference_advance_request_id(
        &selected->reference,
        request_id
    );
    return compare_transition(selected);
}

static bool model_descriptor_pressure(struct vfs_model *selected)
{
    size_t descriptor;
    struct micros_vfs_request request;
    struct micros_vfs_result_action action;

    for (
        descriptor = 3;
        descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY;
        ++descriptor
    ) {
        if (
            !model_open_path(
                selected,
                first_endpoint,
                "/etc/motd",
                MICROS_VFS_OPEN_READ,
                0,
                (uint32_t)descriptor,
                false
            )
        ) {
            return false;
        }
    }
    request = model_path_request(
        selected,
        MICROS_VFS_MESSAGE_OPEN,
        first_endpoint,
        selected->step + 1,
        "/etc/motd",
        MICROS_VFS_OPEN_READ,
        0
    );
    MODEL_EXPECT(
        selected,
        request.grant != MICROS_GRANT_NONE
        && micros_vfs_handle_request(
            &selected->production,
            &request,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_NO_SPACE
        && revoke_request_grant(selected, &request)
    );
    MODEL_EXPECT(selected, compare_transition(selected));
    for (
        descriptor = 3;
        descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY;
        ++descriptor
    ) {
        if (
            !model_close(
                selected,
                first_endpoint,
                (uint32_t)descriptor
            )
        ) {
            return false;
        }
    }
    ++selected->coverage.descriptor_pressure;
    return true;
}

static bool pressure_open(
    struct micros_vfs_state *state,
    struct vfs_test_fixture *fixture,
    micros_endpoint_t endpoint,
    const char *path,
    uint32_t flags,
    enum micros_vfs_result expected
)
{
    struct micros_vfs_request request = {
        .type = MICROS_VFS_MESSAGE_OPEN,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = endpoint,
        .reply_token = 1,
        .open_flags = flags,
    };
    struct micros_vfs_result_action action;
    size_t length = strlen(path) + 1;

    request.grant = vfs_test_fixture_add_application_grant(
        fixture,
        endpoint,
        MICROS_GRANT_PERMISSION_READ,
        (const uint8_t *)path,
        length
    );
    request.path_length = (uint32_t)length;
    return (
        request.grant != MICROS_GRANT_NONE
        && micros_vfs_handle_request(
            state,
            &request,
            &fixture->io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == expected
        && vfs_test_fixture_revoke_application_grant(
            fixture,
            request.grant
        )
    );
}

static bool run_vnode_pressure(struct vfs_model *selected)
{
    enum micros_vfs_trusted_result result;
    micros_endpoint_t endpoints[5];
    char names[61][4];
    char paths[61][5];
    size_t used_descriptors[5] = {1, 1, 0, 0, 0};
    size_t index;
    size_t active_vnodes = 0;

    vfs_test_fixture_initialize(
        &pressure_fixture,
        vfs_endpoint,
        ramfs_endpoint,
        tty_endpoint
    );
    MODEL_EXPECT(
        selected,
        micros_vfs_state_initialize(
            &pressure_state,
            vfs_endpoint,
            ramfs_endpoint,
            tty_endpoint
        ) == MICROS_VFS_CORE_OK
        && micros_vfs_mount(
            &pressure_state,
            &pressure_fixture.io
        ) == MICROS_VFS_CORE_OK
    );
    for (index = 0; index < 61; ++index) {
        int written = snprintf(
            names[index],
            sizeof(names[index]),
            "f%02zu",
            index
        );

        MODEL_EXPECT(
            selected,
            written == 3
            && vfs_test_fixture_add_root_file(
                &pressure_fixture,
                names[index],
                (const uint8_t *)"x",
                1
            )
        );
        paths[index][0] = '/';
        memcpy(&paths[index][1], names[index], 4);
    }
    for (index = 0; index < 5; ++index) {
        endpoints[index] = (micros_endpoint_t)(
            ((uint32_t)index + 3) << MICROS_ENDPOINT_SLOT_BITS
            | ((uint32_t)index + 10)
        );
        MODEL_EXPECT(
            selected,
            micros_vfs_attach_console(
                &pressure_state,
                endpoints[index],
                0,
                &result
            ) == MICROS_VFS_CORE_OK
            && result == MICROS_VFS_TRUSTED_OK
        );
    }
    MODEL_EXPECT(
        selected,
        pressure_open(
            &pressure_state,
            &pressure_fixture,
            endpoints[0],
            "/etc",
            MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_DIRECTORY,
            MICROS_VFS_RESULT_OK
        )
        && pressure_open(
            &pressure_state,
            &pressure_fixture,
            endpoints[1],
            "/etc/motd",
            MICROS_VFS_OPEN_READ,
            MICROS_VFS_RESULT_OK
        )
    );
    for (index = 0; index < 61; ++index) {
        size_t process = 0;

        while (
            process < 5
            && used_descriptors[process] == 13
        ) {
            ++process;
        }
        MODEL_EXPECT(
            selected,
            process < 5
            &&
            pressure_open(
                &pressure_state,
                &pressure_fixture,
                endpoints[process],
                paths[index],
                MICROS_VFS_OPEN_READ,
                MICROS_VFS_RESULT_OK
            )
        );
        ++used_descriptors[process];
    }
    for (index = 0; index < MICROS_VFS_VNODE_CAPACITY; ++index) {
        if (
            pressure_state.vnodes[index].state
                == MICROS_VFS_VNODE_ACTIVE
        ) {
            ++active_vnodes;
        }
    }
    MODEL_EXPECT(
        selected,
        active_vnodes == MICROS_VFS_VNODE_CAPACITY
        && pressure_open(
            &pressure_state,
            &pressure_fixture,
            endpoints[4],
            "/etc/motd",
            MICROS_VFS_OPEN_READ,
            MICROS_VFS_RESULT_NO_SPACE
        )
        && micros_vfs_state_validate(&pressure_state)
            == MICROS_VFS_CORE_OK
    );
    ++selected->coverage.vnode_pressure;
    return true;
}

static bool run_process_pressure(struct vfs_model *selected)
{
    enum micros_vfs_trusted_result result;
    size_t index;
    size_t active_processes = 0;
    size_t active_open_files = 0;

    vfs_test_fixture_initialize(
        &pressure_fixture,
        vfs_endpoint,
        ramfs_endpoint,
        tty_endpoint
    );
    MODEL_EXPECT(
        selected,
        micros_vfs_state_initialize(
            &pressure_state,
            vfs_endpoint,
            ramfs_endpoint,
            tty_endpoint
        ) == MICROS_VFS_CORE_OK
        && micros_vfs_mount(
            &pressure_state,
            &pressure_fixture.io
        ) == MICROS_VFS_CORE_OK
    );
    for (index = 0; index < MICROS_VFS_PROCESS_CAPACITY; ++index) {
        micros_endpoint_t endpoint = (micros_endpoint_t)(
            ((uint32_t)index + 2) << MICROS_ENDPOINT_SLOT_BITS
            | UINT32_C(7)
        );

        MODEL_EXPECT(
            selected,
            micros_vfs_attach_console(
                &pressure_state,
                endpoint,
                0,
                &result
            ) == MICROS_VFS_CORE_OK
            && result == MICROS_VFS_TRUSTED_OK
        );
    }
    MODEL_EXPECT(
        selected,
        micros_vfs_attach_console(
            &pressure_state,
            (micros_endpoint_t)(
                UINT32_C(66) << MICROS_ENDPOINT_SLOT_BITS
                | UINT32_C(7)
            ),
            0,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_NO_SPACE
    );
    for (index = 0; index < MICROS_VFS_PROCESS_CAPACITY; ++index) {
        if (
            pressure_state.processes[index].state
                == MICROS_VFS_PROCESS_ACTIVE
        ) {
            ++active_processes;
        }
    }
    for (
        index = 0;
        index < MICROS_VFS_OPEN_FILE_CAPACITY;
        ++index
    ) {
        if (
            pressure_state.open_files[index].state
                == MICROS_VFS_OPEN_FILE_ACTIVE
        ) {
            ++active_open_files;
        }
    }
    MODEL_EXPECT(
        selected,
        active_processes == MICROS_VFS_PROCESS_CAPACITY
        && active_open_files == MICROS_VFS_PROCESS_CAPACITY * 3
        && pressure_state.vnodes[0].local_reference_count
            == 1 + MICROS_VFS_PROCESS_CAPACITY * 2
        && micros_vfs_state_validate(&pressure_state)
            == MICROS_VFS_CORE_OK
    );
    ++selected->coverage.process_pressure;
    return true;
}

static bool run_fatal_backend_case(struct vfs_model *selected)
{
    enum micros_vfs_trusted_result result;
    struct micros_vfs_request request;
    struct micros_vfs_result_action action;

    vfs_test_fixture_initialize(
        &fatal_fixture,
        vfs_endpoint,
        ramfs_endpoint,
        tty_endpoint
    );
    MODEL_EXPECT(
        selected,
        micros_vfs_state_initialize(
            &fatal_state,
            vfs_endpoint,
            ramfs_endpoint,
            tty_endpoint
        ) == MICROS_VFS_CORE_OK
        && micros_vfs_mount(
            &fatal_state,
            &fatal_fixture.io
        ) == MICROS_VFS_CORE_OK
        && micros_vfs_attach_console(
            &fatal_state,
            first_endpoint,
            0,
            &result
        ) == MICROS_VFS_CORE_OK
        && result == MICROS_VFS_TRUSTED_OK
    );
    request = (struct micros_vfs_request){
        .type = MICROS_VFS_MESSAGE_OPEN,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = first_endpoint,
        .reply_token = 1,
        .path_length = 10,
        .open_flags = MICROS_VFS_OPEN_READ,
    };
    request.grant = vfs_test_fixture_add_application_grant(
        &fatal_fixture,
        first_endpoint,
        MICROS_GRANT_PERMISSION_READ,
        (const uint8_t *)"/etc/motd",
        10
    );
    fatal_fixture.inject_ramfs_result = true;
    fatal_fixture.injected_ramfs_operation =
        MICROS_VFS_RAMFS_LOOKUP;
    fatal_fixture.injected_ramfs_result =
        MICROS_RAMFS_RESULT_GRANT;
    MODEL_EXPECT(
        selected,
        micros_vfs_handle_request(
            &fatal_state,
            &request,
            &fatal_fixture.io,
            &action
        ) == MICROS_VFS_CORE_ERROR_INVARIANT
    );
    ++selected->coverage.fatal;
    return true;
}

static bool model_pressure_and_failures(
    struct vfs_model *selected,
    size_t cycle
)
{
    struct micros_vfs_request request;
    struct micros_vfs_result_action action;

    if (!model_descriptor_pressure(selected)) {
        return false;
    }
    request = model_path_request(
        selected,
        MICROS_VFS_MESSAGE_OPEN,
        first_endpoint,
        selected->step + 1,
        "/etc/motd",
        MICROS_VFS_OPEN_READ,
        0
    );
    selected->fixture.force_backend_grant_capacity = true;
    MODEL_EXPECT(
        selected,
        micros_vfs_handle_request(
            &selected->production,
            &request,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_NO_SPACE
        && revoke_request_grant(selected, &request)
    );
    selected->fixture.force_backend_grant_capacity = false;
    ++selected->coverage.backend_capacity;
    MODEL_EXPECT(selected, compare_transition(selected));
    if (cycle == 32) {
        MODEL_EXPECT(selected, run_vnode_pressure(selected));
    }
    if (cycle == 64) {
        MODEL_EXPECT(selected, run_process_pressure(selected));
    }
    if ((cycle % 32) == 0) {
        MODEL_EXPECT(selected, run_fatal_backend_case(selected));
    }
    return true;
}

static bool model_detach_and_finish_cycle(
    struct vfs_model *selected
)
{
    enum micros_vfs_trusted_result result;
    size_t process_slot;
    struct micros_vfs_request request;
    struct micros_vfs_result_action action;

    if (
        reference_find_process(
            &selected->reference,
            second_endpoint,
            &process_slot
        )
    ) {
        MODEL_EXPECT(
            selected,
            micros_vfs_detach(
                &selected->production,
                second_endpoint,
                &selected->fixture.io,
                &result
            ) == MICROS_VFS_CORE_OK
            && result == MICROS_VFS_TRUSTED_OK
            && reference_detach(
                &selected->reference,
                second_endpoint
            )
        );
        ++selected->coverage.detach;
    } else {
        MODEL_EXPECT(
            selected,
            micros_vfs_detach(
                &selected->production,
                second_endpoint,
                &selected->fixture.io,
                &result
            ) == MICROS_VFS_CORE_OK
            && result == MICROS_VFS_TRUSTED_NOT_FOUND
        );
    }
    MODEL_EXPECT(selected, compare_transition(selected));
    MODEL_EXPECT(
        selected,
        vfs_test_fixture_drain_output(&selected->fixture) == 0
    );
    request = model_request(
        MICROS_VFS_MESSAGE_CLOSE,
        first_endpoint,
        selected->step + 1
    );
    request.version = 2;
    MODEL_EXPECT(
        selected,
        micros_vfs_handle_request(
            &selected->production,
            &request,
            &selected->fixture.io,
            &action
        ) == MICROS_VFS_CORE_OK
        && action.result == MICROS_VFS_RESULT_BAD_VERSION
    );
    ++selected->coverage.malformed;
    return compare_transition(selected);
}

static bool execute_step(struct vfs_model *selected)
{
    size_t phase = selected->step % MODEL_CYCLE_LENGTH;
    size_t cycle = selected->step / MODEL_CYCLE_LENGTH;
    uint64_t random = model_random(selected);
    uint32_t variant = (uint32_t)(random % 3);

    switch (phase) {
    case 0:
        record_operation(
            selected,
            MODEL_OPERATION_ATTACH,
            (uint32_t)random
        );
        return model_attach_second(selected);
    case 1:
        record_operation(
            selected,
            (random & 1) == 0
                ? MODEL_OPERATION_MALFORMED
                : MODEL_OPERATION_FOREIGN,
            (uint32_t)random
        );
        return model_malformed_or_foreign(selected, random);
    case 2:
        record_operation(
            selected,
            MODEL_OPERATION_MKDIR,
            (uint32_t)random
        );
        return model_mkdir(selected);
    case 3:
        record_operation(
            selected,
            MODEL_OPERATION_OPEN,
            (uint32_t)random
        );
        return model_open_path(
            selected,
            first_endpoint,
            "/etc/motd",
            MICROS_VFS_OPEN_READ,
            0,
            3,
            false
        );
    case 4:
        record_operation(
            selected,
            MODEL_OPERATION_SHARE,
            (uint32_t)random
        );
        return model_share_motd(selected);
    case 5:
        record_operation(
            selected,
            MODEL_OPERATION_READ,
            (uint32_t)random
        );
        return model_read(selected, first_endpoint, 3, 4, true);
    case 6:
        record_operation(
            selected,
            MODEL_OPERATION_GRANT_FAILURE,
            (uint32_t)random
        );
        return model_read(selected, second_endpoint, 3, 3, false);
    case 7:
        record_operation(
            selected,
            MODEL_OPERATION_READ,
            (uint32_t)random
        );
        return model_read(selected, second_endpoint, 3, 3, true);
    case 8:
        record_operation(
            selected,
            MODEL_OPERATION_CLOSE,
            (uint32_t)random
        );
        return model_close(selected, first_endpoint, 3);
    case 9:
        record_operation(
            selected,
            MODEL_OPERATION_CLOSE,
            (uint32_t)random
        );
        return model_close(selected, second_endpoint, 3);
    case 10:
        record_operation(
            selected,
            MODEL_OPERATION_CREATE,
            (uint32_t)random
        );
        return model_open_path(
            selected,
            first_endpoint,
            "tmp/note",
            MICROS_VFS_OPEN_READ
                | MICROS_VFS_OPEN_WRITE
                | MICROS_VFS_OPEN_CREATE,
            UINT32_C(0644),
            3,
            true
        );
    case 11:
        record_operation(
            selected,
            MODEL_OPERATION_WRITE,
            (uint32_t)random
        );
        return model_write_note(selected);
    case 12:
        record_operation(
            selected,
            MODEL_OPERATION_CLOSE,
            (uint32_t)random
        );
        return model_close(selected, first_endpoint, 3);
    case 13:
        record_operation(
            selected,
            MODEL_OPERATION_CHDIR,
            (uint32_t)random
        );
        return model_chdir_path(selected, "/tmp");
    case 14:
        record_operation(
            selected,
            MODEL_OPERATION_OPEN,
            (uint32_t)random
        );
        return model_open_path(
            selected,
            first_endpoint,
            "note",
            MICROS_VFS_OPEN_READ,
            0,
            3,
            false
        );
    case 15:
        record_operation(
            selected,
            MODEL_OPERATION_READ,
            (uint32_t)random
        );
        return model_read(selected, first_endpoint, 3, 5, true);
    case 16:
        record_operation(
            selected,
            MODEL_OPERATION_CHDIR,
            (uint32_t)random
        );
        if (!model_close(selected, first_endpoint, 3)) {
            return false;
        }
        return model_chdir_path(selected, "/");
    case 17:
        record_operation(
            selected,
            MODEL_OPERATION_OPEN,
            (uint32_t)random
        );
        return model_open_path(
            selected,
            first_endpoint,
            "/",
            MICROS_VFS_OPEN_READ
                | MICROS_VFS_OPEN_DIRECTORY,
            0,
            3,
            false
        );
    case 18:
        record_operation(
            selected,
            MODEL_OPERATION_GETDENTS,
            (uint32_t)random
        );
        return model_getdents(selected, false, 3, 160);
    case 19:
        record_operation(
            selected,
            MODEL_OPERATION_GRANT_FAILURE,
            (uint32_t)random
        );
        return model_getdents(selected, true, 3, 0);
    case 20:
        record_operation(
            selected,
            MODEL_OPERATION_GETDENTS,
            (uint32_t)random
        );
        if (
            !model_getdents(
                selected,
                false,
                MICROS_RAMFS_DIRECTORY_CURSOR_END,
                160
            )
        ) {
            return false;
        }
        return model_close(selected, first_endpoint, 3);
    case 21:
        record_operation(
            selected,
            MODEL_OPERATION_TTY_SUBMIT,
            (uint32_t)random
        );
        return model_console_read_submit(selected);
    case 22:
        record_operation(
            selected,
            variant == 0
                ? MODEL_OPERATION_TTY_COMPLETION
                : MODEL_OPERATION_TTY_CANCEL,
            variant
        );
        return model_console_read_finish(selected, variant);
    case 23:
        record_operation(
            selected,
            selected->reference.pending.state
                    == MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
                ? MODEL_OPERATION_TTY_COMPLETION
                : MODEL_OPERATION_MALFORMED,
            (uint32_t)random
        );
        return model_clear_debt_or_bad_version(selected);
    case 24:
        record_operation(
            selected,
            MODEL_OPERATION_TTY_SUBMIT,
            (uint32_t)random
        );
        return model_console_write_submit(selected, false);
    case 25:
        record_operation(
            selected,
            MODEL_OPERATION_TTY_COMPLETION,
            (uint32_t)random
        );
        return model_console_write_completion(selected);
    case 26:
        record_operation(
            selected,
            MODEL_OPERATION_TTY_SUBMIT,
            (uint32_t)random
        );
        return model_console_write_submit(selected, true);
    case 27:
        record_operation(
            selected,
            MODEL_OPERATION_TTY_WRITABLE,
            (uint32_t)random
        );
        return model_console_write_writable(selected);
    case 28:
        record_operation(
            selected,
            MODEL_OPERATION_TTY_COMPLETION,
            (uint32_t)random
        );
        return model_console_write_completion(selected);
    case 29:
        record_operation(
            selected,
            MODEL_OPERATION_DRAIN,
            (uint32_t)random
        );
        return model_test_drain_begin(selected);
    case 30:
        record_operation(
            selected,
            MODEL_OPERATION_DESCRIPTOR_PRESSURE,
            (uint32_t)random
        );
        if (
            !model_test_drain_finish(selected)
            || !model_pressure_and_failures(selected, cycle)
        ) {
            return false;
        }
        return true;
    case 31:
        record_operation(
            selected,
            MODEL_OPERATION_DETACH,
            (uint32_t)random
        );
        return model_detach_and_finish_cycle(selected);
    }
    return model_failure(
        selected,
        "model phase is defined",
        __LINE__
    );
}

static bool coverage_is_complete(struct vfs_model *selected)
{
    const struct model_coverage *coverage = &selected->coverage;

    MODEL_EXPECT(
        selected,
        coverage->attach != 0
        && coverage->detach != 0
        && coverage->open != 0
        && coverage->create != 0
        && coverage->close != 0
        && coverage->read != 0
        && coverage->write != 0
        && coverage->getdents != 0
        && coverage->mkdir != 0
        && coverage->chdir != 0
        && coverage->shared != 0
        && coverage->descriptor_pressure != 0
        && coverage->vnode_pressure != 0
        && coverage->process_pressure != 0
        && coverage->grant_failure != 0
        && coverage->backend_capacity != 0
        && coverage->tty_submit != 0
        && coverage->tty_completion != 0
        && coverage->tty_writable != 0
        && coverage->tty_cancel != 0
        && coverage->tty_collect != 0
        && coverage->malformed != 0
        && coverage->foreign != 0
        && coverage->fatal != 0
        && coverage->threshold_cleanup != 0
    );
    return true;
}

int main(void)
{
    size_t previous_transition_checks;

    if (!initialize_model(&model)) {
        fputs("VFS model initialization failed\n", stderr);
        return 1;
    }
    for (model.step = 0; model.step < MODEL_STEP_COUNT; ++model.step) {
        previous_transition_checks = model.transition_checks;
        if (!execute_step(&model)) {
            return 1;
        }
        if (
            model.transition_checks
                == previous_transition_checks
        ) {
            model_failure(
                &model,
                "each step compares at least one core transition",
                __LINE__
            );
            return 1;
        }
    }
    --model.step;
    if (!coverage_is_complete(&model)) {
        return 1;
    }
    printf(
        "VFS_MODEL_TEST_PASS seed=0x%016llx steps=%u\n",
        (unsigned long long)model.seed,
        MODEL_STEP_COUNT
    );
    return 0;
}
