#include "lib/runtime/application_internal.h"

#include "micros/pm.h"
#include "micros/runtime.h"

_Static_assert(sizeof(int) == sizeof(int32_t), "application main returns a 32-bit code");

micros_endpoint_t micros_application_pm_endpoint;
micros_endpoint_t micros_application_vfs_endpoint;
uint64_t micros_application_pid;
/* Preserve writable file-backed data even when the application defines none. */
uint64_t micros_application_page_size
#if defined(__ELF__)
    __attribute__((section(".data.application_runtime")))
#endif
    = 0;

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    size_t index;

    for (index = 0; index < sizeof(value); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

_Noreturn void micros_application_start_from_page(
    const uint8_t *page,
    size_t page_size,
    uint64_t stack_pointer
)
{
    struct micros_application_startup startup;
    enum micros_application_startup_error error;
    int code;

    error = micros_application_startup_decode(
        page, page_size, stack_pointer, &startup
    );
    if (error != MICROS_APPLICATION_STARTUP_OK) {
        micros_application_startup_failed(error);
    }
    micros_application_pm_endpoint = startup.pm_endpoint;
    micros_application_vfs_endpoint = startup.vfs_endpoint;
    micros_application_pid = startup.pid;
    micros_application_page_size = startup.page_size;
    code = micros_application_main(
        (int)startup.argument_count,
        (char **)(uintptr_t)startup.arguments_address,
        (char **)(uintptr_t)startup.environment_address
    );
    micros_application_exit((int32_t)code);
}

_Noreturn void micros_application_start(uint64_t stack_pointer)
{
    micros_application_start_from_page(
        (const uint8_t *)(uintptr_t)MICROS_APPLICATION_STACK_BASE,
        MICROS_APPLICATION_STACK_PAGE_SIZE,
        stack_pointer
    );
}

_Noreturn void micros_application_exit(int32_t code)
{
    struct micros_ipc_message message = {0};
    micros_runtime_result_t result;

    message.type = MICROS_PM_MESSAGE_EXIT;
    write_u32_le(&message.payload[0], MICROS_PM_PROTOCOL_VERSION);
    write_u32_le(&message.payload[8], (uint32_t)code);
    result = micros_runtime_call(micros_application_pm_endpoint, &message);
    micros_application_exit_returned(result);
}
