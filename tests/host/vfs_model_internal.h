#ifndef MICROS_TESTS_HOST_VFS_MODEL_INTERNAL_H
#define MICROS_TESTS_HOST_VFS_MODEL_INTERNAL_H

#include "servers/vfs/vfs_core.h"
#include "tests/host/vfs_test_fixture.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    struct micros_vfs_process_record processes[MICROS_VFS_PROCESS_CAPACITY];
    struct micros_vfs_open_file open_files[MICROS_VFS_OPEN_FILE_CAPACITY];
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
    struct micros_vfs_state pressure_state;
    struct vfs_test_fixture pressure_fixture;
    struct micros_vfs_state fatal_state;
    struct vfs_test_fixture fatal_fixture;
};

#define VFS_MODEL_VFS_ENDPOINT UINT32_C(0x00001006)
#define VFS_MODEL_VM_ENDPOINT UINT32_C(0x00001002)
#define VFS_MODEL_PM_ENDPOINT UINT32_C(0x00001003)
#define VFS_MODEL_RAMFS_ENDPOINT UINT32_C(0x00001005)
#define VFS_MODEL_TTY_ENDPOINT UINT32_C(0x00001004)
#define VFS_MODEL_FIRST_ENDPOINT UINT32_C(0x00002007)
#define VFS_MODEL_SECOND_ENDPOINT UINT32_C(0x00002008)

bool model_failure(const struct vfs_model *model, const char *expression, int line);

#define MODEL_EXPECT(selected, expression)                                                         \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            return model_failure((selected), #expression, __LINE__);                               \
        }                                                                                          \
    } while (false)

uint64_t model_random(struct vfs_model *model);
void record_operation(struct vfs_model *model, enum model_operation operation, uint32_t detail);
struct micros_vfs_request model_request(uint32_t type, micros_endpoint_t source,
                                        uint64_t reply_token);
struct micros_vfs_request model_path_request(struct vfs_model *model, uint32_t type,
                                             micros_endpoint_t endpoint, uint64_t reply_token,
                                             const char *path, uint32_t open_flags, uint32_t mode);
bool revoke_request_grant(struct vfs_model *model, const struct micros_vfs_request *request);
bool vfs_model_initialize(struct vfs_model *model);

uint64_t reference_node_handle(size_t slot);
bool reference_path_metadata(const struct vfs_model *model, const char *path, uint64_t *node,
                             uint32_t *mode, uint64_t *size);
const char *absolute_model_path(const char *path);
bool reference_find_process(const struct reference_state *reference, micros_endpoint_t endpoint,
                            size_t *slot);
bool reference_close(struct reference_state *reference, micros_endpoint_t endpoint,
                     uint32_t descriptor);
bool reference_attach(struct reference_state *reference, micros_endpoint_t endpoint);
bool reference_detach(struct reference_state *reference, micros_endpoint_t endpoint);
bool reference_open(struct vfs_model *model, micros_endpoint_t endpoint, uint64_t node,
                    uint32_t mode, uint64_t size, uint32_t open_flags,
                    uint32_t expected_descriptor);
bool reference_share(struct reference_state *reference, micros_endpoint_t source,
                     uint32_t source_descriptor, micros_endpoint_t destination,
                     uint32_t destination_descriptor);
struct micros_vfs_open_file *reference_open_file(struct reference_state *reference,
                                                 micros_endpoint_t endpoint, uint32_t descriptor);
bool reference_chdir(struct vfs_model *model, micros_endpoint_t endpoint, uint64_t node,
                     uint32_t mode);
void reference_clear_pending(struct reference_state *reference);
void reference_advance_request_id(struct reference_state *reference, uint64_t request_id);
uint32_t reference_backend_references(const struct reference_state *reference, size_t node_slot);

bool compare_complete_state(struct vfs_model *model);
bool compare_transition(struct vfs_model *model);

bool model_attach_second(struct vfs_model *model);
bool model_detach_and_finish_cycle(struct vfs_model *model);

bool model_malformed_or_foreign(struct vfs_model *model, uint64_t random);
bool model_mkdir(struct vfs_model *model);
bool model_open_path(struct vfs_model *model, micros_endpoint_t endpoint, const char *path,
                     uint32_t flags, uint32_t mode, uint32_t expected_descriptor, bool creation);
bool model_share_motd(struct vfs_model *model);
bool model_read(struct vfs_model *model, micros_endpoint_t endpoint, uint32_t descriptor,
                uint32_t count, bool valid);
bool model_write_note(struct vfs_model *model);
bool model_close(struct vfs_model *model, micros_endpoint_t endpoint, uint32_t descriptor);
bool model_chdir_path(struct vfs_model *model, const char *path);
bool model_getdents(struct vfs_model *model, bool reject_copy, uint64_t expected_position,
                    uint64_t expected_count);

bool model_console_read_submit(struct vfs_model *model);
bool model_console_read_finish(struct vfs_model *model, uint32_t variant);
bool model_clear_debt_or_bad_version(struct vfs_model *model);
bool model_console_write_submit(struct vfs_model *model, bool expect_busy);
bool model_console_write_completion(struct vfs_model *model);
bool model_console_write_writable(struct vfs_model *model);
bool model_test_drain_begin(struct vfs_model *model);
bool model_test_drain_finish(struct vfs_model *model);

bool model_pressure_and_failures(struct vfs_model *model, size_t cycle);

bool vfs_model_execute_step(struct vfs_model *model);
bool vfs_model_coverage_is_complete(struct vfs_model *model);

#endif
