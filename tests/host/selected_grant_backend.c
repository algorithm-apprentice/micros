#include "tests/host/selected_grant_validation_internal.h"
#include "tests/host/selected_grant_irq.h"

#include <stdlib.h>

#include "kernel/memory/user_address_space_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"

uintptr_t riscv_irq_save(void)
{
    struct grant01_fixture *fixture = grant01_fixture();
    uintptr_t status = fixture->irq_status;

    fixture->irq_status &= ~(uintptr_t)MICROS_RISCV_SSTATUS_SIE;
    return status;
}

void riscv_irq_restore(uintptr_t status)
{
    grant01_fixture()->irq_status = status;
}

bool riscv_irq_is_enabled(void)
{
    return (grant01_fixture()->irq_status & MICROS_RISCV_SSTATUS_SIE) != 0;
}

const struct micros_kernel_objects *micros_kernel_object_runtime_registry(void)
{
    return &grant01_fixture()->objects;
}

enum micros_kernel_object_error micros_kernel_object_runtime_validate(void)
{
    return micros_kernel_objects_validate(&grant01_fixture()->objects);
}

const struct micros_frame_ownership *micros_frame_ownership_runtime_ledger(void)
{
    struct grant01_fixture *fixture = grant01_fixture();

    return fixture->ledger_available ? &fixture->ledger : NULL;
}

enum micros_user_address_space_error micros_user_address_space_translate(
    struct micros_process_handle process, uint64_t address, uint64_t *physical_address,
    uint32_t *permissions, size_t *contiguous_bytes)
{
    struct grant01_fixture *fixture = grant01_fixture();
    size_t offset, page;

    if (riscv_irq_is_enabled() || process.slot >= GRANT01_ACTORS
        || process.generation != fixture->processes[process.slot].generation
        || physical_address == NULL || permissions == NULL || contiguous_bytes == NULL) {
        abort();
    }
    if (++fixture->translations[process.slot] > 2) {
        abort();
    }
    if (address < fixture->memory_base[process.slot]
        || address - fixture->memory_base[process.slot] >= GRANT01_MEMORY_SIZE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    offset = address - fixture->memory_base[process.slot];
    page = offset / MICROS_SV39_PAGE_SIZE;
    if (fixture->translation_error[process.slot][page] != MICROS_USER_ADDRESS_SPACE_OK) {
        return fixture->translation_error[process.slot][page];
    }
    *physical_address = (uintptr_t)&fixture->memory[process.slot][offset];
    *permissions = fixture->permissions[process.slot][page];
    *contiguous_bytes = MICROS_SV39_PAGE_SIZE - offset % MICROS_SV39_PAGE_SIZE;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error micros_user_address_space_validate_tty_uart_mapping(
    struct micros_process_handle process, uint64_t root)
{
    struct grant01_fixture *fixture = grant01_fixture();
    const struct micros_tty_handoff_runtime_state *state = &tty_handoff_context()->tty_handoff_state;

    if (riscv_irq_is_enabled() || process.slot != state->process.slot
        || process.generation != state->process.generation || root != state->root_physical_address) {
        abort();
    }
    ++fixture->device_checks;
    return fixture->device_error;
}

_Noreturn void micros_trap_panic_entry(
    uintptr_t hart_id, const char *reason, const char *file, uint32_t line,
    const struct micros_trap_frame *frame)
{
    struct grant01_fixture *fixture = grant01_fixture();

    if (!fixture->panic_armed || hart_id != 0 || reason == NULL
        || file == NULL || line == 0 || frame != &fixture->frame) {
        abort();
    }
    fixture->panic_reason = reason;
    longjmp(fixture->panic_jump, 1);
}
