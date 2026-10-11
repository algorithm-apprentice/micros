#include "tests/host/vfs_model_internal.h"

#include <stdio.h>
#include <string.h>

static bool model_descriptor_pressure(struct vfs_model *selected) {
    size_t descriptor;
    struct micros_vfs_request request;
    struct micros_vfs_result_action action;

    for (descriptor = 3; descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY; ++descriptor) {
        if (!model_open_path(selected, VFS_MODEL_FIRST_ENDPOINT, "/etc/motd", MICROS_VFS_OPEN_READ,
                             0, (uint32_t)descriptor, false)) {
            return false;
        }
    }
    request = model_path_request(selected, MICROS_VFS_MESSAGE_OPEN, VFS_MODEL_FIRST_ENDPOINT,
                                 selected->step + 1, "/etc/motd", MICROS_VFS_OPEN_READ, 0);
    MODEL_EXPECT(selected, request.grant != MICROS_GRANT_NONE &&
                               micros_vfs_handle_request(&selected->production, &request,
                                                         &selected->fixture.io,
                                                         &action) == MICROS_VFS_CORE_OK &&
                               action.result == MICROS_VFS_RESULT_NO_SPACE &&
                               revoke_request_grant(selected, &request));
    MODEL_EXPECT(selected, compare_transition(selected));
    for (descriptor = 3; descriptor < MICROS_VFS_DESCRIPTOR_CAPACITY; ++descriptor) {
        if (!model_close(selected, VFS_MODEL_FIRST_ENDPOINT, (uint32_t)descriptor)) {
            return false;
        }
    }
    ++selected->coverage.descriptor_pressure;
    return true;
}

static bool pressure_open(struct micros_vfs_state *state, struct vfs_test_fixture *fixture,
                          micros_endpoint_t endpoint, const char *path, uint32_t flags,
                          enum micros_vfs_result expected) {
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
        fixture, endpoint, MICROS_GRANT_PERMISSION_READ, (const uint8_t *)path, length);
    request.path_length = (uint32_t)length;
    return (request.grant != MICROS_GRANT_NONE &&
            micros_vfs_handle_request(state, &request, &fixture->io, &action) ==
                MICROS_VFS_CORE_OK &&
            action.result == expected &&
            vfs_test_fixture_revoke_application_grant(fixture, request.grant));
}

struct vnode_pressure_scenario {
    micros_endpoint_t endpoints[5];
    char names[61][4];
    char paths[61][5];
    size_t used_descriptors[5];
    size_t active_vnodes;
};

static bool initialize_vnode_pressure(struct vfs_model *selected) {
    vfs_test_fixture_initialize(&selected->pressure_fixture, VFS_MODEL_VFS_ENDPOINT,
                                VFS_MODEL_VM_ENDPOINT, VFS_MODEL_PM_ENDPOINT,
                                VFS_MODEL_RAMFS_ENDPOINT, VFS_MODEL_TTY_ENDPOINT);
    MODEL_EXPECT(selected,
                 micros_vfs_state_initialize(&selected->pressure_state, VFS_MODEL_VFS_ENDPOINT,
                                             VFS_MODEL_VM_ENDPOINT, VFS_MODEL_PM_ENDPOINT,
                                             VFS_MODEL_RAMFS_ENDPOINT,
                                             VFS_MODEL_TTY_ENDPOINT) == MICROS_VFS_CORE_OK &&
                     micros_vfs_mount(&selected->pressure_state, &selected->pressure_fixture.io) ==
                         MICROS_VFS_CORE_OK);
    return true;
}

static bool seed_vnode_pressure_files(struct vfs_model *selected,
                                      struct vnode_pressure_scenario *scenario) {
    size_t index;

    for (index = 0; index < 61; ++index) {
        int written =
            snprintf(scenario->names[index], sizeof(scenario->names[index]), "f%02zu", index);

        MODEL_EXPECT(selected,
                     written == 3 && vfs_test_fixture_add_root_file(&selected->pressure_fixture,
                                                                    scenario->names[index],
                                                                    (const uint8_t *)"x", 1));
        scenario->paths[index][0] = '/';
        memcpy(&scenario->paths[index][1], scenario->names[index], 4);
    }
    return true;
}

static bool attach_vnode_pressure_processes(struct vfs_model *selected,
                                            struct vnode_pressure_scenario *scenario) {
    enum micros_vfs_trusted_result result;
    size_t index;

    for (index = 0; index < 5; ++index) {
        scenario->endpoints[index] =
            (micros_endpoint_t)(((uint32_t)index + 3) << MICROS_ENDPOINT_SLOT_BITS |
                                ((uint32_t)index + 10));
        MODEL_EXPECT(selected, micros_vfs_attach_console(&selected->pressure_state,
                                                         scenario->endpoints[index], 0,
                                                         &result) == MICROS_VFS_CORE_OK &&
                                   result == MICROS_VFS_TRUSTED_OK);
    }
    return true;
}

static bool reserve_baseline_pressure_vnodes(struct vfs_model *selected,
                                             const struct vnode_pressure_scenario *scenario) {
    MODEL_EXPECT(selected, pressure_open(&selected->pressure_state, &selected->pressure_fixture,
                                         scenario->endpoints[0], "/etc",
                                         MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_DIRECTORY,
                                         MICROS_VFS_RESULT_OK) &&
                               pressure_open(&selected->pressure_state, &selected->pressure_fixture,
                                             scenario->endpoints[1], "/etc/motd",
                                             MICROS_VFS_OPEN_READ, MICROS_VFS_RESULT_OK));
    return true;
}

static bool fill_vnode_pressure_capacity(struct vfs_model *selected,
                                         struct vnode_pressure_scenario *scenario) {
    size_t index;

    for (index = 0; index < 61; ++index) {
        size_t process = 0;

        while (process < 5 && scenario->used_descriptors[process] == 13) {
            ++process;
        }
        MODEL_EXPECT(selected,
                     process < 5 &&
                         pressure_open(&selected->pressure_state, &selected->pressure_fixture,
                                       scenario->endpoints[process], scenario->paths[index],
                                       MICROS_VFS_OPEN_READ, MICROS_VFS_RESULT_OK));
        ++scenario->used_descriptors[process];
    }
    return true;
}

static void count_active_pressure_vnodes(struct vfs_model *selected,
                                         struct vnode_pressure_scenario *scenario) {
    size_t index;

    for (index = 0; index < MICROS_VFS_VNODE_CAPACITY; ++index) {
        if (selected->pressure_state.vnodes[index].state == MICROS_VFS_VNODE_ACTIVE) {
            ++scenario->active_vnodes;
        }
    }
}

static bool verify_vnode_pressure_limit(struct vfs_model *selected,
                                        const struct vnode_pressure_scenario *scenario) {
    MODEL_EXPECT(selected,
                 scenario->active_vnodes == MICROS_VFS_VNODE_CAPACITY &&
                     pressure_open(&selected->pressure_state, &selected->pressure_fixture,
                                   scenario->endpoints[4], "/etc/motd", MICROS_VFS_OPEN_READ,
                                   MICROS_VFS_RESULT_NO_SPACE) &&
                     micros_vfs_state_validate(&selected->pressure_state) == MICROS_VFS_CORE_OK);
    ++selected->coverage.vnode_pressure;
    return true;
}

static bool run_vnode_pressure(struct vfs_model *selected) {
    struct vnode_pressure_scenario scenario = {
        .used_descriptors = {1, 1, 0, 0, 0},
    };

    if (!initialize_vnode_pressure(selected) || !seed_vnode_pressure_files(selected, &scenario) ||
        !attach_vnode_pressure_processes(selected, &scenario) ||
        !reserve_baseline_pressure_vnodes(selected, &scenario) ||
        !fill_vnode_pressure_capacity(selected, &scenario)) {
        return false;
    }
    count_active_pressure_vnodes(selected, &scenario);
    return verify_vnode_pressure_limit(selected, &scenario);
}

static bool run_process_pressure(struct vfs_model *selected) {
    enum micros_vfs_trusted_result result;
    size_t index;
    size_t active_processes = 0;
    size_t active_open_files = 0;

    vfs_test_fixture_initialize(&selected->pressure_fixture, VFS_MODEL_VFS_ENDPOINT,
                                VFS_MODEL_VM_ENDPOINT, VFS_MODEL_PM_ENDPOINT,
                                VFS_MODEL_RAMFS_ENDPOINT, VFS_MODEL_TTY_ENDPOINT);
    MODEL_EXPECT(selected,
                 micros_vfs_state_initialize(&selected->pressure_state, VFS_MODEL_VFS_ENDPOINT,
                                             VFS_MODEL_VM_ENDPOINT, VFS_MODEL_PM_ENDPOINT,
                                             VFS_MODEL_RAMFS_ENDPOINT,
                                             VFS_MODEL_TTY_ENDPOINT) == MICROS_VFS_CORE_OK &&
                     micros_vfs_mount(&selected->pressure_state, &selected->pressure_fixture.io) ==
                         MICROS_VFS_CORE_OK);
    for (index = 0; index < MICROS_VFS_PROCESS_CAPACITY; ++index) {
        micros_endpoint_t endpoint =
            (micros_endpoint_t)(((uint32_t)index + 2) << MICROS_ENDPOINT_SLOT_BITS | UINT32_C(7));

        MODEL_EXPECT(selected, micros_vfs_attach_console(&selected->pressure_state, endpoint, 0,
                                                         &result) == MICROS_VFS_CORE_OK &&
                                   result == MICROS_VFS_TRUSTED_OK);
    }
    MODEL_EXPECT(selected,
                 micros_vfs_attach_console(
                     &selected->pressure_state,
                     (micros_endpoint_t)(UINT32_C(66) << MICROS_ENDPOINT_SLOT_BITS | UINT32_C(7)),
                     0, &result) == MICROS_VFS_CORE_OK &&
                     result == MICROS_VFS_TRUSTED_NO_SPACE);
    for (index = 0; index < MICROS_VFS_PROCESS_CAPACITY; ++index) {
        if (selected->pressure_state.processes[index].state == MICROS_VFS_PROCESS_ACTIVE) {
            ++active_processes;
        }
    }
    for (index = 0; index < MICROS_VFS_OPEN_FILE_CAPACITY; ++index) {
        if (selected->pressure_state.open_files[index].state == MICROS_VFS_OPEN_FILE_ACTIVE) {
            ++active_open_files;
        }
    }
    MODEL_EXPECT(selected,
                 active_processes == MICROS_VFS_PROCESS_CAPACITY &&
                     active_open_files == MICROS_VFS_PROCESS_CAPACITY * 3 &&
                     selected->pressure_state.vnodes[0].local_reference_count ==
                         1 + MICROS_VFS_PROCESS_CAPACITY * 2 &&
                     micros_vfs_state_validate(&selected->pressure_state) == MICROS_VFS_CORE_OK);
    ++selected->coverage.process_pressure;
    return true;
}

static bool run_fatal_backend_case(struct vfs_model *selected) {
    enum micros_vfs_trusted_result result;
    struct micros_vfs_request request;
    struct micros_vfs_result_action action;

    vfs_test_fixture_initialize(&selected->fatal_fixture, VFS_MODEL_VFS_ENDPOINT,
                                VFS_MODEL_VM_ENDPOINT, VFS_MODEL_PM_ENDPOINT,
                                VFS_MODEL_RAMFS_ENDPOINT, VFS_MODEL_TTY_ENDPOINT);
    MODEL_EXPECT(selected,
                 micros_vfs_state_initialize(&selected->fatal_state, VFS_MODEL_VFS_ENDPOINT,
                                             VFS_MODEL_VM_ENDPOINT, VFS_MODEL_PM_ENDPOINT,
                                             VFS_MODEL_RAMFS_ENDPOINT,
                                             VFS_MODEL_TTY_ENDPOINT) == MICROS_VFS_CORE_OK &&
                     micros_vfs_mount(&selected->fatal_state, &selected->fatal_fixture.io) ==
                         MICROS_VFS_CORE_OK &&
                     micros_vfs_attach_console(&selected->fatal_state, VFS_MODEL_FIRST_ENDPOINT, 0,
                                               &result) == MICROS_VFS_CORE_OK &&
                     result == MICROS_VFS_TRUSTED_OK);
    request = (struct micros_vfs_request){
        .type = MICROS_VFS_MESSAGE_OPEN,
        .version = MICROS_VFS_PROTOCOL_VERSION,
        .source = VFS_MODEL_FIRST_ENDPOINT,
        .reply_token = 1,
        .path_length = 10,
        .open_flags = MICROS_VFS_OPEN_READ,
    };
    request.grant = vfs_test_fixture_add_application_grant(
        &selected->fatal_fixture, VFS_MODEL_FIRST_ENDPOINT, MICROS_GRANT_PERMISSION_READ,
        (const uint8_t *)"/etc/motd", 10);
    selected->fatal_fixture.inject_ramfs_result = true;
    selected->fatal_fixture.injected_ramfs_operation = MICROS_VFS_RAMFS_LOOKUP;
    selected->fatal_fixture.injected_ramfs_result = MICROS_RAMFS_RESULT_GRANT;
    MODEL_EXPECT(selected, micros_vfs_handle_request(&selected->fatal_state, &request,
                                                     &selected->fatal_fixture.io,
                                                     &action) == MICROS_VFS_CORE_ERROR_INVARIANT);
    ++selected->coverage.fatal;
    return true;
}

bool model_pressure_and_failures(struct vfs_model *selected, size_t cycle) {
    struct micros_vfs_request request;
    struct micros_vfs_result_action action;

    if (!model_descriptor_pressure(selected)) {
        return false;
    }
    request = model_path_request(selected, MICROS_VFS_MESSAGE_OPEN, VFS_MODEL_FIRST_ENDPOINT,
                                 selected->step + 1, "/etc/motd", MICROS_VFS_OPEN_READ, 0);
    selected->fixture.force_backend_grant_capacity = true;
    MODEL_EXPECT(selected,
                 micros_vfs_handle_request(&selected->production, &request, &selected->fixture.io,
                                           &action) == MICROS_VFS_CORE_OK &&
                     action.result == MICROS_VFS_RESULT_NO_SPACE &&
                     revoke_request_grant(selected, &request));
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
