"""Fatal qualifiers require complete source, linked and preservation evidence."""

from dataclasses import replace
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tests.host.stack_fatal_fixture import build_fatal_fixture, fixture_catalog
from tools.stack_bound_types import StackEvidenceError


ROOT = Path(__file__).resolve().parents[2]


class StackFatalBindingsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="micros-fatal-bindings-", dir=ROOT / "build")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root, cls.directory, dependencies = build_fatal_fixture(
            ROOT, Path(cls.temporary.name).resolve(),
        )
        cls.catalog = fixture_catalog(cls.root, cls.directory, dependencies)

    def bind(self, catalog=None):
        from tools.stack_bound_fatal import derive_fatal_context

        with patch("tools.stack_bound_fatal.resolve_default_firmware", return_value={"fixture": "pinned"}):
            return derive_fatal_context(
                self.root, self.directory, self.catalog if catalog is None else catalog,
                qemu="/fixture/qemu-system-riscv64",
            )

    def test_qualification_records_every_post_seizure_call_and_the_exact_guard(self):
        proof = self.bind()
        self.assertEqual(4, proof.version)
        history_calls = tuple(row for row in proof.establishment if row.target == "micros_diagnostic_panic_report")
        trace_calls = tuple(row for row in proof.establishment if row.target == "micros_panic_trace_report")
        self.assertEqual(1, len(history_calls))
        self.assertEqual(1, len(trace_calls))
        self.assertEqual(52, len(proof.bindings.establishing) - len(history_calls) - len(trace_calls))
        self.assertEqual(
            {"uart_write_direct", "uart_write", "uart_write_hex64", "micros_panic_report"},
            {key.symbol for key, _ in proof.bindings.infeasible},
        )
        self.assertEqual(
            {"direct-console-owner", "text-recording-arm", "hex-recording-arm", "first-report-body"},
            {row.role for row in proof.exclusions},
        )
        self.assertEqual(4, len(proof.exclusions))
        self.assertEqual(52, len(proof.establishment) - len(history_calls) - len(trace_calls))
        self.assertEqual(("panic_control", 4), (proof.control.symbol, proof.control.size))
        self.assertEqual(1, len(proof.bindings.latching))
        self.assertTrue(proof.bindings.latch_preservers)
        self.assertTrue(any(row.function.symbol == "sbi_system_reset" for row in proof.preservation))
        self.assertTrue(any(row.function.symbol == "uart_flush" for row in proof.preservation))
        self.assertEqual(16, proof.console.size)
        self.assertEqual(("diagnostic_history", 12012), (proof.history.symbol, proof.history.size))
        self.assertEqual({"sbi_set_timer", "sbi_system_reset"}, {
            row.function.symbol for row in proof.sbi_inventory
        })

    def test_missing_flush_reset_origin_or_machine_site_cannot_qualify(self):
        for name in ("uart_flush", "sbi_system_reset"):
            target = next(item.function for item in self.catalog.frames if item.function.symbol == name)
            with self.subTest(omitted=name), self.assertRaises(StackEvidenceError):
                self.bind(replace(
                    self.catalog, frames=tuple(frame for frame in self.catalog.frames if frame.function != target),
                ))
        call = next(call for call in self.catalog.calls if (
            call.caller.symbol == "panic_report_claimed" and call.targets[0].symbol == "sbi_system_reset"
        ))
        with self.assertRaises(StackEvidenceError):
            self.bind(replace(self.catalog, calls=tuple(item for item in self.catalog.calls if item != call)))
        origin = next(row for row in self.catalog.origins if row.function.symbol == "uart_write")
        with self.assertRaises(StackEvidenceError):
            self.bind(replace(self.catalog, origins=tuple(
                replace(row, line=row.line + 1) if row == origin else row for row in self.catalog.origins
            )))

    def test_layout_cache_cannot_borrow_the_other_admitted_process_geometry(self):
        from tools.stack_bound_fatal_effects import trace_object_layout

        header = self.root / "include/micros/kernel_objects.h"
        original = header.read_text()
        extended = original.replace(
            "    bool endpoint_lifecycle_consumed;\n",
            "    bool endpoint_lifecycle_consumed;\n    struct micros_thread_handle sole_thread;\n", 1,
        )
        self.assertNotEqual(original, extended)
        baseline = trace_object_layout(self.root)
        try:
            header.write_text(extended)
            current = trace_object_layout(self.root)
            self.assertEqual(baseline["process_bytes"] + 8, current["process_bytes"])
            for name in ("thread_table", "hart_table", "registry_bytes"):
                self.assertEqual(baseline[name] + 8 * baseline["process_capacity"], current[name], name)
            header.write_text((ROOT / "include/micros/kernel_objects.h").read_text())
            reply = trace_object_layout(self.root)
            self.assertEqual(current["process_bytes"], reply["process_bytes"])
            self.assertEqual(current["thread_bytes"] + 8, reply["thread_bytes"])
            self.assertEqual(current["thread_table"], reply["thread_table"])
            for name in ("hart_table", "registry_bytes"):
                self.assertEqual(current[name] + 8 * current["thread_capacity"], reply[name], name)
        finally:
            header.write_text(original)
        self.assertEqual(baseline, trace_object_layout(self.root))

    def test_selected_owner_extension_cannot_change_resolver_or_shared_state(self):
        source = self.root / "kernel/objects/kernel_object_identity.c"
        original = source.read_text()
        extension = "\nint unrelated_owner_query(void);\nint unrelated_owner_query(void) { return 0; }\n"
        try:
            source.write_text(original + extension)
            self.bind()
            for changed in (
                original + extension + "\nstatic uint64_t hidden;\n",
                original + extension.replace("return 0;", "\n#define MICROS_KERNEL_OBJECTS_MAGIC 0\nreturn 0;"),
                original.replace("handle.generation != 0", "handle.generation == 0", 1) + extension,
            ):
                source.write_text(changed)
                with self.assertRaises(StackEvidenceError):
                    self.bind()
        finally:
            source.write_text(original)

    def test_fault_prefix_extension_retains_its_source_and_actual_frame(self):
        from tools.stack_bound_fatal import collect_fatal_artifacts, trace_fault_artifacts

        unit = "kernel/objects/kernel_object_identity.c"
        name = "kernel_object_require_initialized"
        artifacts = collect_fatal_artifacts(
            self.root, self.directory, self.catalog, observed_mode=False, tty_test=False,
        )
        source = self.root / unit
        original = source.read_text()
        extension = "\nint unrelated_lifetime_query(void) { return 0; }\n"
        try:
            with patch("tools.stack_bound_fatal.TRACE_FAULT_FUNCTIONS", ((unit, name),)):
                trace_fault_artifacts(self.root, self.directory, self.catalog, artifacts)
                source.write_text(original + extension)
                trace_fault_artifacts(self.root, self.directory, self.catalog, artifacts)
                for changed in (
                    original + extension + "\nstatic uint64_t hidden;\n",
                    original.replace("objects->initialization_magic !=", "objects->initialization_magic ==", 1)
                    + extension,
                ):
                    source.write_text(changed)
                    with self.assertRaises(StackEvidenceError):
                        trace_fault_artifacts(self.root, self.directory, self.catalog, artifacts)
                source.write_text(original + extension)
                changed = replace(self.catalog, frames=tuple(
                    replace(row, bytes_used=row.bytes_used + 16) if row.function.symbol == name else row
                    for row in self.catalog.frames
                ))
                with self.assertRaises(StackEvidenceError):
                    trace_fault_artifacts(self.root, self.directory, changed, artifacts)
        finally:
            source.write_text(original)

    def test_new_control_and_claimed_report_reject_foreign_source_origins(self):
        for name, foreign in (
            ("micros_panic_claim_first", "kernel/diagnostic/panic.c"),
            ("panic_report_claimed", "kernel/diagnostic/panic_control.c"),
        ):
            origin = next(row for row in self.catalog.origins if row.function.symbol == name)
            with self.subTest(name=name), self.assertRaises(StackEvidenceError):
                self.bind(replace(self.catalog, origins=tuple(
                    replace(row, source=foreign) if row == origin else row for row in self.catalog.origins
                )))

    def test_tty_counter_has_its_own_linked_object_effect_and_live_frame(self):
        from tools.stack_bound_fatal import derive_fatal_context

        with tempfile.TemporaryDirectory(prefix="micros-fatal-tty-counter-", dir=ROOT / "build") as directory:
            root, output, dependencies = build_fatal_fixture(ROOT, Path(directory).resolve(), tty_test=True)
            catalog = fixture_catalog(root, output, dependencies)
            with patch("tools.stack_bound_fatal.resolve_default_firmware", return_value={"fixture": "pinned"}):
                proof = derive_fatal_context(root, output, catalog, qemu="/fixture/qemu-system-riscv64")
            self.assertEqual(("diagnostic_thr_writes", 8), (proof.test_counter.symbol, proof.test_counter.size))
            for other in (proof.console, proof.history):
                self.assertTrue(
                    proof.test_counter.address + proof.test_counter.size <= other.address
                    or other.address + other.size <= proof.test_counter.address
                )
            observer, = (row for row in proof.preservation if row.function.symbol == "micros_diagnostic_test_observe_thr")
            character, = (row for row in proof.preservation if row.function.symbol == "uart_write_character")
            self.assertIn(observer.function, proof.bindings.preservers)
            self.assertEqual(1, len(observer.writes))
            self.assertEqual({"maximum", "below-maximum"}, {row[0] for row in observer.relations})
            self.assertEqual(1, sum(row.target == observer.function.symbol for row in character.calls))

    def test_terminal_observers_have_disjoint_actual_atomic_and_snapshot_effects(self):
        from tools.stack_bound_fatal import derive_fatal_context
        from tools.stack_bound_assembly import collect_linked_assembly
        from tools.stack_bound_context import certify_context_bounds
        from tools.stack_bound_types import Root

        with tempfile.TemporaryDirectory(prefix="micros-fatal-terminal-", dir=ROOT / "build") as directory:
            root, output, dependencies = build_fatal_fixture(ROOT, Path(directory).resolve(), terminal_test=True)
            catalog = fixture_catalog(root, output, dependencies)
            with patch("tools.stack_bound_fatal.resolve_default_firmware", return_value={"fixture": "pinned"}):
                proof = derive_fatal_context(root, output, catalog, qemu="/fixture/qemu-system-riscv64")
            observed = {item.symbol: item.size for item in proof.test_objects}
            self.assertEqual(104, observed["micros_terminal_test_observation"])
            self.assertEqual(3056, observed["micros_terminal_test_work_snapshot"])
            self.assertEqual(3072, observed["collector"])
            self.assertTrue(any(row.function.symbol == "micros_test_work_record" for row in proof.preservation))
            self.assertTrue(any(row.function.symbol == "publish_snapshot" for row in proof.preservation))
            graph = collect_linked_assembly(root, output, "kernel/micros.elf", "riscv64-fatal-fixture", catalog)
            report = next(frame.function for frame in graph.frames if frame.function.symbol == "micros_panic_report")
            certify_context_bounds(graph.frames, graph.edges, (Root("panic", report, 4096, 48),), proof.bindings)

    def test_compiler_hooks_remain_separate_from_actual_constructor_and_state_calls(self):
        from tools.stack_bound_fatal import derive_fatal_context
        from tools.stack_bound_assembly import collect_linked_assembly
        from tools.stack_bound_context import certify_context_bounds
        from tools.stack_bound_types import Root

        with tempfile.TemporaryDirectory(prefix="micros-fatal-compiler-hooks-", dir=ROOT / "build") as directory:
            root, output, dependencies = build_fatal_fixture(ROOT, Path(directory).resolve(), observed=True)
            catalog = fixture_catalog(root, output, dependencies)
            with patch("tools.stack_bound_fatal.resolve_default_firmware", return_value={"fixture": "pinned"}):
                proof = derive_fatal_context(root, output, catalog, qemu="/fixture/qemu-system-riscv64")
            self.assertTrue(proof.compiler_hooks)
            graph = collect_linked_assembly(root, output, "kernel/micros.elf", "riscv64-fatal-fixture", catalog)
            report = next(frame.function for frame in graph.frames if frame.function.symbol == "micros_panic_report")
            certify_context_bounds(graph.frames, graph.edges, (Root("panic", report, 4096, 48),), proof.bindings)

    def test_terminal_capture_cannot_borrow_a_smaller_observation_as_its_snapshot(self):
        from tools.stack_bound_fatal import derive_fatal_context

        owner = ROOT / "tests/qemu/terminal_failure_kernel.c"
        unpatched_read = Path.read_bytes
        source = owner.read_bytes()
        changed = source.replace(
            b"micros_test_work_finish(&micros_terminal_test_work_snapshot)",
            b"((void)*(volatile uint32_t *)&micros_terminal_test_work_snapshot, "
            b"micros_test_work_finish((struct micros_test_work_snapshot *)&micros_terminal_test_observation))",
            1,
        )
        self.assertNotEqual(source, changed)

        def read_fixture(path):
            return changed if path == owner else unpatched_read(path)

        with tempfile.TemporaryDirectory(prefix="micros-fatal-snapshot-alias-", dir=ROOT / "build") as directory:
            with patch.object(Path, "read_bytes", read_fixture):
                root, output, dependencies = build_fatal_fixture(ROOT, Path(directory).resolve(), terminal_test=True)
            catalog = fixture_catalog(root, output, dependencies)
            with (
                patch("tools.stack_bound_fatal.resolve_default_firmware", return_value={"fixture": "pinned"}),
                self.assertRaisesRegex(StackEvidenceError, "snapshot|observer"),
            ):
                derive_fatal_context(root, output, catalog, qemu="/fixture/qemu-system-riscv64")

    def test_changed_source_state_or_missing_fallback_cannot_supply_qualification(self):
        for unit, old, new in (
            ("arch/riscv64/uart.c", "uart0[UART_DLL] = UART_DIVISOR_LSB;", "uart0[UART_DLL] = 0;"),
            ("kernel/diagnostic/panic.c", 'uart_write("MICROS_TEST_FAILURE sbi-system-reset-returned\\n");',
             'uart_write("incorrect-success\\n");'),
        ):
            path = self.root / unit
            source = path.read_text()
            self.assertIn(old, source)
            try:
                path.write_text(source.replace(old, new, 1))
                with self.subTest(unit=unit), self.assertRaises(StackEvidenceError):
                    self.bind()
            finally:
                path.write_text(source)

    def test_unpinned_firmware_fails_instead_of_publishing_an_unqualified_graph(self):
        from tools.stack_bound_fatal import derive_fatal_context

        with (
            patch("tools.stack_bound_fatal.resolve_default_firmware", side_effect=StackEvidenceError("firmware drift")),
            self.assertRaisesRegex(StackEvidenceError, "firmware drift"),
        ):
            derive_fatal_context(
                self.root, self.directory, self.catalog, qemu="/fixture/qemu-system-riscv64",
            )

    def test_machine_inventory_cannot_hide_every_flush_occurrence(self):
        from tools.stack_bound_fatal import source_calls
        from tools.stack_bound_flow import function_flow
        from tools.stack_bound_objects import function_symbols, load_object

        key = next(row.function for row in self.catalog.frames if row.function.symbol == "panic_report_claimed")
        origin = next(row for row in self.catalog.origins if row.function == key)
        obj = load_object(self.directory / dict(self.catalog.objects)[key])
        calls = function_flow(obj, function_symbols(obj)[key.symbol]).calls
        with self.assertRaises(StackEvidenceError):
            source_calls(self.root, key, origin, tuple(
                call for call in calls if call.target_symbol != "uart_flush"
            ))

    def test_firmware_resolver_cannot_borrow_a_different_configured_qemu(self):
        from tools.stack_bound_fatal import derive_fatal_context

        with (
            patch("tools.stack_bound_fatal.resolve_default_firmware") as firmware,
            self.assertRaises(StackEvidenceError),
        ):
            derive_fatal_context(self.root, self.directory, self.catalog, qemu="/foreign/qemu")
        firmware.assert_not_called()

    def test_linked_assembly_closes_the_real_panic_cycle_with_both_uart_invocations(self):
        from tools.stack_bound_assembly import collect_linked_assembly
        from tools.stack_bound_context import Qualifier, certify_context_bounds
        from tools.stack_bound_types import Root

        graph = collect_linked_assembly(
            self.root, self.directory, "kernel/micros.elf", "riscv64-fatal-fixture", self.catalog,
        )
        uart = next(frame.function for frame in graph.frames if frame.function.symbol == "uart_write")
        proof, = certify_context_bounds(
            graph.frames, graph.edges, (Root("supervisor", uart, 16384),), self.bind().bindings,
        )
        self.assertEqual(
            [Qualifier.UNQUALIFIED, Qualifier.PANIC_LATCHED_UART_READY],
            [node.qualifier for node in proof.witness if node.function == uart],
        )
        self.assertTrue(any(node.function.symbol == "micros_trap_panic_entry" for node in proof.witness))


if __name__ == "__main__":
    unittest.main()
