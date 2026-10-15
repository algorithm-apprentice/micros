#include "kernel/elf_spawn_test_internal.h"

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "kernel/plic.h"
#include "kernel/pm_control_runtime.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

bool elf_spawn_test_read_process(
    struct micros_process_handle process, uint64_t address,
    void *destination, size_t size, uint32_t required
)
{
    uint8_t *output = destination;
    size_t offset = 0;

    if (
        destination == NULL || address < MICROS_USER_VIRTUAL_BASE
        || address >= MICROS_USER_VIRTUAL_END
        || size > MICROS_USER_VIRTUAL_END - address
    ) {
        return false;
    }
    while (offset < size) {
        uint64_t physical;
        uint32_t permissions;
        size_t contiguous;
        size_t chunk;

        if (micros_user_address_space_translate(
            process, address + offset, &physical, &permissions, &contiguous
        ) != MICROS_USER_ADDRESS_SPACE_OK || contiguous == 0
            || (permissions & required) != required) {
            return false;
        }
        chunk = size - offset < contiguous ? size - offset : contiguous;
        micros_elf_spawn_resource_copy(output + offset, (const void *)(uintptr_t)physical, chunk);
        offset += chunk;
    }
    return true;
}

bool elf_spawn_test_current_process(
    struct micros_process_handle *process, micros_endpoint_t *endpoint
)
{
    const struct micros_kernel_objects *objects = micros_kernel_object_runtime_registry();
    const struct micros_thread *thread;
    const struct micros_process *resolved;
    struct micros_thread_handle current;

    if (
        objects == NULL
        || micros_hart_current_thread(
            objects, micros_kernel_object_runtime_boot_hart_handle(), &current
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_resolve(objects, current, &thread) != MICROS_KERNEL_OBJECT_OK
        || micros_process_resolve(objects, thread->owner, &resolved) != MICROS_KERNEL_OBJECT_OK
        || resolved->privilege_profile != MICROS_PRIVILEGE_PROFILE_APPLICATION
    ) {
        return false;
    }
    *process = thread->owner;
    *endpoint = resolved->primary_endpoint;
    return true;
}

static bool elf_spawn_test_live_roots_valid(const struct micros_kernel_objects *objects)
{
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_process *process = &objects->processes[index];
        struct micros_process_handle handle = {(uint16_t)index, process->generation};

        if (process->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && micros_user_address_space_validate(handle) != MICROS_USER_ADDRESS_SPACE_OK) {
            return false;
        }
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];
        struct micros_ipc_message ignored;

        if (thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->ipc_receive_buffer != 0
            && !elf_spawn_test_read_process(
                thread->owner, thread->ipc_receive_buffer, &ignored, sizeof(ignored),
                MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE
            )) {
            return false;
        }
    }
    return true;
}

bool elf_spawn_test_runtime_valid(void)
{
    const struct micros_bootstrap_control_state *bootstrap = micros_bootstrap_runtime_state();
    const struct micros_kernel_objects *objects = micros_kernel_object_runtime_registry();
    const struct micros_endpoint_registry *endpoints = micros_ipc_runtime_registry();
    const struct micros_tty_handoff_runtime_state *tty = micros_tty_handoff_runtime_state();
    size_t index;

    if (
        bootstrap == NULL || objects == NULL || endpoints == NULL || tty == NULL
        || bootstrap->phase != MICROS_BOOTSTRAP_PHASE_SEALED
        || bootstrap->entry_count != MICROS_ELF_SPAWN_SERVICE_COUNT
        || micros_bootstrap_runtime_validate() != MICROS_BOOTSTRAP_OK
        || micros_kernel_object_runtime_validate() != MICROS_KERNEL_OBJECT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_frame_ownership_runtime_validate(objects) != MICROS_FRAME_OWNERSHIP_OK
        || micros_vm_handoff_runtime_validate(bootstrap, endpoints, objects) != MICROS_VM_HANDOFF_OK
        || micros_pm_control_runtime_validate(bootstrap, endpoints, objects) != MICROS_PM_CONTROL_OK
        || micros_tty_handoff_runtime_validate(bootstrap, endpoints, objects) != MICROS_TTY_HANDOFF_OK
        || tty->handoff.console_phase != MICROS_TTY_CONSOLE_OWNED
        || tty->handoff.route_phase != MICROS_TTY_ROUTE_IDLE
        || tty->handoff.claimed_source != 0 || tty->handoff.deadline_armed
        || !uart_console_handoff_is_active() || !riscv_external_interrupt_is_enabled()
        || !micros_plic_validate(MICROS_PLIC_ENABLED)
        || !elf_spawn_test_live_roots_valid(objects)
    ) {
        return false;
    }
    for (index = 0; index < bootstrap->entry_count; ++index) {
        if (bootstrap->transitions.entries[index].state != MICROS_BOOTSTRAP_SERVICE_READY) {
            return false;
        }
    }
    return true;
}
