#include "tests/host/vfs_model_internal.h"

static bool execute_process_setup_phase(struct vfs_model *selected, size_t phase, uint64_t random) {
    switch (phase) {
    case 0:
        record_operation(selected, MODEL_OPERATION_ATTACH, (uint32_t)random);
        return model_attach_second(selected);
    case 1:
        record_operation(selected,
                         (random & 1) == 0 ? MODEL_OPERATION_MALFORMED : MODEL_OPERATION_FOREIGN,
                         (uint32_t)random);
        return model_malformed_or_foreign(selected, random);
    }
    return model_failure(selected, "model phase is defined", __LINE__);
}

static bool execute_namespace_phase(struct vfs_model *selected, size_t phase, uint64_t random) {
    switch (phase) {
    case 2:
        record_operation(selected, MODEL_OPERATION_MKDIR, (uint32_t)random);
        return model_mkdir(selected);
    case 3:
        record_operation(selected, MODEL_OPERATION_OPEN, (uint32_t)random);
        return model_open_path(selected, VFS_MODEL_FIRST_ENDPOINT, "/etc/motd",
                               MICROS_VFS_OPEN_READ, 0, 3, false);
    case 4:
        record_operation(selected, MODEL_OPERATION_SHARE, (uint32_t)random);
        return model_share_motd(selected);
    }
    return model_failure(selected, "model phase is defined", __LINE__);
}

static bool execute_regular_io_phase(struct vfs_model *selected, size_t phase, uint64_t random) {
    switch (phase) {
    case 5:
        record_operation(selected, MODEL_OPERATION_READ, (uint32_t)random);
        return model_read(selected, VFS_MODEL_FIRST_ENDPOINT, 3, 4, true);
    case 6:
        record_operation(selected, MODEL_OPERATION_GRANT_FAILURE, (uint32_t)random);
        return model_read(selected, VFS_MODEL_SECOND_ENDPOINT, 3, 3, false);
    case 7:
        record_operation(selected, MODEL_OPERATION_READ, (uint32_t)random);
        return model_read(selected, VFS_MODEL_SECOND_ENDPOINT, 3, 3, true);
    case 8:
        record_operation(selected, MODEL_OPERATION_CLOSE, (uint32_t)random);
        return model_close(selected, VFS_MODEL_FIRST_ENDPOINT, 3);
    case 9:
        record_operation(selected, MODEL_OPERATION_CLOSE, (uint32_t)random);
        return model_close(selected, VFS_MODEL_SECOND_ENDPOINT, 3);
    case 10:
        record_operation(selected, MODEL_OPERATION_CREATE, (uint32_t)random);
        return model_open_path(selected, VFS_MODEL_FIRST_ENDPOINT, "tmp/note",
                               MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_WRITE |
                                   MICROS_VFS_OPEN_CREATE,
                               UINT32_C(0644), 3, true);
    case 11:
        record_operation(selected, MODEL_OPERATION_WRITE, (uint32_t)random);
        return model_write_note(selected);
    case 12:
        record_operation(selected, MODEL_OPERATION_CLOSE, (uint32_t)random);
        return model_close(selected, VFS_MODEL_FIRST_ENDPOINT, 3);
    }
    return model_failure(selected, "model phase is defined", __LINE__);
}

static bool execute_directory_phase(struct vfs_model *selected, size_t phase, uint64_t random) {
    switch (phase) {
    case 13:
        record_operation(selected, MODEL_OPERATION_CHDIR, (uint32_t)random);
        return model_chdir_path(selected, "/tmp");
    case 14:
        record_operation(selected, MODEL_OPERATION_OPEN, (uint32_t)random);
        return model_open_path(selected, VFS_MODEL_FIRST_ENDPOINT, "note", MICROS_VFS_OPEN_READ, 0,
                               3, false);
    case 15:
        record_operation(selected, MODEL_OPERATION_READ, (uint32_t)random);
        return model_read(selected, VFS_MODEL_FIRST_ENDPOINT, 3, 5, true);
    case 16:
        record_operation(selected, MODEL_OPERATION_CHDIR, (uint32_t)random);
        if (!model_close(selected, VFS_MODEL_FIRST_ENDPOINT, 3)) {
            return false;
        }
        return model_chdir_path(selected, "/");
    case 17:
        record_operation(selected, MODEL_OPERATION_OPEN, (uint32_t)random);
        return model_open_path(selected, VFS_MODEL_FIRST_ENDPOINT, "/",
                               MICROS_VFS_OPEN_READ | MICROS_VFS_OPEN_DIRECTORY, 0, 3, false);
    case 18:
        record_operation(selected, MODEL_OPERATION_GETDENTS, (uint32_t)random);
        return model_getdents(selected, false, 3, 160);
    case 19:
        record_operation(selected, MODEL_OPERATION_GRANT_FAILURE, (uint32_t)random);
        return model_getdents(selected, true, 3, 0);
    case 20:
        record_operation(selected, MODEL_OPERATION_GETDENTS, (uint32_t)random);
        if (!model_getdents(selected, false, MICROS_RAMFS_DIRECTORY_CURSOR_END, 160)) {
            return false;
        }
        return model_close(selected, VFS_MODEL_FIRST_ENDPOINT, 3);
    }
    return model_failure(selected, "model phase is defined", __LINE__);
}

static bool execute_console_phase(struct vfs_model *selected, size_t phase, uint64_t random,
                                  uint32_t variant) {
    switch (phase) {
    case 21:
        record_operation(selected, MODEL_OPERATION_TTY_SUBMIT, (uint32_t)random);
        return model_console_read_submit(selected);
    case 22:
        record_operation(selected,
                         variant == 0 ? MODEL_OPERATION_TTY_COMPLETION : MODEL_OPERATION_TTY_CANCEL,
                         variant);
        return model_console_read_finish(selected, variant);
    case 23:
        record_operation(selected,
                         selected->reference.pending.state ==
                                 MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT
                             ? MODEL_OPERATION_TTY_COMPLETION
                             : MODEL_OPERATION_MALFORMED,
                         (uint32_t)random);
        return model_clear_debt_or_bad_version(selected);
    case 24:
        record_operation(selected, MODEL_OPERATION_TTY_SUBMIT, (uint32_t)random);
        return model_console_write_submit(selected, false);
    case 25:
        record_operation(selected, MODEL_OPERATION_TTY_COMPLETION, (uint32_t)random);
        return model_console_write_completion(selected);
    case 26:
        record_operation(selected, MODEL_OPERATION_TTY_SUBMIT, (uint32_t)random);
        return model_console_write_submit(selected, true);
    case 27:
        record_operation(selected, MODEL_OPERATION_TTY_WRITABLE, (uint32_t)random);
        return model_console_write_writable(selected);
    case 28:
        record_operation(selected, MODEL_OPERATION_TTY_COMPLETION, (uint32_t)random);
        return model_console_write_completion(selected);
    case 29:
        record_operation(selected, MODEL_OPERATION_DRAIN, (uint32_t)random);
        return model_test_drain_begin(selected);
    }
    return model_failure(selected, "model phase is defined", __LINE__);
}

static bool execute_pressure_phase(struct vfs_model *selected, size_t phase, uint64_t random,
                                   size_t cycle) {
    switch (phase) {
    case 30:
        record_operation(selected, MODEL_OPERATION_DESCRIPTOR_PRESSURE, (uint32_t)random);
        if (!model_test_drain_finish(selected) || !model_pressure_and_failures(selected, cycle)) {
            return false;
        }
        return true;
    }
    return model_failure(selected, "model phase is defined", __LINE__);
}

static bool execute_cycle_finish_phase(struct vfs_model *selected, size_t phase, uint64_t random) {
    switch (phase) {
    case 31:
        record_operation(selected, MODEL_OPERATION_DETACH, (uint32_t)random);
        return model_detach_and_finish_cycle(selected);
    }
    return model_failure(selected, "model phase is defined", __LINE__);
}

bool vfs_model_execute_step(struct vfs_model *selected) {
    size_t phase = selected->step % MODEL_CYCLE_LENGTH;
    size_t cycle = selected->step / MODEL_CYCLE_LENGTH;
    uint64_t random = model_random(selected);
    uint32_t variant = (uint32_t)(random % 3);

    if (phase < 2) {
        return execute_process_setup_phase(selected, phase, random);
    }
    if (phase < 5) {
        return execute_namespace_phase(selected, phase, random);
    }
    if (phase < 13) {
        return execute_regular_io_phase(selected, phase, random);
    }
    if (phase < 21) {
        return execute_directory_phase(selected, phase, random);
    }
    if (phase < 30) {
        return execute_console_phase(selected, phase, random, variant);
    }
    if (phase == 30) {
        return execute_pressure_phase(selected, phase, random, cycle);
    }
    return execute_cycle_finish_phase(selected, phase, random);
}

bool vfs_model_coverage_is_complete(struct vfs_model *selected) {
    const struct model_coverage *coverage = &selected->coverage;

    MODEL_EXPECT(selected,
                 coverage->attach != 0 && coverage->detach != 0 && coverage->open != 0 &&
                     coverage->create != 0 && coverage->close != 0 && coverage->read != 0 &&
                     coverage->write != 0 && coverage->getdents != 0 && coverage->mkdir != 0 &&
                     coverage->chdir != 0 && coverage->shared != 0 &&
                     coverage->descriptor_pressure != 0 && coverage->vnode_pressure != 0 &&
                     coverage->process_pressure != 0 && coverage->grant_failure != 0 &&
                     coverage->backend_capacity != 0 && coverage->tty_submit != 0 &&
                     coverage->tty_completion != 0 && coverage->tty_writable != 0 &&
                     coverage->tty_cancel != 0 && coverage->tty_collect != 0 &&
                     coverage->malformed != 0 && coverage->foreign != 0 && coverage->fatal != 0 &&
                     coverage->threshold_cleanup != 0);
    return true;
}
