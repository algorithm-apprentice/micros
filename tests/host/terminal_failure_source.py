"""Finite current observation, callback and fatal source/machine contracts."""

from functools import lru_cache
from pathlib import Path
import subprocess
from tests.host.panic_trace_source import effective_trace_observations, project_trace_runtime
from tests.host.evidence_records import regular_source


RECIPE_ADMISSION = {
    "reference_cmake_sha256": "98f213a7c251fb57f50e3de534b86f732bcacf1913bae67cf367ffaff900a2cb",
    "native_target": "panic_control_test",
    "native_case": "panic-control",
    "native_sources": (
        "tests/host/panic_control_test.c",
        "kernel/diagnostic/panic_control_core.c",
    ),
    "native_tiers": ("test-unit-fast", "test-unit"),
    "kernel_sources": (
        "kernel/diagnostic/panic_control_core.c",
        "kernel/diagnostic/panic_control.c",
    ),
    "target_configure": "riscv64-terminal-failure-test",
    "target_workflow": "test-qemu-terminal-failure",
    "target_definition": "MICROS_BUILD_TERMINAL_FAILURE_TEST",
    "target_work_definition": "MICROS_TEST_WORK_COUNTS=1",
    "target_sources": ("tests/qemu/terminal_failure_kernel.c", "tests/work/work_counts.c"),
    "kernel_image": "kernel/micros.elf",
    "controller": "tools/check_terminal_failures.py",
    "case_timeout": 8,
    "selector": ("micros_terminal_test_scenario", 4),
    "observation": {
        "symbol": "micros_terminal_test_observation",
        "version": 1,
        "size": 104,
        "alignment": 8,
        "fields": (
            ("version", 0, 4), ("scenario", 4, 4), ("phase", 8, 4), ("reserved0", 12, 4),
            ("thre_reads", 16, 8), ("temt_reads", 24, 8), ("thr_writes", 32, 8),
            ("seizure_entries", 40, 8), ("report_bodies", 48, 8), ("reset_entries", 56, 8),
            ("first_entry_sp", 64, 8), ("sstatus", 72, 8), ("sie", 80, 8),
            ("work_status", 88, 4), ("reset_type", 92, 4), ("reset_reason", 96, 4),
            ("reserved1", 100, 4),
        ),
        "phases": (("SETUP", 0), ("ARMED", 1), ("RESET_REACHED", 2),
                   ("POLL_OVERRUN", 3), ("UNEXPECTED_RETURN", 4), ("FIXTURE_ERROR", 5)),
    },
    "work_snapshot": {
        "symbol": "micros_terminal_test_work_snapshot",
        "version": 1,
        "producer_prefix": 0x504C415430320000,
        "capture": 1,
        "owner_mask": 2,
        "sites": ("terminal-thre-poll", "terminal-temt-poll"),
    },
    "cases": (
        (1, "transmit-first", "positive"), (2, "transmit-last", "positive"),
        (3, "handoff-drain-last", "positive"), (4, "preseized-first-report", "positive"),
        (5, "early-thre-stuck", "overrun"), (6, "handoff-temt-stuck", "overrun"),
        (7, "panic-thre-stuck", "overrun"), (8, "panic-temt-stuck", "overrun"),
        (9, "recursive-panic", "repeated-report"),
        (10, "corrupt-magic", "corruption-spin"),
        (11, "corrupt-magic-thre-stuck", "corruption-spin"),
        (12, "fdt-failure-reset", "missing-reset-ownership"),
    ),
    "scope": (43, 112, 10, 49),
}


FATAL_EFFECT_CONTRACT = {
    "version": 3,
    "qualifiers": (
        ("UNQUALIFIED", 0), ("UART_PANIC_READY", 1),
        ("PANIC_LATCHED", 2), ("PANIC_LATCHED_UART_READY", 3),
    ),
    "control": {
        "source": "kernel/diagnostic/panic_control.c", "symbol": "panic_control",
        "size": 4, "phase_offset": 0,
        "phases": (("IDLE", 0), ("REPORTING", 1), ("RESET_REQUESTED", 2)),
    },
    "console": {
        "symbol": "uart_console_state", "size": 16,
        "magic": (0, 8, 0x554152544F574E52), "owner": (8, 4, 3), "quiesced": (12, 1, 1),
    },
    "poll_limit": 100000,
    "reset": {
        "source": "arch/riscv64/sbi.c", "function": "sbi_system_reset",
        "tuple": (0, 1, 0, 0x53525354),
        "claim": "micros_panic_claim_reset",
        "caller_preclaim": False,
    },
    "excluded": (
        ("arch/riscv64/uart.c", "uart_write_direct", "arch/riscv64/panic.S", "micros_panic_entry", 1),
        ("arch/riscv64/uart.c", "uart_write", "arch/riscv64/uart.c", "uart_write_record", 1),
        ("arch/riscv64/uart.c", "uart_write_hex64", "arch/riscv64/uart.c", "uart_write_hex64_record", 1),
        ("kernel/diagnostic/panic.c", "micros_panic_report", "kernel/diagnostic/panic.c", "panic_report_claimed", 2),
    ),
    "panic_context_bytes": 48,
    "stack_budgets": (("boot", 16384), ("supervisor", 16384), ("emergency", 4096)),
}


SOURCE_PROJECTION = {'source_reference': '8375ca2f5285bc42063af8ed92ba6f3811d517e8',
 'new_kernel_members': ('kernel/diagnostic/panic_control_core.h',
                        'kernel/diagnostic/panic_control_core.c',
                        'kernel/diagnostic/panic_control.h',
                        'kernel/diagnostic/panic_control.c')}


TEST_HEADER = '#include "tests/qemu/terminal_failure_kernel.h"\n'


WORK_HEADER = '#include "micros/work_observation.h"\n'


TEST_FLAG = "MICROS_BUILD_TERMINAL_FAILURE_TEST"


CORRUPT_CONSOLE = '''
#ifdef MICROS_BUILD_TERMINAL_FAILURE_TEST
void micros_terminal_test_corrupt_console(void)
{
    uart_console_state.initialization_magic ^= UINT64_C(1);
}
#endif
'''


def _exact_changes(source, changes, *, inverse=False):
    for original, added in reversed(changes) if inverse else changes:
        before, after = (added, original) if inverse else (original, added)
        if source.count(before) != 1:
            raise ValueError("PLAT02 source addition is missing, duplicated or outside its exact anchor")
        source = source.replace(before, after, 1)
    return source


def _hook(statement):
    return "#ifdef " + TEST_FLAG + "\n    " + statement + "\n#endif\n"


def _observation_changes(relative, source):
    from tests.host.c_source_records import function_records

    if relative == "kernel/boot/boot_component_tests.c":
        before = "#endif\n\nvoid boot_test_foundation("
        finish = '    MICROS_PANIC(boot->hart_id, "nested-trap-test-returned");\n#endif\n'
        return (
            (before, "#endif\n" + TEST_HEADER + "void boot_test_foundation("),
            (finish, finish + _hook("micros_terminal_test_run(boot->hart_id);")),
        )
    if relative == "arch/riscv64/uart.c":
        record = function_records(source)["uart_panic_seize"]
        declaration = record.signature + "{\n"
        if record.signature.lstrip().startswith("bool "):
            declaration += "    bool repaired = false;\n\n"
        changes = [
            ('#include "arch/riscv64/interrupt.h"\n',
             '#include "arch/riscv64/interrupt.h"\n' + TEST_HEADER + WORK_HEADER),
            (declaration, declaration + _hook("micros_terminal_test_seizure();")),
            ("    uart0[UART_THR] = (uint8_t)character;\n",
             "    uart0[UART_THR] = (uint8_t)character;\n" + _hook("micros_terminal_test_thr();")),
        ]
        for suffix, mask, drain in (
            ("thre", "UART_LSR_THR_EMPTY", "false"),
            ("temt", "UART_LSR_TRANSMITTER_EMPTY", "true"),
        ):
            before = f"    while ((uart0[UART_LSR] & {mask}) == 0) {{\n"
            after = (
                "    while (MICROS_TEST_WORK_COMPARE(\n"
                f'        "terminal-{suffix}-poll", MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC,\n'
                f"        (MICROS_TERMINAL_TEST_LSR(uart0[UART_LSR], {drain}) & {mask}) == 0\n"
                "    )) {\n"
            )
            changes.append((before, after))
        return tuple(changes)
    if relative == "kernel/diagnostic/panic.c":
        return (
            ('#include "arch/riscv64/platform.h"\n', '#include "arch/riscv64/platform.h"\n' + TEST_HEADER),
            (
                "    micros_panic_seize();\n",
                _hook("micros_terminal_test_report(context);")
                + "    micros_panic_seize();\n" + _hook("micros_terminal_test_after_seize(hart_id);"),
            ),
        )
    if relative == "arch/riscv64/sbi.c":
        binding = '    register uintptr_t argument0 __asm__("a0") = reset_type;\n'
        return (
            ('#include "arch/riscv64/platform.h"\n', '#include "arch/riscv64/platform.h"\n' + TEST_HEADER),
            (binding, _hook("micros_terminal_test_reset(reset_type, reset_reason);") + binding),
        )
    raise ValueError("PLAT02 has no test observation overlay for this source")


def observe_source(relative, source):
    if TEST_HEADER in source or TEST_FLAG in source or "micros_terminal_test_" in source:
        raise ValueError("PLAT02 cannot observe an already overlaid source")
    result = _exact_changes(source, _observation_changes(relative, source))
    return result + CORRUPT_CONSOLE if relative == "arch/riscv64/uart.c" else result


def project_observations(relative, source):
    source = project_trace_runtime(relative, source)
    if not any(marker in source for marker in (TEST_HEADER, TEST_FLAG, "micros_terminal_test_")):
        return source
    if relative == "arch/riscv64/uart.c":
        if source.count(CORRUPT_CONSOLE) != 1 or not source.endswith(CORRUPT_CONSOLE):
            raise ValueError("PLAT02 corruption fixture is not its exact same-owner test hook")
        source = source[:-len(CORRUPT_CONSOLE)]
    result = _exact_changes(source, _observation_changes(relative, source), inverse=True)
    if any(marker in result for marker in (TEST_HEADER, TEST_FLAG, "micros_terminal_test_", "MICROS_TERMINAL_TEST_")):
        raise ValueError("PLAT02 observation projection leaves an unbound test operation")
    return result


@lru_cache(maxsize=32)
def fixed_source(relative):
    if relative not in (*SOURCE_OVERLAY_PATHS, "arch/riscv64/platform.h"):
        raise ValueError("PLAT02 fixed-source lookup has no finite machine/observer contract")
    return subprocess.check_output(
        ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + relative),
        cwd=Path(__file__).resolve().parents[2], timeout=30,
    ).decode("utf-8")


CONTROL_SOURCES = {
    "kernel/diagnostic/panic_control_core.h": '''#ifndef MICROS_KERNEL_PANIC_CONTROL_CORE_H
#define MICROS_KERNEL_PANIC_CONTROL_CORE_H

#include <stdint.h>

enum micros_panic_phase {
    MICROS_PANIC_IDLE = 0,
    MICROS_PANIC_REPORTING = 1,
    MICROS_PANIC_RESET_REQUESTED = 2,
};

enum micros_panic_enter_result {
    MICROS_PANIC_FIRST = 0,
    MICROS_PANIC_REENTERED = 1,
    MICROS_PANIC_ENTRY_INVARIANT = 2,
};

enum micros_panic_reset_result {
    MICROS_PANIC_ISSUE = 0,
    MICROS_PANIC_ALREADY_ISSUED = 1,
    MICROS_PANIC_RESET_INVARIANT = 2,
};

struct micros_panic_control {
    uint32_t phase;
};

enum micros_panic_enter_result micros_panic_control_enter(struct micros_panic_control *control);
enum micros_panic_reset_result micros_panic_control_request_reset(struct micros_panic_control *control);

#endif
''',
    "kernel/diagnostic/panic_control_core.c": '''#include "kernel/diagnostic/panic_control_core.h"

#include <stddef.h>

enum micros_panic_enter_result micros_panic_control_enter(struct micros_panic_control *control)
{
    if (control == NULL) {
        return MICROS_PANIC_ENTRY_INVARIANT;
    }
    if (control->phase == MICROS_PANIC_IDLE) {
        control->phase = MICROS_PANIC_REPORTING;
        return MICROS_PANIC_FIRST;
    }
    if (control->phase == MICROS_PANIC_REPORTING || control->phase == MICROS_PANIC_RESET_REQUESTED) {
        return MICROS_PANIC_REENTERED;
    }
    return MICROS_PANIC_ENTRY_INVARIANT;
}

enum micros_panic_reset_result micros_panic_control_request_reset(struct micros_panic_control *control)
{
    if (control == NULL) {
        return MICROS_PANIC_RESET_INVARIANT;
    }
    if (control->phase == MICROS_PANIC_IDLE || control->phase == MICROS_PANIC_REPORTING) {
        control->phase = MICROS_PANIC_RESET_REQUESTED;
        return MICROS_PANIC_ISSUE;
    }
    if (control->phase == MICROS_PANIC_RESET_REQUESTED) {
        return MICROS_PANIC_ALREADY_ISSUED;
    }
    return MICROS_PANIC_RESET_INVARIANT;
}
''',
    "kernel/diagnostic/panic_control.h": '''#ifndef MICROS_KERNEL_PANIC_CONTROL_H
#define MICROS_KERNEL_PANIC_CONTROL_H

#include <stdbool.h>

bool micros_panic_claim_first(void);
bool micros_panic_claim_reset(void);
_Noreturn void micros_panic_terminate_no_console(void);
_Noreturn void micros_panic_halt(void);

#endif
''',
    "kernel/diagnostic/panic_control.c": '''#include "kernel/diagnostic/panic_control.h"

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "kernel/diagnostic/panic_control_core.h"

static struct micros_panic_control panic_control;

bool micros_panic_claim_first(void)
{
    enum micros_panic_enter_result result;

    (void)riscv_irq_save();
    riscv_external_interrupt_disable();
    result = micros_panic_control_enter(&panic_control);
    if (result == MICROS_PANIC_ENTRY_INVARIANT) {
        micros_panic_halt();
    }
    return result == MICROS_PANIC_FIRST;
}

bool micros_panic_claim_reset(void)
{
    enum micros_panic_reset_result result;

    (void)riscv_irq_save();
    riscv_external_interrupt_disable();
    result = micros_panic_control_request_reset(&panic_control);
    if (result == MICROS_PANIC_RESET_INVARIANT) {
        micros_panic_halt();
    }
    return result == MICROS_PANIC_ISSUE;
}

_Noreturn void micros_panic_terminate_no_console(void)
{
    (void)riscv_irq_save();
    riscv_external_interrupt_disable();
    (void)sbi_system_reset(SBI_RESET_TYPE_SHUTDOWN, SBI_RESET_REASON_SYSTEM_FAILURE);
    micros_panic_halt();
}

_Noreturn void micros_panic_halt(void)
{
    for (;;) {
        __asm__ volatile("wfi");
    }
}
''',
}


PANIC_ENTRY_WRAPPER = '''
_Noreturn void micros_panic_report(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line,
    const struct micros_trap_frame *trap_frame,
    const struct micros_panic_machine_context *context
)
{
    if (!micros_panic_claim_first()) {
        micros_panic_terminate_no_console();
    }
    panic_report_claimed(hart_id, reason, file, line, trap_frame, context);
}
'''


RESET_ADMISSION = '''    if (
        reset_type == SBI_RESET_TYPE_SHUTDOWN
        && reset_reason == SBI_RESET_REASON_SYSTEM_FAILURE
        && !micros_panic_claim_reset()
    ) {
        micros_panic_halt();
    }
'''


CONTROL_HEADER = '#include "kernel/diagnostic/panic_control.h"\n'


def _runtime_changes(relative):
    if relative == "arch/riscv64/platform.h":
        return (("void uart_panic_seize(void);", "bool uart_panic_seize(void);"),)
    include = '#include "arch/riscv64/platform.h"\n'
    if relative == "arch/riscv64/sbi.c":
        signature = "intptr_t sbi_system_reset(uint32_t reset_type, uint32_t reset_reason)\n{\n"
        return ((include, include + CONTROL_HEADER), (signature, signature + RESET_ADMISSION))
    if relative == "kernel/diagnostic/panic.c":
        return (
            (include, include + CONTROL_HEADER),
            ("_Noreturn void micros_panic_report(", "static _Noreturn void panic_report_claimed("),
            (
                "    uart_panic_seize();\n",
                '    if (uart_panic_seize()) {\n        uart_write("MICROS_CONSOLE_METADATA_REPAIRED\\n");\n    }\n',
            ),
        )
    if relative != "arch/riscv64/uart.c":
        raise ValueError("PLAT02 has no runtime source transformation for this owner")
    changes = [
        (include, include + CONTROL_HEADER),
        ("void uart_panic_seize(void)\n{\n", "bool uart_panic_seize(void)\n{\n    bool repaired = false;\n\n"),
        (
            "        for (;;) {\n        }\n",
            "        uart_console_state.initialization_magic = MICROS_UART_CONSOLE_INITIALIZATION_MAGIC;\n"
            "        uart_console_state.owner = MICROS_UART_CONSOLE_OWNER_PANIC;\n"
            "        uart_console_state.quiesced = true;\n"
            "        repaired = true;\n",
        ),
        (
            "    uart0[UART_LCR] = UART_LCR_8N1;\n    riscv_mmio_fence();\n}\n",
            "    uart0[UART_LCR] = UART_LCR_8N1;\n    riscv_mmio_fence();\n    return repaired;\n}\n",
        ),
    ]
    for mask in ("UART_LSR_THR_EMPTY", "UART_LSR_TRANSMITTER_EMPTY"):
        wait = f"    while ((uart0[UART_LSR] & {mask}) == 0) {{\n"
        changes.append((
            wait + "    }\n",
            "    uint32_t failed = 0;\n\n" + wait
            + "        if (++failed == UINT32_C(100000)) {\n"
            + "            micros_panic_terminate_no_console();\n        }\n    }\n",
        ))
    return tuple(changes)


def terminal_runtime_source(relative):
    if relative in CONTROL_SOURCES:
        return CONTROL_SOURCES[relative]
    return extend_terminal_runtime_source(relative, fixed_source(relative))


def extend_terminal_runtime_source(relative, source):
    source = _exact_changes(source, _runtime_changes(relative))
    return source + PANIC_ENTRY_WRAPPER if relative == "kernel/diagnostic/panic.c" else source


def project_terminal_runtime(relative, source):
    if relative in SOURCE_OVERLAY_PATHS:
        source = project_observations(relative, source)
    if relative == "arch/riscv64/platform.h":
        changed = "bool uart_panic_seize(void);" in source
    else:
        changed = CONTROL_HEADER in source
    if not changed:
        return source
    if relative == "kernel/diagnostic/panic.c":
        if source.count(PANIC_ENTRY_WRAPPER) != 1 or not source.endswith(PANIC_ENTRY_WRAPPER):
            raise ValueError("PLAT02 entry wrapper is not its exact first-claim boundary")
        source = source[:-len(PANIC_ENTRY_WRAPPER)]
    return _exact_changes(source, _runtime_changes(relative), inverse=True)


SOURCE_OVERLAY_PATHS = (
    "arch/riscv64/uart.c", "arch/riscv64/sbi.c",
    "kernel/diagnostic/panic.c", "kernel/boot/boot_component_tests.c",
)


def terminal_kernel_members(root):
    from tests.host.c_source_records import tokens

    errors = []
    for name in SOURCE_PROJECTION["new_kernel_members"]:
        try:
            path = regular_source(Path(root), name)
            if path.stat().st_mode & 0o777 != 0o644 or tokens(path.read_text()) != tokens(CONTROL_SOURCES[name]):
                raise ValueError("terminal source changed its ordinary mode, private dependencies or closed state contract")
        except (OSError, ValueError, AssertionError) as error:
            errors.append(f"{name}: {error}")
    return SOURCE_PROJECTION["new_kernel_members"], tuple(errors)


def effective_terminal_body(body, *, enabled, trace_enabled=False):
    if type(enabled) is not bool or type(trace_enabled) is not bool or enabled and trace_enabled:
        raise ValueError("PLAT02 observation mode is not explicit")
    body = effective_trace_observations(body, enabled=trace_enabled)
    statements = (
        "micros_terminal_test_seizure();", "micros_terminal_test_thr();",
        "micros_terminal_test_report(context);", "micros_terminal_test_after_seize(hart_id);",
        "micros_terminal_test_reset(reset_type, reset_reason);",
    )
    for statement in statements:
        marker = _hook(statement)
        if marker in body:
            if body.count(marker) != 1:
                raise ValueError("PLAT02 source repeats an observation site")
            body = body.replace(marker, "    " + statement + "\n" if enabled else "", 1)
    for suffix, mask, drain in (
        ("thre", "UART_LSR_THR_EMPTY", "false"), ("temt", "UART_LSR_TRANSMITTER_EMPTY", "true"),
    ):
        read = "micros_panic_trace_test_lsr(uart0[UART_LSR])" if trace_enabled and suffix == "thre" else "uart0[UART_LSR]"
        marker = (
            "MICROS_TEST_WORK_COMPARE(\n"
            f'        "terminal-{suffix}-poll", MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC,\n'
            f"        (MICROS_TERMINAL_TEST_LSR({read}, {drain}) & {mask}) == 0\n"
            "    )"
        )
        if marker not in body:
            continue
        if body.count(marker) != 1:
            raise ValueError("PLAT02 repeats a readiness-comparison observation")
        predicate = f"({read} & {mask}) == 0"
        if enabled:
            predicate = f"(micros_terminal_test_lsr({read}, {drain}) & {mask}) == 0"
        if enabled or trace_enabled:
            predicate = "(micros_test_work_record(MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC, MICROS_WORK_KIND_COMPARISON, 1), " + predicate + ")"
        body = body.replace(marker, predicate, 1)
    if "MICROS_TERMINAL_TEST_" in body or "MICROS_BUILD_TERMINAL_FAILURE_TEST" in body or "terminal-thre-poll" in body or "terminal-temt-poll" in body:
        raise ValueError("PLAT02 effective source left an unbound observation construct")
    return body
