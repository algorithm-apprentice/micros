#include "tests/qemu/vm_server_internal.h"

bool micros_vm01_work_active(void)
{
    return vm_server_context_get()->work.active == 1;
}

bool micros_vm01_service_begin(uint32_t command)
{
    struct micros_vm01_work_capture *capture = &vm_server_context_get()->work;

    if (command < MICROS_VM_CONTROL_LOAD || command > MICROS_VM_CONTROL_EXIT_RELEASE
        || capture->active || capture->complete || capture->invalid) {
        return false;
    }
    if (!capture->started) {
        capture->started = true;
        if (micros_test_work_begin(
            MICROS_VM01_SERVICE_PRODUCER, 1, MICROS_VM01_SERVICE_OWNER_MASK
        ) != MICROS_TEST_WORK_OK) {
            capture->invalid = true;
            return false;
        }
    }
    if (capture->attempts == UINT64_MAX || capture->commands[command - 1] == UINT64_MAX) {
        capture->invalid = true;
        return false;
    }
    ++capture->attempts;
    ++capture->commands[command - 1];
    capture->active = true;
    return true;
}

bool micros_vm01_service_finish(uint32_t command)
{
    struct micros_vm01_work_capture *capture = &vm_server_context_get()->work;

    capture->active = false;
    if (command == MICROS_VM_CONTROL_EXIT_RELEASE) {
        if (micros_test_work_finish(&capture->work) != MICROS_TEST_WORK_OK) {
            capture->invalid = true;
            return false;
        }
        capture->complete = 1;
    }
    return !capture->invalid;
}
