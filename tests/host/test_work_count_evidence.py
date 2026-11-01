"""Independent snapshot-composition and observation-admission regressions."""

from dataclasses import replace
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tools import validation_context
from tools.validation_build_manifest import native_outputs
from tests.host.c_source_records import tokens
from tests.host.stack_recipe import MODULE_PATH as STACK_MODULE_PATH, RECIPE as STACK_RECIPE
from tests.host.work_count_recipe import (
    INCLUDE, MODULE_PATH, project_root_recipe, projected_recipe_digest,
    render_native_recipe, validate_native_recipe,
)
from tests.host.work_count_evidence import (
    Capture,
    Kind,
    ObservationSite,
    Owner,
    Placement,
    Primitive,
    Region,
    Snapshot,
    SourceAdmission,
    Stage,
    Status,
    Totals,
    WorkEvidenceError,
    compose_snapshots,
    project_observed_source,
)


ORIGINAL = """int probe(const int *values, int count, int target)
{
    int index;
    int value;
    for (index = 0; index < count; ++index) {
        value = values[index];
        if (value == target) {
            return 1;
        }
    }
    return 0;
}
"""
HEADER = '#include "micros/work_observation.h"\n'
HIT = (
    'MICROS_TEST_WORK_HIT("visit", MICROS_WORK_OWNER_KERNEL_MEMORY, '
    "MICROS_WORK_KIND_RECORD_VISIT, 1);"
)
COMPARE = (
    'MICROS_TEST_WORK_COMPARE("compare", '
    "MICROS_WORK_OWNER_KERNEL_MEMORY, value == target)"
)
SOURCE_PATH = "tests/host/work_fixture.c"
ROOT = Path(__file__).resolve().parents[2]


def instrumented():
    return HEADER + ORIGINAL.replace(
        "value = values[index];",
        "value = values[index];\n        " + HIT,
    ).replace("value == target", COMPARE)


def admission():
    region = Region("for (index = 0; index < count; ++index) {")
    return SourceAdmission(
        SOURCE_PATH,
        Stage.FIXTURE,
        (
            ObservationSite(
                "probe", "visit", Owner.KERNEL_MEMORY, Kind.RECORD_VISIT,
                Placement.AFTER, (region,), Primitive("value = values[index];"),
            ),
            ObservationSite(
                "probe", "compare", Owner.KERNEL_MEMORY, Kind.COMPARISON,
                Placement.WRAPS, (region,), Primitive("value == target"),
            ),
        ),
        include_header=True,
    )


def capture(producer, units):
    counts = [[0] * len(Kind) for _ in Owner]
    counts[Owner.RUNTIME][Kind.RECORD_VISIT] = units
    return Capture(
        Status.OK,
        Snapshot(
            producer, 1, 1 << Owner.RUNTIME,
            tuple(tuple(row) for row in counts),
        ),
    )


class WorkCountEvidenceTest(unittest.TestCase):
    def test_shared_source_owner_is_composed_once_per_producer(self):
        result = compose_snapshots(
            (capture(1, 2), capture(2, 3)),
            capture_label=1,
            expected_producers={1: 1 << Owner.RUNTIME, 2: 1 << Owner.RUNTIME},
        )
        self.assertEqual(5, result.per_owner[Owner.RUNTIME][Kind.RECORD_VISIT])
        self.assertEqual(5, result.per_kind[Kind.RECORD_VISIT])

    def test_anchored_observations_preserve_original_transport(self):
        result = project_observed_source(
            SOURCE_PATH, instrumented(), admission(), stage=Stage.FIXTURE,
        )
        self.assertEqual(tokens(ORIGINAL), tokens(result))

    def compose(self, values):
        return compose_snapshots(
            values, capture_label=1,
            expected_producers={1: 1 << Owner.RUNTIME, 2: 1 << Owner.RUNTIME},
        )

    def project(self, source, declared=None, stage=Stage.FIXTURE):
        return project_observed_source(
            SOURCE_PATH, source, declared or admission(), stage=stage,
        )

    def test_composition_rejects_missing_duplicate_extra_and_failed_producers(self):
        cases = (
            (capture(1, 2),),
            (capture(1, 2), capture(1, 3)),
            (capture(1, 2), capture(3, 3)),
            (capture(1, 2), Capture(Status.STATE, capture(2, 3).snapshot)),
            (capture(1, 2), Capture(Status.OK, None)),
            (capture(1, 2), Capture(False, capture(2, 3).snapshot)),
            (capture(1, 2), Totals((), ())),
        )
        for case in cases:
            with self.subTest(case=case), self.assertRaises(WorkEvidenceError):
                self.compose(case)

    def test_snapshot_shape_provenance_and_foreign_rows_are_required(self):
        valid = capture(2, 3)
        foreign = [list(row) for row in valid.snapshot.counts]
        foreign[Owner.KERNEL_MEMORY][Kind.RECORD_VISIT] = 1
        changes = (
            {"capture_label": 2}, {"producer_label": True},
            {"owner_mask": 1 << Owner.KERNEL_MEMORY}, {"version": 0},
            {"reserved0": 1}, {"reserved1": True},
            {"counts": tuple(foreign)}, {"counts": valid.snapshot.counts[:-1]},
            {"counts": (tuple([0] * (len(Kind) - 1)),) * len(Owner)},
            {"counts": tuple(tuple(row) for row in foreign)},
        )
        for change in changes:
            with self.subTest(change=change), self.assertRaises(WorkEvidenceError):
                self.compose((capture(1, 2), replace(
                    valid, snapshot=replace(valid.snapshot, **change),
                )))

    def test_quantities_are_exact_uint64_and_composition_cannot_overflow(self):
        for value in (True, -1, 1.5, "2", 1 << 64):
            with self.subTest(value=value), self.assertRaises(WorkEvidenceError):
                self.compose((capture(1, 2), capture(2, value)))
        with self.assertRaisesRegex(WorkEvidenceError, "overflow"):
            self.compose((capture(1, (1 << 64) - 1), capture(2, 1)))
        second = capture(2, 0)
        rows = [list(row) for row in second.snapshot.counts]
        rows[Owner.KERNEL_MEMORY][Kind.RECORD_VISIT] = 1
        second = replace(second, snapshot=replace(
            second.snapshot, owner_mask=1 << Owner.KERNEL_MEMORY,
            counts=tuple(tuple(row) for row in rows),
        ))
        with self.assertRaisesRegex(WorkEvidenceError, "overflow"):
            compose_snapshots(
                (capture(1, (1 << 64) - 1), second), capture_label=1,
                expected_producers={
                    1: 1 << Owner.RUNTIME, 2: 1 << Owner.KERNEL_MEMORY,
                },
            )

    def test_loop_and_conditional_relocation_fail_despite_equal_transport(self):
        outside = instrumented().replace(HIT, "").replace(
            "    return 0;", "    " + HIT + "\n    return 0;",
        )
        conditional = instrumented().replace(HIT, "").replace(
            "            return 1;", "            " + HIT + "\n            return 1;",
        )
        before = instrumented().replace(
            "value = values[index];\n        " + HIT,
            HIT + "\n        value = values[index];",
        )
        for source in (outside, conditional, before):
            unobserved = source.replace(HEADER, "").replace(
                HIT, "",
            ).replace(COMPARE, "value == target")
            self.assertEqual(tokens(ORIGINAL), tokens(unobserved))
            with self.subTest(source=source), self.assertRaisesRegex(
                WorkEvidenceError, "moved|relocated",
            ):
                self.project(source)

    def test_declaration_and_namespace_drift_cannot_be_projected_away(self):
        sources = (
            instrumented().replace(HIT, ""),
            instrumented().replace(HIT, HIT + "\n        " + HIT),
            instrumented().replace('"visit"', '"unregistered"'),
            instrumented().replace("WORK_KIND_RECORD_VISIT, 1", "WORK_KIND_RECORD_VISIT, 2"),
            instrumented().replace(COMPARE, COMPARE.replace(
                "value == target", "value == target && count > 0",
            )),
            instrumented().replace(COMPARE, "-" + COMPARE),
            instrumented().replace(COMPARE, COMPARE + " + 1"),
            "#define MICROS_TEST_WORK_COUNTS 1\n" + instrumented(),
            instrumented().replace(HIT, HIT + "\n        MICROS_TEST_WORK_BOGUS();"),
            instrumented().replace(HIT, HIT + "\n        micros_test_work_record(0, 0, 1);"),
            HEADER + instrumented(),
            "#if 1\n" + HEADER + "#endif\n" + instrumented().removeprefix(HEADER),
        )
        for source in sources:
            with self.subTest(source=source), self.assertRaises(WorkEvidenceError):
                self.project(source)
        with self.assertRaises(WorkEvidenceError):
            self.project(instrumented(), stage=Stage.RED)
        with self.assertRaises(WorkEvidenceError):
            self.project(instrumented(), replace(admission(), source_path="../escape.c"))

    def test_statement_primitive_cannot_match_a_chained_assignment_suffix(self):
        source = instrumented().replace(
            "value = values[index];", "other = value = values[index];",
        )
        with self.assertRaisesRegex(WorkEvidenceError, "primitive"):
            self.project(source)

    def test_function_entry_is_bound_before_all_original_tokens(self):
        hit = (
            'MICROS_TEST_WORK_HIT("entry", MICROS_WORK_OWNER_KERNEL_MEMORY, '
            "MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL, 1);"
        )
        declared = SourceAdmission(
            SOURCE_PATH, Stage.FIXTURE,
            (ObservationSite(
                "empty", "entry", Owner.KERNEL_MEMORY,
                Kind.COMPLETE_VALIDATOR_CALL, Placement.FUNCTION_ENTRY,
            ),),
        )
        self.assertEqual(
            tokens("void empty(void) {}"),
            tokens(self.project("void empty(void) {" + hit + "}", declared)),
        )
        with self.assertRaisesRegex(WorkEvidenceError, "relocated"):
            self.project("void empty(void) { int value; " + hit + "}", declared)

    def test_comments_literals_and_unrelated_source_are_not_erased(self):
        banner = 'const char *text = "MICROS_TEST_WORK_HIT(fake)";\n'
        source = banner + "/* " + HIT + " */\n" + instrumented()
        projected = self.project(source)
        self.assertEqual(tokens(banner + ORIGINAL), tokens(projected))
        changed = self.project(instrumented().replace("return 0;", "return 7;"))
        self.assertNotEqual(tokens(ORIGINAL), tokens(changed))

    def test_c_and_hosted_vocabularies_cannot_misattribute_counts(self):
        header = (ROOT / "include/micros/work_observation.h").read_text()
        for enum, prefix in (
            (Owner, "MICROS_WORK_OWNER_"), (Kind, "MICROS_WORK_KIND_"),
        ):
            observed = re.findall(rf"^\s*{prefix}([A-Z_]+),", header, re.MULTILINE)
            self.assertEqual([item.name for item in enum] + ["COUNT"], observed)

    def test_native_recipe_preserves_original_closure_without_rebaselining(self):
        validate_native_recipe(ROOT)
        source = (ROOT / "CMakeLists.txt").read_text()
        self.assertEqual(
            validation_context.RECIPE_DIGESTS["CMakeLists.txt"],
            hashlib.sha256(project_root_recipe(source).encode()).hexdigest(),
        )
        self.assertEqual(
            validation_context.RECIPE_DIGESTS[MODULE_PATH],
            hashlib.sha256(render_native_recipe().encode()).hexdigest(),
        )
        self.assertEqual(
            validation_context.RECIPE_DIGESTS["CMakeLists.txt"],
            projected_recipe_digest(ROOT / "CMakeLists.txt"),
        )
        with self.assertRaises(AssertionError):
            project_root_recipe(source.replace(INCLUDE, "").replace(
                "cmake_minimum_required(VERSION 3.25)\n",
                "cmake_minimum_required(VERSION 3.25)\n" + INCLUDE,
            ))
        with tempfile.TemporaryDirectory(prefix="micros-work-recipe-") as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text(source)
            module = root / MODULE_PATH
            module.parent.mkdir(parents=True)
            module.write_text(render_native_recipe())
            stack_module = root / STACK_MODULE_PATH
            stack_module.parent.mkdir(parents=True)
            stack_module.write_text(STACK_RECIPE)
            self.assertEqual(
                validation_context.RECIPE_DIGESTS["CMakeLists.txt"],
                projected_recipe_digest(root / "CMakeLists.txt"),
            )
            module.write_text(render_native_recipe().replace(
                "MICROS_TEST_WORK_COUNTS=1", "MICROS_TEST_WORK_COUNTS=0",
            ))
            with self.assertRaises(validation_context.UnsupportedContext):
                projected_recipe_digest(root / "CMakeLists.txt")

    def test_complete_native_inventory_includes_only_the_closed_extension(self):
        source = (ROOT / "CMakeLists.txt").read_text()
        declared = re.findall(
            r"add_test\(\s*NAME\s+([A-Za-z0-9_-]+)\s+COMMAND\s+([A-Za-z0-9_]+)",
            source,
        )
        declared.extend((
            ("work-counts", "work_counts_test"),
            ("work-observation-disabled", "work_observation_disabled_test"),
        ))
        slow = set(re.findall(
            r"set_tests_properties\(\s*([A-Za-z0-9_-]+)\s+PROPERTIES\s+LABELS\s+slow\s*\)",
            source,
        ))
        with tempfile.TemporaryDirectory(prefix="micros-work-inventory-") as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text(source)
            module = root / MODULE_PATH
            module.parent.mkdir(parents=True)
            module.write_text(render_native_recipe())
            stack_module = root / STACK_MODULE_PATH
            stack_module.parent.mkdir(parents=True)
            stack_module.write_text(STACK_RECIPE)
            output = root / "build"
            output.mkdir()
            inventory = {"tests": []}
            for name, executable in declared:
                artifact = output / executable
                artifact.write_bytes(b"test artifact")
                artifact.chmod(0o700)
                properties = [{"name": "WORKING_DIRECTORY", "value": str(output)}]
                if name in slow:
                    properties.append({"name": "LABELS", "value": ["slow"]})
                inventory["tests"].append({
                    "name": name, "command": [str(artifact)],
                    "properties": properties,
                })
            observed = native_outputs(root, output, inventory)
            self.assertIn(output / "work_counts_test", observed)
            self.assertIn(output / "work_observation_disabled_test", observed)
            inventory["tests"].append(inventory["tests"][-1])
            with self.assertRaises(validation_context.UnsupportedContext):
                native_outputs(root, output, inventory)

    def test_rv64_collector_is_freestanding_and_default_observations_are_absent(self):
        compiler = Path(validation_context.target_compiler(dict(os.environ)))
        nm = compiler.parent / "llvm-nm"
        self.assertTrue(nm.is_file(), "selected LLVM nm is missing")
        compiler_environment = {**os.environ, "CLANG_NO_DEFAULT_CONFIG": "1"}
        common = [
            str(compiler), "--target=riscv64-unknown-elf",
            "-std=c17", "-march=rv64imac_zicsr_zifencei", "-mabi=lp64",
            "-mcmodel=medany", "-msmall-data-limit=0", "-mno-save-restore",
            "-ffreestanding", "-fno-builtin", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT), "-I", str(ROOT / "include"),
        ]
        with tempfile.TemporaryDirectory(prefix="micros-work-") as directory:
            enabled = Path(directory) / "enabled.o"
            result = subprocess.run(
                [*common, "-DMICROS_TEST_WORK_COUNTS=1", "-c",
                 str(ROOT / "tests/work/work_counts.c"), "-o", str(enabled)],
                capture_output=True, text=True, timeout=60,
                env=compiler_environment,
            )
            self.assertEqual(0, result.returncode, result.stderr)
            undefined = subprocess.run(
                [str(nm), "-u", str(enabled)], capture_output=True,
                text=True, timeout=30,
            )
            self.assertEqual(0, undefined.returncode, undefined.stderr)
            self.assertEqual("", undefined.stdout.strip())
            disabled = Path(directory) / "disabled.o"
            result = subprocess.run(
                [*common, "-c", str(ROOT / "tests/host/work_counts_fixture.c"),
                 "-o", str(disabled)], capture_output=True, text=True, timeout=60,
                env=compiler_environment,
            )
            self.assertEqual(0, result.returncode, result.stderr)
            symbols = subprocess.run(
                [str(nm), "-a", str(disabled)], capture_output=True,
                text=True, timeout=30,
            )
            self.assertEqual(0, symbols.returncode, symbols.stderr)
            self.assertNotIn("micros_test_work_", symbols.stdout)
            for mode in ("0", "2"):
                result = subprocess.run(
                    [*common, f"-DMICROS_TEST_WORK_COUNTS={mode}", "-c",
                     str(ROOT / "tests/host/work_counts_fixture.c"),
                     "-o", str(disabled)],
                    capture_output=True, text=True, timeout=60,
                    env=compiler_environment,
                )
                self.assertNotEqual(0, result.returncode)
                self.assertIn("must be undefined or equal to 1", result.stderr)


if __name__ == "__main__":
    unittest.main()
