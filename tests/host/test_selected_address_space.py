from contextlib import redirect_stdout
import io
import os
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from tests.host.selected_address_space_evidence import (
    parse_semantics, parse_target_output, semantic_results, target_failures,
)
from tests.host import native_observations
from tests.host.work_count_lexical import function_spans
from tests.host.work_count_types import Stage


ROOT = Path(__file__).resolve().parents[2]


def semantic_records(corrected):
    cursor = (
        "MEM04_CURSOR version=1 observed=8 before=64 after="
        + ("64" if corrected else "128")
        + " other_state_unchanged=1 output_unchanged=1 allocator_unchanged="
        + ("1" if corrected else "0")
    )
    scoped = []
    for phase in (1, 2):
        scoped.append(
            f"MEM04_SCOPED version=1 phase={phase} diagnostic=12 lookup="
            + ("0 physical=4096" if corrected else "12 physical=18446744073709551615")
            + " mapped=4096 permissions=" + ("3" if corrected else "4294967295")
            + " absent=" + ("10" if corrected else "12")
            + " absent_outputs_unchanged=1 activation=" + ("0" if corrected else "12")
            + " before_satp=12 after_satp=" + ("13" if corrected else "12")
            + " expected_satp=13 restored_satp=12 state_unchanged=1"
        )
    return "\n".join((cursor, *scoped)) + "\n"


class SelectedAddressSpaceCaptureTest(unittest.TestCase):
    def test_current_native_generation_does_not_restore_an_ancestor(self):
        from tools import generate_selected_address_space_fixture as generator

        with mock.patch.object(generator, "fixed_inputs", side_effect=AssertionError("historical native inputs"), create=True):
            inputs, outputs = generator.source_bundle(ROOT, Stage.GREEN, "ordinary")
        self.assertIn("kernel/memory/user_address_space_runtime.c", inputs)
        self.assertEqual(set(inputs), set(outputs))


    def test_kernel_semantic_mismatch_is_not_a_capture_error(self):
        observed = parse_semantics(semantic_records(False))
        self.assertEqual(
            {"selected": False, "activation": False, "scoped-diagnostic": False},
            semantic_results(observed),
        )
        self.assertEqual(
            {"selected": True, "activation": True, "scoped-diagnostic": True},
            semantic_results(parse_semantics(semantic_records(True))),
        )

    def test_missing_duplicate_reordered_and_wide_records_fail_closed(self):
        text = semantic_records(False)
        lines = text.splitlines()
        malformed = (
            "", "\n".join(lines[:2]), "\n".join((*lines, lines[0])),
            "\n".join((lines[0], lines[2], lines[1])),
            text.replace("version=1", "version=2", 1),
            text.replace("observed=8", "observed=8 observed=8", 1),
            text.replace("observed=8", "observed=-1", 1),
            text.replace("before=64", "before=064", 1),
            text.replace("after=128", "after=18446744073709551616", 1),
            text.replace("output_unchanged=1", "output_unchanged=2", 1),
            text + "PASS\n",
        )
        for value in malformed:
            with self.subTest(value=value[:100]), self.assertRaises(ValueError):
                parse_semantics(value)

    def test_current_primitive_bindings_reject_relocation_and_source_changes(self):
        path = "kernel/memory/user_address_space_runtime.c"
        original = (ROOT / path).read_text()
        native_observations.observe_source(ROOT, "MEM04", path, original, "aggregate")
        span = function_spans(original)["clear_scratch_bitmap"]
        body = original[span.body_start:span.body_end]
        store = "bitmap[index] = 0;"
        self.assertIn(store, body)
        for changed in (
            body.replace(store, ""),
            body.replace(store, store + store),
            body.replace(store, "") + store,
            body.replace(store, "bitmap[index] = 1;"),
        ):
            with self.subTest(body=changed), self.assertRaises(ValueError):
                native_observations.observe_source(
                    ROOT, "MEM04", path, original[:span.body_start] + changed + original[span.body_end:], "aggregate",
                )

    def test_target_protocol_requires_exact_witnesses_and_the_owned_failure_path(self):
        before, selected = (8 << 60) | 0x1234, (8 << 60) | 0x1235

        def line(name, values):
            return "# " + name + " version=1 " + " ".join(
                key + "=0x" + format(value, "016x") for key, value in values.items()
            ) + "\n"

        tables = "".join(line("MEM04_TABLE", {
            "case": case, "lookup": 11, "activation": 11, "before_satp": before, "after_satp": before,
            "poison_unchanged": 1, "state_unchanged": 1, "outputs_unchanged": 1,
        }) for case in (0, 1))
        activation = {
            "case": 0, "diagnostic": 11, "activation": 11, "release": 11, "destroy": 11,
            "before_satp": before, "after_satp": before, "process_satp": selected, "restored_satp": before,
            "state_unchanged": 1, "output_unchanged": 1,
        }
        panic = "# MICROS_PANIC reason=user-address-space-test\n"
        baseline = tables + line("MEM04_ACTIVATION", activation) + panic
        workflow = "test-qemu-user-address-space"
        rows = parse_target_output(baseline, workflow, Stage.RED, 2)
        self.assertEqual((), target_failures(rows, baseline=True))
        self.assertTrue(target_failures(rows))
        for output, status in (
            (panic, 2), (baseline + line("MEM04_ACTIVATION", activation), 2),
            (baseline.replace(panic, "# MICROS_PANIC reason=unexpected-trap\n"), 2),
            (baseline, 0), (baseline, -9), (baseline.replace("version=1", "version=2"), 2),
        ):
            with self.subTest(output=output[-80:], status=status), self.assertRaises(ValueError):
                parse_target_output(output, workflow, Stage.RED, status)
        current = tables + "".join(line("MEM04_ACTIVATION", {
            **activation, "case": case, "diagnostic": 12 if case == 2 else 11,
            "release": 12 if case == 2 else 11, "destroy": 12 if case == 2 else 11,
            "activation": 0, "after_satp": selected,
        }) for case in range(5))
        current += (
            "# MICROS_USER_ADDRESS_SPACE_TEST_PASS roots=isolated reuse=zeroed active=guarded "
            "ownership=validated sum=cleared\n"
        )
        self.assertEqual((), target_failures(parse_target_output(current, workflow, Stage.GREEN, 0)))

    def test_native_launcher_rejects_consumed_environment_before_starting_a_child(self):
        from tests.host.c_source_records import function_records
        from tests.host.runtime_memory_evidence import native_compiler

        record = function_records((ROOT / "tests/host/selected_address_space_test.c").read_text())["launch_native_products"]
        with tempfile.TemporaryDirectory(prefix="micros-mem04-launcher-") as directory:
            directory = Path(directory)
            child, source, executable = directory / "child", directory / "launcher.c", directory / "launcher"
            child.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
            child.chmod(0o700)
            source.write_text(
                "#include <stdio.h>\n#include <stdlib.h>\n#include <unistd.h>\n"
                '#define MICROS_SELECTED_ADDRESS_SPACE_PYTHON "' + str(child) + '"\n'
                '#define MICROS_SELECTED_ADDRESS_SPACE_DRIVER "fixture-driver"\n'
                '#define MICROS_SELECTED_ADDRESS_SPACE_ROOT "' + str(directory) + '"\n'
                + record.signature + "{" + record.body + "}\n"
                "int main(int argc, char **argv) { return launch_native_products(argc, argv); }\n"
            )
            compiler = native_compiler(ROOT)
            built = subprocess.run(
                (compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                 str(source), "-o", str(executable)),
                stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual((0, ""), (built.returncode, built.stderr), built.stdout + built.stderr)
            command = (str(executable), "aggregate", "path", "context")
            for value in (None, "", "/fixture/consumed-launcher"):
                environment = dict(os.environ)
                environment.pop("__PYVENV_LAUNCHER__", None)
                if value is not None:
                    environment["__PYVENV_LAUNCHER__"] = value
                result = subprocess.run(
                    command, env=environment, stdin=subprocess.DEVNULL,
                    capture_output=True, text=True, timeout=30,
                )
                if value:
                    self.assertEqual(2, result.returncode)
                    self.assertEqual("", result.stdout)
                    self.assertIn("__PYVENV_LAUNCHER__ is unsupported", result.stderr)
                else:
                    self.assertEqual((0, ""), (result.returncode, result.stderr))
                    self.assertEqual(
                        ["-E", "-S", "-B", "fixture-driver", "--native",
                         "--executable", "aggregate", "--executable", "path",
                         "--executable", "context", "--executable", str(executable)],
                        result.stdout.splitlines(),
                    )

    def test_native_orchestrator_owns_one_current_snapshot(self):
        from tests.host import source_snapshot as scope
        from tools import check_selected_ownership as checker

        def observed(root, products):
            self.assertIsNotNone(scope._active)
            self.assertEqual(ROOT, scope._active.root)
            self.assertEqual(ROOT, root)
            self.assertEqual(["fixture"] * 6, products)

        with (
            scope.isolated_source_view(),
            mock.patch.object(checker, "require_current_runtime", return_value={}),
            mock.patch.object(checker, "check_native_products", side_effect=observed) as execute,
            redirect_stdout(io.StringIO()),
        ):
            self.assertEqual(0, checker.main(["--native", *(["--executable", "fixture"] * 6)]))
            execute.assert_called_once()
            self.assertIsNone(scope._active)


class SelectedAddressSpaceDependencyTest(unittest.TestCase):

    def test_tty_owner_direction_distinguishes_tags_from_function_references(self):
        from types import SimpleNamespace
        from tests.host import test_tty_source_layout as tty

        records = {
            tty.RUNTIME_SOURCES[0]: {"getter": SimpleNamespace(body="return 0;")},
            tty.RUNTIME_SOURCES[1]: {"context": SimpleNamespace(body="return 0;")},
            tty.RUNTIME_SOURCES[2]: {},
            tty.RUNTIME_SOURCES[3]: {},
        }
        case = tty.TtySourceLayoutTest("test_runtime_state_leaf_keeps_actual_owner_directions_acyclic")
        with (
            mock.patch.object(tty, "source", side_effect=lambda path: path),
            mock.patch.object(tty, "function_records", side_effect=lambda path: records[path]),
        ):
            for tag in ("struct", "union", "enum"):
                records[tty.RUNTIME_SOURCES[2]] = {
                    "inspect": SimpleNamespace(body=tag + " getter *value = context();"),
                }
                case.test_runtime_state_leaf_keeps_actual_owner_directions_acyclic()
            for body in ("return getter();", "void *value = &getter;"):
                records[tty.RUNTIME_SOURCES[2]] = {"inspect": SimpleNamespace(body=body)}
                with self.subTest(body=body), self.assertRaises(AssertionError):
                    case.test_runtime_state_leaf_keeps_actual_owner_directions_acyclic()
