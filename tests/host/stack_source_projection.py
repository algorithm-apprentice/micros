"""Finite observer transport; callers retain their independent source oracle."""

import re


HEADER = '#include "micros/stack_observation.h"\n'
HOOKS = {
    "kernel/objects/kernel_object_context.c": (
        ("micros_thread_attach_execution_context",
         "    kernel_object_copy_user_context(&thread->user_context, context);",
         "MICROS_TEST_STACK_ATTACHED(objects, thread_handle);", "after"),
        ("micros_thread_detach_execution_context",
         "    thread->context_attached = false;",
         "MICROS_TEST_STACK_DETACHING(objects, thread_handle);", "before"),
    ),
    "kernel/pm/pm_control_execution.c": (
        ("micros_pm_control_prepare_commit_prevalidated",
         "    state->prepared.endpoint = plan->endpoint;",
         "MICROS_TEST_STACK_ATTACHED(objects, plan->thread);", "after"),
        ("micros_pm_control_abort_prepared_commit_prevalidated",
         "    uint32_t generation = thread->generation;",
         "MICROS_TEST_STACK_DETACHING(objects, plan->thread);", "after"),
    ),
    "kernel/pm/pm_control_exit.c": (
        ("micros_pm_control_exit_detach_commit_prevalidated",
         "    uint32_t generation = thread->generation;",
         "MICROS_TEST_STACK_DETACHING(objects, plan->thread);", "after"),
    ),
    "kernel/tests/scheduler/scheduler_test_finish.c": (
        ("report_scheduler_success", "    uart_flush();",
         "MICROS_TEST_STACK_REPORT();", "after"),
    ),
    "kernel/tests/ipc/ipc_syscall_test_report.c": (
        ("micros_ipc_syscall_test_finish",
         '    uart_write(\n'
         '        "MICROS_IPC_SYSCALL_TEST_PASS "\n'
         '        "spaces=three operations=six blocking=validated "\n'
         '        "calls=tokenized notifications=coalesced "\n'
         '        "errors=stable completion=deferred "\n'
         '        "registers=preserved cleanup=complete\\n"\n'
         '    );\n'
         '    uart_flush();',
         "MICROS_TEST_STACK_REPORT();", "after"),
    ),
    "kernel/tests/grant/grant_syscall_test.c": (
        ("micros_grant_syscall_test_finish",
         '    uart_write(\n'
         '        "MICROS_GRANT_SYSCALL_TEST_PASS "\n'
         '        "namespace=unified phase=bootstrap lifecycle=checked "\n'
         '        "directions=checked errors=stable "\n'
         '        "registers=preserved cleanup=complete\\n"\n'
         '    );\n'
         '    uart_flush();',
         "MICROS_TEST_STACK_REPORT();", "after"),
    ),
    "kernel/tests/boot/bootstrap_test.c": (
        ("micros_bootstrap_test_handle_trap",
         '    uart_write(\n'
         '        "MICROS_BOOTSTRAP_TEST_PASS "\n'
         '        "manifest=immutable order=topological profiles=exact "\n'
         '        "endpoints=staged readiness=acknowledged authority=revoked\\n"\n'
         '    );\n'
         '    uart_flush();',
         "MICROS_TEST_STACK_REPORT();", "after"),
    ),
    "kernel/tests/vm/vm_scratch_preemption_test_report.c": (
        ("vm_scratch_test_report_success",
         '    (void)sbi_system_reset(SBI_RESET_TYPE_SHUTDOWN, SBI_RESET_REASON_NONE);',
         "MICROS_TEST_STACK_REPORT();", "before"),
    ),
    "kernel/tests/pm/pm_service_test_report.c": (
        ("report_success_and_shutdown",
         '    uart_write(\n'
         '        "MICROS_APPLICATION_RUNTIME_TEST_PASS image=checked "\n'
         '        "argc=2 env=1 auxv=validated gp-tp=zero "\n'
         '        "return=pm-exit code=37 cleanup=complete\\n"\n'
         '    );\n'
         '    uart_flush();',
         "MICROS_TEST_STACK_REPORT();", "after"),
    ),
    "kernel/tests/tty/tty_service_test.c": (
        ("micros_tty_service_test_handle_trap",
         '    (void)sbi_system_reset(\n'
         '        SBI_RESET_TYPE_SHUTDOWN,\n'
         '        SBI_RESET_REASON_NONE\n'
         '    );',
         "MICROS_TEST_STACK_REPORT();", "before"),
    ),
    "kernel/tests/ramfs/ramfs_service_test_report.c": (
        ("micros_ramfs_service_test_handle_trap",
         '    (void)sbi_system_reset(\n'
         '        SBI_RESET_TYPE_SHUTDOWN,\n'
         '        SBI_RESET_REASON_NONE\n'
         '    );',
         "MICROS_TEST_STACK_REPORT();", "before"),
    ),
    "kernel/tests/vfs/vfs_service_test.c": (
        ("vfs_test_shutdown",
         '    (void)sbi_system_reset(\n'
         '        SBI_RESET_TYPE_SHUTDOWN,\n'
         '        SBI_RESET_REASON_NONE\n'
         '    );',
         "MICROS_TEST_STACK_REPORT();", "before"),
    ),
    "kernel/tests/spawn/elf_spawn_test_report.c": (
        ("elf_spawn_test_process_report",
         '            (void)sbi_system_reset(SBI_RESET_TYPE_SHUTDOWN, SBI_RESET_REASON_NONE);',
         "MICROS_TEST_STACK_REPORT();", "before"),
    ),
}


def lexical(text):
    def blank(match):
        return "".join("\n" if character == "\n" else " " for character in match.group())
    return re.sub(
        r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', blank, text,
    )


def function_range(source, name):
    masked = lexical(source)
    found = tuple(re.finditer(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", masked))
    if len(found) != 1:
        raise AssertionError("observer transport has no unique complete enclosing function")
    begin = found[0].end()
    depth = 1
    for point in range(begin, len(masked)):
        if masked[point] == "{":
            depth += 1
        elif masked[point] == "}":
            depth -= 1
            if not depth:
                return begin, point
    raise AssertionError("observer transport has an unbalanced enclosing function")


def added_statement(original, hook, position):
    if position == "after":
        return original + " " + hook
    indentation = original[:len(original) - len(original.lstrip())]
    return indentation + hook + " " + original.lstrip()


def replace_in_function(source, function, selected, replacement):
    begin, end = function_range(source, function)
    body = source[begin:end]
    if body.count(selected) != 1:
        raise AssertionError("observer hook moved, duplicated or lost its exact primitive")
    point = begin + body.index(selected)
    masked = lexical(source)
    hook_at = selected.find("MICROS_TEST_STACK_")
    check = point + hook_at if hook_at >= 0 else point + len(selected) - len(selected.lstrip())
    if masked[check].isspace():
        raise AssertionError("observer hook/primitive is hidden in a comment or literal")
    return source[:point] + replacement + source[point + len(selected):]


def instrument_source(unit, source):
    if unit not in HOOKS or "MICROS_TEST_STACK_" in lexical(source) or HEADER in source:
        raise AssertionError("unknown or already instrumented observer source")
    lines = source.splitlines(keepends=True)
    if len(lines) < 2 or lines[1] != "\n":
        raise AssertionError("observer header has no exact existing blank-line slot")
    lines[1] = HEADER
    source = "".join(lines)
    for function, primitive, hook, placement in HOOKS[unit]:
        source = replace_in_function(
            source, function, primitive, added_statement(primitive, hook, placement),
        )
    return source


def project_stack_source(unit, source):
    if HEADER not in source and "MICROS_TEST_STACK_" not in lexical(source):
        return source
    if unit not in HOOKS:
        raise AssertionError("observer source is outside the finite transport declaration")
    lines = source.splitlines(keepends=True)
    if len(lines) < 2 or lines[1] != HEADER or source.count(HEADER) != 1:
        raise AssertionError("observer header moved or has a borrowed source owner")
    lines[1] = "\n"
    source = "".join(lines)
    for function, primitive, hook, placement in HOOKS[unit]:
        source = replace_in_function(
            source, function, added_statement(primitive, hook, placement), primitive,
        )
    if "MICROS_TEST_STACK_" in lexical(source):
        raise AssertionError("unbound observer source call or namespace mutation")
    return source
