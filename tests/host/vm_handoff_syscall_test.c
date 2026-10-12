#include "vm_handoff_syscall_test_internal.h"

int main(void)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    return (
        vm_handoff_syscall_test_load_begin_dispatches(fixture)
        && vm_handoff_syscall_test_load_begin_failures(fixture)
        && vm_handoff_syscall_test_phase_and_authority(fixture)
        && vm_handoff_syscall_test_freeze_failure(fixture)
        && vm_handoff_syscall_test_invariants_are_fatal(fixture)
        && vm_handoff_syscall_test_lifecycle_commit_and_exit(fixture)
        && vm_handoff_syscall_test_abort_loading_scratch(fixture)
        && vm_handoff_syscall_test_abort_loading_clear(fixture)
        && vm_handoff_syscall_test_abort_frozen(fixture)
        && vm_handoff_syscall_test_successful_mapping(fixture)
        && vm_handoff_syscall_test_unauthorized_mapping(fixture)
        && vm_handoff_syscall_test_wrong_phase(fixture)
        && vm_handoff_syscall_test_notification_atomicity(fixture)
        && vm_handoff_syscall_test_uncleared_console_event(fixture, 0)
        && vm_handoff_syscall_test_uncleared_console_event(fixture, 1)
        && vm_handoff_syscall_test_uncleared_console_event(fixture, 2)
        && vm_handoff_syscall_test_uncleared_console_event(fixture, 3)
    ) ? 0 : 1;
}
