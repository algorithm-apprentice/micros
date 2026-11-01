"""Evidence additions preserve real source bytes, locations and runtime edits."""

from pathlib import Path
import tempfile
import unittest

from tools.stack_bound_bindings import reference_source


ROOT = Path(__file__).resolve().parents[2]


class StackSourceProjectionTest(unittest.TestCase):
    def test_only_declared_inline_hooks_project_to_the_exact_fixed_source(self):
        from tests.host.stack_source_projection import HOOKS, instrument_source, project_stack_source

        for unit in HOOKS:
            original = reference_source(str(ROOT), unit)
            observed = instrument_source(unit, original)
            self.assertEqual(original, project_stack_source(unit, observed))
            self.assertEqual(original.count("\n"), observed.count("\n"))
            self.assertEqual(original, project_stack_source(unit, original))

    def test_wrong_arguments_duplicate_and_borrowed_scope_are_not_erased(self):
        from tests.host.stack_source_projection import instrument_source, project_stack_source

        unit = "kernel/objects/kernel_object_context.c"
        observed = instrument_source(unit, reference_source(str(ROOT), unit))
        call = "MICROS_TEST_STACK_ATTACHED(objects, thread_handle);"
        for changed in (
            observed.replace(call, "MICROS_TEST_STACK_ATTACHED(objects, foreign);", 1),
            observed.replace(call, call + " " + call, 1),
            observed.replace(call, "", 1).replace(
                "    return MICROS_KERNEL_OBJECT_OK;\n",
                "    " + call + " return MICROS_KERNEL_OBJECT_OK;\n", 1,
            ),
        ):
            with self.subTest(changed=changed[:100]), self.assertRaises(AssertionError):
                project_stack_source(unit, changed)
        with self.assertRaises(AssertionError):
            project_stack_source("kernel/foreign.c", observed)

    def test_projection_keeps_an_unrelated_runtime_mutation_visible(self):
        from tests.host.stack_source_projection import instrument_source, project_stack_source

        unit = "kernel/objects/kernel_object_context.c"
        original = reference_source(str(ROOT), unit)
        observed = instrument_source(unit, original)
        changed = observed.replace("thread->context_attached = true;", "thread->context_attached = false;", 1)
        self.assertNotEqual(original, project_stack_source(unit, changed))

    def test_disabled_hooks_preserve_the_actual_ordinary_target_object(self):
        from tests.host.stack_source_projection import instrument_source
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_objects import allocatable_object_bytes

        unit = "kernel/objects/kernel_object_context.c"
        source = reference_source(str(ROOT), unit)
        with tempfile.TemporaryDirectory(prefix="micros-stack-disabled-hooks-") as temporary:
            root = Path(temporary)
            ordinary, annotated = root / "ordinary", root / "annotated"
            ordinary.mkdir()
            annotated.mkdir()
            _, before = compile_fixture(ordinary, source)
            _, after = compile_fixture(annotated, instrument_source(unit, source))
            self.assertEqual(allocatable_object_bytes(before), allocatable_object_bytes(after))

    def test_historical_kernel_oracle_keeps_its_original_hash_after_exact_projection(self):
        from tests.host import kernel_layout_source
        from tests.host.stack_source_projection import instrument_source

        logical = "kernel/kernel_object_context.c"
        physical = kernel_layout_source.KERNEL_LOGICAL_TO_PHYSICAL[logical]
        expected = kernel_layout_source.read_logical_bytes(ROOT, logical)
        with tempfile.TemporaryDirectory(prefix="micros-stack-kernel-oracle-") as temporary:
            root = Path(temporary)
            path = root / physical
            path.parent.mkdir(parents=True)
            path.write_text(instrument_source(physical, reference_source(str(ROOT), physical)))
            self.assertEqual(expected, kernel_layout_source.read_logical_bytes(root, logical))


if __name__ == "__main__":
    unittest.main()
