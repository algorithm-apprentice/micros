#include "tests/qemu/ramfs_service_vfs_internal.h"

static _Noreturn void vfs_report_and_stop(uint64_t magic)
{
    micros_ramfs_service_test_report(magic);
    for (;;) {
    }
}

void micros_service_main(void)
{
    struct vfs_service_fixture *fixture = vfs_service_context();

    if (
        vfs_initialized_data_value() != UINT64_C(0x52414d4653564653)
        || !vfs_validate_configuration(fixture)
    ) {
        vfs_report_and_stop(
            MICROS_RAMFS_TEST_FAILURE_CONFIGURATION
        );
    }
    micros_ramfs_service_test_report(
        MICROS_RAMFS_TEST_REPORT_PRE_READY
    );
    if (!vfs_run_filesystem_scenario(fixture)) {
        vfs_report_and_stop(
            MICROS_RAMFS_TEST_FAILURE_FILESYSTEM
        );
    }
    if (!vfs_send_ready()) {
        vfs_report_and_stop(
            MICROS_RAMFS_TEST_FAILURE_READY
        );
    }
    if (!vfs_write_pass_marker(fixture)) {
        vfs_report_and_stop(
            MICROS_RAMFS_TEST_FAILURE_TTY
        );
    }
    vfs_report_and_stop(
        MICROS_RAMFS_TEST_REPORT_MAGIC
    );
}
