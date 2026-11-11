"""Finite current observation, callback and fatal source/machine contracts."""

from functools import lru_cache
from pathlib import Path
import re
import subprocess


RECIPE_ADMISSION = {
    "reference_cmake_sha256": "5d017aa1674aa64ebae0414a3a2fabeec080284e3135bc9218ea089a058ac668",
    "native_target": "panic_trace_test",
    "native_case": "panic-trace",
    "native_sources": (
        "tests/host/panic_trace_test.c",
        "kernel/diagnostic/panic_trace_core.c",
        "tests/work/work_counts.c",
    ),
    "native_tiers": ("test-unit-fast", "test-unit"),
    "kernel_sources": (
        "kernel/diagnostic/panic_trace_core.c",
        "kernel/diagnostic/panic_trace.c",
        "arch/riscv64/panic_trace.S",
    ),
    "kernel_c_options": (
        "-fno-omit-frame-pointer",
        "-mno-omit-leaf-frame-pointer",
        "-fno-optimize-sibling-calls",
    ),
    "target_configure": "riscv64-panic-trace-test",
    "target_workflow": "test-qemu-panic-trace",
    "target_definition": "MICROS_BUILD_PANIC_TRACE_TEST",
    "target_work_definition": "MICROS_TEST_WORK_COUNTS=1",
    "trace_work_definition": "MICROS_TEST_PANIC_TRACE_COUNTS=1",
    "target_sources": (
        "tests/qemu/panic_trace_kernel.c",
        "tests/qemu/panic_trace_kernel.S",
        "tests/work/work_counts.c",
    ),
    "kernel_image": "kernel/micros.elf",
    "controller": "tools/check_panic_traces.py",
    "case_timeout": 8,
    "selector": ("micros_panic_trace_test_scenario", 4),
    "observation": {
        "symbol": "micros_panic_trace_test_observation",
        "version": 1,
        "size": 192,
        "alignment": 8,
        "fields": (
            ("version", 0, 4), ("scenario", 4, 4), ("phase", 8, 4), ("work_status", 12, 4),
            ("report_bodies", 16, 8), ("seizure_entries", 24, 8), ("trace_entries", 32, 8),
            ("completed_traces", 40, 8), ("read_words", 48, 8), ("accepted_links", 56, 8),
            ("reset_entries", 64, 8), ("first_panic_sp", 72, 8), ("trace_sp", 80, 8),
            ("trace_fp", 88, 8), ("allocation_bottom", 96, 8), ("root_cfa", 104, 8),
            ("owner", 112, 4), ("stop", 116, 4), ("record_count", 120, 4), ("reserved0", 124, 4),
            ("trace_sstatus", 128, 8), ("trace_sie", 136, 8), ("trace_sscratch", 144, 8),
            ("reset_sstatus", 152, 8), ("reset_sie", 160, 8), ("reset_sscratch", 168, 8),
            ("reset_type", 176, 4), ("reset_reason", 180, 4), ("reserved1", 184, 8),
        ),
        "phases": (("SETUP", 0), ("ARMED", 1), ("TRACE", 2), ("RESET", 3), ("ERROR", 4)),
        "owners": (("UNAVAILABLE", 0), ("BOOT", 1), ("PRIMARY", 2), ("THREAD", 3), ("EMERGENCY", 4)),
        "stops": (
            ("ACTIVE", 0), ("END", 1), ("BOUNDARY", 2), ("LIMIT", 3),
            ("OWNER", 4), ("INVALID_FRAME", 5), ("INVALID_PC", 6), ("NONMONOTONIC", 7),
        ),
    },
    "work_snapshot": {
        "symbol": "micros_panic_trace_test_work_snapshot",
        "version": 1,
        "size": 3056,
        "producer_prefix": 0x504C415430330000,
        "capture": 1,
        "owner_mask": 2,
        "sites": (
            ("panic-trace-word-pc", "RECORD_VISIT", 1),
            ("panic-trace-word-link", "RECORD_VISIT", 1),
            ("panic-trace-advance", "LINK_TRAVERSE", 1),
        ),
    },
    "output": (
        "MICROS_TRACE_BEGIN version=1 owner=",
        "MICROS_TRACE_FRAME index=",
        "MICROS_TRACE_END count=",
    ),
    "cases": (
        (1, "boot-current-chain", "boundary"),
        (2, "boot-corrupt-registry-after-core", "boundary"),
        (3, "primary-forged-trap-registers", "boundary"),
        (4, "thread-one-forged-trap-registers", "boundary"),
        (5, "emergency-second-trap", "boundary"),
        (6, "null-outer-link", "end"),
        (7, "increasing-misaligned-successor", "invalid-frame"),
        (8, "user-window-successor", "nonmonotonic"),
        (9, "self-successor", "nonmonotonic"),
        (10, "null-saved-pc", "invalid-pc"),
        (11, "user-window-saved-pc", "invalid-pc"),
        (12, "depth-limit", "limit"),
        (13, "stale-current-generation", "owner"),
        (14, "foreign-thread-stack-bounds", "owner"),
        (15, "actual-load-fault", "incomplete"),
        (16, "explicit-recursive-panic", "incomplete"),
        (17, "trace-thre-stuck", "incomplete"),
        (18, "ordinary-shutdown", "absent"),
    ),
    "successor_case_inputs": (
        (7, "valid-pc-interior-frame", "increasing-misaligned"),
        (8, "valid-pc-interior-frame", "nonzero-user-window-below-kernel"),
        (9, "valid-pc-interior-frame", "equal-to-current"),
    ),
    "remote": {
        "registers": (2, 8, 10, 11, 16, 17, 32),
        "read_chunk": 1024,
        "fixed_stops": (
            "kernel_main", "micros_panic_trace_test_armed",
            "micros_panic_trace_test_started", "micros_panic_trace_test_error",
        ),
        "reset_stop": ("sbi_system_reset", "ecall"),
        "reentry_stop": ("micros_panic_report", (15, 16)),
        "thread_slot": 1,
        "selector_writes": 1,
        "mmio_reads": 0,
    },
    "scope": (44, 113, 10, 50),
}


FATAL_EFFECT_CONTRACT = {
    "version": 4,
    "prior_version": 3,
    "qualifiers": (
        ("UNQUALIFIED", 0), ("UART_PANIC_READY", 1),
        ("PANIC_LATCHED", 2), ("PANIC_LATCHED_UART_READY", 3),
    ),
    "excluded": (
        ("arch/riscv64/uart.c", "uart_write_direct", "arch/riscv64/panic.S", "micros_panic_entry", 1),
        ("arch/riscv64/uart.c", "uart_write", "arch/riscv64/uart.c", "uart_write_record", 1),
        ("arch/riscv64/uart.c", "uart_write_hex64", "arch/riscv64/uart.c", "uart_write_hex64_record", 1),
        ("kernel/diagnostic/panic.c", "micros_panic_report", "kernel/diagnostic/panic.c", "panic_report_claimed", 2),
    ),
    "entry": (
        "kernel/diagnostic/panic.c", "panic_report_claimed",
        "micros_panic_trace_report", "before-micros_diagnostic_panic_report",
    ),
    "frame_record": {
        "register": "s0",
        "word_bytes": 8,
        "alignment": 16,
        "pc_bytes_before_cfa": 8,
        "link_bytes_before_cfa": 16,
        "read_order": ("pc", "link"),
        "limit": 32,
        "cursor_bytes": 64,
        "owner_view_bytes": 256,
        "pc_alignment": 2,
        "text": ("__kernel_text_start", "__kernel_text_end"),
        "after_emit": ("root-boundary", "null-end", "nonmonotonic", "successor-range", "depth-limit", "advance"),
    },
    "physical_stacks": (
        ("BOOT", "__boot_stack_bottom", "__boot_stack_top", 16384),
        ("PRIMARY", "__trap_stack_bottom", "__trap_stack_top", 16384),
        ("EMERGENCY", "__trap_emergency_stack_bottom", "__trap_emergency_stack_top", 4096),
        ("THREAD", "micros_thread_kernel_stacks", "micros_thread_kernel_stacks_end", 16384, 128),
    ),
    "nested_test_stack": (
        "MICROS_BUILD_NESTED_TRAP_TEST",
        "micros_nested_trap_test_emergency_stack_bottom",
        "micros_nested_trap_test_emergency_stack_top", 4096,
    ),
    "roots": (
        ("BOOT", "allocation-top", 0),
        ("PRIMARY", "sscratch-zero", 288),
        ("PRIMARY", "sscratch-authenticated-anchor", 0),
        ("THREAD", "sscratch-zero-and-selected-generation", 288),
        ("EMERGENCY", "sscratch-zero", 0),
    ),
    "owner_rule": "actual-sp-and-linked-interval;selected-hart-thread-process;constant-work;no-slot-scan",
    "register_helper": ("arch/riscv64/panic_trace.S", "micros_panic_trace_frame_pointer", "s0", "a0"),
    "load_helper": ("arch/riscv64/panic_trace.S", "micros_panic_trace_load_word", "a0", 8, 1),
    "selected_resolvers": (
        ("kernel/objects/kernel_object_runtime.c", "micros_kernel_object_runtime_registry"),
        ("kernel/objects/kernel_object_runtime.c", "micros_kernel_object_runtime_boot_hart_handle"),
        ("kernel/objects/kernel_object_hart.c", "micros_hart_resolve"),
        ("kernel/objects/kernel_object_thread.c", "micros_thread_resolve"),
        ("kernel/objects/kernel_object_process.c", "micros_process_resolve"),
        ("kernel/objects/kernel_object_identity.c", "kernel_object_require_initialized"),
        ("kernel/objects/kernel_object_identity.c", "kernel_object_hart_handle_is_valid"),
        ("kernel/objects/kernel_object_identity.c", "kernel_object_thread_handle_is_valid"),
        ("kernel/objects/kernel_object_identity.c", "kernel_object_process_handle_is_valid"),
    ),
    "forbidden_lookup": (
        "micros_kernel_objects_validate", "micros_kernel_object_runtime_validate",
        "micros_hart_resolve_context", "micros_kernel_object_runtime_hart_from_context",
    ),
    "trace_copy_fill_calls": 0,
    "trace_reset_calls": 0,
    "panic_context_bytes": 48,
    "trap_context_bytes": 288,
    "stack_budgets": (("boot", 16384), ("supervisor", 16384), ("emergency", 4096)),
    "fault_containment": (
        (15, "actual-invalid-load", "real-trap-pivot-retains-both-epochs"),
        (16, "explicit-reentry", "both-captures-remain-in-one-epoch"),
        (17, "actual-thre-wait", "100000-no-unready-store"),
    ),
}


SOURCE_PROJECTION = {'source_reference': 'e12b2d6c163ac92282d380eff5be419fad7f7a30',
 'test_overlays': (('kernel/boot/boot_component_tests.c', 'append-trace-invocation-after-terminal-hook'),
                   ('include/micros/kernel_object_runtime.h',
                    'existing-test-registry-declaration-guard-only'),
                   ('kernel/objects/kernel_object_runtime.c', 'existing-test-registry-definition-guard-only'),
                   ('arch/riscv64/trap.S', 'fixed-second-fault-after-complete-capture-before-dispatch'),
                   ('kernel/diagnostic/panic.c', 'post-claim-body-and-post-core-observation'),
                   ('arch/riscv64/uart.c', 'after-actual-thre-read-filter-seizure-and-physical-output'),
                   ('arch/riscv64/sbi.c', 'post-admission-pre-ecall-reset-snapshot'))}


def _paths(value, count=None):
    return (
        type(value) is tuple and bool(value) and (count is None or len(value) == count)
        and all(type(path) is str and path and not Path(path).is_absolute()
                and Path(path).as_posix() == path and ".." not in Path(path).parts
                and "." not in Path(path).parts and "\\" not in path for path in value)
        and len(set(value)) == len(value)
    )


TEST_FLAG = "MICROS_BUILD_PANIC_TRACE_TEST"


@lru_cache(maxsize=None)
def fixed_source(relative):
    if not _paths((relative,)):
        raise ValueError("PLAT03 fixed source has an invalid repository path")
    return subprocess.check_output(
        ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + relative),
        cwd=Path(__file__).resolve().parents[2], text=True, timeout=30,
    )


def _exact_changes(source, changes, *, inverse=False):
    for original, added in reversed(changes) if inverse else changes:
        before, after = (added, original) if inverse else (original, added)
        if source.count(before) != 1:
            raise ValueError("PLAT03 source extension is missing, duplicated or outside its exact anchor")
        source = source.replace(before, after, 1)
    return source


TEST_HEADER = '#include "tests/qemu/panic_trace_kernel.h"\n'


SOURCE_OVERLAYS = tuple(row[0] for row in SOURCE_PROJECTION["test_overlays"])


def _hook(statement):
    return "#ifdef " + TEST_FLAG + "\n    " + statement + "\n#endif\n"


def _observation_changes(relative):
    if relative == "kernel/boot/boot_component_tests.c":
        include = '#include "kernel/boot/boot_internal.h"\n\n'
        terminal = (
            "#ifdef MICROS_BUILD_TERMINAL_FAILURE_TEST\n"
            "    micros_terminal_test_run(boot->hart_id);\n#endif\n"
        )
        return (
            (include, include[:-1] + TEST_HEADER),
            (terminal, terminal + _hook("micros_panic_trace_test_run(boot->hart_id);")),
        )
    if relative in ("include/micros/kernel_object_runtime.h", "kernel/objects/kernel_object_runtime.c"):
        before = (
            "    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)\n"
            "struct micros_kernel_objects *\nmicros_kernel_object_runtime_test_registry(void)"
        )
        after = (
            "    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST) \\\n"
            "    || defined(" + TEST_FLAG + ")\n"
            "struct micros_kernel_objects *\nmicros_kernel_object_runtime_test_registry(void)"
        )
        return ((before, after),)
    if relative == "arch/riscv64/trap.S":
        before = "    sd t0, MICROS_TRAP_CONTEXT_STVAL_OFFSET(sp)\n\n    mv a0, sp\n"
        injected = (
            "#ifdef " + TEST_FLAG + "\n"
            "    la t0, micros_panic_trace_test_scenario\n"
            "    lw t0, 0(t0)\n"
            "    li t1, 5\n"
            "    bne t0, t1, .Lpanic_trace_dispatch\n"
            ".globl micros_panic_trace_test_nested_fault\n"
            "micros_panic_trace_test_nested_fault:\n"
            "    .4byte 0xc0001073\n"
            ".Lpanic_trace_dispatch:\n"
            "#endif\n\n"
        )
        return ((before, before.replace("    mv a0, sp\n", injected + "    mv a0, sp\n")),)
    if relative == "kernel/diagnostic/panic.c":
        include = '#include "micros/panic.h"\n\n'
        terminal = (
            "#ifdef MICROS_BUILD_TERMINAL_FAILURE_TEST\n"
            "    micros_terminal_test_report(context);\n#endif\n"
        )
        history = "    micros_diagnostic_panic_report();\n"
        return (
            (include, include[:-1] + TEST_HEADER),
            (terminal, terminal + _hook("micros_panic_trace_test_report(context);")),
            (history, _hook("micros_panic_trace_test_after_core();") + history),
        )
    if relative == "arch/riscv64/uart.c":
        include = '#include "kernel/diagnostic/panic_control.h"\n\n'
        lsr = "MICROS_TERMINAL_TEST_LSR(uart0[UART_LSR], false)"
        store = "    uart0[UART_THR] = (uint8_t)character;\n"
        seize = "bool uart_panic_seize(void)\n{\n    bool repaired = false;\n\n"
        return (
            (include, include[:-1] + TEST_HEADER),
            (lsr, "MICROS_TERMINAL_TEST_LSR(MICROS_PANIC_TRACE_TEST_LSR(uart0[UART_LSR]), false)"),
            (store, store + _hook("micros_panic_trace_test_thr();")),
            (seize, seize + _hook("micros_panic_trace_test_seizure();")),
        )
    if relative == "arch/riscv64/sbi.c":
        include = '#include "kernel/diagnostic/panic_control.h"\n\n'
        arguments = '    register uintptr_t argument0 __asm__("a0") = reset_type;\n'
        return (
            (include, include[:-1] + TEST_HEADER),
            (arguments, _hook("micros_panic_trace_test_reset(reset_type, reset_reason);") + arguments),
        )
    raise ValueError("PLAT03 has no observation overlay in this source owner")


def observe_source(relative, source):
    if relative not in SOURCE_OVERLAYS or source != fixed_source(relative):
        raise ValueError("PLAT03 observation must start at its complete fixed source owner")
    return _exact_changes(source, _observation_changes(relative))


def project_observations(relative, source):
    if relative not in SOURCE_OVERLAYS:
        raise ValueError("PLAT03 has no observation projection in this source owner")
    expected = fixed_source(relative)
    if source == expected:
        return source
    result = _exact_changes(source, _observation_changes(relative), inverse=True)
    if result != expected:
        raise ValueError("PLAT03 observation changed an original operation, declaration or capture")
    return result


def _panic_trace_changes():
    include = '#include "kernel/diagnostic/diagnostic_history.h"\n\n'
    history = "    micros_diagnostic_panic_report();\n"
    return (
        (include, include[:-1] + '#include "kernel/diagnostic/panic_trace.h"\n'),
        (history, "    micros_panic_trace_report();\n" + history),
    )


def project_trace_runtime(relative, source):
    if relative not in SOURCE_OVERLAYS:
        return source
    if relative == "kernel/diagnostic/panic.c" and (
        '#include "kernel/diagnostic/panic_trace.h"' in source or "micros_panic_trace_report(" in source
    ):
        source = _exact_changes(source, _panic_trace_changes(), inverse=True)
    if TEST_FLAG in source or TEST_HEADER in source or "MICROS_PANIC_TRACE_TEST_LSR" in source:
        return project_observations(relative, source)
    return source


def effective_trace_observations(body, *, enabled):
    if type(enabled) is not bool:
        raise ValueError("PLAT03 observation mode is not explicit")
    for statement in (
        "micros_panic_trace_test_report(context);", "micros_panic_trace_test_after_core();",
        "micros_panic_trace_test_seizure();", "micros_panic_trace_test_thr();",
        "micros_panic_trace_test_reset(reset_type, reset_reason);", "micros_panic_trace_test_run(boot->hart_id);",
    ):
        marker = _hook(statement)
        if marker in body:
            if body.count(marker) != 1:
                raise ValueError("PLAT03 repeated an existing-subject observation site")
            body = body.replace(marker, "    " + statement + "\n" if enabled else "", 1)
    marker = "MICROS_PANIC_TRACE_TEST_LSR(uart0[UART_LSR])"
    if marker in body:
        if body.count(marker) != 1:
            raise ValueError("PLAT03 repeated its after-read THRE filter")
        body = body.replace(marker, "micros_panic_trace_test_lsr(uart0[UART_LSR])" if enabled else "uart0[UART_LSR]", 1)
    if TEST_FLAG in body or "MICROS_PANIC_TRACE_TEST_" in body:
        raise ValueError("PLAT03 effective source retains an unbound observation")
    return body


def effective_trace_body(body, *, trace_test=False, nested_test=False):
    if type(trace_test) is not bool or type(nested_test) is not bool or trace_test and nested_test:
        raise ValueError("PLAT03 private source has an unbound combined configuration")
    for flag, enabled in (
        (TEST_FLAG, trace_test), ("MICROS_TEST_PANIC_TRACE_COUNTS", trace_test),
        ("MICROS_BUILD_NESTED_TRAP_TEST", nested_test),
    ):
        def select(match):
            branches = match[1].split("#else\n")
            if len(branches) > 2 or any(re.search(r"^\s*#", branch, re.MULTILINE) for branch in branches):
                raise ValueError("PLAT03 private body has a nested or unknown preprocessing effect")
            return branches[0] if enabled else branches[1] if len(branches) == 2 else ""

        body = re.sub(r"^#ifdef " + flag + r"\n(.*?)^#endif\n", select, body, flags=re.MULTILINE | re.DOTALL)
    if re.search(r"^\s*#", body, re.MULTILINE):
        raise ValueError("PLAT03 private body retains an unbound preprocessor scope")
    sites = {name: kind for name, kind, _ in RECIPE_ADMISSION["work_snapshot"]["sites"]}
    seen = set()

    def work(match):
        site, kind = match[1], match[2]
        if not trace_test or sites.get(site) != kind or site in seen:
            raise ValueError("PLAT03 private work site has another owner, primitive or occurrence")
        seen.add(site)
        return "micros_test_work_record(MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC, MICROS_WORK_KIND_" + kind + ", 1)"

    body = re.sub(
        r'MICROS_TEST_WORK_HIT\(\s*"([^"]+)",\s*MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC,\s*MICROS_WORK_KIND_(\w+),\s*1\s*\)',
        work, body,
    )
    if "MICROS_TEST_WORK_" in body:
        raise ValueError("PLAT03 private body contains an undeclared observation expression")
    return body


def trace_function_records(relative, source):
    from tests.host.c_source_records import function_records

    if relative == "kernel/diagnostic/panic_trace_core.c":
        guard = (
            "#if defined(MICROS_TEST_PANIC_TRACE_COUNTS) && MICROS_TEST_PANIC_TRACE_COUNTS != 1\n"
            '#error "MICROS_TEST_PANIC_TRACE_COUNTS must be undefined or equal to 1"\n'
            "#endif\n"
        )
        if source.count(guard) != 1:
            raise ValueError("PLAT03 portable source lost its exact rejected-mode preamble")
        source = source.replace(guard, "\n" * guard.count("\n"), 1)
    elif relative != "kernel/diagnostic/panic_trace.c":
        raise ValueError("PLAT03 has no private source parser for this owner")
    return function_records(source)


def project_trace_boot_bodies(source):
    if TEST_FLAG not in source:
        return source
    return _exact_changes(source, (_observation_changes("kernel/boot/boot_component_tests.c")[1],), inverse=True)


def validate_trace_helper_instructions(frame, load, *, selector=None):
    def words(rows, count):
        if (
            type(rows) is not tuple or len(rows) != count
            or any(type(row) is not tuple or len(row) != 3
                   or any(type(value) is not int for value in row) for row in rows)
            or rows[0][0] <= 0 or rows[0][0] % 4
            or any(address != rows[0][0] + 4 * index or width != 4
                   or address > 0xffffffffffffffff or not 0 <= word <= 0xffffffff
                   for index, (address, width, word) in enumerate(rows))
        ):
            raise ValueError("PLAT03 helper lacks its complete aligned fixed instruction extent")
        return tuple(row[2] for row in rows)

    if words(frame, 2) != (0x00040513, 0x00008067):
        raise ValueError("PLAT03 frame helper does not preserve every register except its s0 result in a0")
    if selector is None:
        if words(load, 2) != (0x00053503, 0x00008067):
            raise ValueError("PLAT03 ordinary helper is not exactly one admitted a0 word load and return")
        return None
    if type(selector) is not int or selector <= 0 or selector % 4 or selector > 0xffffffffffffffff:
        raise ValueError("PLAT03 fault selector has no aligned typed object")
    code = words(load, 10)
    if (
        code[0] & 0xfff != 0x297 or code[1] & 0xfffff != 0x28293
        or code[2:] != (
            0x0002a283, 0x00f00313, 0x00628663, 0x00053503,
            0x00008067, 0x00000513, 0x00053503, 0x00008067,
        )
    ):
        raise ValueError("PLAT03 test helper changed its selector, case-15 branch, load or no-frame effects")
    upper = code[0] & 0xfffff000
    upper -= (1 << 32) if upper & (1 << 31) else 0
    lower = code[1] >> 20
    lower -= 4096 if lower & 2048 else 0
    if (load[0][0] + upper + lower) & 0xffffffffffffffff != selector:
        raise ValueError("PLAT03 test helper reads another object as its fault selector")
    return load[8][0]
