"""Current release/handoff work, primitive placement and evidence protocols."""

from contextlib import nullcontext, redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import unittest
from unittest import mock

from tests.host.selected_release_handoff_evidence import (
    CASE_FIELDS, expected_cases, parse_target_output, target_failures, work_failures,
)
from tests.host import selected_release_handoff_source as source
from tests.host import native_observations
from tests.host.c_source_records import function_records
from tests.host.work_count_lexical import function_spans
from tests.host.work_count_evidence import Kind, Owner, Stage


ROOT = Path(__file__).resolve().parents[2]


def empty_counts():
    return {
        channel: [[0 for _ in Kind] for _ in Owner]
        for channel in ("frames", "plans", "context")
    }


def target_line(name, version, fields):
    return "# " + name + " version=" + str(version) + " " + " ".join(
        name + "=0x" + format(value, "016x") for name, value in fields.items()
    ) + "\n"


def user_target_output(stage):
    before = (8 << 60) | 0x80000
    process = before + 1
    frame = 0x90000000
    pte = ((frame >> 12) << 10) | 0xd7
    output = ""
    for case in range(2):
        output += target_line("MEM04_TABLE", 1, {
            "case": case, "lookup": 11, "activation": 11, "before_satp": before,
            "after_satp": before, "poison_unchanged": 1, "state_unchanged": 1,
            "outputs_unchanged": 1,
        })
    for case in range(5):
        diagnostic = 12 if case == 2 else 11
        output += target_line("MEM04_ACTIVATION", 2, {
            "case": case, "diagnostic": diagnostic, "activation": 0, "destroy": diagnostic,
            "before_satp": before, "after_satp": process, "process_satp": process,
            "restored_satp": before, "state_unchanged": 1,
        })
    for case in range(2):
        output += target_line("MEM05_TABLE", 1, {
            "case": case, "release": 11, "destroy": 11, "before_satp": before,
            "after_satp": before, "poison_unchanged": 1, "state_unchanged": 1,
            "output_unchanged": 1, "cleanup_complete": 1,
        })
    for case in range(4 if stage is Stage.RED else 5):
        diagnostic = 12 if case == 2 else 11
        result = (11, 11, 12, 11 if stage is Stage.RED else 10, 0)[case]
        selected = 0 if case == 3 else pte
        output += target_line("MEM05_RELEASE", 1, {
            "case": case, "diagnostic": diagnostic, "release": result,
            "before_satp": before, "after_satp": before, "frame": frame,
            "output": frame if result == 0 else 0xfeedfacefeedface,
            "selected_before": selected, "selected_after": 0 if result == 0 else selected,
            "owner_before": 5, "owner_after": 0 if result == 0 else 5,
            "state_unchanged": int(result != 0), "effect_matches": 1,
            "unrelated_unchanged": 1, "data_unchanged": 1, "cleanup_complete": 1,
        })
    return output + (
        "# MICROS_PANIC reason=user-address-space-test\n" if stage is Stage.RED
        else "# MICROS_USER_ADDRESS_SPACE_TEST_PASS isolation=preserved\n"
    )


class SelectedReleaseHandoffWorkTest(unittest.TestCase):
    def setUp(self):
        self.case = {
            "operation": "release-set", "frames": 65, "ranges": 1,
            "selected": 0, "words": 0, "indexed": 0, "owned": 0,
            "tables": 0, "roots": 0,
        }

    def test_capacity_bitmap_scan_fails_even_for_an_empty_selection(self):
        counts = empty_counts()
        counts["plans"][Owner.KERNEL_MEMORY][Kind.BITMAP_WORD_LOAD] = 262144
        self.assertIn(
            ("bitmap-word-loads", 262144, 16),
            work_failures(self.case, counts),
        )

    def test_allocator_and_selection_word_loads_share_one_dimension(self):
        counts = empty_counts()
        counts["plans"][Owner.KERNEL_MEMORY][Kind.BITMAP_WORD_LOAD] = 9
        counts["frames"][Owner.KERNEL_MEMORY][Kind.BITMAP_WORD_LOAD] = 8
        self.assertIn(
            ("bitmap-word-loads", 17, 16),
            work_failures(self.case, counts),
        )

    def test_unknown_operation_or_incomplete_capture_is_not_a_pass(self):
        with self.assertRaises(ValueError):
            work_failures({**self.case, "operation": "unknown"}, empty_counts())
        counts = empty_counts()
        del counts["context"]
        with self.assertRaises(ValueError):
            work_failures(self.case, counts)

    def test_process_count_rejects_work_over_unrelated_capacity(self):
        case = {
            **self.case, "operation": "count-process", "frames": 262144,
            "indexed": 16,
        }
        counts = empty_counts()
        counts["frames"][Owner.KERNEL_MEMORY][Kind.RECORD_VISIT] = 262144
        try:
            failures = work_failures(case, counts)
        except ValueError as error:
            self.fail("A declared process-count capture was rejected instead of measured: " + str(error))
        self.assertIn(("owner-record-visits", 262144, 80), failures)

    def test_destruction_cannot_hide_a_global_scan_behind_page_clearing(self):
        case = next(row for row in expected_cases()
                    if row["operation"] == "destroy" and row["pattern"] == "one" and row["frames"] == 262144)
        counts = empty_counts()
        counts["frames"][Owner.KERNEL_MEMORY][Kind.RECORD_VISIT] = case["frames"]
        counts["plans"][Owner.KERNEL_MEMORY][Kind.CLEARED_BYTE] = 4096 * case["owned"]
        try:
            failures = work_failures({key: case[key] for key in CASE_FIELDS}, counts)
        except ValueError as error:
            self.fail("A declared destruction capture was rejected instead of measured: " + str(error))
        self.assertIn(("owner-record-visits", 262144, 128), failures)
        self.assertFalse(any(row[0] == "cleared-bytes" for row in failures))

    def test_repeated_handoff_thread_search_is_not_a_single_object_audit(self):
        case = next(row for row in expected_cases()
                    if row["operation"] == "prepare-handoff" and row["roots"] == 64)
        counts = empty_counts()
        counts["context"][Owner.KERNEL_MEMORY][Kind.RECORD_VISIT] = 64 * 128
        counts["context"][Owner.KERNEL_OBJECTS][Kind.COMPLETE_VALIDATOR_CALL] = 1
        try:
            failures = work_failures({key: case[key] for key in CASE_FIELDS}, counts)
        except ValueError as error:
            self.fail("A declared handoff capture was rejected instead of measured: " + str(error))
        self.assertIn(("process-thread-record-visits", 8192, 3072), failures)


class SelectedReleaseHandoffInterfaceTest(unittest.TestCase):
    def test_declared_scratch_owner_has_its_private_selection_preflight(self):
        from tests.host.runtime_memory_evidence import native_compiler

        path = "kernel/memory/user_address_space_runtime.c"
        declared = (ROOT / path).read_text()
        includes = "\n".join(line for line in declared.splitlines() if line.startswith("#include "))
        probe = includes + (
            "\nint main(void) {\n"
            "    return micros_frame_ownership_check_selection((const struct micros_frame_selection *)0, 0);\n"
            "}\n"
        )
        compiler = native_compiler(ROOT)
        checked = subprocess.run(
            (compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
             "-I", str(ROOT), "-I", str(ROOT / "include"), "-fsyntax-only", "-x", "c", "-"),
            cwd=ROOT, input=probe, capture_output=True, text=True, timeout=30, check=False,
        )
        self.assertEqual((0, ""), (checked.returncode, checked.stderr), checked.stdout + checked.stderr)









class SelectedReleaseHandoffSourceTest(unittest.TestCase):
    def test_current_audit_boundaries_do_not_restore_an_ancestor(self):
        from unittest.mock import patch
        from tests.host import selected_release_handoff_source as source
        from tests.host.work_count_types import Stage

        with patch.object(source, "fixed_source", side_effect=AssertionError("historical audit input"), create=True):
            measured = source.measure_audit_boundaries(ROOT, Stage.GREEN)
        self.assertEqual(17, len(measured["retained"]))
        self.assertEqual(4, len(measured["plans"]))

    def test_current_native_generation_does_not_restore_an_ancestor(self):
        from unittest.mock import patch
        from tools import generate_selected_release_handoff_fixture as generator
        from tests.host.work_count_types import Stage

        with patch.object(generator, "fixed_inputs", side_effect=AssertionError("historical native inputs"), create=True):
            inputs, outputs = generator.source_bundle(ROOT, Stage.GREEN, "ordinary")
        self.assertIn("kernel/memory/user_address_space_handoff.c", inputs)
        self.assertEqual(set(inputs), set(outputs))



    def test_array_declarations_do_not_become_observed_variable_length_arrays(self):
        from tests.host.runtime_memory_evidence import native_compiler

        body = (
            "uint64_t observed_counts[8];\n"
            "observed_counts[0] = 1;\n"
            '_Static_assert(sizeof(observed_counts) == 64, "fixed native storage");\n'
            "return observed_counts[0] == 1 ? 0 : 1;\n"
        )
        sites = tuple(
            (
                "index-" + str(index), "wrap", "KERNEL_MEMORY", "INDEX_PROBE",
                1, "value", "0", (), index, 3,
            ) for index in range(2)
        )
        observed = native_observations.render_body(
            body, native_observations.bind_sites(body, sites),
        )
        code = (
            "#include <stdint.h>\n"
            "static unsigned long observed_index(unsigned long index) { return index; }\n"
            "#define MICROS_WORK_OWNER_KERNEL_MEMORY 0\n"
            "#define MICROS_WORK_KIND_INDEX_PROBE 0\n"
            "#define MICROS_MEM05_OBSERVE_VALUE(site, owner, kind, value) observed_index(value)\n"
            "int main(void) {\n" + observed + "\n}\n"
        )
        compiler = native_compiler(ROOT)
        checked = subprocess.run(
            (compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-Wvla",
             "-fsyntax-only", "-x", "c", "-"),
            input=code, capture_output=True, text=True, timeout=30, check=False,
        )
        self.assertEqual((0, ""), (checked.returncode, checked.stderr), checked.stdout + checked.stderr)


    def test_declared_helpers_bind_all_primitive_placements_and_callers(self):
        path = "kernel/memory/frame_ownership.c"
        name = "micros_frame_selection_clear"
        text = (ROOT / path).read_text()
        span = function_spans(text)[name]
        body = text[span.body_start:span.body_end]

        def validate(value):
            candidate = text[:span.body_start] + value + text[span.body_end:]
            return native_observations.observe_source(ROOT, "MEM05", path, candidate, "aggregate")

        validate(body)
        validate("size_t scalar = 0; (void)scalar;\n" + body)
        store = "selection->bitmap[word_index] = 0;"
        for changed in (
            body.replace(store, "", 1), body.replace(store, store + store, 1),
            body.replace(store, "", 1) + store,
            body.replace("selection->words[position] = 0;", "selection->positions[position] = 0;", 1),
            body + "for (size_t hidden = 0; hidden < 8; ++hidden) {}",
            body.replace("micros_frame_ownership_check_selection(", "undeclared_preflight("),
            "\n#if 0\n" + body + "#endif\n",
        ):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                validate(changed)
        with self.assertRaises(ValueError):
            native_observations.observe_source(
                ROOT, "MEM05", path, text.replace(
                    function_records(text)[name].signature,
                    function_records(text)[name].signature.replace("bool ", "int ", 1), 1,
                ), "aggregate",
            )
        with self.assertRaises(ValueError):
            native_observations.observe_source(ROOT, "MEM05", path, text.replace(name, "unknown_helper"), "aggregate")

    def test_scalar_header_probe_cannot_disappear_or_be_duplicated(self):
        path = "kernel/memory/frame_ownership.c"
        name = "selection_header_is_valid"
        text = (ROOT / path).read_text()
        span = function_spans(text)[name]
        body = text[span.body_start:span.body_end]
        native_observations.observe_source(ROOT, "MEM05", path, text, "aggregate")
        for changed in (body.replace("selection->word_count", "0"), body + body):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                native_observations.observe_source(
                    ROOT, "MEM05", path, text[:span.body_start] + changed + text[span.body_end:], "aggregate",
                )

    def test_consumed_link_binding_keeps_bounds_before_record_access(self):
        path = "kernel/memory/frame_ownership.c"
        name = "index_owner_matches_slot"
        text = (ROOT / path).read_text()
        span = function_spans(text)[name]
        body = text[span.body_start:span.body_end]
        native_observations.observe_source(ROOT, "MEM05", path, text, "aggregate")
        store = "owner = ownership->owners[encoded - 1];"
        moved = body.replace(store, "", 1).replace("if (encoded", store + "\nif (encoded", 1)
        for changed in (
            moved, body.replace(store, store + store),
            body.replace("owners[encoded - 1]", "owners[encoded]"),
            body.replace(store, "if (slot == 0) { " + store + " }"),
        ):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                native_observations.observe_source(
                    ROOT, "MEM05", path, text[:span.body_start] + changed + text[span.body_end:], "aggregate",
                )




class SelectedReleaseHandoffTargetTest(unittest.TestCase):

    def test_baseline_requires_the_actual_scoped_release_mismatch(self):
        rows = parse_target_output(
            user_target_output(Stage.RED), "test-qemu-user-address-space", Stage.RED, 1,
        )
        self.assertEqual(13, len(rows))
        self.assertEqual((), target_failures(rows, baseline=True))
        self.assertEqual(
            (("MEM05_RELEASE", 3, "release", 11, 10),),
            target_failures(rows),
        )

    def test_current_requires_all_retained_and_new_target_witnesses(self):
        rows = parse_target_output(
            user_target_output(Stage.GREEN), "test-qemu-user-address-space", Stage.GREEN, 0,
        )
        self.assertEqual(14, len(rows))
        self.assertEqual((), target_failures(rows))

    def test_generic_panic_missing_case_and_foreign_record_are_not_evidence(self):
        baseline = user_target_output(Stage.RED)
        current = user_target_output(Stage.GREEN)
        for output, stage, exit_code in (
            ("# MICROS_PANIC reason=user-address-space-test\n", Stage.RED, 1),
            (baseline.replace("reason=user-address-space-test", "reason=unrelated"), Stage.RED, 1),
            (baseline, Stage.RED, 0),
            ("\n".join(line for line in current.splitlines()
                       if not line.startswith("# MEM05_RELEASE version=1 case=0x0000000000000004")),
             Stage.GREEN, 0),
            (current + "# MEM05_UNKNOWN version=1 case=0x0000000000000000\n", Stage.GREEN, 0),
        ):
            with self.subTest(stage=stage, exit=exit_code), self.assertRaises(ValueError):
                parse_target_output(output, "test-qemu-user-address-space", stage, exit_code)


class SelectedReleaseHandoffNativeTest(unittest.TestCase):


    def test_current_native_output_cannot_omit_new_field_guard_execution(self):
        from tests.host.selected_release_handoff_evidence import _expected_result, parse_native_output

        lines = ["MICROS_MEM05_NATIVE version=1 stage=green channel=ordinary"]
        for case in expected_cases():
            result, output = _expected_result(case, Stage.GREEN)
            lines.append("MICROS_MEM05_CASE " + json.dumps({**case, "result": result, "output": output, "state": True}))
        lines.append("MICROS_MEM05_NATIVE_PASS cases=128")
        with self.assertRaises(ValueError):
            parse_native_output("\n".join(lines) + "\n", Stage.GREEN, "ordinary")

    def test_guard_records_have_exact_current_membership_and_preservation(self):
        from tests.host.selected_release_handoff_evidence import _expected_result, expected_guards, parse_native_output

        lines = ["MICROS_MEM05_NATIVE version=1 stage=green channel=ordinary"]
        for case in expected_cases():
            result, output = _expected_result(case, Stage.GREEN)
            lines.append("MICROS_MEM05_CASE " + json.dumps({**case, "result": result, "output": output, "state": True}))
        guards = ["MICROS_MEM05_GUARD " + json.dumps({**case, "state": True}) for case in expected_guards()]
        ending = ["MICROS_MEM05_NATIVE_PASS cases=128"]
        parsed = parse_native_output("\n".join(lines + guards + ending) + "\n", Stage.GREEN, "ordinary")
        self.assertEqual(tuple({**case, "state": True} for case in expected_guards()), parsed["guards"])
        for changed in (
            guards[1:], [guards[1], guards[0], *guards[2:]],
            [guards[0].replace('"state": true', '"state": false'), *guards[1:]],
            [guards[0].replace('"result": 0', '"result": true'), *guards[1:]],
            [*guards[:-1], guards[-1].replace("scratch-release", "unrelated-panic")],
        ):
            with self.subTest(first=changed[0]), self.assertRaises(ValueError):
                parse_native_output("\n".join(lines + changed + ending) + "\n", Stage.GREEN, "ordinary")





class SelectedReleaseHandoffControllerTest(unittest.TestCase):

    def test_failed_native_products_cannot_print_success(self):
        from tools import check_selected_release_handoff as driver

        output = io.StringIO()
        arguments = ["--native", *(word for index in range(5) for word in ("--executable", "/fixture/" + str(index)))]
        with (
            mock.patch.object(driver, "require_current_runtime", return_value={}),
            mock.patch.object(driver, "checked_source_view", side_effect=lambda root: nullcontext()),
            mock.patch.object(driver, "check_native_products", side_effect=ValueError("native failure")),
            redirect_stdout(output),
            self.assertRaisesRegex(ValueError, "native failure"),
        ):
            driver.main(arguments)
        self.assertEqual("", output.getvalue())



if __name__ == "__main__":
    unittest.main()
