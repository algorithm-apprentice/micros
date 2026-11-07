"""Closed PLAT01 source declarations and independent transport projections."""

import ast
from functools import lru_cache
import hashlib
from pathlib import Path
import re
import subprocess

from tests.host.ast_evidence import ast_dump
from tests.host.evidence_authority_source import literal_value, regular_source, single_assignment
from tests.host.evidence_authority_types import AuthorityClass, Fact, digest
from tests.host.terminal_failure_source import project_terminal_boot_bodies, project_terminal_gate


FAMILY = "diagnostic-history-baseline"
AUTHORITY_PATH = "tests/host/diagnostic_history_authority.py"
SOURCE_PATH = "tests/host/diagnostic_history_source.py"
REFERENCES = (
    "docs/architecture/diagnostic-history.md",
    "docs/adr/0093-minix-diagnostic-histories.md",
    "docs/adr/0094-retrigger-safe-diagnostic-probe.md",
    "docs/research/minix-diagnostic-history.md",
)

SOURCE_PROJECTION = {
    "source_reference": "32f19782075ab34ff2bd6992f80540b6bfd47a4f",
    "stack_reference": "fc98edd26505ac522d3752ad85f09366e8085f32",
    "retirement_keys": (
        "tests/host/kernel_layout_source.py:data-entry:EXTERNAL_SOURCE_FACTS/arch/riscv64/uart.c/1",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/boot_component_tests.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/panic.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_idle.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_return.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/tty_control_core.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/tty_control_core.h/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/tty_control_syscall.c/2",
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/tty_service_test.c/2",
        "tests/host/test_grant_data_plane_source_layout.py:data-entry:STABLE_HASHES/kernel/boot_component_tests.c",
        "tests/host/test_kernel_object_source_layout.py:data-entry:STABLE_HASHES/kernel/boot_component_tests.c",
        "tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/include/micros/tty.h",
        "tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/kernel/tty_control_core.c",
        "tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/kernel/tty_control_core.h",
        "tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/kernel/tty_control_syscall.c",
        "tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/kernel/tty_service_test.c",
        "tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/servers/tty/tty_control.c",
        "tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/servers/tty/tty_control.h",
        "tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/servers/tty/tty_service.c",
        "tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/tests/qemu/tty_service_vfs.c",
    ),
    "new_kernel_members": (
        "kernel/diagnostic/diagnostic_history_core.h",
        "kernel/diagnostic/diagnostic_history_core.c",
        "kernel/diagnostic/diagnostic_history.h",
        "kernel/diagnostic/diagnostic_history.c",
        "kernel/diagnostic/diagnostic_control_core.h",
        "kernel/diagnostic/diagnostic_control_core.c",
        "kernel/diagnostic/diagnostic_control.c",
        "kernel/diagnostic/panic_history.c",
    ),
    "input_paths": (
        "CMakeLists.txt",
        "arch/riscv64/uart.c",
        "include/micros/tty.h",
        "kernel/boot/boot_component_tests.c",
        "kernel/diagnostic/panic.c",
        "kernel/scheduler/scheduler_idle.c",
        "kernel/scheduler/scheduler_return.c",
        "kernel/tests/tty/tty_service_test.c",
        "kernel/tty/tty_control_core.c",
        "kernel/tty/tty_control_core.h",
        "kernel/tty/tty_control_syscall.c",
        "servers/tty/tty_control.c",
        "servers/tty/tty_control.h",
        "servers/tty/tty_service.c",
        "tests/qemu/tty_service_vfs.c",
        "tests/host/kernel_layout_source.py",
        "tests/host/test_grant_data_plane_source_layout.py",
        "tests/host/test_kernel_object_source_layout.py",
        "tests/host/test_tty_source_layout.py",
        "include/micros/diagnostic.h",
        "kernel/diagnostic/diagnostic_history_core.h",
        "kernel/diagnostic/diagnostic_history_core.c",
        "kernel/diagnostic/diagnostic_history.h",
        "kernel/diagnostic/diagnostic_history.c",
        "kernel/diagnostic/diagnostic_control_core.h",
        "kernel/diagnostic/diagnostic_control_core.c",
        "kernel/diagnostic/diagnostic_control.c",
        "kernel/diagnostic/panic_history.c",
        "servers/tty/tty_diagnostic.h",
        "servers/tty/tty_diagnostic.c",
        "tests/qemu/diagnostic_history_kernel.c",
        "tests/qemu/diagnostic_history_kernel.h",
        "tests/qemu/diagnostic_history_tty.c",
        "tests/qemu/diagnostic_history_tty.h",
        "tests/host/test_kernel_directory_layout.py",
        "tests/host/test_kernel_bootstrap_source_layout.py",
        "tests/host/test_qemu_runner_source_layout.py",
        "tools/stack_bound_bindings.py",
        "tools/run_qemu_smoke.py",
        "tools/qemu_smoke_arguments.py",
        "tools/qemu_smoke_cli.py",
        "tools/qemu_smoke_command.py",
        "tools/qemu_smoke_execution.py",
        "tools/qemu_smoke_faults.py",
        "tools/qemu_smoke_objects.py",
        "tools/qemu_smoke_outcome.py",
        "tools/qemu_smoke_platform.py",
        "tools/qemu_smoke_records.py",
    ),
    "admissions": (
        "uart-direct-name-visibility",
        "uart-test-thr-annotation",
        "directory-eight-member-addition",
        "kernel-layout-exact-key-equality-policy",
        "four-source-owner-whole-ast-inverses",
        "boot-intentional-panic-seed",
        "scheduler-two-pump-sites",
        "tty-constructor-and-two-bit-dispatch",
        "tty-legacy-integration-and-vfs-busy-retry",
        "tty-test-retrigger-admission-and-deferred-fixture",
        "runner-options-report-checks-and-original-85-cases",
        "root-recipe-closed-source-test-and-requirement-extension",
    ),
}


def _literal_declaration(root, path, name):
    source = regular_source(Path(root), path).read_text(encoding="utf-8")
    return literal_value(single_assignment(ast.parse(source), name).value)


def checked_oracle_rules(rules, identities):
    if type(rules) is not tuple or len(rules) != 4:
        raise ValueError("PLAT01 requires exactly four literal oracle rules")
    result = []
    seen = set()
    tests = set()
    covered = set()
    for row in rules:
        if (
            type(row) is not tuple or len(row) != 6
            or any(type(value) is not str or not value for value in row[:5])
            or row[3] not in ("ADR-0093", "ADR-0094")
            or type(row[5]) is not tuple or not row[5]
            or any(type(index) is not int or not 1 <= index <= 20 for index in row[5])
            or len(set(row[5])) != len(row[5]) or row[0] in seen or row[1] in tests
        ):
            raise ValueError("PLAT01 has malformed, duplicate or unbound oracle coverage")
        identifier, test, property_name, adr, section, positions = row
        selected = tuple(identities[index - 1] for index in positions)
        seen.add(identifier)
        tests.add(test)
        covered.update(selected)
        result.append({
            "id": identifier, "test": test, "property": property_name,
            "replaces": selected,
            "provenance": ("ACCEPTED_ADAPTATION", adr, section),
        })
    if covered != set(identities):
        raise ValueError("PLAT01 oracle coverage omits a planned identity")
    return tuple(result)


def declaration_facts(root, owner):
    if owner not in (AUTHORITY_PATH, SOURCE_PATH):
        raise ValueError("PLAT01 has no declaration in this source owner")
    projection = _literal_declaration(root, SOURCE_PATH, "SOURCE_PROJECTION")
    if type(projection) is not dict or set(projection) != {
        "source_reference", "stack_reference", "retirement_keys",
        "new_kernel_members", "input_paths", "admissions",
    }:
        raise ValueError("PLAT01 source projection declaration is incomplete")
    identities = projection["retirement_keys"]
    if (
        type(identities) is not tuple or len(identities) != 20
        or any(type(key) is not str or not key for key in identities)
        or len(set(identities)) != 20
    ):
        raise ValueError("PLAT01 requires its twenty distinct transport keys")
    consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)

    def fact(kind, scope, classification, payloads, value):
        return Fact(
            f"{owner}:{kind}:{scope}", FAMILY, owner, kind, scope, classification,
            "B01", consumers, tuple(sorted({owner, *REFERENCES, *payloads})), digest(value),
        )

    if owner == SOURCE_PATH:
        return (fact(
            "source-projection", "PLAT01", AuthorityClass.TRANSPORT_IDENTITY,
            projection["input_paths"], projection,
        ),)
    pins = _literal_declaration(root, owner, "DESIGN_PINS")
    roles = _literal_declaration(root, owner, "SOURCE_ROLES")
    recipe = _literal_declaration(root, owner, "RECIPE_ADMISSION")
    rules = _literal_declaration(root, owner, "ORACLE_RULES")
    if type(pins) is not dict or type(roles) is not dict or set(roles) != {
        "base_membership_sha256", "candidate_paths", "immutable_additions",
    } or type(recipe) is not dict:
        raise ValueError("PLAT01 design, membership or recipe declaration is malformed")
    candidates, additions = roles["candidate_paths"], roles["immutable_additions"]
    if (
        type(candidates) is not tuple or len(candidates) != 29
        or any(type(path) is not str or not path for path in candidates)
        or len(set(candidates)) != 29
        or type(additions) is not tuple or len(additions) != 14
        or any(type(path) is not str or not path for path in additions)
        or len(set(additions)) != 14
        or set(candidates) & set(additions)
    ):
        raise ValueError("PLAT01 source roles are incomplete or overlap")
    result = [
        fact("design-anchor", "PLAT01", AuthorityClass.TRANSPORT_IDENTITY, (), pins),
        fact("source-membership", "PLAT01", AuthorityClass.PERMANENT_INVARIANT,
             (SOURCE_PATH, *candidates, *additions), roles),
        fact("artifact-contract", "PLAT01-recipes", AuthorityClass.ARTIFACT_CONTRACT,
             (SOURCE_PATH, "CMakeLists.txt", "tests/work/WorkCounts.cmake"), recipe),
    ]
    result.extend(
        fact("baseline-oracle", row["id"], AuthorityClass.BASELINE_ORACLE, additions, row)
        for row in checked_oracle_rules(rules, identities)
    )
    return tuple(result)


RUNNER_ARGUMENT_ADDITION = '''    parser.add_argument(
        "--require-diagnostic-history",
        choices=("retained", "seeded"),
        help="Require bounded encoded panic history, optionally the exact intentional seed",
    )
    parser.add_argument(
        "--require-diagnostic-forwarding",
        action="store_true",
        help="Require the unique TTY-forwarded diagnostic before the legacy I/O sequence",
    )
'''
RUNNER_RESULT_ADDITION = '''    return (
        (require_diagnostic_history is None or _has_diagnostic_history(result.output, require_diagnostic_history))
        and (not require_diagnostic_forwarding or _has_diagnostic_forwarding(result.output))
    )
'''


def _project_exact_changes(source, changes, label):
    counts = tuple(source.count(added) for added, _ in changes)
    if not any(counts):
        return source
    if any(count != 1 for count in counts):
        raise AssertionError(f"PLAT01 {label} has an incomplete or repeated extension")
    for added, original in changes:
        source = source.replace(added, original, 1)
    return source


def _project_runner_faults(source):
    names = (
        ("_diagnostic_history_payload", ("output",)),
        ("_has_diagnostic_history", ("output", "requirement")),
        ("_has_diagnostic_forwarding", ("output",)),
    )
    module = ast.parse(source)
    selected = [
        node for node in module.body
        if isinstance(node, ast.FunctionDef) and node.name in {name for name, _ in names}
    ]
    if not selected:
        return source
    if len(selected) != 3 or module.body[-3:] != selected:
        raise AssertionError("PLAT01 report helpers are one complete final extension")
    for node, (name, parameters) in zip(selected, names):
        expected = ast.parse(f"def {name}({', '.join(parameters)}):\n    pass\n").body[0]
        if (
            node.name != name or ast_dump(node.args) != ast_dump(expected.args)
            or node.decorator_list or node.returns is not None
            or node.type_comment is not None or node.type_params
        ):
            raise AssertionError("PLAT01 report helper has an undeclared invocation effect")
        if any(isinstance(child, (ast.Global, ast.Nonlocal, ast.Import, ast.ImportFrom))
               for child in ast.walk(node)):
            raise AssertionError("PLAT01 report helpers do not mutate or import global authority")
    lines = source.splitlines(keepends=True)
    start = selected[0].lineno - 1
    if (
        not source.startswith("import re\n") or source.count("import re\n") != 1
        or lines[start - 2:start] != ["\n", "\n"]
    ):
        raise AssertionError("PLAT01 report import and function boundary are not exact")
    return "".join(lines[:start - 2])[len("import re\n"):]


def project_runner_source(relative, source):
    from tests.host.qemu_transcript_source import project_transcript_source

    source = project_transcript_source(relative, source)
    if relative == "tools/qemu_smoke_faults.py":
        return _project_runner_faults(source)
    if relative == "tools/qemu_smoke_arguments.py":
        return _project_exact_changes(source, ((RUNNER_ARGUMENT_ADDITION, ""),), "runner arguments")
    if relative == "tools/qemu_smoke_outcome.py":
        return _project_exact_changes(source, (
            ("    _has_diagnostic_history,\n    _has_diagnostic_forwarding,\n", ""),
            ("    require_diagnostic_history=None,\n    require_diagnostic_forwarding=False,\n", ""),
            (RUNNER_RESULT_ADDITION, "    return True\n"),
        ), "runner outcomes")
    if relative == "tools/qemu_smoke_cli.py":
        return _project_exact_changes(source, ((
            "        require_diagnostic_history=arguments.require_diagnostic_history,\n"
            "        require_diagnostic_forwarding=arguments.require_diagnostic_forwarding,\n",
            "",
        ),), "runner forwarding")
    return source


def project_diagnostic_external(relative, raw):
    if relative in (
        "tools/qemu_smoke_faults.py", "tools/qemu_smoke_arguments.py",
        "tools/qemu_smoke_outcome.py", "tools/qemu_smoke_cli.py",
    ):
        return project_runner_source(relative, raw.decode("utf-8")).encode("utf-8")
    return raw


def projected_runner_digest(path):
    from tools.validation_inputs import UnsupportedContext

    path = Path(path)
    try:
        source = project_runner_source("tools/" + path.name, path.read_text(encoding="utf-8"))
        return hashlib.sha256(source.encode("utf-8")).hexdigest()
    except (AssertionError, SyntaxError, UnicodeError, ValueError) as error:
        raise UnsupportedContext("QEMU argument builder is outside the reviewed closure") from error


def project_diagnostic_context(source):
    return _project_exact_changes(source, (
        ("from tests.host.diagnostic_history_source import projected_runner_digest\n", ""),
        (
            "or projected_runner_digest(path) != expected:",
            "or _file_digest(path) != expected:",
        ),
    ), "runner source guard")


def project_runner_options(observed):
    from tests.host.qemu_transcript_source import project_transcript_options

    observed = project_transcript_options(observed)
    if any(
        type(row) not in (tuple, list) or len(row) != 2
        or type(row[0]) is not int or type(row[1]) is not str
        for row in observed
    ):
        raise AssertionError("PLAT01 runner options require actual parser observations")
    names = tuple(name for _, name in observed)
    added = ("--require-diagnostic-history", "--require-diagnostic-forwarding")
    if names.count("--require-panic-report") != 1 or any(names.count(name) != 1 for name in added):
        raise AssertionError("PLAT01 runner options are missing or duplicated")
    anchor = names.index("--require-panic-report")
    if names[anchor + 1:anchor + 3] != added or any(
        observed[index][0] != observed[anchor][0] for index in (anchor + 1, anchor + 2)
    ):
        raise AssertionError("PLAT01 runner options moved or borrowed another parser")
    return (*observed[:anchor + 1], *observed[anchor + 3:])


def project_diagnostic_gate(relative, source):
    try:
        source = project_terminal_gate(relative, source)
    except ValueError as error:
        raise AssertionError(str(error)) from error
    if relative == "tests/host/kernel_layout_source.py":
        return project_kernel_gate_adapter(source)
    if relative == "tests/host/test_kernel_directory_layout.py":
        return project_directory_gate_adapter(source)
    if relative == "tests/host/test_kernel_bootstrap_source_layout.py":
        return _project_exact_changes(source, (
            ("from tests.host.diagnostic_history_source import project_boot_diagnostic_seed\n", ""),
            ("branch_records(project_boot_diagnostic_seed(bodies))", "branch_records(bodies)"),
        ), "boot seed requirement")
    if relative in (
        "tests/host/test_grant_data_plane_source_layout.py",
        "tests/host/test_kernel_object_source_layout.py",
        "tests/host/test_tty_source_layout.py",
    ):
        return project_stable_gate_adapter(relative, source)
    if relative == "tests/host/test_qemu_runner_source_layout.py":
        return _project_exact_changes(source, (
            ("from tests.host.diagnostic_history_source import project_runner_options\n", ""),
            (
                "tuple(name for _, name in project_runner_options(observed))",
                "tuple(name for _, name in observed)",
            ),
        ), "runner option gate")
    return source


def _recipe_admission():
    return _literal_declaration(Path(__file__).resolve().parents[2], AUTHORITY_PATH, "RECIPE_ADMISSION")


def render_diagnostic_native_recipe():
    recipe = _recipe_admission()
    target = recipe["native_target"]
    lines = [
        "    add_executable(",
        "        " + target,
        *("        " + name for name in recipe["native_sources"]),
        "    )",
        f'    target_include_directories({target} PRIVATE "${{CMAKE_CURRENT_SOURCE_DIR}}" "${{CMAKE_CURRENT_SOURCE_DIR}}/include")',
        f"    target_compile_features({target} PRIVATE c_std_17)",
        f"    target_compile_options({target} PRIVATE -Wall -Wextra -Werror -Wpedantic -Wmissing-prototypes -Wstrict-prototypes)",
        '    if(MICROS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")',
        f"        target_compile_options({target} PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)",
        f"        target_link_options({target} PRIVATE -fsanitize=address,undefined)",
        "    endif()",
        f'    target_compile_definitions({target} PRIVATE {recipe["native_definition"]})',
        f'    add_test(NAME {recipe["native_case"]} COMMAND {target})',
        *(f"    add_dependencies({tier} {target})" for tier in recipe["native_tiers"]),
        "",
    ]
    return "\n".join(lines) + "\n"


def _project_qemu_target(source, target, added, original):
    beginning = f"    add_custom_target(\n        {target}\n"
    if source.count(beginning) != 1:
        raise AssertionError("PLAT01 has no unique existing QEMU target")
    start = source.index(beginning)
    end = source.index("\n    )", start)
    block = source[start:end]
    if block.count(added) != 1:
        raise AssertionError("PLAT01 QEMU requirement moved or changed its mode")
    return source[:start] + block.replace(added, original, 1) + source[end:]


def project_diagnostic_cmake(source):
    if not any(marker in source for marker in ("diagnostic_history", "tty_diagnostic", "--require-diagnostic")):
        return source
    recipe = _recipe_admission()
    native = render_diagnostic_native_recipe()
    stack_include = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
    panic_source = "    kernel/diagnostic/panic.c\n"
    kernel_sources = "".join("    " + name + "\n" for name in recipe["kernel_sources"])
    tty_control = "            servers/tty/tty_control.c\n"
    claim_wait = "                    tests/qemu/tty_service_claim_wait.S\n"
    fixture = (
        "if(MICROS_BUILD_PANIC_TEST OR MICROS_BUILD_TTY_SERVICE_TEST)\n"
        "    target_sources(micros PRIVATE tests/qemu/diagnostic_history_kernel.c)\n"
        "endif()\n\n"
    )
    changes = (
        (native + stack_include, stack_include),
        (panic_source + kernel_sources, panic_source),
        (tty_control + "            " + recipe["tty_source"] + "\n", tty_control),
        (claim_wait + "                    " + recipe["tty_fixture"] + "\n", claim_wait),
        (fixture + "if(MICROS_BUILD_UART_CONSOLE_TEST)\n", "if(MICROS_BUILD_UART_CONSOLE_TEST)\n"),
    )
    if any(source.count(added) != 1 for added, _ in changes):
        raise AssertionError("PLAT01 recipe has incomplete, misplaced or repeated source additions")
    source = _project_exact_changes(source, changes, "root recipe")
    panic_requirement = "            --require-panic-report\n"
    for target, mode in recipe["panic_requirements"]:
        added = panic_requirement + "            --require-diagnostic-history\n            " + mode + "\n"
        source = _project_qemu_target(source, target, added, panic_requirement)
    objects_requirement = "            --require-objects-ready\n"
    source = _project_qemu_target(
        source, recipe["tty_requirement"][0],
        objects_requirement + "            --require-diagnostic-forwarding\n",
        objects_requirement,
    )
    if hashlib.sha256(source.encode("utf-8")).hexdigest() != recipe["reference_cmake_sha256"]:
        raise AssertionError("PLAT01 does not reconstruct the complete independently pinned root recipe")
    return source


FATAL_SOURCE_CONTRACTS = {
    ("tests/qemu/diagnostic_history_kernel.c", "micros_diagnostic_test_observe_thr"): """
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
void micros_diagnostic_test_observe_thr(void)
{
    if (diagnostic_thr_writes != UINT64_MAX) {
        ++diagnostic_thr_writes;
    }
}
#endif
""",
    ("arch/riscv64/uart.c", "uart_panic_mode"): """
static bool uart_panic_mode(void)
{
    return uart_console_state.owner == MICROS_UART_CONSOLE_OWNER_PANIC;
}
""",
    ("arch/riscv64/uart.c", "uart_write"): """
void uart_write(const char *text)
{
    if (uart_panic_mode()) {
        uart_write_direct(text);
        return;
    }
    uart_write_record(text);
}
""",
    ("arch/riscv64/uart.c", "uart_write_hex64"): """
void uart_write_hex64(uint64_t value)
{
    if (uart_panic_mode()) {
        uart_write_hex64_direct(value);
        return;
    }
    uart_write_hex64_record(value);
}
""",
    ("kernel/diagnostic/diagnostic_history_core.c", "micros_diagnostic_history_valid"): """
bool micros_diagnostic_history_valid(const struct micros_diagnostic_history *history)
{
    return (
        history != NULL
        && history->next < MICROS_DIAGNOSTIC_RING_CAPACITY
        && history->size <= MICROS_DIAGNOSTIC_RING_CAPACITY
        && history->tail_position <= MICROS_DIAGNOSTIC_TAIL_PAYLOAD
        && (history->size == MICROS_DIAGNOSTIC_RING_CAPACITY || history->next == history->size)
        && history->tail_position == (
            history->size < MICROS_DIAGNOSTIC_TAIL_PAYLOAD
                ? history->size : MICROS_DIAGNOSTIC_TAIL_PAYLOAD
        )
        && history->tail[history->tail_position] == 0
        && history->tail[MICROS_DIAGNOSTIC_TAIL_CAPACITY - 1] == 0
    );
}
""",
    ("kernel/diagnostic/diagnostic_history.c", "micros_diagnostic_panic_size"): """
int32_t micros_diagnostic_panic_size(void)
{
    return micros_diagnostic_history_valid(&diagnostic_history)
        ? (int32_t)diagnostic_history.tail_position : -1;
}
""",
    ("kernel/diagnostic/diagnostic_history.c", "micros_diagnostic_panic_byte_prevalidated"): """
uint8_t micros_diagnostic_panic_byte_prevalidated(uint32_t index)
{
    return diagnostic_history.tail[index];
}
""",
    ("kernel/diagnostic/panic_history.c", "write_history_byte"): r"""
static void write_history_byte(uint8_t value)
{
    static const char digits[] = "0123456789abcdef";
    char pair[3];

    pair[0] = digits[value >> 4];
    pair[1] = digits[value & 15];
    pair[2] = '\0';
    uart_write(pair);
}
""",
    ("kernel/diagnostic/panic_history.c", "micros_diagnostic_panic_report"): r"""
void micros_diagnostic_panic_report(void)
{
    int32_t size = micros_diagnostic_panic_size();
    uint32_t offset = 0;

    if (size < 0) {
        uart_write("MICROS_DIAGNOSTIC_HISTORY state=unavailable bytes=0x0000000000000000\n");
        return;
    }
    uart_write("MICROS_DIAGNOSTIC_HISTORY state=retained bytes=");
    uart_write_hex64((uint64_t)(uint32_t)size);
    uart_write("\n");
    while (offset < (uint32_t)size) {
        uint32_t chunk = 0;

        uart_write("MICROS_DIAGNOSTIC_HISTORY_DATA offset=");
        uart_write_hex64(offset);
        uart_write(" data=");
        while (chunk < 32 && offset < (uint32_t)size) {
            uint8_t byte = micros_diagnostic_panic_byte_prevalidated(offset);

            write_history_byte(byte);
            ++offset;
            ++chunk;
        }
        uart_write("\n");
    }
}
""",
}
HISTORY_LAYOUT = """
struct micros_diagnostic_history {
    uint8_t ring[MICROS_DIAGNOSTIC_RING_CAPACITY];
    uint8_t tail[MICROS_DIAGNOSTIC_TAIL_CAPACITY];
    uint32_t next;
    uint32_t size;
    uint32_t tail_position;
};
"""
DIAGNOSTIC_CONSTANTS = (
    ("MICROS_DIAGNOSTIC_RING_CAPACITY", "10000"),
    ("MICROS_DIAGNOSTIC_TAIL_CAPACITY", "2000"),
    ("MICROS_DIAGNOSTIC_TAIL_PAYLOAD", "1998"),
    ("MICROS_DIAGNOSTIC_HEADER_SIZE", "16"),
    ("MICROS_DIAGNOSTIC_SNAPSHOT_SIZE", "10016"),
    ("MICROS_DIAGNOSTIC_VERSION", "1"),
)
THR_OBSERVATION = (
    "#ifdef MICROS_BUILD_TTY_SERVICE_TEST\n"
    "    micros_diagnostic_test_observe_thr();\n"
    "#endif\n"
)
THR_OBSERVATION_HEADER = (
    "#ifdef MICROS_BUILD_TTY_SERVICE_TEST\n"
    '#include "tests/qemu/diagnostic_history_kernel.h"\n'
    "#endif\n"
)


def checked_fatal_function(unit, name, source):
    from tests.host.c_source_records import function_records, tokens

    template = FATAL_SOURCE_CONTRACTS.get((unit, name))
    if template is None:
        raise AssertionError("PLAT01 has no new source/effect contract for this function")
    expected = function_records(template)[name]
    actual = function_records(source).get(name)
    if (
        actual is None or actual.guards != expected.guards
        or tokens(actual.signature) != tokens(expected.signature)
        or tokens(actual.body) != tokens(expected.body)
    ):
        raise AssertionError("PLAT01 fatal helper differs from its fixed operation contract")
    return actual


def diagnostic_header_source():
    return (
        "#ifndef MICROS_DIAGNOSTIC_H\n#define MICROS_DIAGNOSTIC_H\n\n"
        "#include <stdint.h>\n\n"
        + "".join("#define " + name + " UINT32_C(" + value + ")\n" for name, value in DIAGNOSTIC_CONSTANTS)
        + "#define MICROS_KERNEL_EVENT_DIAGNOSTIC UINT64_C(0x10)\n\n#endif\n"
    )


def diagnostic_no_callback_scope(root, unit, callback_types):
    from tests.host.c_source_records import c_lexical_text

    members = {
        *SOURCE_PROJECTION["new_kernel_members"], "include/micros/diagnostic.h",
        "servers/tty/tty_diagnostic.h", "servers/tty/tty_diagnostic.c",
    }
    if unit not in members or not unit.endswith(".c"):
        return False
    expression = r"\b(?:" + "|".join(re.escape(name) for name in callback_types) + r")\b"
    pending, visited = [unit], set()
    while pending:
        current = pending.pop()
        if current in visited:
            continue
        visited.add(current)
        source = regular_source(Path(root), current).read_text(encoding="utf-8")
        lexical = c_lexical_text(source)
        if re.search(expression, lexical) or re.search(r"\(\s*\*[^()]*\)\s*\(", lexical):
            raise AssertionError("PLAT01 new diagnostic source introduces a callback carrier or type")
        for include in re.findall(r'(?m)^\s*#\s*include\s+[<"]([^>"]+)[>"]', source):
            if Path(include).is_absolute() or ".." in Path(include).parts:
                raise AssertionError("PLAT01 diagnostic include leaves its explicit namespace")
            choices = (include, str(Path(current).parent / include), "include/" + include)
            matches = tuple(dict.fromkeys(name for name in choices if name in members))
            if len(matches) > 1:
                raise AssertionError("PLAT01 diagnostic header has ambiguous current ownership")
            pending.extend(matches)
    return True


def effective_thr_body(body, enabled):
    store = "    uart0[UART_THR] = (uint8_t)character;\n"
    if type(enabled) is not bool or body.count(store + THR_OBSERVATION) != 1:
        raise AssertionError("PLAT01 THR observation is not immediately after its actual store")
    return body.replace(
        THR_OBSERVATION, "    micros_diagnostic_test_observe_thr();\n" if enabled else "", 1,
    )


def _remove_c_function(source, name):
    from tests.host.c_source_records import function_records

    record = function_records(source).get(name)
    if record is None or record.guards:
        raise AssertionError("PLAT01 source extension lacks an ordinary uniquely owned definition")
    fragment = record.signature + "{" + record.body + "}"
    if source.count(fragment) != 1:
        raise AssertionError("PLAT01 source function has an ambiguous complete extent")
    return source.replace(fragment, "", 1)


def project_fatal_uart(source):
    from tests.host.c_source_records import function_records

    records = function_records(source)
    for name in ("uart_panic_mode", "uart_write", "uart_write_hex64"):
        checked_fatal_function("arch/riscv64/uart.c", name, source)
    character = records["uart_write_character"]
    effective_thr_body(character.body, False)
    source = source.replace(THR_OBSERVATION, "", 1)
    for name in (
        "uart_panic_mode", "uart_write", "uart_write_hex64",
        "uart_write_record", "uart_write_hex64_record",
    ):
        source = _remove_c_function(source, name)
    if "uart_write_logical_byte" in records:
        source = _remove_c_function(source, "uart_write_logical_byte")
    for name in ("uart_write_direct", "uart_write_hex64_direct"):
        original = name.removesuffix("_direct")
        declaration = "static void " + name + "("
        if source.count(declaration) != 1:
            raise AssertionError("PLAT01 direct writer lacks its exact private-name transport")
        source = source.replace(declaration, "void " + original + "(", 1)
    include = '#include "kernel/diagnostic/diagnostic_history.h"\n'
    if source.count(include) != 1 or source.count(THR_OBSERVATION_HEADER) != 1:
        raise AssertionError("PLAT01 UART source includes do not match their fixed boundaries")
    source = source.replace(include, "", 1).replace(THR_OBSERVATION_HEADER, "", 1)
    quiesce = function_records(source)["uart_console_begin_quiesce_prevalidated"]
    handoff = "    micros_diagnostic_handoff();\n"
    if quiesce.body.count(handoff) != 1 or not quiesce.body.rstrip().endswith(handoff.strip()):
        raise AssertionError("PLAT01 handoff cursor is not captured after complete quiescence")
    return source.replace(handoff, "", 1)


def project_fatal_report(source):
    from tests.host.c_source_records import function_records

    include = '#include "kernel/diagnostic/diagnostic_history.h"\n'
    call = "    micros_diagnostic_panic_report();\n"
    record = function_records(source).get("micros_panic_report")
    if (
        record is None or source.count(include) != 1 or source.count(call) != 1
        or record.body.count(call + "    uart_flush();\n") != 1
        or record.body.index(call) > record.body.index("    uart_flush();\n")
    ):
        raise AssertionError("PLAT01 history is not after the core/trap records and before first flush")
    return source.replace(include, "", 1).replace(call, "", 1)


TTY_INITIALIZATION = """        || !micros_tty_diagnostic_initialize(
            &tty_diagnostic, micros_bootstrap_service_config.self_endpoint
        )
"""
TTY_STARTUP_PROBE = """#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    uint64_t initial_events;

    tty_service_clear_bytes(&message, sizeof(message));
    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &message) != MICROS_SYSCALL_ABI_OK
        || !micros_tty_diagnostic_notification(&message, &initial_events)
        || initial_events != (MICROS_KERNEL_EVENT_TTY_IRQ | MICROS_KERNEL_EVENT_DIAGNOSTIC)
        || !handle_irq(&bus, &io)
    ) {
        __builtin_trap();
    }
    while (true) {
        enum micros_diagnostic_test_transfer_result transfer = micros_diagnostic_test_tty_transfer(&tty_diagnostic);

        if (transfer == MICROS_DIAGNOSTIC_TEST_TRANSFER_COMPLETE) {
            break;
        }
        tty_service_clear_bytes(&message, sizeof(message));
        if (
            transfer != MICROS_DIAGNOSTIC_TEST_TRANSFER_DEFERRED
            || micros_runtime_receive(MICROS_ENDPOINT_ANY, &message) != MICROS_SYSCALL_ABI_OK
            || !micros_tty_diagnostic_notification(&message, &initial_events)
            || initial_events != MICROS_KERNEL_EVENT_TTY_IRQ
            || !handle_irq(&bus, &io)
        ) {
            __builtin_trap();
        }
    }
#endif
"""
TTY_DIAGNOSTIC_DISPATCH = """                !micros_tty_diagnostic_notification(&message, &diagnostic_events)
                || (
                    (diagnostic_events & MICROS_KERNEL_EVENT_TTY_IRQ) != 0
                    && !handle_irq(&bus, &io)
                )
"""
TTY_DIAGNOSTIC_PROGRESS = """        if (
            !micros_tty_diagnostic_progress(
                &tty_diagnostic, &tty_service.terminal,
                (diagnostic_events & MICROS_KERNEL_EVENT_DIAGNOSTIC) != 0, &diagnostic_effects
            )
            || micros_tty_uart_apply_effects(
                &tty_uart, &bus, &tty_service.terminal, &diagnostic_effects
            ) != MICROS_TTY_UART_OK
        ) {
            __builtin_trap();
        }
"""
TTY_OWNERSHIP_ANCHOR = (
    "        || micros_tty_service_commit_ownership(&tty_service)\n"
    "            != MICROS_TTY_SERVICE_OK\n"
)
TTY_CALL_ANCHOR = """        } else if (!handle_call(&bus, &io, &message)) {
            __builtin_trap();
        }
"""
TTY_CONSTRUCTOR_CHANGES = (
    (
        "    struct micros_ipc_message message;\n"
        "    struct micros_tty_effects diagnostic_effects;\n",
        "    struct micros_ipc_message message;\n",
    ),
    (TTY_OWNERSHIP_ANCHOR + TTY_INITIALIZATION, TTY_OWNERSHIP_ANCHOR),
    ("    }\n\n" + TTY_STARTUP_PROBE + "\n    for (;;) {\n", "    }\n\n    for (;;) {\n"),
    ("    for (;;) {\n        uint64_t diagnostic_events = 0;\n", "    for (;;) {\n"),
    (
        TTY_DIAGNOSTIC_DISPATCH,
        "                !notification_is_tty_irq(&message)\n"
        "                || !handle_irq(&bus, &io)\n",
    ),
    (TTY_CALL_ANCHOR + TTY_DIAGNOSTIC_PROGRESS, TTY_CALL_ANCHOR),
)


def project_diagnostic_callback_source(unit, source):
    from tests.host.c_source_records import function_records

    if unit != "servers/tty/tty_service.c":
        return source
    record = function_records(source).get("micros_service_main")
    if record is None:
        raise AssertionError("TTY diagnostic integration has no actual service constructor")
    body = _project_exact_changes(record.body, TTY_CONSTRUCTOR_CHANGES, "TTY constructor")
    original = record.signature + "{" + record.body + "}"
    if source.count(original) != 1:
        raise AssertionError("TTY diagnostic constructor has an ambiguous extent")
    return source.replace(original, record.signature + "{" + body + "}", 1)


def diagnostic_kernel_members(root):
    from tests.host.c_source_records import function_records

    members = SOURCE_PROJECTION["new_kernel_members"]
    portable_headers = {
        "kernel/diagnostic/diagnostic_history_core.h",
        "kernel/diagnostic/diagnostic_control_core.h",
    }
    errors = []
    for relative in members:
        try:
            path = regular_source(Path(root), relative)
            if path.stat().st_mode & 0o777 != 0o644:
                raise ValueError("diagnostic source mode is not ordinary 0644")
            source = path.read_text(encoding="utf-8")
            if relative.endswith(".h") and function_records(source):
                raise ValueError("diagnostic private header contains an implementation")
            for name in re.findall(r'(?m)^\s*#\s*include\s+"([^"]+)"', source):
                if Path(name).is_absolute() or ".." in Path(name).parts or name.endswith((".c", ".S")):
                    raise ValueError("diagnostic source has an escaping or textual implementation include")
                selected = name if "/" in name else str(Path(relative).parent / name)
                if selected.startswith(("servers/", "user/", "tests/host/")):
                    raise ValueError("diagnostic kernel source imports another policy/evidence owner")
                if "_core." in relative and selected.startswith("kernel/") and selected not in portable_headers:
                    raise ValueError("portable diagnostic core gains privileged adapter authority")
        except (ValueError, AssertionError, OSError) as error:
            errors.append(f"{relative}: {error}")
    return members, tuple(errors)


IDENTITY_ADAPTER_IMPORT = (
    "from tests.host.diagnostic_history_authority import identity_required as diagnostic_identity_required\n"
)


def project_stable_gate_adapter(relative, source):
    indent = "            " if relative == "tests/host/test_tty_source_layout.py" else "                "
    read = indent + (
        'value = restored_runner_source().encode() if path == "tools/run_qemu_smoke.py" '
        "else kernel_layout_source.read_bytes_path(ROOT / path)\n"
    )
    skip = (
        indent + "if not diagnostic_identity_required(\n"
        + indent + '    ROOT, "' + relative + ':data-entry:STABLE_HASHES/" + path,\n'
        + indent + "):\n" + indent + "    continue\n"
    )
    changes = (
        (IDENTITY_ADAPTER_IMPORT, ""), (read + skip, read),
    )
    if relative == "tests/host/test_tty_source_layout.py":
        changes += (
            ("from tests.host.diagnostic_history_source import project_diagnostic_native_fixture\n", ""),
            (
                "    return project_diagnostic_native_fixture(path, kernel_layout_source.read_path(ROOT / path))\n",
                "    return kernel_layout_source.read_path(ROOT / path)\n",
            ),
        )
    return _project_exact_changes(source, changes, "stable artifact equality")


def project_directory_gate_adapter(source):
    kernel_read = "            restored = kernel_layout_source.read_logical_bytes(ROOT, logical)\n"
    external_read = "            restored = kernel_layout_source.read_bytes_path(ROOT / relative)\n"
    kernel_skip = (
        "            if not diagnostic_identity_required(\n"
        '                ROOT, "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/" + logical + "/2",\n'
        "            ):\n                continue\n"
    )
    external_skip = (
        "            if not diagnostic_identity_required(\n"
        '                ROOT, "tests/host/kernel_layout_source.py:data-entry:EXTERNAL_SOURCE_FACTS/" + relative + "/1",\n'
        "            ):\n                continue\n"
    )
    return _project_exact_changes(source, (
        (IDENTITY_ADAPTER_IMPORT, ""),
        (kernel_read + kernel_skip, kernel_read),
        (external_read + external_skip, external_read),
    ), "directory equality")


def project_kernel_gate_adapter(source):
    import_line = (
        "from tests.host.diagnostic_history_source import (\n"
        "    diagnostic_kernel_members, project_diagnostic_context, project_diagnostic_external, project_diagnostic_gate,\n"
        ")\n"
    )
    required = (
        "    required = diagnostic_identity_required(\n"
        '        root, "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/" + logical + "/2",\n'
        "    )\n"
        "    return _restored_include_bytes(logical, raw, expected_sha256, transports, required=required)\n"
    )
    external = (
        "        required=diagnostic_identity_required(\n"
        '            REPOSITORY_ROOT, "tests/host/kernel_layout_source.py:data-entry:EXTERNAL_SOURCE_FACTS/" + relative + "/1",\n'
        "        ),\n"
    )
    return _project_exact_changes(source, (
        (IDENTITY_ADAPTER_IMPORT, ""), (import_line, ""),
        (
            "    additional, additional_errors = diagnostic_kernel_members(root)\n"
            "    expected.update(additional)\n    errors.extend(additional_errors)\n",
            "",
        ),
        (required, "    return _restored_include_bytes(logical, raw, expected_sha256, transports)\n"),
        ("    transports,\n    *,\n    required: bool = True,\n", "    transports,\n"),
        (
            "    if type(required) is not bool:\n"
            '        raise AssertionError("source identity policy is not a checked Boolean")\n',
            "",
        ),
        (
            "    if required and hashlib.sha256(restored).hexdigest() != expected_sha256:\n",
            "    if hashlib.sha256(restored).hexdigest() != expected_sha256:\n",
        ),
        (external, ""),
        ("    source = project_diagnostic_context(source)\n", ""),
        ("    source = project_diagnostic_gate(relative, source)\n", ""),
        (
            "    raw = project_diagnostic_external(\n"
            "        relative, project_stack_external(relative, project_external_evidence(relative, path.read_bytes()))\n"
            "    )\n",
            "    raw = project_stack_external(relative, project_external_evidence(relative, path.read_bytes()))\n",
        ),
    ), "kernel directory/source transport")


@lru_cache(maxsize=16)
def _fixed_gate_ast(root, relative):
    from tests.host.ast_evidence import ast_dump

    completed = subprocess.run(
        ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + relative),
        cwd=root, stdin=subprocess.DEVNULL, capture_output=True, check=True, timeout=30,
    )
    return ast_dump(ast.parse(completed.stdout.decode("utf-8")))


def validate_source_adapters(root):
    from tests.host.ast_evidence import ast_dump

    owners = (
        "tests/host/kernel_layout_source.py",
        "tests/host/test_grant_data_plane_source_layout.py",
        "tests/host/test_kernel_object_source_layout.py",
        "tests/host/test_tty_source_layout.py",
        "tests/host/test_kernel_directory_layout.py",
        "tests/host/test_qemu_runner_source_layout.py",
        "tests/host/test_kernel_bootstrap_source_layout.py",
    )
    for relative in owners:
        source = regular_source(Path(root), relative).read_text(encoding="utf-8")
        restored = project_diagnostic_gate(relative, source)
        if ast_dump(ast.parse(restored)) != _fixed_gate_ast(str(root), relative):
            raise ValueError("PLAT01 assertion adapter changed an unrelated original gate node: " + relative)


def project_boot_diagnostic_seed(source):
    try:
        source = project_terminal_boot_bodies(source)
    except ValueError as error:
        raise AssertionError(str(error)) from error
    original = (
        "#ifdef MICROS_BUILD_PANIC_TEST\n"
        '    MICROS_PANIC(boot->hart_id, "intentional-test");\n'
        "#endif"
    )
    added = original.replace(
        '    MICROS_PANIC(boot->hart_id, "intentional-test");',
        '    micros_diagnostic_test_seed_panic();\n    MICROS_PANIC(boot->hart_id, "intentional-test");',
        1,
    )
    if source.count("micros_diagnostic_test_seed_panic(") != 1 or source.count(added) != 1:
        raise AssertionError("PLAT01 requires exactly one seed immediately before the original intentional panic")
    return source.replace(added, original, 1)


VFS_DIAGNOSTIC_BUSY_RETRY = """static bool submit_write_when_writable(
    uint64_t request_id,
    micros_grant_t grant,
    size_t count,
    struct tty_call_result *result
)
{
    uint64_t events;

    for (;;) {
        if (!call_tty(
            MICROS_TTY_MESSAGE_SUBMIT_WRITE,
            request_id,
            grant,
            count,
            result
        )) {
            return false;
        }
        if (result->result != MICROS_TTY_RESULT_BUSY) {
            return true;
        }
        if (result->transferred_count != 0) {
            return false;
        }
        do {
            if (!receive_tty_events(&events)) {
                return false;
            }
        } while ((events & MICROS_TTY_EVENT_WRITABLE) == 0);
    }
}

"""
LEGACY_SUBJECT_CHANGES = {
    "kernel/tests/tty/tty_service_test.c": (
        (
            '#include "tests/qemu/tty_handoff_protocol.h"\n'
            '#include "tests/qemu/diagnostic_history_kernel.h"\n',
            '#include "tests/qemu/tty_handoff_protocol.h"\n',
        ),
        (
            "    micros_diagnostic_test_finish();\n"
            "    MICROS_TEST_STACK_REPORT(); (void)sbi_system_reset(\n",
            "    MICROS_TEST_STACK_REPORT(); (void)sbi_system_reset(\n",
        ),
    ),
    "tests/qemu/tty_service_vfs.c": (
        (
            VFS_DIAGNOSTIC_BUSY_RETRY + "static bool submit_write(\n",
            "static bool submit_write(\n",
        ),
        (
            "        submit_write_when_writable(\n"
            "            request_id,\n            grant,\n            count,\n            &result\n        )\n",
            "        call_tty(\n            MICROS_TTY_MESSAGE_SUBMIT_WRITE,\n"
            "            request_id,\n            grant,\n            count,\n            &result\n        )\n",
        ),
    ),
}
SCHEDULER_PUMP_CHANGES = {
    "kernel/scheduler/scheduler_return.c": (
        (
            '#include "micros/user_execution.h"\n#include "kernel/diagnostic/diagnostic_history.h"\n',
            '#include "micros/user_execution.h"\n',
        ),
        (
            "    micros_diagnostic_notify_pending(hart, frame);\n"
            "    error = micros_hart_plan_user_return(\n",
            "    error = micros_hart_plan_user_return(\n",
        ),
    ),
    "kernel/scheduler/scheduler_idle.c": (
        (
            '#include "micros/timer.h"\n#include "kernel/diagnostic/diagnostic_history.h"\n',
            '#include "micros/timer.h"\n',
        ),
        (
            "    preflight_idle_selection(objects, hart);\n"
            "    micros_diagnostic_notify_pending(hart, NULL);\n",
            "    preflight_idle_selection(objects, hart);\n",
        ),
    ),
}


def validate_legacy_subject(root, relative, source, *, required):
    changes = {**LEGACY_SUBJECT_CHANGES, **SCHEDULER_PUMP_CHANGES}.get(relative)
    if changes is None or type(required) is not bool:
        raise AssertionError("PLAT01 has no legacy integration admission for this subject")
    if required and any(source.count(added) != 1 for added, _ in changes):
        raise AssertionError("PLAT01 legacy subject lacks its complete required diagnostic integration")
    restored = _project_exact_changes(source, changes, "legacy target integration")
    reference = subprocess.run(
        ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + relative),
        cwd=root, stdin=subprocess.DEVNULL, capture_output=True, check=True, timeout=30,
    ).stdout.decode("utf-8")
    if restored != reference:
        raise AssertionError("PLAT01 changed an original legacy case, assertion or failure path: " + relative)


NATIVE_DIAGNOSTIC_DISPATCH_DOUBLE = """uint64_t micros_diagnostic_control_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_tty_control_request *request,
    const struct micros_tty_handoff_runtime_state *state,
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry
)
{
    (void)hart;
    (void)frame;
    (void)context;
    (void)request;
    (void)state;
    (void)bootstrap;
    (void)registry;
    abort();
}

"""


def project_diagnostic_native_fixture(relative, source):
    if relative != "tests/host/tty_control_syscall_test_controller.c":
        return source
    header = '#include "tty_control_syscall_test_internal.h"\n'
    return _project_exact_changes(source, ((
        header + '#include "kernel/diagnostic/diagnostic_history.h"\n\n' + NATIVE_DIAGNOSTIC_DISPATCH_DOUBLE,
        header + "\n",
    ),), "native diagnostic dispatch double")


RETRIGGER_READ_BOUNDARY = """    result = micros_diagnostic_prepare_read(
        &diagnostic_reader, identity, history, request->diagnostic_cursor, &span
    );
    if (result != MICROS_DIAGNOSTIC_CONTROL_OK) {
        return control_result(hart, frame, result);
    }
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    if (!micros_diagnostic_test_before_read(request->diagnostic_cursor)) {
        return (uint64_t)(int64_t)MICROS_SYSCALL_ABI_STATE;
    }
    /* The admitted fixture appends only after the original request preflight. */
    if (micros_diagnostic_prepare_read(
        &diagnostic_reader, identity, history, request->diagnostic_cursor, &span
    ) != MICROS_DIAGNOSTIC_CONTROL_OK) {
        panic_control(hart, frame);
    }
#endif
    micros_diagnostic_copy_prevalidated(&diagnostic_reader, history, &span);
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    micros_diagnostic_test_after_read();
#endif
"""
RETRIGGER_QUEUED_IRQ = """    if (state->handoff.route_phase == MICROS_TTY_ROUTE_IN_SERVICE) {
        const struct micros_endpoint_registry *registry = micros_ipc_runtime_registry();
        const struct micros_endpoint_record *endpoint;

        if (registry == NULL || state->process.slot != MICROS_TTY_PROCESS_SLOT) {
            MICROS_PANIC(0, "diagnostic-test-deferred-irq");
        }
        endpoint = &registry->endpoints[MICROS_TTY_PROCESS_SLOT];
        if (
            state->handoff.claimed_source != 10
            || endpoint->state != MICROS_ENDPOINT_STATE_ACTIVE || endpoint->value != state->endpoint
            || endpoint->owner.slot != state->process.slot
            || endpoint->owner.generation != state->process.generation
            || endpoint->pending_kernel_events != UINT64_C(0x8)
        ) {
            MICROS_PANIC(0, "diagnostic-test-deferred-irq");
        }
        return false;
    }
"""
RETRIGGER_FIRST_PROBE = """    if (
        diagnostic_fixture.phase != 1 || cursor != diagnostic_fixture.cursor
        || state->handoff.deadline_armed || state->handoff.deadline != 0
    ) {
        MICROS_PANIC(0, "diagnostic-test-reader");
    }
"""
RETRIGGER_IDLE = """    if (state->handoff.route_phase != MICROS_TTY_ROUTE_IDLE || state->handoff.claimed_source != 0) {
        MICROS_PANIC(0, "diagnostic-test-reader");
    }
    diagnostic_fixture.phase = 2;
"""


def validate_retrigger_read_adapter(source):
    from tests.host.c_source_records import function_records

    record = function_records(source).get("read_history")
    if record is None or record.guards or record.body.count(RETRIGGER_READ_BOUNDARY) != 1:
        raise AssertionError("PLAT01 deferral must follow full preflight and precede every test copy/after-copy")
    for name in (
        "micros_diagnostic_test_before_read", "micros_diagnostic_copy_prevalidated",
        "micros_diagnostic_test_after_read",
    ):
        if len(re.findall(r"\b" + name + r"\s*\(", record.body)) != 1:
            raise AssertionError("PLAT01 read adapter repeats an admission or copy site")


def validate_retrigger_kernel_fixture(source):
    from tests.host.c_source_records import function_records, tokens

    record = function_records(source).get("micros_diagnostic_test_before_read")
    if (
        record is None or record.guards != ("ifdef MICROS_BUILD_TTY_SERVICE_TEST",)
        or tokens(record.signature) != tokens("bool micros_diagnostic_test_before_read(uint32_t cursor)")
        or record.body.count(RETRIGGER_FIRST_PROBE + RETRIGGER_QUEUED_IRQ + RETRIGGER_IDLE) != 1
        or record.body.count("return false;") != 1
        or record.body.count("diagnostic_fixture.phase = 2;") != 1
    ):
        raise AssertionError("PLAT01 first-probe admission lost its exact readiness, cursor, queued IRQ or idle guard")
