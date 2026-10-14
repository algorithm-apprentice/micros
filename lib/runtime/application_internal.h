#ifndef MICROS_LIB_RUNTIME_APPLICATION_INTERNAL_H
#define MICROS_LIB_RUNTIME_APPLICATION_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "micros/application_runtime.h"
#include "micros/spawn.h"

enum micros_application_startup_error {
    MICROS_APPLICATION_STARTUP_OK = 0,
    MICROS_APPLICATION_STARTUP_ERROR_ARGUMENT,
    MICROS_APPLICATION_STARTUP_ERROR_MALFORMED,
};

struct micros_application_startup {
    uint64_t arguments_address;
    uint64_t environment_address;
    uint64_t pid;
    uint64_t page_size;
    micros_endpoint_t pm_endpoint;
    micros_endpoint_t vfs_endpoint;
    uint32_t argument_count;
    uint32_t environment_count;
};

enum micros_application_startup_error micros_application_startup_decode(
    const uint8_t *page,
    size_t page_size,
    uint64_t stack_pointer,
    struct micros_application_startup *startup
);

_Noreturn void micros_application_start(uint64_t stack_pointer);
_Noreturn void micros_application_start_from_page(
    const uint8_t *page,
    size_t page_size,
    uint64_t stack_pointer
);
_Noreturn void micros_application_startup_failed(int64_t error);
_Noreturn void micros_application_exit_returned(int64_t result);

#endif
