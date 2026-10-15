#include "servers/pm/pm_service_internal.h"

#include "tests/qemu/elf_spawn_request.h"

void pm_service_start_elf_spawn_init(struct pm_service_context *context)
{
    uint8_t request[MICROS_ELF_SPAWN_REQUEST_BUFFER_SIZE];
    struct micros_pm_coordinator_child child;
    size_t size;

    if (
        context->coordinator_mode != PM_SERVICE_COORDINATOR_READY
        || !context->process_table.runtime_enabled
        || context->process_table.init_attempted
        || !micros_elf_spawn_build_request(
            request, sizeof(request), MICROS_ELF_SPAWN_REQUEST_INIT, &size
        )
        || micros_pm_coordinator_spawn_init(
            &context->coordinator, pm_service_backend_get(), NULL,
            request, size, &child
        ) != MICROS_PM_COORDINATOR_OK
        || child.pid != MICROS_ELF_SPAWN_INIT_PID
    ) {
        __builtin_trap();
    }
}
