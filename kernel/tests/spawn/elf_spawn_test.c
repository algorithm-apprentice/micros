#include "kernel/tests/spawn/elf_spawn_test_internal.h"

#include "micros/trap.h"

_Noreturn void micros_elf_spawn_test_launch(void)
{
    micros_bootstrap_runtime_launch(&micros_elf_spawn_test_config);
}

bool micros_elf_spawn_test_handle_trap(
    struct micros_hart *hart, struct micros_trap_frame *frame
)
{
    if (
        hart == NULL || frame == NULL || frame->scause != 3
        || (
            frame->sepc != micros_elf_spawn_report_addresses[0]
            && frame->sepc != micros_elf_spawn_report_addresses[1]
        )
    ) {
        return false;
    }
    return elf_spawn_test_process_report(hart, frame);
}
