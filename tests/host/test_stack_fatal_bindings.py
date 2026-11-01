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
        self.assertEqual(51, len(proof.bindings.establishing))
        self.assertEqual(
            ("arch/riscv64/uart.c", "uart_write", 0x34),
            (
                proof.bindings.infeasible[0].translation_unit,
                proof.bindings.infeasible[0].symbol, proof.bindings.infeasible[1],
            ),
        )
        self.assertEqual(51, len(proof.establishment))
        self.assertTrue(any(row.function.symbol == "sbi_system_reset" for row in proof.preservation))
        self.assertTrue(any(row.function.symbol == "uart_flush" for row in proof.preservation))
        self.assertEqual(16, proof.console.size)
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
            call.caller.symbol == "micros_panic_report" and call.targets[0].symbol == "sbi_system_reset"
        ))
        with self.assertRaises(StackEvidenceError):
            self.bind(replace(self.catalog, calls=tuple(item for item in self.catalog.calls if item != call)))
        origin = next(row for row in self.catalog.origins if row.function.symbol == "uart_write")
        with self.assertRaises(StackEvidenceError):
            self.bind(replace(self.catalog, origins=tuple(
                replace(row, line=row.line + 1) if row == origin else row for row in self.catalog.origins
            )))

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

        key = next(row.function for row in self.catalog.frames if row.function.symbol == "micros_panic_report")
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
            [Qualifier.UNQUALIFIED, Qualifier.UART_PANIC_READY],
            [node.qualifier for node in proof.witness if node.function == uart],
        )
        self.assertTrue(any(node.function.symbol == "micros_trap_panic_entry" for node in proof.witness))


if __name__ == "__main__":
    unittest.main()
