"""Independent PLAT02 terminal ownership and evidence regressions."""

import ast
from contextlib import redirect_stderr
from dataclasses import replace
import io
from pathlib import Path
import struct
import subprocess
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from tests.host.evidence_authority_inventory import current_inventory
from tests.host.evidence_authority_types import AuthorityClass, fact_set_digest
from tests.host.test_evidence_authority import prior_standalone_binding_facts


ROOT = Path(__file__).resolve().parents[2]


class TerminalAuthorityTest(unittest.TestCase):
    def test_exact_nine_authorities_exist_without_a_new_retirement_claim(self):
        source = "tests/host/terminal_failure_source.py"
        evidence = "tests/host/terminal_failure_evidence.py"
        expected = {
            evidence + ":design-anchor:PLAT02": AuthorityClass.TRANSPORT_IDENTITY,
            source + ":source-membership:PLAT02": AuthorityClass.PERMANENT_INVARIANT,
            source + ":artifact-contract:PLAT02-recipes": AuthorityClass.ARTIFACT_CONTRACT,
            source + ":effect-contract:PLAT02-fatal": AuthorityClass.PERMANENT_INVARIANT,
            source + ":source-projection:PLAT02": AuthorityClass.TRANSPORT_IDENTITY,
        }
        for name in (
            "terminal-transmit-bound", "terminal-drain-bound",
            "terminal-entry-reset-ownership", "terminal-console-repair",
        ):
            expected[evidence + ":baseline-oracle:" + name] = AuthorityClass.BASELINE_ORACLE
        actual = {
            fact.key: fact.authority_class
            for fact in current_inventory(ROOT).facts
            if fact.family == "terminal-failure-baseline"
        }
        self.assertEqual(expected, actual)
        from tests.host.diagnostic_history_authority import task_definition

        _, _, _, registry = task_definition(ROOT)
        self.assertEqual(
            {"COPY01-runtime-memory", "PLAT01-diagnostic-history"},
            {claim.transition_id for claim in registry.claims},
        )

    def test_every_committed_family_and_complete_fact_fingerprint_is_preserved(self):
        from tests.host.terminal_failure_evidence import DESIGN_PINS

        source = subprocess.check_output(
            ("git", "show", DESIGN_PINS["design_commit"] + ":tests/host/evidence_authority_inventory.py"),
            cwd=ROOT, text=True, timeout=30,
        )
        declaration, = (
            node for node in ast.parse(source).body
            if isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == "DECLARED_FAMILY_FACT_DIGESTS"
                for target in node.targets
            )
        )
        expected = ast.literal_eval(declaration.value)
        inventory = current_inventory(ROOT)
        self.assertEqual(12, len(expected))
        self.assertEqual(3920, sum(fact.family in expected for fact in inventory.facts))
        from tests.host.evidence_authority_inventory import historical_object_binding_facts

        historical = historical_object_binding_facts(ROOT, prior_standalone_binding_facts(inventory.facts))
        for family, fingerprint in expected.items():
            self.assertEqual(
                fingerprint, fact_set_digest(fact for fact in historical if fact.family == family),
                family,
            )

    def test_oracles_reject_duplicate_test_ownership_missing_cases_and_boolean_ids(self):
        from tests.host.terminal_failure_evidence import ORACLE_RULES
        from tests.host.terminal_failure_source import checked_oracle_rules

        self.assertEqual(ORACLE_RULES, checked_oracle_rules(ORACLE_RULES))
        for replacement in (
            (*ORACLE_RULES[0][:1], ORACLE_RULES[1][1], *ORACLE_RULES[0][2:]),
            (*ORACLE_RULES[0][:5], (1, 2, 5)),
            (*ORACLE_RULES[0][:5], (True, 2, 5, 7)),
        ):
            with self.subTest(rule=replacement), self.assertRaises(ValueError):
                checked_oracle_rules((replacement, *ORACLE_RULES[1:]))

    def test_literal_roles_reject_duplicate_assignments_keys_and_overlapping_inputs(self):
        from tests.host.terminal_failure_source import EVIDENCE_PATH, SOURCE_PATH, declaration_facts

        with tempfile.TemporaryDirectory(prefix="micros-plat02-declarations-") as directory:
            root = Path(directory)
            for name in (SOURCE_PATH, EVIDENCE_PATH):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes((ROOT / name).read_bytes())
            path = root / SOURCE_PATH
            source = path.read_text()
            for changed in (
                source + "\nSOURCE_ROLES = {}\n",
                source.replace('    "candidate_paths": (\n', '    "candidate_paths": (),\n    "candidate_paths": (\n', 1),
                source.replace('"arch/riscv64/uart.c"', '"tests/host/panic_control_test.c"', 1),
            ):
                path.write_text(changed)
                with self.assertRaises(ValueError):
                    declaration_facts(root, SOURCE_PATH)
            with self.assertRaises(ValueError):
                declaration_facts(ROOT, "tests/host/test_terminal_failure.py")

    def test_no_preclaim_boolean_cannot_be_coerced_or_admitted_in_another_field(self):
        from tests.host.terminal_failure_source import SOURCE_PATH, literal_declaration

        source = (ROOT / SOURCE_PATH).read_text()
        with tempfile.TemporaryDirectory(prefix="micros-plat02-boolean-") as directory:
            root = Path(directory)
            path = root / SOURCE_PATH
            path.parent.mkdir(parents=True)
            path.write_text(source)
            self.assertIs(
                False, literal_declaration(root, SOURCE_PATH, "FATAL_EFFECT_CONTRACT")["reset"]["caller_preclaim"],
            )
            for changed in (
                source.replace('"caller_preclaim": False', '"caller_preclaim": 0', 1),
                source.replace('"caller_preclaim": False', '"caller_preclaim": True', 1),
                source.replace('"caller_preclaim": False', '"caller_preclaim": False, "caller_preclaim": False', 1),
                source.replace('"poll_limit": 100000', '"poll_limit": False', 1),
            ):
                path.write_text(changed)
                with self.assertRaises(ValueError):
                    literal_declaration(root, SOURCE_PATH, "FATAL_EFFECT_CONTRACT")


class TerminalObservationTest(unittest.TestCase):
    def test_observation_decoding_rejects_truncated_foreign_or_unknown_fields(self):
        from tests.host.terminal_failure_evidence import decode_observation

        data = struct.pack("<IIII9QIIII", 1, 5, 3, 0, 100001, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        observed = decode_observation(data, 5)
        self.assertEqual((100001, 0, 3), (observed["thre_reads"], observed["temt_reads"], observed["phase"]))
        for offset, value in ((0, 2), (4, 6), (8, 6), (12, 1), (88, 6), (100, 1)):
            changed = bytearray(data)
            struct.pack_into("<I", changed, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                decode_observation(bytes(changed), 5)
        for changed in (data[:-1], data + b"\0"):
            with self.assertRaises(ValueError):
                decode_observation(changed, 5)
        with self.assertRaises(ValueError):
            decode_observation(data, True)

    def test_work_decoder_requires_the_entire_exact_single_producer_capture(self):
        from tests.host.terminal_failure_evidence import decode_work
        from tests.host.work_count_types import Kind, Owner

        counts = [0] * (27 * 14)
        counts[14] = 100000
        data = struct.pack("<IIQQII378Q", 1, 0, 0x504C415430320005, 1, 2, 0, *counts)
        snapshot = decode_work(data, 5)
        self.assertEqual(100000, snapshot.counts[Owner.KERNEL_DIAGNOSTIC][Kind.COMPARISON])
        for offset, format_, value in ((0, "<I", 2), (4, "<I", 1), (8, "<Q", 0x504C415430320006),
                                        (16, "<Q", 2), (24, "<I", 3), (28, "<I", 1), (32, "<Q", 1)):
            changed = bytearray(data)
            struct.pack_into(format_, changed, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                decode_work(bytes(changed), 5)
        with self.assertRaises(ValueError):
            decode_work(data[:-8], 5)
        with self.assertRaises(ValueError):
            decode_work(None, 5)

    def test_history_expectation_preserves_wrap_tail_shift_and_complete_inactive_bytes(self):
        from tests.host.terminal_failure_evidence import expected_history

        original = bytearray(12012)
        original[:10000] = b"a" * 10000
        original[10000:11998] = b"b" * 1998
        struct.pack_into("<III", original, 12000, 9999, 10000, 1998)
        changed, work = expected_history(bytes(original), b"x\n")
        self.assertEqual(b"\n", changed[:1])
        self.assertEqual(b"x", changed[9999:10000])
        self.assertEqual(b"b" * 1996 + b"x\n\0\0", changed[10000:12000])
        self.assertEqual((1, 10000, 1998), struct.unpack_from("<III", changed, 12000))
        self.assertEqual(4002, work)
        self.assertEqual((bytes(original), 0), expected_history(bytes(original), b""))
        with self.assertRaises(ValueError):
            expected_history(bytes(original), b"\0")
        original[11999] = 1
        with self.assertRaises(ValueError):
            expected_history(bytes(original), b"x")


class TerminalSourceTest(unittest.TestCase):
    def test_historical_workflow_view_removes_only_the_exact_ordered_addition(self):
        from tests.host.terminal_failure_source import fixed_source, project_terminal_workflows
        from tools import validation_plan

        statement, = (
            node for node in ast.parse(fixed_source("tools/validation_plan.py")).body
            if isinstance(node, ast.Assign) and any(isinstance(target, ast.Name) and target.id == "QEMU_WORKFLOWS" for target in node.targets)
        )
        original = ast.literal_eval(statement.value)
        self.assertEqual(original, project_terminal_workflows(validation_plan.QEMU_WORKFLOWS))
        extra = "test-qemu-terminal-failure"
        for invalid in ((*validation_plan.QEMU_WORKFLOWS, extra), (extra, *original)):
            with self.assertRaises(ValueError):
                project_terminal_workflows(invalid)
        self.assertIn("test-qemu-unreviewed", project_terminal_workflows((*validation_plan.QEMU_WORKFLOWS, "test-qemu-unreviewed")))

    def test_planner_extension_preserves_the_complete_original_selection_algorithm(self):
        from tests.host.terminal_failure_source import fixed_source, project_terminal_planner, project_terminal_plan
        from tests.host.panic_trace_source import project_trace_plan
        from tools import validation_plan

        source = (ROOT / "tools/validation_plan.py").read_text()
        self.assertEqual(fixed_source("tools/validation_plan.py"), project_terminal_planner(source))
        plan = project_trace_plan(validation_plan.plan([], "full"))
        projected = project_terminal_plan(plan)
        self.assertEqual(49, len(plan))
        self.assertEqual(48, len(projected))
        self.assertEqual(plan[-3:], projected[-3:])
        extra = ("cmake", "--workflow", "--preset", "test-qemu-terminal-failure")
        self.assertNotIn(extra, projected)
        for invalid in ((source.replace('"test-qemu-terminal-failure"', '"test-qemu-foreign"', 1)),):
            with self.assertRaises(ValueError):
                project_terminal_planner(invalid)
        with self.assertRaises(ValueError):
            project_terminal_plan([*plan, extra])

    def test_new_private_kernel_members_reject_missing_code_and_an_adapter_in_the_portable_core(self):
        from tests.host.terminal_failure_source import CONTROL_SOURCES, terminal_kernel_members

        with tempfile.TemporaryDirectory(prefix="pt2-members-") as temporary:
            root = Path(temporary)
            for name, source in CONTROL_SOURCES.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(source)
            names, errors = terminal_kernel_members(root)
            self.assertEqual(set(CONTROL_SOURCES), set(names))
            self.assertEqual((), errors)
            path = root / "kernel/diagnostic/panic_control_core.c"
            path.write_text(path.read_text() + '\n#include "arch/riscv64/platform.h"\n')
            self.assertTrue(terminal_kernel_members(root)[1])
            path.unlink()
            self.assertTrue(terminal_kernel_members(root)[1])

    def test_current_build_wires_exactly_the_declared_native_and_target_extensions(self):
        from tests.host.terminal_failure_source import extend_cmake, extend_presets, fixed_source
        from tests.host.panic_trace_source import project_trace_cmake, project_trace_presets

        self.assertEqual(extend_cmake(fixed_source("CMakeLists.txt")), project_trace_cmake((ROOT / "CMakeLists.txt").read_text()))
        self.assertEqual(extend_presets(fixed_source("CMakePresets.json")), project_trace_presets((ROOT / "CMakePresets.json").read_text()))

    def test_terminal_runtime_inverse_preserves_every_original_operation_and_fixed_bound(self):
        from tests.host.terminal_failure_source import (
            fixed_source, observe_source, project_terminal_runtime, terminal_runtime_source,
        )

        for path in (
            "arch/riscv64/uart.c", "arch/riscv64/platform.h",
            "arch/riscv64/sbi.c", "kernel/diagnostic/panic.c",
        ):
            candidate = terminal_runtime_source(path)
            if path.endswith(".c"):
                candidate = observe_source(path, candidate)
            self.assertEqual(fixed_source(path), project_terminal_runtime(path, candidate))
        uart = terminal_runtime_source("arch/riscv64/uart.c")
        with self.assertRaises(ValueError):
            project_terminal_runtime("arch/riscv64/uart.c", uart.replace("UINT32_C(100000)", "UINT32_C(100001)", 1))
        reset = terminal_runtime_source("arch/riscv64/sbi.c")
        with self.assertRaises(ValueError):
            project_terminal_runtime("arch/riscv64/sbi.c", reset.replace("!micros_panic_claim_reset()", "true", 1))
        report = terminal_runtime_source("kernel/diagnostic/panic.c")
        with self.assertRaises(ValueError):
            project_terminal_runtime("kernel/diagnostic/panic.c", report.replace("!micros_panic_claim_first()", "false", 1))

    def test_observation_overlay_is_exactly_reversible_to_each_original_body(self):
        from tests.host.terminal_failure_evidence import DESIGN_PINS
        from tests.host.terminal_failure_source import observe_source, project_observations

        for path in (
            "arch/riscv64/uart.c", "arch/riscv64/sbi.c",
            "kernel/diagnostic/panic.c", "kernel/boot/boot_component_tests.c",
        ):
            original = subprocess.check_output(
                ("git", "show", DESIGN_PINS["runtime_reference"] + ":" + path),
                cwd=ROOT, text=True, timeout=30,
            )
            observed = observe_source(path, original)
            self.assertNotEqual(original, observed)
            self.assertEqual(original, project_observations(path, observed))
            with self.assertRaises(ValueError):
                observe_source(path, observed)
            if path == "arch/riscv64/uart.c":
                for changed in (
                    observed.replace("terminal-thre-poll", "terminal-temt-poll", 1),
                    observed.replace("MICROS_TERMINAL_TEST_LSR(uart0[UART_LSR], false)", "0", 1),
                    observed.replace("micros_terminal_test_thr();", "micros_terminal_test_thr(); micros_terminal_test_thr();", 1),
                ):
                    with self.assertRaises(ValueError):
                        project_observations(path, changed)
            if path == "kernel/boot/boot_component_tests.c":
                self.assertEqual(
                    [(index, line) for index, line in enumerate(original.splitlines(), 1) if "MICROS_PANIC(" in line],
                    [(index, line) for index, line in enumerate(observed.splitlines(), 1) if "MICROS_PANIC(" in line],
                )

    def test_recipe_and_preset_additions_recover_complete_fixed_inputs(self):
        from tests.host.terminal_failure_evidence import DESIGN_PINS
        from tests.host.terminal_failure_source import (
            extend_cmake, extend_presets, project_terminal_cmake, project_terminal_presets,
        )

        for path, extend, project in (
            ("CMakeLists.txt", extend_cmake, project_terminal_cmake),
            ("CMakePresets.json", extend_presets, project_terminal_presets),
        ):
            original = subprocess.check_output(
                ("git", "show", DESIGN_PINS["runtime_reference"] + ":" + path),
                cwd=ROOT, text=True, timeout=30,
            )
            current = extend(original)
            self.assertEqual(original, project(current))
            with self.assertRaises(ValueError):
                extend(current)
            with self.assertRaises(ValueError):
                project(current.replace("test-qemu-terminal-failure", "test-qemu-foreign", 1))
            with self.assertRaises(ValueError):
                project(current + "\n# unexpected recipe input\n")


class TerminalRemoteTest(unittest.TestCase):
    def test_cli_rejects_external_debug_inputs_and_reserves_baseline_exit_one(self):
        from tools import check_terminal_failures as controller

        with patch.object(controller, "execute", side_effect=RuntimeError("execution failure")):
            errors = io.StringIO()
            with redirect_stderr(errors):
                self.assertEqual(2, controller.main(["--baseline"]))
            self.assertIn("MICROS_PLAT02_EXECUTION_ERROR", errors.getvalue())
        with patch.object(controller, "execute") as execute:
            for option in ("--source", "--kernel", "--qemu", "--timeout", "--scenario", "--address", "--command"):
                with self.subTest(option=option), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                    controller.main([option, "untrusted"])
                self.assertEqual(2, error.exception.code)
            execute.assert_not_called()

    def test_packets_reject_wrong_checksums_sizes_framing_and_unsupported_encoding(self):
        from tools.check_terminal_failures import decode_packet, encode_packet

        for payload in (b"T05core:01;", b"OK", b"0010000000000000"):
            self.assertEqual(payload, decode_packet(encode_packet(payload)))
        valid = encode_packet(b"OK")
        for packet in (
            valid[:-1], valid + b"+", b"!" + valid[1:], b"$OK#00",
            b"$O*K#c4", b"$O}K#17", b"$\x80#80", b"$" + b"0" * 16385 + b"#00",
        ):
            with self.subTest(packet=packet[:20]), self.assertRaises(ValueError):
                decode_packet(packet)

    def test_remote_selector_is_once_only_at_kernel_main_and_mmio_is_case_bound(self):
        from tools.check_terminal_failures import ImageBinding, Remote, encode_packet

        class Connection:
            def __init__(self, replies):
                self.replies = bytearray(replies)
                self.sent = []

            def settimeout(self, timeout):
                self.timeout = timeout

            def sendall(self, data):
                self.sent.append(data)

            def recv(self, size):
                value = bytes(self.replies[:min(size, 1)])
                del self.replies[:len(value)]
                return value

        binding = ImageBinding(
            "fixture", (("micros_terminal_test_scenario", 0x8000, 4),),
            (("kernel_main", 0x9000, 4),), (0xa000, 0xe000), False,
        )
        connection = Connection(b"+" + encode_packet(b"OK"))
        remote = Remote(connection, binding, 1, time.monotonic() + 8)
        with self.assertRaises(ValueError):
            remote.select_scenario()
        self.assertEqual([], connection.sent)
        remote.stopped_at = "kernel_main"
        remote.select_scenario()
        self.assertEqual(encode_packet(b"M8000,4:01000000"), connection.sent[0])
        with self.assertRaises(ValueError):
            remote.select_scenario()
        with self.assertRaises(ValueError):
            remote.read_ier()
        with self.assertRaises(ValueError):
            remote.read_object("unrelated-memory")
        for payload in (b"P2=0000000000000000", b"M8000,8:0000000000000000", b"m10000005,1", b"qRcmd,00"):
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                remote.exchange(payload)
        raw_connection = Connection((b"+" + encode_packet(b"OK")) * 2)
        raw = Remote(raw_connection, binding, 1, time.monotonic() + 8)
        raw.stopped_at = "kernel_main"
        raw.exchange(b"M8000,4:01000000")
        with self.assertRaises(ValueError):
            raw.exchange(b"M8000,4:01000000")
        self.assertEqual(2, len(raw_connection.sent))

    def test_disconnect_nack_and_expired_deadline_are_not_a_stop_or_success(self):
        from tools.check_terminal_failures import ImageBinding, Remote

        class Connection:
            def __init__(self, reply):
                self.reply = reply

            def settimeout(self, timeout):
                pass

            def sendall(self, data):
                pass

            def recv(self, size):
                return self.reply

        binding = ImageBinding("fixture", (), (), (0xa000, 0xe000), False)
        for reply in (b"", b"-", b"$"):
            with self.subTest(reply=reply), self.assertRaises(ValueError):
                Remote(Connection(reply), binding, 1, time.monotonic() + 8).exchange(b"?")
        with self.assertRaises(TimeoutError):
            Remote(Connection(b""), binding, 1, time.monotonic() - 1).exchange(b"?")


class TerminalCaptureTest(unittest.TestCase):
    @staticmethod
    def early_failure(*, baseline=False, reads=None):
        from tests.host.terminal_failure_evidence import GuestState, TerminalCapture

        reads = (100001 if baseline else 100000) if reads is None else reads
        armed = struct.pack("<IIII9QIIII", 1, 5, 1, 0, *([0] * 9), 0xffffffff, 0, 0, 0)
        final = struct.pack(
            "<IIII9QIIII", 1, 5, 3 if baseline else 2, 0,
            reads, 0, 0, 0, 0, 0 if baseline else 1, 0, 0, 0,
            0, 0, 0 if baseline else 1, 0,
        )
        counts = [0] * (27 * 14)
        counts[14] = reads
        work = struct.pack("<IIQQII378Q", 1, 0, 0x504C415430320005, 1, 2, 0, *counts)
        initial = GuestState(
            armed, bytes(12012), struct.pack("<QIB3x", 0x554152544F574E52, 1, 0), bytes(8),
            None if baseline else bytes(4), bytes(3056), None, 0xc000,
        )
        stopped = replace(initial, observation=final, work=work,
                          control=None if baseline else struct.pack("<I", 2), sp=0xb000)
        return TerminalCapture(
            5, "overrun" if baseline else "reset", initial, stopped,
            ((2, 0xb000), (10, 0), (11, 1), (16, 0), (17, 0x53525354), (32, 0x9000)),
            (0xa000, 0xe000), 0x9000,
        )

    def test_no_console_failure_requires_actual_ownership_work_registers_and_no_mutation(self):
        from tests.host.terminal_failure_evidence import validate_capture

        capture = self.early_failure()
        validate_capture(capture, baseline=False)
        for invalid in (
            replace(capture, final=replace(capture.final, control=None)),
            replace(capture, final=replace(capture.final, control=bytes(4))),
            replace(capture, final=replace(capture.final, history=b"x" + capture.final.history[1:])),
            replace(capture, final=replace(capture.final, work=None)),
            replace(capture, final=replace(capture.final, sp=0xc001)),
            replace(capture, registers=tuple((key, 0 if key == 17 else value) for key, value in capture.registers)),
            replace(capture, checkpoint="timeout"),
        ):
            with self.subTest(capture=invalid.checkpoint), self.assertRaises(ValueError):
                validate_capture(invalid, baseline=False)

    def test_baseline_red_is_only_the_actual_100001st_observation_with_typed_absence(self):
        from tests.host.terminal_failure_evidence import validate_capture

        capture = self.early_failure(baseline=True)
        validate_capture(capture, baseline=True)
        with self.assertRaises(ValueError):
            validate_capture(capture, baseline=False)
        with self.assertRaises(ValueError):
            validate_capture(self.early_failure(baseline=True, reads=99999), baseline=True)
        with self.assertRaises(ValueError):
            validate_capture(replace(capture, final=replace(capture.final, control=bytes(4))), baseline=True)


class TerminalArchiveTest(unittest.TestCase):
    def test_fixed_archive_keeps_real_original_bodies_and_no_substitute_control(self):
        from tools.check_terminal_failures import prepare_baseline, validate_baseline
        from tests.host.terminal_failure_evidence import DESIGN_PINS
        from tests.host.terminal_failure_source import fixed_source

        with tempfile.TemporaryDirectory(prefix="pt2-archive-", dir=ROOT / "build") as temporary:
            source = prepare_baseline(Path(temporary))
            validate_baseline(source)
            protected = source / "kernel/boot/boot_foundation.c"
            original = subprocess.check_output(
                ("git", "show", DESIGN_PINS["runtime_reference"] + ":kernel/boot/boot_foundation.c"),
                cwd=ROOT, timeout=30,
            )
            self.assertEqual(original, protected.read_bytes())
            self.assertIn("while ((uart0[UART_LSR]", fixed_source("arch/riscv64/uart.c"))
            for name in ("panic_control.c", "panic_control_core.c", "panic_control.h", "panic_control_core.h"):
                self.assertFalse((source / "kernel/diagnostic" / name).exists())
            for path, changed in (
                (protected, original + b"\n"),
                (source / "tests/qemu/terminal_failure_kernel.c", b"/* borrowed fixture */\n"),
            ):
                previous = path.read_bytes()
                path.write_bytes(changed)
                with self.assertRaises(ValueError):
                    validate_baseline(source)
                path.write_bytes(previous)
            substitute = source / "kernel/diagnostic/panic_control_core.c"
            substitute.write_text("int fake_control;\n")
            with self.assertRaises(ValueError):
                validate_baseline(source)


class TerminalImageBindingTest(unittest.TestCase):
    def test_function_name_collision_is_not_a_panic_control_object(self):
        from tools.check_terminal_failures import _symbol
        from tools.user_elf_format import ElfSymbol

        function = ElfSymbol("panic_control", 0, 2, 1, 0x8000, 80)
        control = ElfSymbol("panic_control", 0, 1, 2, 0x9000, 4)
        self.assertEqual(
            control, _symbol(SimpleNamespace(symbols=(function, control)), "panic_control", symbol_type=1),
        )
        self.assertEqual(
            function, _symbol(SimpleNamespace(symbols=(function, control)), "panic_control", symbol_type=2),
        )
        for symbols in ((function,), (function, control, replace(control, value=0xa000))):
            with self.assertRaises(ValueError):
                _symbol(SimpleNamespace(symbols=symbols), "panic_control", symbol_type=1)


class TerminalContextTest(unittest.TestCase):
    @staticmethod
    def graph(*, recursive=False):
        from tools.stack_bound_context import ContextBindings
        from tools.stack_bound_types import CallSite, EdgeKind, Frame, FunctionKey, Root

        names = {
            name: FunctionKey("terminal-fixture", "kernel/micros.elf", unit, name)
            for unit, members in (
                ("arch/riscv64/uart.c", ("uart_write_direct", "uart_write", "uart_write_record",
                                       "uart_write_hex64", "uart_write_hex64_record", "uart_write_character")),
                ("arch/riscv64/panic.S", ("micros_panic_entry", "micros_trap_panic_entry")),
                ("kernel/diagnostic/panic.c", ("micros_panic_report", "panic_report_claimed", "micros_panic_seize")),
                ("kernel/diagnostic/panic_control.c", ("micros_panic_claim_first", "micros_panic_terminate_no_console")),
                ("tests/qemu/terminal_failure_kernel.c", ("micros_terminal_test_after_seize",)),
            ) for name in members
        }
        frame_sizes = {name: 16 for name in names}
        frame_sizes.update({
            "micros_panic_entry": 0, "micros_trap_panic_entry": 48,
            "panic_report_claimed": 64, "uart_write_character": 16 if recursive else 128,
            "micros_panic_terminate_no_console": 96 if recursive else 16,
        })
        pairs = (
            ("uart_write_direct", 0, "micros_panic_entry"),
            ("uart_write_direct", 4, "uart_write_character"),
            ("uart_write", 0, "uart_write_record"), ("uart_write", 4, "uart_write_direct"),
            ("uart_write_record", 0, "micros_panic_entry"),
            ("uart_write_hex64", 0, "uart_write_hex64_record"), ("uart_write_hex64", 4, "uart_write_direct"),
            ("uart_write_hex64_record", 0, "micros_panic_entry"),
            ("micros_panic_entry", 0, "micros_trap_panic_entry"),
            ("micros_trap_panic_entry", 0, "micros_panic_report"),
            ("micros_panic_report", 0, "micros_panic_claim_first"),
            ("micros_panic_report", 4, "panic_report_claimed"),
            ("micros_panic_report", 8, "micros_panic_terminate_no_console"),
            ("panic_report_claimed", 0, "micros_panic_seize"),
            ("panic_report_claimed", 4, "uart_write_direct"),
        )
        if recursive:
            pairs += (
                ("panic_report_claimed", 8, "micros_terminal_test_after_seize"),
                ("micros_terminal_test_after_seize", 0, "micros_panic_entry"),
            )
        edges = tuple(CallSite(
            names[caller], offset, (names[target],),
            EdgeKind.TAIL if caller == "micros_panic_entry" else EdgeKind.CALL,
        ) for caller, offset, target in pairs)
        exclusions = tuple((names[name], offset) for name, offset in (
            ("uart_write_direct", 0), ("uart_write", 0), ("uart_write_hex64", 0), ("micros_panic_report", 4),
        ))
        binding = ContextBindings(
            establishing=((names["panic_report_claimed"], 4),) + (
                ((names["panic_report_claimed"], 8),) if recursive else ()
            ),
            preservers=tuple(names.values()),
            infeasible=exclusions,
            latching=((names["micros_panic_report"], 4),),
            latch_preservers=tuple(names.values()),
        )
        return (
            names, tuple(Frame(names[name], size) for name, size in frame_sizes.items()),
            edges, (Root("boot", names["uart_write_direct"], 16384),), binding,
        )

    def test_uart_ready_and_entry_latch_are_distinct_and_retain_both_uart_frames(self):
        from tools.stack_bound_context import Qualifier, certify_context_bounds
        from tools.stack_bound_types import StackEvidenceError

        names, frames, edges, roots, binding = self.graph()
        proof, = certify_context_bounds(frames, edges, roots, binding)
        self.assertEqual(
            [Qualifier.UNQUALIFIED, Qualifier.PANIC_LATCHED_UART_READY],
            [node.qualifier for node in proof.witness if node.function == names["uart_write_direct"]],
        )
        self.assertTrue(any(
            node.function == names["panic_report_claimed"] and node.qualifier is Qualifier.PANIC_LATCHED
            for node in proof.witness
        ))
        with self.assertRaises(StackEvidenceError):
            certify_context_bounds(frames, edges, roots, replace(binding, establishing=()))
        with self.assertRaises(StackEvidenceError):
            certify_context_bounds(frames, edges, roots, replace(binding, latching=()))

    def test_recursive_entry_keeps_both_real_captures_without_a_second_body(self):
        from tools.stack_bound_context import Qualifier, certify_context_bounds
        from tools.stack_bound_types import StackEvidenceError

        names, frames, edges, roots, binding = self.graph(recursive=True)
        proof, = certify_context_bounds(frames, edges, roots, binding)
        captures = [node for node in proof.witness if node.function == names["micros_trap_panic_entry"]]
        self.assertEqual([Qualifier.UNQUALIFIED, Qualifier.PANIC_LATCHED_UART_READY], [node.qualifier for node in captures])
        self.assertEqual(1, sum(node.function == names["panic_report_claimed"] for node in proof.witness))
        for changed in (
            replace(binding, infeasible=binding.infeasible[:3]),
            replace(binding, infeasible=(*binding.infeasible, binding.infeasible[3])),
            replace(binding, latch_preservers=tuple(key for key in binding.latch_preservers if key.symbol != "micros_panic_entry")),
            replace(binding, preservers=tuple(key for key in binding.preservers if key.symbol != "micros_terminal_test_after_seize")),
        ):
            with self.assertRaises(StackEvidenceError):
                certify_context_bounds(frames, edges, roots, changed)
        with self.assertRaises(StackEvidenceError):
            certify_context_bounds(frames, edges, (replace(roots[0], capacity=proof.bytes_used - 1),), binding)

    def test_only_actually_reached_qualifications_need_their_preservation_effects(self):
        from tools.stack_bound_context import certify_context_bounds
        from tools.stack_bound_types import CallSite, StackEvidenceError

        names, frames, edges, roots, binding = self.graph(recursive=True)
        reduced = replace(binding, latch_preservers=tuple(
            function for function in binding.latch_preservers
            if function.symbol not in ("uart_write_record", "uart_write_hex64_record")
        ))
        certify_context_bounds(frames, edges, roots, reduced)
        with self.assertRaises(StackEvidenceError):
            certify_context_bounds(frames, (
                *edges, CallSite(names["panic_report_claimed"], 12, (names["uart_write_record"],)),
            ), roots, reduced)
        with self.assertRaises(StackEvidenceError):
            certify_context_bounds(frames, (
                *edges, CallSite(names["uart_write_record"], 4, (names["uart_write_record"],)),
            ), roots, reduced)


class TerminalControlMachineTest(unittest.TestCase):
    def test_uart_machine_repairs_only_semantic_fields_and_keeps_real_terminal_edges(self):
        from tests.host.terminal_failure_source import CONTROL_SOURCES, terminal_runtime_source
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import uart_machine_effects
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        declarations = terminal_runtime_source("arch/riscv64/platform.h") + CONTROL_SOURCES["kernel/diagnostic/panic_control.h"]
        original = terminal_runtime_source("arch/riscv64/uart.c")
        original = original.replace('#include "kernel/diagnostic/panic_control.h"', "", 1)
        with tempfile.TemporaryDirectory(prefix="pt2-uart-effects-", dir=ROOT / "build") as temporary:
            for index, source in enumerate((
                original,
                original.replace("uart_console_state.owner = MICROS_UART_CONSOLE_OWNER_PANIC;",
                                 "uart_console_state.owner = MICROS_UART_CONSOLE_OWNER_EARLY;", 1),
                original.replace("uart_console_state.quiesced = true;", "((uint8_t *)&uart_console_state)[13] = 1;", 1),
                original.replace("return repaired;", "return !repaired;", 1),
            )):
                directory = Path(temporary) / str(index)
                directory.mkdir()
                _, output = compile_fixture(directory, declarations + "\n" + source)
                obj = load_object(output)
                symbols = function_symbols(obj)
                console, = (symbol for symbol in obj.symbols if symbol.name == "uart_console_state")
                if index:
                    with self.subTest(index=index), self.assertRaises(StackEvidenceError):
                        uart_machine_effects(obj, symbols["uart_panic_seize"], terminal=True, console=console)
                else:
                    uart_machine_effects(obj, symbols["uart_panic_seize"], terminal=True, console=console)
                    for name in ("uart_flush", "uart_write_character"):
                        effects = uart_machine_effects(obj, symbols[name], terminal=True)
                        self.assertIn("micros_panic_terminate_no_console", {target for _, target in effects.calls})

    def test_reporter_machine_claim_dominates_every_argument_dereference_and_first_body(self):
        from tests.host.terminal_failure_source import CONTROL_SOURCES, PANIC_ENTRY_WRAPPER
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import terminal_reporter_effects
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        declarations = (
            '#include "micros/panic.h"\n' + CONTROL_SOURCES["kernel/diagnostic/panic_control.h"]
            + "_Noreturn void panic_report_claimed(uintptr_t, const char *, const char *, uint32_t, "
            "const struct micros_trap_frame *, const struct micros_panic_machine_context *);\n"
        )
        with tempfile.TemporaryDirectory(prefix="pt2-report-gate-", dir=ROOT / "build") as temporary:
            for index, wrapper in enumerate((
                PANIC_ENTRY_WRAPPER,
                PANIC_ENTRY_WRAPPER.replace("!micros_panic_claim_first()", "micros_panic_claim_first()", 1),
                PANIC_ENTRY_WRAPPER.replace("    if (!micros_panic_claim_first())",
                                           "    (void)*(volatile const char *)reason;\n    if (!micros_panic_claim_first())", 1),
                PANIC_ENTRY_WRAPPER.replace("hart_id, reason, file, line, trap_frame, context",
                                           "hart_id, reason, file, line + 1, trap_frame, context", 1),
            )):
                directory = Path(temporary) / str(index)
                directory.mkdir()
                _, output = compile_fixture(directory, declarations + wrapper)
                obj = load_object(output)
                if index:
                    with self.subTest(index=index), self.assertRaises(StackEvidenceError):
                        terminal_reporter_effects(obj, function_symbols(obj)["micros_panic_report"])
                else:
                    terminal_reporter_effects(obj, function_symbols(obj)["micros_panic_report"])

    def test_runtime_claims_bind_the_real_control_and_reject_false_results_or_irq_enable(self):
        from tests.host.terminal_failure_source import CONTROL_SOURCES
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import panic_runtime_effects
        from tools.stack_bound_objects import load_object
        from tools.stack_bound_types import StackEvidenceError

        declarations = CONTROL_SOURCES["kernel/diagnostic/panic_control_core.h"] + CONTROL_SOURCES["kernel/diagnostic/panic_control.h"]
        source = CONTROL_SOURCES["kernel/diagnostic/panic_control.c"]
        source = source.replace('#include "kernel/diagnostic/panic_control.h"', "", 1)
        source = source.replace('#include "kernel/diagnostic/panic_control_core.h"', "", 1)
        with tempfile.TemporaryDirectory(prefix="pt2-runtime-effects-", dir=ROOT / "build") as temporary:
            for index, current in enumerate((
                source,
                source.replace("return result == MICROS_PANIC_FIRST;", "return true;", 1),
                source.replace("&panic_control", "(struct micros_panic_control *)0x10000000", 1),
                source.replace("result = micros_panic_control_enter", '__asm__ volatile("csrsi sstatus, 2"); result = micros_panic_control_enter', 1),
            )):
                directory = Path(temporary) / str(index)
                directory.mkdir()
                _, output = compile_fixture(directory, declarations + "\n" + current)
                if index:
                    with self.subTest(index=index), self.assertRaises(StackEvidenceError):
                        panic_runtime_effects(load_object(output))
                else:
                    self.assertEqual(4, len(panic_runtime_effects(load_object(output))))

    def test_shared_srst_machine_guard_covers_only_failure_shutdown_and_cannot_bypass_claim(self):
        from tests.host.terminal_failure_source import CONTROL_SOURCES, terminal_runtime_source
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import terminal_srst_effects
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        source = CONTROL_SOURCES["kernel/diagnostic/panic_control.h"] + terminal_runtime_source("arch/riscv64/sbi.c").replace(
            '#include "kernel/diagnostic/panic_control.h"', "", 1,
        )
        with tempfile.TemporaryDirectory(prefix="pt2-srst-effects-", dir=ROOT / "build") as temporary:
            for index, current in enumerate((
                source,
                source.replace("reset_reason == SBI_RESET_REASON_SYSTEM_FAILURE", "reset_reason != SBI_RESET_REASON_SYSTEM_FAILURE", 1),
                source.replace("        micros_panic_halt();\n", "        (void)0;\n", 1),
                source.replace("SBI_SYSTEM_RESET = 0", "SBI_SYSTEM_RESET = 1", 1),
            )):
                directory = Path(temporary) / str(index)
                directory.mkdir()
                _, output = compile_fixture(directory, current)
                obj = load_object(output)
                symbol = function_symbols(obj)["sbi_system_reset"]
                if index:
                    with self.subTest(index=index), self.assertRaises(StackEvidenceError):
                        terminal_srst_effects(obj, symbol)
                else:
                    terminal_srst_effects(obj, symbol)

    def test_actual_machine_phase_classes_reject_clearing_aliasing_and_unknown_phase_coercion(self):
        from tests.host.terminal_failure_source import CONTROL_SOURCES
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import panic_control_effects
        from tools.stack_bound_objects import load_object
        from tools.stack_bound_types import StackEvidenceError

        header = CONTROL_SOURCES["kernel/diagnostic/panic_control_core.h"]
        original = CONTROL_SOURCES["kernel/diagnostic/panic_control_core.c"].replace(
            '#include "kernel/diagnostic/panic_control_core.h"', "", 1,
        )
        with tempfile.TemporaryDirectory(prefix="pt2-control-machine-", dir=ROOT / "build") as temporary:
            for index, source in enumerate((
                original,
                original.replace("control->phase = MICROS_PANIC_REPORTING;", "control->phase = MICROS_PANIC_RESET_REQUESTED;", 1),
                original.replace("return MICROS_PANIC_ALREADY_ISSUED;",
                                 "control->phase = MICROS_PANIC_IDLE; return MICROS_PANIC_ALREADY_ISSUED;", 1),
                original.replace("control->phase == MICROS_PANIC_IDLE", "(control->phase & 3) == MICROS_PANIC_IDLE", 1),
                original.replace("control->phase = MICROS_PANIC_REPORTING;",
                                 "*(volatile uint32_t *)0x10000000 = 1; control->phase = MICROS_PANIC_REPORTING;", 1),
            )):
                directory = Path(temporary) / str(index)
                directory.mkdir()
                _, output = compile_fixture(directory, header + "\n" + source)
                if index:
                    with self.subTest(index=index), self.assertRaises(StackEvidenceError):
                        panic_control_effects(load_object(output))
                else:
                    proof = panic_control_effects(load_object(output))
                    self.assertEqual({"enter", "reset"}, {row[0] for row in proof})


if __name__ == "__main__":
    unittest.main()
