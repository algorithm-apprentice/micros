#include <stdbool.h>
#include <stdint.h>

#include "micros/application_runtime.h"
#include "micros/spawn.h"
#include "tests/qemu/pm_service_protocol.h"

static volatile uint64_t initialized_data = UINT64_C(0x41505052554e544d);
static volatile uint64_t zero_data;

static bool text_is_equal(const char *actual, const char *expected)
{
    size_t index;

    if (actual == NULL) {
        return false;
    }
    for (index = 0; index < MICROS_SPAWN_STRING_SIZE_MAX; ++index) {
        if (actual[index] != expected[index]) {
            return false;
        }
        if (expected[index] == '\0') {
            return true;
        }
    }
    return false;
}

int micros_application_main(int argc, char *argv[], char *envp[])
{
    const micros_endpoint_t pm_endpoint =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS)
        | (MICROS_PM_TEST_PM_SERVICE_ID - 1);
    const micros_endpoint_t metadata_peer =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS)
        | (MICROS_PM_TEST_PROBE_SERVICE_ID - 1);
    uint64_t gp;
    uint64_t tp;

    __asm__ volatile("mv %0, gp\nmv %1, tp" : "=r"(gp), "=r"(tp));
    if (
        argc != 2 || argv == NULL || envp == NULL
        || !text_is_equal(argv[0], "probe")
        || !text_is_equal(argv[1], "child")
        || argv[2] != NULL
        || !text_is_equal(envp[0], "MODE=runtime")
        || envp[1] != NULL
        || micros_application_pm_endpoint != pm_endpoint
        || micros_application_vfs_endpoint != metadata_peer
        || micros_application_pid != MICROS_PM_TEST_APPLICATION_PID
        || micros_application_page_size != MICROS_APPLICATION_STACK_PAGE_SIZE
        || gp != 0 || tp != 0
        || initialized_data != UINT64_C(0x41505052554e544d)
        || zero_data != 0
    ) {
        return -MICROS_PM_TEST_APPLICATION_EXIT_CODE;
    }
    return MICROS_PM_TEST_APPLICATION_EXIT_CODE;
}
