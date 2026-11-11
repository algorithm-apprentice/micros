"""Independent PLAT01 declaration, runtime and transition regressions."""

import ast
import importlib.util
import json
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

from tests.host.qemu_smoke_test_fixture import PANIC_OUTPUT, TRAP_CONTEXT_RECORD
from tests.host.diagnostic_history_source import SOURCE_PROJECTION
from tools import qemu_smoke_arguments, qemu_smoke_cli, qemu_smoke_faults
from tools.qemu_smoke_records import QemuResult, SmokeOutcome


ROOT = Path(__file__).resolve().parents[2]


class DiagnosticReportTest(unittest.TestCase):
    @staticmethod
    def report(payload, *, prefix=PANIC_OUTPUT, chunk=32):
        result = prefix + f"MICROS_DIAGNOSTIC_HISTORY state=retained bytes=0x{len(payload):016x}\n"
        for offset in range(0, len(payload), chunk):
            result += (
                f"MICROS_DIAGNOSTIC_HISTORY_DATA offset=0x{offset:016x} "
                f"data={payload[offset:offset + chunk].hex()}\n"
            )
        return result

    def history_checker(self):
        checker = getattr(qemu_smoke_faults, "_has_diagnostic_history", None)
        self.assertIsNotNone(checker, "the additional bounded history requirement is not implemented")
        return checker

    def test_retained_empty_and_exact_seeded_histories(self):
        check = self.history_checker()
        seed = bytes(ord("a") + index % 23 for index in range(1999)) + b"\n"
        self.assertTrue(check(self.report(b""), "retained"))
        self.assertFalse(check(self.report(b""), "seeded"))
        self.assertTrue(check(self.report(seed[2:]), "seeded"))
        self.assertFalse(check(self.report(seed[1:]), "seeded"))
        self.assertTrue(check(self.report(bytes((1, 10, 27, 128, 255)), chunk=2), "retained"))
        self.assertTrue(check(self.report(b"tail", prefix=PANIC_OUTPUT + TRAP_CONTEXT_RECORD), "retained"))
        self.assertFalse(check(PANIC_OUTPUT, "retained"))
        self.assertFalse(check(self.report(b"tail"), "unknown"))

    def test_history_rejects_malformed_and_reordered_records(self):
        check = self.history_checker()
        valid = self.report(b"m" * 64)
        header, first, second = valid[len(PANIC_OUTPUT):].splitlines(keepends=True)
        malformed = (
            PANIC_OUTPUT + header + header + first + second,
            PANIC_OUTPUT + header + first,
            valid + second,
            PANIC_OUTPUT + header + first + second.replace("0000000000000020", "0000000000000021"),
            valid.replace("data=6d", "data=6D", 1),
            PANIC_OUTPUT + header + first.replace("data=6d", "data=d", 1) + second,
            self.report(b"m" * 64, chunk=33),
            valid.replace("bytes=0x0000000000000040", "bytes=0x00000000000007cf"),
            valid[:-1],
            header + PANIC_OUTPUT + first + second,
            PANIC_OUTPUT + header + first + "MICROS_BOOT 0.1.0\n" + second,
            valid.replace("state=retained", "state=unavailable"),
            valid.replace("data=6d", "data=MICROS_PANIC", 1),
            valid + "MICROS_BOOT 0.1.0\n",
        )
        for index, value in enumerate(malformed):
            with self.subTest(case=index):
                self.assertFalse(check(value, "retained"))
        self.assertTrue(check(valid, "retained"))

    def test_forwarding_requires_unique_terminated_witness_before_legacy_io(self):
        check = getattr(qemu_smoke_faults, "_has_diagnostic_forwarding", None)
        self.assertIsNotNone(check, "the forwarding order requirement is not implemented")
        marker = (
            "MICROS_DIAGNOSTIC_TEST_PASS transfer=three-pages envelope=irq-diagnostic "
            "output=tty early=drained\n"
        )
        ready = "MICROS_TTY_INPUT_READY\n"
        done = (
            "MICROS_TTY_TEST_PASS handoff=two-phase mapping=exact irq=deferred "
            "input=canonical output=interrupt-driven grants=checked\n"
        )
        self.assertTrue(check(marker + ready + done))
        for value in (
            ready + done, marker + marker + ready + done, ready + marker + done,
            marker + done + ready, marker[:-1], marker[:-1] + ready + done,
        ):
            with self.subTest(output=value):
                self.assertFalse(check(value))

    def test_optional_requirements_preserve_default_arguments(self):
        with tempfile.TemporaryDirectory(prefix="micros-plat01-arguments-") as directory:
            kernel = Path(directory) / "kernel.elf"
            kernel.touch()
            base = ("--kernel", str(kernel), "--marker", "boot")
            arguments = qemu_smoke_arguments.parse_arguments(base)
            self.assertIsNone(getattr(arguments, "require_diagnostic_history", "missing"))
            self.assertIs(getattr(arguments, "require_diagnostic_forwarding", None), False)
            arguments = qemu_smoke_arguments.parse_arguments((
                *base, "--require-diagnostic-history", "seeded", "--require-diagnostic-forwarding",
            ))
            self.assertEqual("seeded", arguments.require_diagnostic_history)
            self.assertTrue(arguments.require_diagnostic_forwarding)

    def test_selected_requirement_reaches_actual_outcome_composition(self):
        with tempfile.TemporaryDirectory(prefix="micros-plat01-composition-") as directory:
            kernel = Path(directory) / "kernel.elf"
            kernel.touch()
            base = (
                "--kernel", str(kernel), "--marker", "MICROS_BOOT 0.1.0",
                "--expect", "panic", "--require-panic-report",
            )
            arguments = qemu_smoke_arguments.parse_arguments(base)
            original = QemuResult(output=PANIC_OUTPUT, return_code=0, timed_out=False)

            def accepted(result):
                return qemu_smoke_cli._matches_observed_run(
                    arguments, result, SmokeOutcome.PANIC, SmokeOutcome.PANIC, None, None,
                )

            self.assertTrue(accepted(original))
            arguments = qemu_smoke_arguments.parse_arguments((*base, "--require-diagnostic-history", "retained"))
            self.assertFalse(accepted(original))
            current = QemuResult(output=self.report(b"tail"), return_code=0, timed_out=False)
            self.assertTrue(accepted(current))


class DiagnosticSourceTest(unittest.TestCase):
    def test_current_scheduler_pump_keeps_custody_and_precedes_selection(self):
        from tests.host.c_source_records import c_lexical_text, function_records, matching_lexical

        def check(source, argument):
            records = [
                record for record in function_records(source).values()
                if "micros_diagnostic_notify_pending" in record.body
            ]
            self.assertEqual(1, len(records))
            body = c_lexical_text(records[0].body)
            call = re.compile(r"\bmicros_diagnostic_notify_pending\s*\(\s*hart\s*,\s*" + argument + r"\s*\)\s*;")
            sites = tuple(call.finditer(body))
            self.assertEqual(1, len(sites))
            self.assertIn("micros_hart_plan_user_return", body)
            self.assertLess(sites[0].end(), body.index("micros_hart_plan_user_return"))
            if argument == "NULL":
                self.assertLess(body.index("preflight_idle_selection(objects, hart);"), sites[0].start())
            else:
                guard = body.index("if (scheduler_error != MICROS_SCHEDULER_OK)")
                opening = body.index("{", guard)
                end = matching_lexical(body, opening, "{", "}")
                self.assertIn("return scheduler_error;", body[opening:end])
                self.assertLess(end, sites[0].start())

        for name, argument in (("scheduler_return.c", "frame"), ("scheduler_idle.c", "NULL")):
            source = (ROOT / "kernel/scheduler" / name).read_text()
            check(source, argument)
            call = f"    micros_diagnostic_notify_pending(hart, {argument});\n"
            for changed in (
                source.replace(call, call * 2),
                source.replace(call, "").replace("    if (error != ", call + "    if (error != ", 1),
            ):
                with self.subTest(name=name), self.assertRaises(AssertionError):
                    check(changed, argument)

    def test_current_retrigger_transfer_binding_is_complete(self):
        from tests.host.diagnostic_history_source import validate_retrigger_kernel_fixture, validate_retrigger_read_adapter

        validate_retrigger_kernel_fixture((ROOT / "tests/qemu/diagnostic_history_kernel.c").read_text())
        validate_retrigger_read_adapter((ROOT / "kernel/diagnostic/diagnostic_control.c").read_text())

    def test_retrigger_admission_keeps_queued_irq_and_no_copy_before_deferral(self):
        from tests.host import diagnostic_history_source as source_contract

        validate = getattr(source_contract, "validate_retrigger_read_adapter", None)
        validate_fixture = getattr(source_contract, "validate_retrigger_kernel_fixture", None)
        self.assertIsNotNone(validate, "the test-only deferred return has no checked copy boundary")
        self.assertIsNotNone(validate_fixture, "the fixture has no exact queued-IRQ admission")
        site = source_contract.RETRIGGER_READ_BOUNDARY
        source = "static void read_history(void)\n{\n" + site + "\n}\n"
        validate(source)
        for changed in (
            source.replace("MICROS_SYSCALL_ABI_STATE", "MICROS_SYSCALL_ABI_ARGUMENT"),
            source.replace("#ifdef MICROS_BUILD_TTY_SERVICE_TEST\n", "", 1),
            source.replace("    micros_diagnostic_copy_prevalidated(&diagnostic_reader, history, &span);\n", "", 1),
            source.replace("    if (!micros_diagnostic_test_before_read", "    if (micros_diagnostic_test_before_read", 1),
        ):
            with self.assertRaises(AssertionError):
                validate(changed)
        fixture = (ROOT / "tests/qemu/diagnostic_history_kernel.c").read_text()
        validate_fixture(fixture)
        for changed in (
            fixture.replace("endpoint->pending_kernel_events != UINT64_C(0x8)", "endpoint->pending_kernel_events == 0"),
            fixture.replace("state->handoff.deadline_armed || state->handoff.deadline != 0", "false"),
            fixture.replace("        return false;\n", "        return true;\n", 1),
        ):
            self.assertNotEqual(fixture, changed)
            with self.assertRaises(AssertionError):
                validate_fixture(changed)


    def test_kernel_extension_keeps_exact_ordinary_owners_and_private_direction(self):
        from tests.host import diagnostic_history_source as source_contract

        validate = getattr(source_contract, "diagnostic_kernel_members", None)
        self.assertIsNotNone(validate, "new diagnostic files lack an explicit closed owner extension")
        members = SOURCE_PROJECTION["new_kernel_members"]
        with tempfile.TemporaryDirectory(prefix="micros-plat01-kernel-members-") as directory:
            root = Path(directory)
            for name in members:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("/* Independent ownership fixture. */\n")
                path.chmod(0o644)
            self.assertEqual((members, ()), validate(root))
            selected = root / "kernel/diagnostic/diagnostic_history_core.c"
            original = selected.read_text()
            selected.write_text('#include "kernel/diagnostic/diagnostic_history.h"\n')
            self.assertTrue(validate(root)[1])
            selected.write_text(original)
            selected.chmod(0o600)
            self.assertTrue(validate(root)[1])
            selected.chmod(0o644)
            selected.unlink()
            self.assertTrue(validate(root)[1])
            selected.symlink_to(root / "kernel/diagnostic/diagnostic_history.c")
            self.assertTrue(validate(root)[1])

    def test_tty_constructor_keeps_original_callback_bindings_and_phase_order(self):
        from unittest.mock import patch
        from tests.host import diagnostic_history_source as source_contract
        from tests.host.c_source_records import function_records
        from tools.stack_bound_bindings import anchored_function, reference_source
        from tools.stack_bound_types import FunctionKey, StackEvidenceError

        changes = getattr(source_contract, "TTY_CONSTRUCTOR_CHANGES", None)
        project = getattr(source_contract, "project_diagnostic_callback_source", None)
        self.assertIsNotNone(changes, "TTY constructor additions lack their fixed insertion register")
        self.assertIsNotNone(project, "TTY constructor has no exact historical inverse")
        unit = "servers/tty/tty_service.c"
        original = reference_source(str(ROOT), unit)
        record = function_records(original)["micros_service_main"]
        body = record.body
        for added, previous in changes:
            self.assertEqual(1, body.count(previous), previous)
            body = body.replace(previous, added, 1)
        candidate = original.replace(
            record.signature + "{" + record.body + "}",
            record.signature + "{" + body + "}", 1,
        )
        self.assertEqual(original, project(unit, candidate))
        with tempfile.TemporaryDirectory(prefix="micros-plat01-tty-constructor-") as directory:
            root = Path(directory)
            path = root / unit
            path.parent.mkdir(parents=True)
            path.write_text(candidate)
            key = FunctionKey("fixture", "tty.elf", unit, "micros_service_main")
            with patch("tools.stack_bound_bindings.reference_source", return_value=original):
                anchored_function(root, key)
                path.write_text(candidate.replace("MICROS_TTY_UART_VIRTUAL_BASE", "0", 1))
                with self.assertRaises(StackEvidenceError):
                    anchored_function(root, key)
        altered = candidate.replace(
            "&tty_uart, &bus, &tty_service.terminal, &diagnostic_effects",
            "&tty_uart, &foreign_bus, &tty_service.terminal, &diagnostic_effects", 1,
        )
        self.assertNotEqual(candidate, altered)
        with self.assertRaises(AssertionError):
            project(unit, altered)

    def test_callback_free_admission_also_checks_every_actual_machine_call(self):
        from types import SimpleNamespace
        from unittest.mock import patch
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_artifacts import object_frames
        from tools.stack_bound_bindings import bind_current_callbacks, reference_source
        from tools.stack_bound_callback_contracts import GROUPS
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import Frame, FunctionKey, StackEvidenceError

        headers = {
            group.header: reference_source(str(ROOT), group.header)
            for group in GROUPS if group.source_prefix == "servers/tty/"
        }
        with tempfile.TemporaryDirectory(prefix="micros-plat01-callback-machine-") as directory:
            root, build = Path(directory) / "source", Path(directory) / "build"
            build.mkdir()
            unit = "servers/tty/tty_diagnostic.c"
            for name, content in (*headers.items(), (unit, "void diagnostic_case(void) {}\n")):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            (build / "CMakeCache.txt").write_text("")

            def pinned(_, name):
                if name in headers:
                    return headers[name]
                raise StackEvidenceError("no historical source for this unit")

            for indirect in (False, True):
                body = "((void (*)(void))0x40000000)();" if indirect else ""
                _, output = compile_fixture(build, "void diagnostic_case(void) { " + body + " }\n")
                obj = load_object(output)
                key = FunctionKey("fixture", "service.elf", unit, "diagnostic_case")
                catalog = SimpleNamespace(
                    frames=(Frame(key, object_frames(obj, function_symbols(obj))["diagnostic_case"]),),
                    definitions=(SimpleNamespace(function=key),), objects=((key, output.name),),
                    origins=(SimpleNamespace(function=key, source=unit),), pending=(), calls=(),
                )
                with patch("tools.stack_bound_bindings.reference_source", side_effect=pinned):
                    if indirect:
                        with self.assertRaises(StackEvidenceError):
                            bind_current_callbacks(root, build, catalog)
                    else:
                        self.assertEqual(((), ()), bind_current_callbacks(root, build, catalog))

    def test_callback_free_source_names_are_not_frozen_but_callback_use_is_qualified(self):
        from unittest.mock import patch
        from tools.stack_bound_bindings import reference_source, require_callback_type_scope
        from tools.stack_bound_callback_contracts import GROUPS
        from tools.stack_bound_types import StackEvidenceError

        group = next(group for group in GROUPS if group.identifier == "tty-uart")
        original_header = reference_source(str(ROOT), group.header)
        with tempfile.TemporaryDirectory(prefix="micros-plat01-callback-scope-") as directory:
            root = Path(directory)
            unit = "servers/tty/tty_diagnostic.c"
            header = "servers/tty/tty_diagnostic.h"
            for name, content in (
                (group.header, original_header),
                (unit, '#include "servers/tty/tty_diagnostic.h"\nint helper(void) { return 0; }\n'),
                (header, "struct diagnostic_fixture { unsigned cursor; };\n"),
                ("servers/tty/undeclared.c", "int other(void) { return 0; }\n"),
            ):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)

            def pinned(_, name):
                if name == group.header:
                    return original_header
                raise StackEvidenceError("no historical source for this unit")

            with patch("tools.stack_bound_bindings.reference_source", side_effect=pinned):
                require_callback_type_scope(root, group, {unit})
                require_callback_type_scope(root, group, {"servers/tty/undeclared.c"})
                (root / unit).write_text(
                    "void helper(struct micros_tty_uart_bus *bus) { (void)bus; }\n",
                )
                with self.assertRaises(StackEvidenceError):
                    require_callback_type_scope(root, group, {unit})

    def test_existing_tty_owner_admits_only_actual_callback_free_diagnostic_wrappers(self):
        from types import SimpleNamespace
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_artifacts import object_frames
        from tools.stack_bound_bindings import bind_current_callbacks
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import Frame, FunctionKey, StackEvidenceError

        unit = "servers/tty/tty_control.c"
        source = (ROOT / unit).read_text()
        beginning = "micros_runtime_result_t micros_tty_control_diagnostic_read(uint32_t previous)\n{\n"
        indirect = source.replace(
            beginning, beginning + "    ((void (*)(void))(uintptr_t)previous)();\n", 1,
        )
        self.assertNotEqual(source, indirect)
        extra = source + "\nvoid micros_tty_control_diagnostic_unknown(void) {}\n"
        with tempfile.TemporaryDirectory(prefix="micros-plat01-control-callbacks-") as directory:
            build = Path(directory)
            (build / "CMakeCache.txt").write_text("")
            for label, body in (
                ("actual", source), ("indirect", indirect), ("unknown", extra), ("borrowed", source),
            ):
                with self.subTest(case=label):
                    _, output = compile_fixture(build, body)
                    obj = load_object(output)
                    symbols = function_symbols(obj)
                    sizes = object_frames(obj, symbols)
                    keys = tuple(FunctionKey("fixture", "service.elf", unit, name) for name in symbols)
                    catalog = SimpleNamespace(
                        frames=tuple(Frame(key, sizes[key.symbol]) for key in keys),
                        definitions=tuple(SimpleNamespace(function=key) for key in keys),
                        objects=tuple((key, output.name) for key in keys),
                        origins=tuple(SimpleNamespace(
                            function=key,
                            source="servers/tty/tty_service.c" if label == "borrowed"
                                and key.symbol == "micros_tty_control_diagnostic_read" else unit,
                        ) for key in keys),
                        pending=(), calls=(),
                    )
                    if label == "actual":
                        self.assertEqual(((), ()), bind_current_callbacks(ROOT, build, catalog))
                    else:
                        with self.assertRaises(StackEvidenceError):
                            bind_current_callbacks(ROOT, build, catalog)


class DiagnosticFatalMachineTest(unittest.TestCase):
    def test_thr_observation_is_after_and_dominated_by_the_actual_store(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_bindings import reference_source
        from tools.stack_bound_fatal_effects import uart_machine_effects
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        source = reference_source(str(ROOT), "arch/riscv64/platform.h") + reference_source(str(ROOT), "arch/riscv64/uart.c")
        store = "    uart0[UART_THR] = (uint8_t)character;\n"
        observation = "    micros_diagnostic_test_observe_thr();\n"
        self.assertEqual(1, source.count(store))
        with tempfile.TemporaryDirectory(prefix="micros-plat01-thr-site-") as directory:
            for position in ("after", "before"):
                for observed in (False, True):
                    root = Path(directory) / f"{position}-{observed}"
                    root.mkdir()
                    changed = "void micros_diagnostic_test_observe_thr(void);\n" + source.replace(
                        store, store + observation if position == "after" else observation + store, 1,
                    )
                    _, output = compile_fixture(root, changed, observed=observed)
                    obj = load_object(output)
                    symbol = function_symbols(obj)["uart_write_character"]
                    with self.subTest(position=position, observed=observed):
                        if position == "after":
                            uart_machine_effects(obj, symbol, test_counter=True)
                            with self.assertRaises(StackEvidenceError):
                                uart_machine_effects(obj, symbol)
                        else:
                            with self.assertRaises(StackEvidenceError):
                                uart_machine_effects(obj, symbol, test_counter=True)

    def test_recording_arms_require_the_false_selector_continuation(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools import stack_bound_fatal_effects as effects
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        prove = getattr(effects, "diagnostic_recording_guard", None)
        self.assertIsNotNone(prove, "logical writers have no source-independent branch proof")
        for name, parameter, argument in (
            ("uart_write", "const char *text", "text"),
            ("uart_write_hex64", "uint64_t value", "value"),
        ):
            prefix = (
                "#include <stdint.h>\n#include <stdbool.h>\n"
                "bool uart_panic_mode(void);\nbool foreign_selector(void);\n"
                f"void {name}_direct({parameter});\nvoid {name}_record({parameter});\n"
            )
            correct = (
                f"if (uart_panic_mode()) {{ {name}_direct({argument}); return; }} "
                f"{name}_record({argument});"
            )
            cases = (
                (correct, True),
                (correct.replace("if (uart_panic_mode())", "if (!uart_panic_mode())"), False),
                (correct.replace(" return;", ""), False),
                (correct.replace("uart_panic_mode()", "foreign_selector()"), False),
                (f"{name}_record({argument}); " + correct, False),
            )
            with tempfile.TemporaryDirectory(prefix="micros-plat01-guard-") as directory:
                for index, (body, accepted) in enumerate(cases):
                    for observed in (False, True):
                        root = Path(directory) / f"{index}-{observed}"
                        root.mkdir()
                        _, output = compile_fixture(
                            root, prefix + f"void {name}({parameter}) {{ {body} }}\n", observed=observed,
                        )
                        obj = load_object(output)
                        symbol = function_symbols(obj)[name]
                        with self.subTest(name=name, body=body, observed=observed):
                            if accepted:
                                predicate, excluded, branch = prove(obj, symbol)
                                self.assertLess(predicate, branch)
                                self.assertNotEqual(excluded, branch)
                            else:
                                with self.assertRaises(StackEvidenceError):
                                    prove(obj, symbol)

    def test_panic_selector_uses_only_the_actual_owner_field(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools import stack_bound_fatal_effects as effects
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        prove = getattr(effects, "diagnostic_selector_effects", None)
        self.assertIsNotNone(prove, "the concrete PANIC selector has no machine proof")
        prefix = (
            "#include <stdint.h>\n#include <stdbool.h>\n"
            "struct console { uint64_t magic; unsigned owner; bool quiesced; };\n"
            "static struct console uart_console_state __attribute__((used));\n"
            "static struct console foreign_console __attribute__((used));\n"
        )
        cases = (
            ("return uart_console_state.owner == 3;", True),
            ("return uart_console_state.owner == 2;", False),
            ("return foreign_console.owner == 3;", False),
            ("uart_console_state.owner = 3; return true;", False),
            ('__asm__ volatile("csrsi sstatus, 2"); return uart_console_state.owner == 3;', False),
        )
        with tempfile.TemporaryDirectory(prefix="micros-plat01-selector-") as directory:
            for index, (body, valid) in enumerate(cases):
                for observed in (False, True):
                    root = Path(directory) / f"{index}-{observed}"
                    root.mkdir()
                    source = prefix + "bool uart_panic_mode(void) { " + body + " }\n"
                    _, output = compile_fixture(root, source, observed=observed)
                    obj = load_object(output)
                    symbol = function_symbols(obj)["uart_panic_mode"]
                    console, = (item for item in obj.symbols if item.name == "uart_console_state")
                    with self.subTest(body=body, observed=observed):
                        if valid:
                            proof = prove(obj, symbol, console)
                            self.assertFalse(proof.writes or proof.systems)
                        else:
                            with self.assertRaises(StackEvidenceError):
                                prove(obj, symbol, console)

    def test_thr_observer_proves_exact_unsigned_saturation_and_no_other_effect(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools import stack_bound_fatal_effects as effects
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        prove = getattr(effects, "diagnostic_counter_effects", None)
        self.assertIsNotNone(prove, "the THR observation has no bounded machine-effect proof")
        prefix = (
            "#include <stdint.h>\n"
            "static uint64_t diagnostic_thr_writes __attribute__((used));\n"
            "static uint64_t foreign_counter __attribute__((used));\n"
            "uint64_t fixture_count(void) { return diagnostic_thr_writes; }\n"
        )
        correct = "if (diagnostic_thr_writes != UINT64_MAX) { ++diagnostic_thr_writes; }"
        cases = (
            (correct, True),
            (correct.replace("!=", "=="), False),
            (correct.replace("++diagnostic_thr_writes", "diagnostic_thr_writes += 2"), False),
            (correct.replace("diagnostic_thr_writes", "foreign_counter"), False),
            ("++diagnostic_thr_writes;", False),
            (correct + ' __asm__ volatile("csrsi sstatus, 2");', False),
        )
        with tempfile.TemporaryDirectory(prefix="micros-plat01-counter-") as directory:
            for index, (body, valid) in enumerate(cases):
                for observed in (False, True):
                    root = Path(directory) / f"{index}-{observed}"
                    root.mkdir()
                    source = prefix + "void micros_diagnostic_test_observe_thr(void) { " + body + " }\n"
                    _, output = compile_fixture(root, source, observed=observed)
                    obj = load_object(output)
                    symbol = function_symbols(obj)["micros_diagnostic_test_observe_thr"]
                    counter, = (item for item in obj.symbols if item.name == "diagnostic_thr_writes")
                    with self.subTest(body=body, observed=observed):
                        if valid:
                            proof = prove(obj, symbol, counter)
                            self.assertEqual(1, len(proof.writes))
                            self.assertFalse(proof.systems)
                        else:
                            with self.assertRaises(StackEvidenceError):
                                prove(obj, symbol, counter)

    def test_history_formatter_rejects_foreign_or_unbounded_writes(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools import stack_bound_fatal_effects as effects
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        prove = getattr(effects, "diagnostic_readonly_effects", None)
        self.assertIsNotNone(prove, "history formatting has no closed frame-only write proof")
        prefix = (
            "void uart_write(const char *text);\n"
            "void foreign_call(void);\n"
            "unsigned long foreign_state;\n"
        )
        valid = "char pair[3]; pair[0] = (char)value; pair[1] = 'a'; pair[2] = 0; uart_write(pair);"
        cases = (
            (valid, True),
            ("foreign_state = value; " + valid, False),
            (valid.replace("pair[0]", "pair[value]"), False),
            (valid + ' __asm__ volatile("csrsi sstatus, 2");', False),
            (valid + " foreign_call();", False),
        )
        with tempfile.TemporaryDirectory(prefix="micros-plat01-format-effects-") as directory:
            for index, (body, accepted) in enumerate(cases):
                root = Path(directory) / str(index)
                root.mkdir()
                _, output = compile_fixture(root, prefix + "void write_history_byte(unsigned value) { " + body + " }\n")
                obj = load_object(output)
                symbol = function_symbols(obj)["write_history_byte"]
                with self.subTest(body=body):
                    if accepted:
                        self.assertFalse(prove(obj, symbol).writes)
                    else:
                        with self.assertRaises(StackEvidenceError):
                            prove(obj, symbol)
