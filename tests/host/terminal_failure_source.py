"""Closed PLAT02 source, recipe and finite-effect declarations."""

import ast
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import re
import subprocess

from tests.host.evidence_authority_source import literal_value, regular_source, single_assignment
from tests.host.evidence_authority_types import AuthorityClass, Fact, digest
from tests.host.panic_trace_source import (
    effective_trace_observations, project_trace_boot_bodies, project_trace_cmake, project_trace_gate, project_trace_plan,
    project_trace_planner, project_trace_presets, project_trace_runtime, project_trace_source_bytes,
    project_trace_workflows,
)


FAMILY = "terminal-failure-baseline"
SOURCE_PATH = "tests/host/terminal_failure_source.py"
EVIDENCE_PATH = "tests/host/terminal_failure_evidence.py"
REFERENCES = (
    "docs/adr/0095-bounded-uart-and-terminal-panic.md",
    "docs/architecture/terminal-failure.md",
    "docs/research/minix-terminal-failure-baseline.md",
)

SOURCE_ROLES = {
    "base_membership_sha256": "7bbeb1e9a6148992ce2c66e640dac0495c508d4223434e70616460dfb721e233",
    "candidate_paths": (
        "arch/riscv64/uart.c",
        "arch/riscv64/platform.h",
        "arch/riscv64/sbi.c",
        "kernel/diagnostic/panic.c",
        "kernel/diagnostic/panic_control_core.h",
        "kernel/diagnostic/panic_control_core.c",
        "kernel/diagnostic/panic_control.h",
        "kernel/diagnostic/panic_control.c",
    ),
    "immutable_additions": (
        "tests/host/panic_control_test.c",
        "tests/host/terminal_failure_source.py",
        "tests/host/terminal_failure_evidence.py",
        "tests/host/test_terminal_failure.py",
        "tests/qemu/terminal_failure_kernel.c",
        "tests/qemu/terminal_failure_kernel.h",
        "tools/check_terminal_failures.py",
    ),
}

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

SOURCE_PROJECTION = {
    "source_reference": "8375ca2f5285bc42063af8ed92ba6f3811d517e8",
    "stack_reference": "fc98edd26505ac522d3752ad85f09366e8085f32",
    "new_kernel_members": (
        "kernel/diagnostic/panic_control_core.h",
        "kernel/diagnostic/panic_control_core.c",
        "kernel/diagnostic/panic_control.h",
        "kernel/diagnostic/panic_control.c",
    ),
    "input_paths": (
        "arch/riscv64/uart.c",
        "arch/riscv64/platform.h",
        "arch/riscv64/sbi.c",
        "kernel/diagnostic/panic.c",
        "kernel/diagnostic/panic_control_core.h",
        "kernel/diagnostic/panic_control_core.c",
        "kernel/diagnostic/panic_control.h",
        "kernel/diagnostic/panic_control.c",
        "tests/host/panic_control_test.c",
        "tests/host/terminal_failure_source.py",
        "tests/host/terminal_failure_evidence.py",
        "tests/host/test_terminal_failure.py",
        "tests/qemu/terminal_failure_kernel.c",
        "tests/qemu/terminal_failure_kernel.h",
        "tools/check_terminal_failures.py",
        "CMakeLists.txt", "CMakePresets.json", "kernel/boot/boot_component_tests.c",
        "arch/riscv64/panic.S", "arch/riscv64/panic_context.h", "arch/riscv64/interrupt.h",
        "kernel/tty/uart_console_core.c", "kernel/tty/uart_console_core.h",
        "tests/work/work_counts.c", "tests/work/work_counts.h", "include/micros/work_observation.h",
        "tests/host/kernel_layout_source.py", "tests/host/stack_recipe.py",
        "tests/host/work_count_recipe.py", "tests/host/test_kernel_directory_layout.py",
        "tests/host/test_validation_build_impact.py",
        "tools/validation_plan.py", "tools/stack_bound_context.py",
        "tools/stack_bound_fatal.py", "tools/stack_bound_fatal_effects.py",
        "tools/stack_bound_bindings.py", "tools/stack_bound_certify.py", "tools/stack_bound_scope.py",
    ),
    "admissions": (
        "bounded-waits-and-direct-exhaustion",
        "first-entry-and-claimed-report-extraction",
        "shared-failure-srst-admission",
        "terminal-console-magic-repair-and-status",
        "four-kernel-members-one-native-target-one-target-workflow",
        "fixed-target-fixture-and-ev02-observation-overlays",
        "four-context-four-site-fatal-proof",
        "prior-authority-and-original-source-conservation",
    ),
}


def literal_declaration(root, relative, name):
    source = regular_source(Path(root), relative).read_text(encoding="utf-8")
    expression = single_assignment(ast.parse(source), name).value
    if name != "FATAL_EFFECT_CONTRACT":
        return literal_value(expression)
    resets = [
        value for key, value in zip(expression.keys, expression.values)
        if isinstance(key, ast.Constant) and key.value == "reset"
    ] if isinstance(expression, ast.Dict) else []
    if len(resets) != 1 or not isinstance(resets[0], ast.Dict):
        raise ValueError("PLAT02 requires one literal reset effect")
    flags = [
        value for key, value in zip(resets[0].keys, resets[0].values)
        if isinstance(key, ast.Constant) and key.value == "caller_preclaim"
    ]
    if len(flags) != 1 or not isinstance(flags[0], ast.Constant) or flags[0].value is not False:
        raise ValueError("PLAT02 caller preclaim must be the exact False literal")
    # This sole Boolean leaf is outside the shared integer/string grammar.
    flags[0].value = 0
    value = literal_value(expression)
    value["reset"]["caller_preclaim"] = False
    return value


def _paths(value, count=None):
    return (
        type(value) is tuple and bool(value) and (count is None or len(value) == count)
        and all(type(path) is str and path and not Path(path).is_absolute()
                and ".." not in Path(path).parts and "\\" not in path for path in value)
        and len(set(value)) == len(value)
    )


def checked_oracle_rules(rules):
    if type(rules) is not tuple or len(rules) != 4:
        raise ValueError("PLAT02 requires four literal baseline oracles")
    identifiers, tests, covered = set(), set(), set()
    for row in rules:
        if (
            type(row) is not tuple or len(row) != 6
            or any(type(value) is not str or not value for value in row[:5])
            or row[3] != "ADR-0095" or type(row[5]) is not tuple or not row[5]
            or any(type(case) is not int or not 1 <= case <= 12 for case in row[5])
            or len(set(row[5])) != len(row[5]) or covered.intersection(row[5])
            or row[0] in identifiers or row[1] in tests
        ):
            raise ValueError("PLAT02 oracle ownership is duplicate, malformed or unbound")
        identifiers.add(row[0])
        tests.add(row[1])
        covered.update(row[5])
    if covered != set(range(1, 13)):
        raise ValueError("PLAT02 baseline oracles omit a fixed target case")
    return rules


def declaration_facts(root, owner):
    if owner not in (SOURCE_PATH, EVIDENCE_PATH):
        raise ValueError("PLAT02 has no declaration in this owner")
    projection = literal_declaration(root, SOURCE_PATH, "SOURCE_PROJECTION")
    roles = literal_declaration(root, SOURCE_PATH, "SOURCE_ROLES")
    recipe = literal_declaration(root, SOURCE_PATH, "RECIPE_ADMISSION")
    effects = literal_declaration(root, SOURCE_PATH, "FATAL_EFFECT_CONTRACT")
    pins = literal_declaration(root, EVIDENCE_PATH, "DESIGN_PINS")
    rules = checked_oracle_rules(literal_declaration(root, EVIDENCE_PATH, "ORACLE_RULES"))
    if (
        type(projection) is not dict or set(projection) != {
            "source_reference", "stack_reference", "new_kernel_members", "input_paths", "admissions",
        }
        or not _paths(projection["new_kernel_members"], 4)
        or not _paths(projection["input_paths"]) or not _paths(projection["admissions"], 8)
        or type(roles) is not dict or set(roles) != {
            "base_membership_sha256", "candidate_paths", "immutable_additions",
        }
        or type(roles["base_membership_sha256"]) is not str
        or re.fullmatch(r"[0-9a-f]{64}", roles["base_membership_sha256"]) is None
        or not _paths(roles["candidate_paths"], 8) or not _paths(roles["immutable_additions"], 7)
        or set(roles["candidate_paths"]) & set(roles["immutable_additions"])
        or not set(projection["new_kernel_members"]) <= set(roles["candidate_paths"])
        or not set((*roles["candidate_paths"], *roles["immutable_additions"])) <= set(projection["input_paths"])
        or type(recipe) is not dict or type(effects) is not dict or type(pins) is not dict
        or pins.get("runtime_reference") != projection["source_reference"]
    ):
        raise ValueError("PLAT02 source, recipe, effect or design declaration is malformed")
    consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)

    def fact(kind, scope, classification, payloads, value):
        return Fact(
            f"{owner}:{kind}:{scope}", FAMILY, owner, kind, scope, classification,
            "B01", consumers, tuple(sorted({owner, *REFERENCES, *payloads})), digest(value),
        )

    if owner == EVIDENCE_PATH:
        return (
            fact("design-anchor", "PLAT02", AuthorityClass.TRANSPORT_IDENTITY, (), pins),
            *(fact("baseline-oracle", row[0], AuthorityClass.BASELINE_ORACLE,
                   roles["immutable_additions"], row) for row in rules),
        )
    return (
        fact("source-membership", "PLAT02", AuthorityClass.PERMANENT_INVARIANT,
             (*roles["candidate_paths"], *roles["immutable_additions"]), roles),
        fact("artifact-contract", "PLAT02-recipes", AuthorityClass.ARTIFACT_CONTRACT,
             ("CMakeLists.txt", "CMakePresets.json", "tests/work/work_counts.c",
              "tests/work/work_counts.h", "include/micros/work_observation.h"), recipe),
        fact("effect-contract", "PLAT02-fatal", AuthorityClass.PERMANENT_INVARIANT,
             projection["input_paths"], effects),
        fact("source-projection", "PLAT02", AuthorityClass.TRANSPORT_IDENTITY,
             projection["input_paths"], projection),
    )


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
    if relative not in SOURCE_PROJECTION["input_paths"]:
        raise ValueError("PLAT02 fixed-source lookup has an unregistered input")
    return subprocess.check_output(
        ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + relative),
        cwd=Path(__file__).resolve().parents[2], timeout=30,
    ).decode("utf-8")


def native_recipe():
    target = RECIPE_ADMISSION["native_target"]
    return "\n".join((
        "    add_executable(",
        "        " + target,
        *("        " + name for name in RECIPE_ADMISSION["native_sources"]),
        "    )",
        f'    target_include_directories({target} PRIVATE "${{CMAKE_CURRENT_SOURCE_DIR}}")',
        f"    target_compile_features({target} PRIVATE c_std_17)",
        f"    target_compile_options({target} PRIVATE -Wall -Wextra -Werror -Wpedantic -Wmissing-prototypes -Wstrict-prototypes)",
        '    if(MICROS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")',
        f"        target_compile_options({target} PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)",
        f"        target_link_options({target} PRIVATE -fsanitize=address,undefined)",
        "    endif()",
        f'    add_test(NAME {RECIPE_ADMISSION["native_case"]} COMMAND {target})',
        *(f"    add_dependencies({tier} {target})" for tier in RECIPE_ADMISSION["native_tiers"]),
        "", "",
    ))


def target_recipe():
    return "\n".join((
        "if(" + TEST_FLAG + ")",
        "    target_compile_definitions(micros PRIVATE " + TEST_FLAG + "=1 MICROS_TEST_WORK_COUNTS=1)",
        "    target_sources(",
        "        micros PRIVATE",
        *("        " + name for name in RECIPE_ADMISSION["target_sources"]),
        "    )",
        "    add_custom_target(",
        "        " + RECIPE_ADMISSION["target_workflow"],
        '        COMMAND "${MICROS_PYTHON}" ${MICROS_PYTHON_ARGS}',
        '            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_terminal_failures.py"',
        "        DEPENDS micros",
        '        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"',
        "        USES_TERMINAL",
        "        VERBATIM",
        "    )",
        "endif()",
        "", "",
    ))


def _cmake_changes(production):
    if type(production) is not bool:
        raise ValueError("PLAT02 recipe requires its exact source stage")
    option = 'option(MICROS_BUILD_TIMER_TEST "Build the timer interrupt test image" OFF)\n'
    choices = "    MICROS_BUILD_TIMER_TEST\n"
    fixture = (
        "if(MICROS_BUILD_PANIC_TEST OR MICROS_BUILD_TTY_SERVICE_TEST)\n"
        "    target_sources(micros PRIVATE tests/qemu/diagnostic_history_kernel.c)\nendif()\n\n"
    )
    changes = [
        (option, option + 'option(' + TEST_FLAG + ' "Build the terminal failure evidence image" OFF)\n'),
        (choices, choices + "    " + TEST_FLAG + "\n"),
        (fixture, fixture + target_recipe()),
    ]
    if production:
        anchor = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
        panic = "    kernel/diagnostic/panic.c\n"
        changes.extend((
            (anchor, native_recipe() + anchor),
            (panic, panic + "".join("    " + name + "\n" for name in RECIPE_ADMISSION["kernel_sources"])),
        ))
    return tuple(changes)


def _require_original_cmake(source):
    if hashlib.sha256(source.encode()).hexdigest() != RECIPE_ADMISSION["reference_cmake_sha256"]:
        raise ValueError("PLAT02 recipe does not recover the complete independently pinned predecessor")


def extend_cmake(source, *, production=True):
    _require_original_cmake(source)
    return _exact_changes(source, _cmake_changes(production))


def project_terminal_cmake(source, *, production=True):
    source = project_trace_cmake(source, production=production)
    if TEST_FLAG not in source and "panic_control" not in source and "check_terminal_failures" not in source:
        return source
    result = _exact_changes(source, _cmake_changes(production), inverse=True)
    _require_original_cmake(result)
    return result


def terminal_presets():
    configure, workflow = RECIPE_ADMISSION["target_configure"], RECIPE_ADMISSION["target_workflow"]
    return (
        {
            "name": configure, "displayName": "RISC-V 64 terminal failure evidence",
            "inherits": "riscv64-debug", "binaryDir": "${sourceDir}/build/" + configure,
            "cacheVariables": {TEST_FLAG: "ON"},
        },
        {"name": workflow, "configurePreset": configure, "targets": [workflow]},
        {
            "name": workflow, "displayName": "Verify bounded UART and terminal panic",
            "steps": [{"type": "configure", "name": configure}, {"type": "build", "name": workflow}],
        },
    )


def _preset_changes():
    boundaries = ('\n  ],\n  "buildPresets": [', '\n  ],\n  "workflowPresets": [', "\n  ]\n}\n")
    return tuple(
        (boundary, ",\n" + "\n".join("    " + line for line in json.dumps(value, indent=2).splitlines()) + boundary)
        for boundary, value in zip(boundaries, terminal_presets())
    )


def extend_presets(source):
    if source != fixed_source("CMakePresets.json"):
        raise ValueError("PLAT02 presets do not start at the complete fixed predecessor")
    return _exact_changes(source, _preset_changes())


def project_terminal_presets(source):
    source = project_trace_presets(source)
    if TEST_FLAG not in source and "terminal-failure" not in source:
        return source
    result = _exact_changes(source, _preset_changes(), inverse=True)
    if result != fixed_source("CMakePresets.json"):
        raise ValueError("PLAT02 presets changed an original configuration, target or workflow")
    return result


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


def project_terminal_source_bytes(relative, raw):
    try:
        raw = project_trace_source_bytes(relative, raw)
        if relative not in (
            "arch/riscv64/uart.c", "arch/riscv64/sbi.c", "arch/riscv64/platform.h", "kernel/diagnostic/panic.c",
        ):
            return raw
        return project_terminal_runtime(relative, raw.decode("utf-8")).encode("utf-8")
    except ValueError as error:
        raise AssertionError(str(error)) from error


SOURCE_OVERLAY_PATHS = (
    "arch/riscv64/uart.c", "arch/riscv64/sbi.c",
    "kernel/diagnostic/panic.c", "kernel/boot/boot_component_tests.c",
)


def validate_terminal_runtime(root):
    from tests.host.c_source_records import tokens

    root = Path(root)
    for name in SOURCE_ROLES["candidate_paths"]:
        source = regular_source(root, name).read_text(encoding="utf-8")
        if name in CONTROL_SOURCES:
            if tokens(source) != tokens(CONTROL_SOURCES[name]):
                raise ValueError("PLAT02 private control source changed its exact state/effect contract")
            continue
        if name in SOURCE_OVERLAY_PATHS and TEST_HEADER not in source:
            raise ValueError("PLAT02 runtime is missing its immutable test observation boundary")
        projected = project_terminal_runtime(name, source)
        if projected == source or tokens(projected) != tokens(fixed_source(name)):
            raise ValueError("PLAT02 runtime changed an unapproved original operation")
    name = "kernel/boot/boot_component_tests.c"
    source = regular_source(root, name).read_text(encoding="utf-8")
    if TEST_HEADER not in source or project_observations(name, source) != fixed_source(name):
        raise ValueError("PLAT02 boot fixture changed an original case, include or diagnostic location")


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


def project_terminal_planner(source):
    source = project_trace_planner(source)
    workflow = RECIPE_ADMISSION["target_workflow"]
    if workflow not in source and "terminal_failure" not in source:
        return source
    prior = '    "test-qemu-uart-console",\n    "test-qemu-frame-allocator",\n'
    added = '    "test-qemu-uart-console",\n    "' + workflow + '",\n    "test-qemu-frame-allocator",\n'
    inputs = tuple(
        "kernel/panic.c" if name == "kernel/diagnostic/panic.c" else name
        for name in (*SOURCE_ROLES["candidate_paths"], *SOURCE_ROLES["immutable_additions"])
    )
    gate = (
        "GATE_INPUTS = {\n" + '    "' + workflow + '": (\n'
        + "".join('        "' + name + '",\n' for name in inputs)
        + "    ),\n"
    )
    result = _exact_changes(source, ((prior, added), ("GATE_INPUTS = {\n", gate)), inverse=True)
    if result != fixed_source("tools/validation_plan.py"):
        raise ValueError("PLAT02 planner changed an original selection algorithm or ownership rule")
    return result


def project_terminal_plan(commands):
    commands = project_trace_plan(commands)
    command = ("cmake", "--workflow", "--preset", RECIPE_ADMISSION["target_workflow"])
    if command not in commands:
        return commands
    position = commands.index(command)
    stack = ("cmake", "--workflow", "--preset", "test-stack-evidence")
    if (
        commands.count(command) != 1 or position == 0 or position >= len(commands) - 3
        or tuple(commands[0][:3]) != ("cmake", "--workflow", "--preset")
        or commands[0][-1] not in ("test-unit", "test-unit-fast")
        or stack in commands and position > commands.index(stack)
    ):
        raise ValueError("PLAT02 target gate is duplicated or outside its original validation phase")
    return [value for value in commands if value != command]


def project_terminal_workflows(workflows):
    workflows = project_trace_workflows(workflows)
    name = RECIPE_ADMISSION["target_workflow"]
    values = tuple(workflows)
    if name not in values:
        return values
    if values.count(name) != 1 or (
        "test-qemu-uart-console" in values
        and values.index(name) != values.index("test-qemu-uart-console") + 1
    ):
        raise ValueError("PLAT02 workflow projection is duplicated or outside its fixed ordered slot")
    return tuple(value for value in values if value != name)


def project_terminal_boot_bodies(source):
    source = project_trace_boot_bodies(source)
    if TEST_FLAG not in source:
        return source
    prior = '    MICROS_PANIC(boot->hart_id, "nested-trap-test-returned");\n#endif\n'
    return _exact_changes(source, ((prior, prior + _hook("micros_terminal_test_run(boot->hart_id);")),), inverse=True)


TERMINAL_LAYOUT_IMPORT = (
    "from tests.host.terminal_failure_source import (\n"
    "    project_terminal_plan, project_terminal_planner, project_terminal_source_bytes, terminal_kernel_members,\n"
    ")\n"
)
TERMINAL_MEMBERSHIP = (
    "    terminal, terminal_errors = terminal_kernel_members(root)\n"
    "    expected.update(terminal)\n"
    "    errors.extend(terminal_errors)\n"
)


def project_terminal_gate(relative, source):
    source = project_trace_gate(relative, source)
    workflow_import = "from tests.host.terminal_failure_source import project_terminal_workflows\n"
    workflow = "validation_plan.QEMU_WORKFLOWS"
    affected = "validation_plan.affected_workflows([path])"
    gates = {
        "tests/host/test_kernel_bootstrap_source_layout.py": ((workflow, 1), (affected, 2)),
        "tests/host/test_trap_syscall_source_layout.py": ((workflow, 1), (affected, 1)),
        "tests/host/test_scheduler_source_layout.py": ((workflow, 1), (affected, 1)),
        "tests/host/test_user_elf_source_layout.py": ((affected, 1),),
        "tests/host/test_grant_data_plane_source_layout.py": ((affected, 1),),
        "tests/host/test_qemu_runner_source_layout.py": (("planner.affected_workflows([old])", 1),),
        "tests/host/test_kernel_directory_layout.py": ((affected, 1),),
    }
    if relative in gates and workflow_import in source:
        if source.count(workflow_import) != 1:
            raise ValueError("PLAT02 historical workflow adapter has duplicate ownership")
        source = source.replace(workflow_import, "", 1)
        for expression, count in gates[relative]:
            wrapped = "project_terminal_workflows(" + expression + ")"
            if source.count(wrapped) != count:
                raise ValueError("PLAT02 historical workflow adapter changed its exact assertion sites")
            source = source.replace(wrapped, expression, count)
        return source
    if relative != "tests/host/kernel_layout_source.py" or TERMINAL_LAYOUT_IMPORT not in source:
        return source
    return _exact_changes(source, (
        ("", TERMINAL_LAYOUT_IMPORT),
        ("", TERMINAL_MEMBERSHIP),
        ("    source = project_planner_registration(source)\n",
         "    source = project_terminal_planner(source)\n    source = project_planner_registration(source)\n"),
        ("    commands = project_command_extension(commands)\n",
         "    commands = project_terminal_plan(commands)\n    commands = project_command_extension(commands)\n"),
        ("    raw = selected.read_bytes()\n",
         "    raw = project_terminal_source_bytes(KERNEL_LOGICAL_TO_PHYSICAL[logical], selected.read_bytes())\n"),
        ("        relative,\n        path.read_bytes(),\n        expected_sha256,\n        transports,\n        required=",
         "        relative,\n        project_terminal_source_bytes(relative, path.read_bytes()),\n        expected_sha256,\n        transports,\n        required="),
    ), inverse=True)


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
