"""Current runtime memory semantics, primitive work and real target artifacts."""

import copy
from dataclasses import replace
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tests.host.work_count_types import Kind, Owner, Stage


ROOT = Path(__file__).resolve().parents[2]


class RuntimeMemoryCurrentTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from tests.host.runtime_memory_evidence import measure_native

        cls.measured = measure_native(ROOT, Stage.GREEN)
        cls.disabled = measure_native(ROOT, Stage.GREEN, observed=False)

    def test_complete_geometry_semantics_modes_and_exact_access_ranges(self):
        from tests.host.runtime_memory_evidence import assert_aligned_work, assert_geometry_work, measure_access

        assert_aligned_work(self.measured, "copy")
        assert_aligned_work(self.measured, "fill")
        assert_geometry_work(self.measured)
        self.assertEqual(
            tuple(replace(row, capture=None) for row in self.measured.records),
            self.disabled.records,
        )
        self.assertIs(True, json.loads(measure_access(ROOT, Stage.GREEN).output)["address_orders"])

    def test_disabled_fixture_preserves_all_residue_and_boundary_cases_without_capture(self):
        from tests.host.runtime_memory_evidence import probe

        for destination in range(8):
            for source in range(8):
                for length in (0, 7, 8, 9, 65, 4096):
                    with self.subTest(destination=destination, source=source, length=length):
                        self.assertIsNone(probe(self.disabled, "copy", length, destination, source).capture)
        for destination in range(4):
            for length in (0, 11, 12, 13, 4096):
                self.assertIsNone(probe(self.disabled, "fill", length, destination, 0).capture)

    def test_record_shape_cannot_borrow_numeric_equality_or_foreign_work(self):
        from tests.host.runtime_memory_evidence import decode_measurements

        records = [json.loads(line) for line in self.measured.output.splitlines()]
        for mutation in ("boolean-offset", "float-length", "foreign-work", "capture-label", "missing-case"):
            changed = copy.deepcopy(records)
            if mutation == "boolean-offset":
                changed[0]["destination_offset"] = False
            elif mutation == "float-length":
                changed[0]["length"] = float(changed[0]["length"])
            elif mutation == "foreign-work":
                changed[0]["capture"]["snapshot"]["counts"][Owner.RUNTIME][Kind.COMPLETE_VALIDATOR_CALL] = 1
            elif mutation == "capture-label":
                changed[0]["capture"]["snapshot"]["capture_label"] += 1
            else:
                changed.pop()
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                decode_measurements("\n".join(json.dumps(row) for row in changed) + "\n", observed=True)

    def test_complete_geometry_rejects_byte_work_outside_the_two_aligned_cases(self):
        from tests.host.runtime_memory_evidence import assert_geometry_work, probe

        selected = probe(self.measured, "copy", 65, 3, 3)
        counts = [list(row) for row in selected.capture.snapshot.counts]
        counts[Owner.RUNTIME][Kind.COPY_UNIT] = selected.length
        changed = replace(selected, capture=replace(
            selected.capture, snapshot=replace(selected.capture.snapshot, counts=tuple(tuple(row) for row in counts)),
        ))
        measured = replace(
            self.measured,
            records=tuple(changed if row == selected else row for row in self.measured.records),
        )
        with self.assertRaisesRegex(AssertionError, "complete aligned intervals"):
            assert_geometry_work(measured)

    def test_current_observation_admission_rejects_missing_or_borrowed_sites(self):
        from tests.host.runtime_memory_evidence import validate_green_source

        source = (ROOT / "lib/runtime/memory.c").read_text()
        validate_green_source(source)
        changed = source.replace('"copy-word-unit"', '"unowned-unit"', 1)
        self.assertNotEqual(source, changed)
        with self.assertRaises(ValueError):
            validate_green_source(changed)

    def test_executed_binary_drift_invalidates_an_otherwise_successful_measurement(self):
        from tests.host import runtime_memory_evidence as evidence

        run = subprocess.run

        def altered(argv, **kwargs):
            completed = run(argv, **kwargs)
            path = Path(argv[0])
            if path.name == "fixture" and path.parent.name.startswith("micros-runtime-memory-"):
                path.write_bytes(b"changed after actual successful execution")
            return completed

        with patch.object(evidence.subprocess, "run", side_effect=altered), self.assertRaisesRegex(
            ValueError, "executable.*changed",
        ):
            evidence.measure_native(ROOT, Stage.GREEN)


class RuntimeMemoryArtifactTest(unittest.TestCase):
    def test_actual_production_body_lowers_to_wide_transfers_in_every_retained_mode(self):
        from tests.host.runtime_memory_evidence import target_transfer_proofs

        target_transfer_proofs(ROOT, Stage.GREEN)

    def test_exact_inline_primitives_have_real_nonstack_word_transfers(self):
        from tests.host.runtime_memory_evidence import target_transfer_proofs

        proofs = target_transfer_proofs(ROOT, Stage.GREEN, fixture=True)
        self.assertEqual({"debug", "size"}, set(proofs))
        for proof in proofs.values():
            self.assertGreater(proof["copy"], 0)
            self.assertGreater(proof["fill"], 0)

    def test_stack_spills_cannot_impersonate_data_word_transfers(self):
        from tests.host.runtime_memory_evidence import wide_data_transfers
        from tools.stack_bound_objects import function_symbols, load_object

        with tempfile.TemporaryDirectory(prefix="micros-memory-spill-") as temporary:
            directory = Path(temporary)
            source, output = directory / "spill.c", directory / "spill.o"
            source.write_text(
                "#include <stdint.h>\n"
                "void copy(void *destination, const void *input);\n"
                "void copy(void *destination, const void *input) {\n"
                "    volatile uintptr_t spilled = (uintptr_t)input;\n"
                "    (void)destination; (void)spilled;\n"
                "}\n"
            )
            result = subprocess.run((
                os.environ["MICROS_CLANG"], "--target=riscv64-unknown-elf",
                "-march=rv64imac_zicsr_zifencei", "-mabi=lp64", "-std=c17",
                "-ffreestanding", "-fno-builtin", "-O0", "-Wall", "-Wextra", "-Werror",
                "-c", str(source), "-o", str(output),
            ), capture_output=True, text=True, timeout=30)
            self.assertEqual((0, ""), (result.returncode, result.stderr), result.stdout)
            obj = load_object(output)
            with self.assertRaisesRegex(ValueError, "wide data transfer"):
                wide_data_transfers(obj, function_symbols(obj)["copy"], "copy")

    def test_ir_rejects_unaligned_variable_or_external_primitive_binding(self):
        from tests.host.runtime_memory_evidence import validate_primitive_ir

        ir = (
            "define dso_local void @memory_artifact_copy(ptr %0, ptr %1) {\n"
            "  call void @llvm.memcpy.inline.p0.p0.i64(ptr align 8 %0, ptr align 8 %1, i64 8, i1 false)\n}\n"
        )
        validate_primitive_ir(ir, "memory_artifact_copy", 8)
        for old, new in (("align 8", "align 1"), ("i64 8", "i64 %length"), ("memcpy.inline", "memcpy")):
            with self.subTest(mutation=new), self.assertRaises(ValueError):
                validate_primitive_ir(ir.replace(old, new), "memory_artifact_copy", 8)

    def test_linked_recipes_come_from_ninja_without_an_optional_compile_database(self):
        from tests.host.runtime_memory_evidence import runtime_compile_commands

        with tempfile.TemporaryDirectory(prefix="micros-copy-ninja-") as temporary:
            directory = Path(temporary)
            (directory / "CMakeCache.txt").write_text("CMAKE_MAKE_PROGRAM:FILEPATH=" + shutil.which("ninja") + "\n")
            (directory / "build.ninja").write_text(
                "rule compile\n  command = cc -c $in -o $out\n"
                "rule status\n  command = printf metadata\n"
                "build memory.o: compile source.c\nbuild holder: phony memory.o\nbuild info: status\n"
            )
            for exported in (False, True):
                if exported:
                    (directory / "compile_commands.json").write_text("[]")
                recipes = runtime_compile_commands(directory)
                self.assertEqual([("source.c", "memory.o")], [
                    (row["file"], row["output"]) for row in recipes if row["file"] == "source.c"
                ])


if __name__ == "__main__":
    unittest.main()
