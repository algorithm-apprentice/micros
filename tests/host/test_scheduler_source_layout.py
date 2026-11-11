"""Current scheduler source, state, model and build ownership."""

from pathlib import Path
import re
import unittest
from unittest.mock import patch

from tests.host.build_source_records import cmake_command, cmake_if_blocks
from tests.host.c_source_records import (
    Function, c_declaration_text, c_function_definitions, c_lexical_text, compact,
    file_scope_objects, function_records, matching, matching_lexical, tokens,
)
from tests.host.source_snapshot import source_bytes
from tools import validation_plan


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]

STRUCTURAL_TEST_PATH = "tests/host/test_scheduler_source_layout.py"

SOURCE_SETS = {
    "MICROS_SCHEDULER_CORE_SOURCES": (
        'kernel/scheduler/scheduler_core.c',
        'kernel/scheduler/scheduler_core_queue.c',
        'kernel/scheduler/scheduler_core_validate.c',
        'kernel/scheduler/scheduler_core_policy.c',
        'kernel/scheduler/scheduler_core_ipc.c',
        'kernel/scheduler/scheduler_core_ipc_transition.c',
        'kernel/scheduler/scheduler_core_return_plan.c',
        'kernel/scheduler/scheduler_core_return.c',
        'kernel/scheduler/scheduler_core_accounting.c',
    ),
    "MICROS_SCHEDULER_RUNTIME_SOURCES": (
        'kernel/scheduler/scheduler.c',
        'kernel/scheduler/scheduler_context.c',
        'kernel/scheduler/scheduler_completion.c',
        'kernel/scheduler/scheduler_return.c',
        'kernel/scheduler/scheduler_interrupt.c',
        'kernel/scheduler/scheduler_idle.c',
        'kernel/tests/scheduler/scheduler_test_support.c',
    ),
    "MICROS_SCHEDULER_QEMU_TEST_SOURCES": (
        'kernel/tests/scheduler/scheduler_test.c',
        'kernel/tests/scheduler/scheduler_test_state.c',
        'kernel/tests/scheduler/scheduler_test_fixture.c',
        'kernel/tests/scheduler/scheduler_test_completion.c',
        'kernel/tests/scheduler/scheduler_test_interrupt.c',
        'kernel/tests/scheduler/scheduler_test_trap.c',
        'kernel/tests/scheduler/scheduler_test_finish.c',
    ),
    "MICROS_SCHEDULER_INVALID_QEMU_TEST_SOURCES": (
        'kernel/tests/scheduler/scheduler_invalid_test.c',
        'kernel/tests/scheduler/scheduler_invalid_test_state.c',
        'kernel/tests/scheduler/scheduler_invalid_test_fixture.c',
        'kernel/tests/scheduler/scheduler_invalid_test_observation.c',
    ),
    "MICROS_SCHEDULER_HOST_TEST_SOURCES": (
        "tests/host/scheduler_test.c",
        "tests/host/scheduler_test_fixture.c",
        "tests/host/scheduler_queue_test.c",
        "tests/host/scheduler_return_test.c",
        "tests/host/scheduler_accounting_test.c",
    ),
    "MICROS_SCHEDULER_MODEL_HOST_TEST_SOURCES": (
        "tests/host/scheduler_model_test.c",
        "tests/host/scheduler_model_fixture.c",
        "tests/host/scheduler_model_reference.c",
        "tests/host/scheduler_model_projection.c",
        "tests/host/scheduler_model_prelude.c",
        "tests/host/scheduler_model_operations.c",
    ),
}

PRIVATE_HEADERS = (
    'kernel/scheduler/scheduler_core_private.h',
    'kernel/scheduler/scheduler_runtime_internal.h',
    'kernel/tests/scheduler/scheduler_test_internal.h',
    'kernel/tests/scheduler/scheduler_invalid_test_internal.h',
    "tests/host/scheduler_test_internal.h",
    "tests/host/scheduler_model_internal.h",
)

FAMILY_PREFIXES = (
    "scheduler_core_", "scheduler_runtime_", "scheduler_test_",
    "scheduler_invalid_test_", "scheduler_host_", "scheduler_model_",
)

ALL_SOURCES = tuple(path for paths in SOURCE_SETS.values() for path in paths)

RUNTIME_FIELDS = """
bool scheduler_initialized; bool scheduler_timer_enabled;
bool scheduler_start_timer_prepared; uint64_t scheduler_preemption_interval;
struct micros_user_context selected_context; uint64_t selected_root;
uint64_t scheduler_kernel_root;
"""

NORMAL_FIELDS = """
struct micros_process_handle processes[2]; struct micros_thread_handle threads[2];
micros_endpoint_t endpoints[2]; struct micros_endpoint_registry *registry;
struct micros_user_context prepared_contexts[2]; uint64_t code_physical[2];
uint64_t counter_physical[2]; uint64_t stack_physical[2];
uintptr_t kernel_stack_bottom[2]; uintptr_t kernel_stack_top[2];
uint64_t baseline_owned; uint64_t baseline_free; uint64_t previous_counter[2];
uint64_t ecall_count[2]; uint64_t timer_trap_count; uint64_t switch_count;
size_t last_running; bool supervisor_returned; bool start_boundary_observed;
bool start_completion_observed; bool idle_requested; bool idle_woke_thread;
bool spurious_idle_bypassed; uint64_t selector_entry_count;
uint64_t selector_entries_before_spurious; uint64_t kernel_ticks_before_start;
struct micros_kernel_objects start_snapshot; uint64_t start_satp_snapshot;
bool start_sie_snapshot; bool start_stie_snapshot; uint64_t start_timer_attempts;
enum test_completion_kind expected_completion[2];
uint64_t expected_completion_result[2]; struct micros_ipc_message expected_message;
bool message_completion_deferred; bool message_completion_observed;
bool captured_completion_observed; bool error_completion_observed;
bool error_completion_staged; bool idle_completion_observed;
volatile enum test_external_origin external_origin;
bool user_external_started; bool user_external_observed;
bool user_external_return_observed; bool supervisor_external_observed;
bool idle_external_started; bool idle_external_observed;
bool idle_external_completion_observed;
"""

INVALID_FIELDS = """
struct micros_process_handle processes[2]; struct micros_thread_handle threads[2];
uint64_t code_physical[2]; uint64_t counter_physical[2]; uint64_t stack_physical[2];
uintptr_t kernel_stack_bottom[2]; uintptr_t kernel_stack_top[2];
struct micros_kernel_objects timer_objects_snapshot;
struct micros_kernel_objects scheduler_snapshot_candidate;
struct micros_trap_frame timer_frame_snapshot; uint64_t timer_satp_snapshot;
"""

NATIVE_FIELDS = """
struct micros_kernel_objects objects; struct micros_process_handle process;
struct micros_thread_handle threads[8]; struct micros_hart_handle harts[2];
"""

CONTEXTS = (
    None,
    ("scheduler_runtime_context", 'kernel/scheduler/scheduler_context.c', RUNTIME_FIELDS),
    ("scheduler_test_context", 'kernel/tests/scheduler/scheduler_test_state.c', NORMAL_FIELDS),
    ("scheduler_invalid_test_context", 'kernel/tests/scheduler/scheduler_invalid_test_state.c', INVALID_FIELDS),
    ("scheduler_host_fixture", "tests/host/scheduler_test_fixture.c", NATIVE_FIELDS),
    ("scheduler_model_fixture", "tests/host/scheduler_model_fixture.c", NATIVE_FIELDS),
)

CORE_CONSUMERS = (
    "kernel_objects_test", "frame_ownership_test", "scheduler_test",
    "endpoint_test", "ipc_model_test", "bootstrap_control_test", "pm_test", "micros",
)

CASE_LABELS = (
    "priority queues preserve tail order", "runtime flags follow zero boundary",
    "policy replacement is atomic", "current selection repairs preemption",
    "quantum rotation retains nonpreemptible current",
    "current block and stale plan are atomic", "accounting owners are separate",
    "trap accounting uses minimal route", "corrupt queue state is rejected",
    "seeded two-hart model", "current IPC scheduler guard",
)

COVERAGE_FIELDS = (
    "preempt_higher", "enqueue_same", "enqueue_lower", "remove_middle",
    "flag_first", "flag_additional", "flag_partial_clear", "flag_final_clear",
    "return_keep", "return_switch", "return_idle", "quantum_renew",
    "thread_to_kernel", "kernel_to_thread", "kernel_to_idle", "idle_to_kernel",
    "policy_runnable", "policy_blocked",
)

def object_definitions(text: str) -> tuple[str, ...]:
    return tuple(
        declaration for declaration in file_scope_objects(text)
        if not (
            re.match(r"\s*extern\b", declaration)
            and "=" not in c_lexical_text(declaration)
        )
    )

def call_sites(text: str) -> tuple[tuple[str, str, int, int], ...]:
    lexical = c_declaration_text(text)
    result = []
    for match in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", lexical):
        name = match.group(1)
        if name in {"if", "for", "while", "switch", "sizeof", "_Alignof"}:
            continue
        open_at = lexical.find("(", match.start())
        end = matching_lexical(lexical, open_at)
        result.append((name, text[open_at + 1:end], match.start(), end + 1))
    return tuple(result)

def require_order(text: str, *fragments: str) -> None:
    selected = compact(text)
    offset = 0
    for fragment in fragments:
        expected = compact(fragment)
        position = selected.find(expected, offset)
        if position < 0:
            raise AssertionError(f"missing/reordered source fact: {fragment}")
        offset = position + len(expected)

def source_texts() -> dict[str, str]:
    return {
        path: source_bytes(REPOSITORY_ROOT, REPOSITORY_ROOT / path).decode()
        for path in (*ALL_SOURCES, *PRIVATE_HEADERS)
        if (REPOSITORY_ROOT / path).is_file()
    }

def family_records(texts: dict[str, str], family: int) -> dict[str, Function]:
    result = {}
    for path in tuple(SOURCE_SETS.values())[family]:
        for name, record in function_records(texts.get(path, "")).items():
            if name in result:
                raise AssertionError(f"duplicate definition in family: {name}")
            result[name] = record
    return result

def struct_body(text: str, name: str) -> str:
    matches = tuple(re.finditer(rf"\bstruct\s+{re.escape(name)}\s*\{{", c_lexical_text(text)))
    if len(matches) != 1:
        raise AssertionError(f"expected one complete struct {name}")
    start = text.find("{", matches[0].start())
    return text[start + 1:matching(text, start, "{", "}")]

class SchedulerSourceLayoutTest(unittest.TestCase):
    def test_cmake_exact_ordered_sets_and_every_existing_consumer(self) -> None:
        for name, paths in SOURCE_SETS.items():
            with self.subTest(source_set=name):
                block = cmake_command(self.cmake, "set", name)
                self.assertEqual(paths, tuple(re.findall(r"(?:kernel|tests/host)/[\w/]+\.c", block)))

        consumers = {}
        for target in re.findall(r"\badd_executable\s*\(\s*(\w+)", self.cmake):
            block = cmake_command(self.cmake, "add_executable", target)
            if "${MICROS_SCHEDULER_CORE_SOURCES}" in block:
                consumers[target] = block
        self.assertEqual(set(CORE_CONSUMERS), set(consumers))
        for target, block in consumers.items():
            self.assertEqual(1, block.count("${MICROS_SCHEDULER_CORE_SOURCES}"), target)
        micros = cmake_command(self.cmake, "add_executable", "micros")
        self.assertEqual(1, micros.count("${MICROS_SCHEDULER_RUNTIME_SOURCES}"))
        scheduler = cmake_command(self.cmake, "add_executable", "scheduler_test")
        require_order(
            scheduler, "${MICROS_KERNEL_OBJECT_CORE_SOURCES}", "${MICROS_SCHEDULER_CORE_SOURCES}",
            "tests/host/ipc_current_guard_test.c", "${MICROS_SCHEDULER_MODEL_HOST_TEST_SOURCES}",
            "${MICROS_SCHEDULER_HOST_TEST_SOURCES}",
        )
        for source_set, condition in (
            ("MICROS_SCHEDULER_QEMU_TEST_SOURCES", "MICROS_BUILD_SCHEDULER_TEST"),
            ("MICROS_SCHEDULER_INVALID_QEMU_TEST_SOURCES", "MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST OR MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST"),
        ):
            blocks = cmake_if_blocks(" ".join(self.cmake.split()), condition)
            relevant = [block for block in blocks if "${" + source_set + "}" in block]
            self.assertEqual(1, len(relevant), source_set)
            require_order(relevant[0], "arch/riscv64/scheduler_test.S", "${" + source_set + "}")
            self.assertEqual(1, self.cmake.count("${" + source_set + "}"))
        options = cmake_command(self.cmake, "set", "MICROS_TARGET_TEST_OPTIONS")
        for option in (
            "MICROS_BUILD_SCHEDULER_TEST", "MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST",
            "MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST",
        ):
            self.assertEqual(1, options.split().count(option))
        require_order(
            self.cmake, "foreach(option_name IN LISTS MICROS_TARGET_TEST_OPTIONS)",
            "if(${option_name})", "math(EXPR MICROS_TARGET_TEST_COUNT",
            "if(MICROS_TARGET_TEST_COUNT GREATER 1)", "message(FATAL_ERROR",
        )

    def test_scheduler_target_ctest_tiers_and_qemu_commands_stay_owned(self) -> None:
        require_order(
            cmake_command(self.cmake, "target_compile_features", "scheduler_test"),
            "scheduler_test PRIVATE c_std_17",
        )
        includes = cmake_command(self.cmake, "target_include_directories", "scheduler_test")
        self.assertIn('"${CMAKE_CURRENT_SOURCE_DIR}/include"', includes)
        self.assertIn('"${CMAKE_CURRENT_SOURCE_DIR}"', includes)
        warnings = cmake_command(self.cmake, "target_compile_options", "scheduler_test")
        require_order(warnings, "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-Wmissing-prototypes", "-Wstrict-prototypes")
        sanitizer = "\n".join(cmake_if_blocks(
            " ".join(self.cmake.split()),
            'MICROS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU"',
        ))
        require_order(
            cmake_command(sanitizer, "target_compile_options", "scheduler_test"),
            "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
        )
        self.assertIn("-fsanitize=address,undefined", cmake_command(sanitizer, "target_link_options", "scheduler_test"))
        self.assertEqual(1, len(re.findall(r"add_test\s*\(\s*NAME\s+scheduler-core\s+COMMAND\s+scheduler_test\s*\)", self.cmake)))
        for tier in ("test-unit-fast", "test-unit"):
            self.assertRegex(cmake_command(self.cmake, "add_custom_target", tier), r"\bDEPENDS\b[\s\S]*\bscheduler_test\b")
        self.assertNotRegex(self.cmake, r"set_tests_properties\s*\(\s*scheduler-core\b")
        for name in (
            "test-qemu-scheduler", "test-qemu-scheduler-invalid-context",
        ):
            self.assertRegex(cmake_command(self.cmake, "add_custom_target", name), r"--timeout\s+8\b")

    def test_exact_state_consolidations_and_no_new_global_duration(self) -> None:
        for family, (source_set, paths) in enumerate(SOURCE_SETS.items()):
            context = CONTEXTS[family]
            declarations = [
                (path, declaration)
                for path in (*paths, PRIVATE_HEADERS[family])
                for declaration in object_definitions(self.texts.get(path, ""))
                if not re.search(r"\bconst\b", declaration)
            ]
            if family == 0:
                self.assertEqual([], declarations)
                self.assertNotRegex(
                    "\n".join(self.texts.get(path, "") for path in paths),
                    r"\b(?:plan_scratch|scheduler_plan_scratch)\b",
                )
                continue
            type_name, owner, fields = context
            consolidated = [(path, declaration) for path, declaration in declarations if re.search(rf"\bstruct\s+{type_name}\b", declaration)]

            self.assertEqual(1, len(consolidated), source_set)
            self.assertEqual(owner, consolidated[0][0])
            self.assertRegex(consolidated[0][1], rf"^static\s+struct\s+{type_name}\b")
            full_text = "\n".join(self.texts.get(path, "") for path in (*paths, PRIVATE_HEADERS[family]))
            actual_fields = struct_body(full_text, type_name)
            actual_fields = re.sub(r"\b(?:TEST|MODEL)_THREAD_COUNT\b", "8", actual_fields)
            actual_fields = re.sub(r"\bMODEL_HART_COUNT\b", "2", actual_fields)
            self.assertEqual(compact(fields), compact(actual_fields), source_set)
            initializer = consolidated[0][1].split("=", 1)
            if family == 2:
                self.assertEqual(2, len(initializer))
                self.assertEqual(compact("{.last_running = 2};"), compact(initializer[1]))
            elif len(initializer) == 2:
                self.assertEqual(compact("{0};"), compact(initializer[1]))
            extra = [entry for entry in declarations if entry not in consolidated]
            expected_extra = []
            if family == 2:
                expected_extra = [
                    ('kernel/tests/scheduler/scheduler_test.c', compact("volatile uint64_t " + name + ";"))
                    for name in ("micros_scheduler_test_idle_bypass_once", "micros_scheduler_test_idle_bypass_observed")
                ]
            self.assertCountEqual(expected_extra, [(path, compact(text)) for path, text in extra])
            storage = re.search(rf"\bstruct\s+{type_name}\s+(\w+)", consolidated[0][1]).group(1)
            for record in self.records[family].values():
                for variable, getter in re.findall(
                    rf"\bstruct\s+{type_name}\s*\*\s*(\w+)\s*=\s*({FAMILY_PREFIXES[family]}\w+)\s*\(\s*\)\s*;",
                    record.body,
                ):
                    self.assertIn(getter, self.records[family], variable)
                    borrow = self.records[family][getter]
                    self.assertEqual(compact("return &" + storage + ";"), compact(borrow.body))
                    self.assertRegex(borrow.signature, rf"\bstruct\s+{type_name}\s*\*")
                    self.assertIn(borrow.name, function_records(self.texts[owner]))

    def test_private_call_direction_and_one_declaration_per_cross_file_helper(self) -> None:
        allowed = (
            ((), (0,), (0,), (0, 1, 2), (0, 1, 2), (0, 1, 2), (0, 2), (0, 6), (0, 2)),
            ((1, 2), (), (1,), (1, 2), (1,), (1, 2), (1,)),
            ((1, 2, 6), (), (1, 3), (1,), (1, 3), (1, 3, 4), (1, 4)),
            ((1, 2), (), (1,), (1,)),
            ((1, 2, 3, 4), (), (1,), (1,), (1,)),
            ((1, 2, 3, 4, 5), (), (1,), (1, 2), (1, 2), (1, 2)),
        )
        for family, paths in enumerate(SOURCE_SETS.values()):
            owners = {
                name: path for path in paths
                for name in function_records(self.texts.get(path, ""))
            }
            edges = {}
            header = self.texts.get(PRIVATE_HEADERS[family], "")
            for name, record in self.records[family].items():
                source = paths.index(owners[name])
                callees = {called for called, _, _, _ in call_sites(record.body) if called in owners}
                edges[name] = callees
                for called in callees:
                    target = paths.index(owners[called])
                    if source != target:
                        self.assertIn(target, allowed[family][source], (owners[name], name, owners[called], called))
                        callee = self.records[family][called]
                        self.assertNotRegex(callee.signature, r"\bstatic\b")
                        if not called.startswith("micros_"):
                            self.assertTrue(called.startswith(FAMILY_PREFIXES[family]), called)
                            self.assertEqual(1, len(re.findall(rf"\b{called}\s*\([^;{{}}]*\)\s*;", c_lexical_text(header))))
                if family == 5 and owners[name].endswith("_projection.c"):
                    for called in callees:
                        if owners[called].endswith("_reference.c"):
                            self.assertIn(called, {"scheduler_model_handle_is_null", "scheduler_model_handles_equal", "scheduler_model_queue_find"})
            def visit(name: str, stack: tuple[str, ...]) -> None:
                self.assertNotIn(name, stack, ("recursive scheduler call graph", stack, name))
                for called in edges[name]:
                    visit(called, (*stack, name))
            for name in edges:
                visit(name, ())

    @classmethod
    def setUpClass(cls):
        cls.texts = source_texts()
        cls.records = tuple(family_records(cls.texts, index) for index in range(6))
        cls.cmake = source_bytes(REPOSITORY_ROOT, REPOSITORY_ROOT / "CMakeLists.txt").decode()

    def test_private_headers_and_automatic_state_boundaries(self):
        for family, paths in enumerate(SOURCE_SETS.values()):
            header = PRIVATE_HEADERS[family]
            self.assertEqual({}, c_function_definitions(self.texts[header]), header)
            self.assertEqual((), object_definitions(self.texts[header]), header)
            for path in (*paths, header):
                text = self.texts[path]
                self.assertNotRegex(text, r'(?m)^\s*#\s*include\s*[<"][^">]+\.c[">]')
                for include in re.findall(r'(?m)^\s*#\s*include\s*"([^"]+)"', text):
                    candidates = (Path(path).parent / include, Path(include))
                    resolved = next((candidate.as_posix() for candidate in candidates if (REPOSITORY_ROOT / candidate).is_file()), None)
                    if resolved in PRIVATE_HEADERS:
                        self.assertEqual(header, resolved, path)
            for record in self.records[family].values():
                self.assertNotRegex(c_lexical_text(record.body), r"\bstatic\s+(?!const\b)", record.name)

    def test_current_model_cases_counts_seed_and_coverage(self):
        main = self.records[4]["main"].body
        self.assertEqual(CASE_LABELS, tuple(re.findall(r'\{\s*"([^"]+)"\s*,', main)))
        paths = tuple(SOURCE_SETS.values())[5]
        model = "\n".join(self.texts[path] for path in (*paths, PRIVATE_HEADERS[5]))
        for constant, value in (("MODEL_THREAD_COUNT", 8), ("MODEL_HART_COUNT", 2), ("MODEL_STEPS", 4096)):
            self.assertRegex(model, rf"\b{constant}\s*=\s*{value}\b")
        self.assertIn("UINT32_C(0x5c4ed026)", model)
        fields = struct_body(model, "model_coverage")
        self.assertEqual(COVERAGE_FIELDS, tuple(re.findall(r"\bsize_t\s+(\w+)\s*;", fields)))
        self.assertEqual(set(COVERAGE_FIELDS), set(re.findall(r"->coverage\.(\w+)\s*==\s*0", model)))
        self.assertIn("scheduler model coverage incomplete", model)

    def test_current_paths_keep_broad_validation_classification(self):
        for family, paths in enumerate(SOURCE_SETS.values()):
            for path in (*paths[1:], PRIVATE_HEADERS[family]):
                for tier in ("fast", "pr"):
                    self.assertEqual(
                        validation_plan.plan([paths[0]], tier, "main"),
                        validation_plan.plan([path], tier, "main"), path,
                    )
        self.assertIn(STRUCTURAL_TEST_PATH, validation_plan.HOST_UNIT_ONLY_INPUTS)


class SchedulerSourceSafetyFixtureTest(unittest.TestCase):
    def test_function_records_reuses_lexing_for_repeated_body_checks(self) -> None:
        source = "void leaf(void) {}\nvoid root(void) {" + "leaf();" * 200 + "}\n"
        function_records.cache_clear()
        with patch("tests.host.c_source_records.c_lexical_text", wraps=c_lexical_text) as lexical:
            first = function_records(source)
            second = function_records(source)
        self.assertEqual({"leaf", "root"}, set(first))
        self.assertIs(first, second)
        self.assertEqual(1, lexical.call_count)

    def test_extern_declarations_do_not_create_storage(self) -> None:
        source = (
            "extern const uint64_t code_address;\n"
            "extern volatile uint64_t idle_probe;\n"
            "extern uint64_t defined = 0;\n"
            "static uint64_t state;\n"
        )
        self.assertEqual(
            {"extern uint64_t defined = 0;", "static uint64_t state;"},
            {item.strip() for item in object_definitions(source)},
        )


if __name__ == "__main__":
    unittest.main()
