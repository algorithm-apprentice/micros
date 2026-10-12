#include "kernel/address_space_handoff_test_internal.h"

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"

bool micros_address_space_handoff_runtime_run_self_test(void)
{
    struct micros_address_space_handoff_test_context *fixture =
        micros_address_space_handoff_test_context();
    bool passed;

    fixture->saved_status = riscv_irq_save();
    handoff_test_fill_bytes(
        fixture,
        &fixture->message,
        0,
        sizeof(fixture->message)
    );
    handoff_test_fill_bytes(
        fixture,
        &fixture->tty_bootstrap,
        0,
        sizeof(fixture->tty_bootstrap)
    );
    passed = (
        handoff_test_setup_runtime(fixture)
        && handoff_test_setup_processes(fixture)
        && handoff_test_setup_execution_and_grants(fixture)
        && handoff_test_verify_pre_handoff_access(fixture)
        && handoff_test_prepare_and_commit_handoff(fixture)
        && handoff_test_verify_post_handoff_runtime(fixture)
        && handoff_test_verify_post_handoff_failures(fixture)
        && handoff_test_verify_corruption_rejection(fixture)
        && handoff_test_verify_mutation_revocation(fixture)
        && handoff_test_run_real_syscalls(fixture)
        && handoff_test_verify_tty_mapping(fixture)
    );
    if (!passed) {
        uart_write("MICROS_TEST_FAILURE handoff-stage=");
        uart_write_hex64(fixture->failure_stage);
        uart_write("\n");
        uart_flush();
    }
    fixture->test_state = HANDOFF_TEST_IDLE;
    riscv_irq_restore(fixture->saved_status);
    return passed;
}
