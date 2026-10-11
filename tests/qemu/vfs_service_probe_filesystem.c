#include "tests/qemu/vfs_service_probe_internal.h"

#include "tests/qemu/vfs_service_protocol.h"

#define MICROS_VFS_TEST_DIRECTORY_MODE UINT32_C(0x000041ed)
#define MICROS_VFS_TEST_REGULAR_MODE UINT32_C(0x000081a4)
#define MICROS_VFS_TEST_CREATE_PERMISSIONS UINT32_C(0x000001a4)
#define MICROS_VFS_TEST_DIRECTORY_PERMISSIONS UINT32_C(0x000001ed)

static bool probe_directory_record_matches(const uint8_t *record, const char *name, uint32_t mode) {
    size_t name_length = probe_string_size(name);

    return (name_length >= 1 && name_length <= MICROS_VFS_NAME_MAX &&
            probe_read_u32_le(&record[0]) == MICROS_VFS_DIRECTORY_RECORD_SIZE &&
            probe_read_u32_le(&record[4]) == mode && probe_read_u32_le(&record[8]) == name_length &&
            probe_read_u32_le(&record[12]) == 0 &&
            probe_bytes_equal(&record[16], (const uint8_t *)name, name_length) &&
            probe_bytes_are_zero(&record[16 + name_length], MICROS_VFS_NAME_MAX - name_length) &&
            probe_bytes_are_zero(&record[76], 4));
}

bool probe_enumerate_root(struct vfs_service_probe_context *context,
                          micros_endpoint_t vfs_endpoint) {
    static const char *const names[] = {".", "..", "etc", "tmp"};
    struct probe_vfs_result result;
    uint32_t descriptor;
    uint64_t previous_position = 0;
    size_t index;

    if (!probe_open(context, vfs_endpoint, "/",
                    MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_DIRECTORY, 0, &result) ||
        result.result != MICROS_VFS_RESULT_OK || result.mode != MICROS_VFS_TEST_DIRECTORY_MODE ||
        result.descriptor < 3) {
        return false;
    }
    descriptor = result.descriptor;
    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        probe_clear_bytes(context->buffer, sizeof(context->buffer));
        if (!probe_transfer(context, vfs_endpoint, MICROS_VFS_MESSAGE_GETDENTS, descriptor,
                            MICROS_VFS_DIRECTORY_RECORD_SIZE, &result) ||
            result.result != MICROS_VFS_RESULT_OK ||
            result.transferred_count != MICROS_VFS_DIRECTORY_RECORD_SIZE ||
            result.position == previous_position ||
            !probe_directory_record_matches(context->buffer, names[index],
                                            MICROS_VFS_TEST_DIRECTORY_MODE)) {
            return false;
        }
        previous_position = result.position;
    }
    probe_clear_bytes(context->buffer, sizeof(context->buffer));
    return (probe_transfer(context, vfs_endpoint, MICROS_VFS_MESSAGE_GETDENTS, descriptor,
                           MICROS_VFS_DIRECTORY_RECORD_SIZE, &result) &&
            result.result == MICROS_VFS_RESULT_OK && result.transferred_count == 0 &&
            result.position == previous_position && probe_close(vfs_endpoint, descriptor));
}

struct filesystem_probe_scenario {
    struct probe_vfs_result result;
    uint32_t descriptor;
};

static bool probe_read_seeded_motd(struct vfs_service_probe_context *context,
                                   micros_endpoint_t vfs_endpoint) {
    static const uint8_t motd[] = MICROS_VFS_TEST_MOTD;
    struct probe_vfs_result result;

    if (!probe_open(context, vfs_endpoint, "/etc/motd", MICROS_VFS_OPEN_READ, 0, &result) ||
        result.result != MICROS_VFS_RESULT_OK || result.mode != MICROS_VFS_TEST_REGULAR_MODE ||
        result.descriptor < 3) {
        return false;
    }
    return probe_read_exact(context, vfs_endpoint, result.descriptor, motd, sizeof(motd) - 1,
                            true) &&
           probe_close(vfs_endpoint, result.descriptor);
}

static bool probe_create_note(struct vfs_service_probe_context *context,
                              micros_endpoint_t vfs_endpoint,
                              struct filesystem_probe_scenario *scenario) {
    if (!probe_path_call(context, vfs_endpoint, MICROS_VFS_MESSAGE_MKDIR, "tmp",
                         MICROS_VFS_TEST_DIRECTORY_PERMISSIONS, &scenario->result) ||
        scenario->result.result != MICROS_VFS_RESULT_OK ||
        !probe_open(context, vfs_endpoint, "tmp/note",
                    MICROS_VFS_OPEN_WRITE | MICROS_VFS_OPEN_CREATE | MICROS_VFS_OPEN_EXCLUSIVE,
                    MICROS_VFS_TEST_CREATE_PERMISSIONS, &scenario->result) ||
        scenario->result.result != MICROS_VFS_RESULT_OK ||
        scenario->result.mode != MICROS_VFS_TEST_REGULAR_MODE || scenario->result.descriptor < 3) {
        return false;
    }
    scenario->descriptor = scenario->result.descriptor;
    return true;
}

static bool probe_write_and_close_note(struct vfs_service_probe_context *context,
                                       micros_endpoint_t vfs_endpoint,
                                       const struct filesystem_probe_scenario *scenario) {
    static const uint8_t note[] = MICROS_VFS_TEST_NOTE_DATA;
    struct probe_vfs_result result;

    probe_clear_bytes(context->buffer, sizeof(context->buffer));
    probe_copy_bytes(context->buffer, note, sizeof(note) - 1);
    return probe_transfer(context, vfs_endpoint, MICROS_VFS_MESSAGE_WRITE, scenario->descriptor,
                          sizeof(note) - 1, &result) &&
           result.result == MICROS_VFS_RESULT_OK && result.transferred_count == sizeof(note) - 1 &&
           result.position == sizeof(note) - 1 && probe_close(vfs_endpoint, scenario->descriptor);
}

static bool probe_read_note_absolute(struct vfs_service_probe_context *context,
                                     micros_endpoint_t vfs_endpoint) {
    static const uint8_t note[] = MICROS_VFS_TEST_NOTE_DATA;
    struct probe_vfs_result result;

    return probe_open(context, vfs_endpoint, "/tmp/note", MICROS_VFS_OPEN_READ, 0, &result) &&
           result.result == MICROS_VFS_RESULT_OK &&
           probe_read_exact(context, vfs_endpoint, result.descriptor, note, sizeof(note) - 1,
                            false) &&
           probe_close(vfs_endpoint, result.descriptor);
}

static bool probe_read_note_relative(struct vfs_service_probe_context *context,
                                     micros_endpoint_t vfs_endpoint) {
    static const uint8_t note[] = MICROS_VFS_TEST_NOTE_DATA;
    struct probe_vfs_result result;

    return probe_path_call(context, vfs_endpoint, MICROS_VFS_MESSAGE_CHDIR, "/tmp", 0, &result) &&
           result.result == MICROS_VFS_RESULT_OK &&
           probe_open(context, vfs_endpoint, "note", MICROS_VFS_OPEN_READ, 0, &result) &&
           result.result == MICROS_VFS_RESULT_OK &&
           probe_read_exact(context, vfs_endpoint, result.descriptor, note, sizeof(note) - 1,
                            false) &&
           probe_close(vfs_endpoint, result.descriptor);
}

static bool probe_restore_root(struct vfs_service_probe_context *context,
                               micros_endpoint_t vfs_endpoint) {
    struct probe_vfs_result result;

    return probe_path_call(context, vfs_endpoint, MICROS_VFS_MESSAGE_CHDIR, "/", 0, &result) &&
           result.result == MICROS_VFS_RESULT_OK;
}

bool probe_test_filesystem(struct vfs_service_probe_context *context,
                           micros_endpoint_t vfs_endpoint) {
    struct filesystem_probe_scenario scenario = {0};

    return probe_read_seeded_motd(context, vfs_endpoint) &&
           probe_create_note(context, vfs_endpoint, &scenario) &&
           probe_write_and_close_note(context, vfs_endpoint, &scenario) &&
           probe_read_note_absolute(context, vfs_endpoint) &&
           probe_read_note_relative(context, vfs_endpoint) &&
           probe_restore_root(context, vfs_endpoint);
}
