import ast
from contextlib import redirect_stderr, redirect_stdout
import io
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from unittest import mock

from tests.host.selected_address_space_evidence import (
    parse_semantics, parse_target_output, semantic_results, target_failures,
)
from tests.host.selected_address_space_source import (
    FAMILY, SOURCE_PROJECTION, SOURCE_ROLES, extended_header, fixed_source,
    observe_source, project_observations, validate_retained_source,
)
from tests.host.work_count_types import Stage


ROOT = Path(__file__).resolve().parents[2]
FROZEN_PREDECESSOR_SHA256 = "2904605d09065ca72937f26aaf2b42861872ba09363438739c817b63450aeb37"
FROZEN_AFFECTED_SHA256 = "6ab74a18b41e7441592cad398f6cd18d41e2b5aab48007c73a110682a012eaa5"


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
    def test_failed_target_capture_reports_its_owned_output_without_another_run(self):
        from tests.host import selected_address_space_evidence as evidence

        diagnostic = "MICROS_TEST_FAILURE handoff-stage=0x0000000000000005\n"
        result = subprocess.CompletedProcess((), 2, diagnostic, "")
        errors = io.StringIO()
        with (
            mock.patch.object(evidence, "_target_inputs", return_value={}),
            mock.patch.object(evidence.subprocess, "run", return_value=result) as execute,
            redirect_stdout(io.StringIO()),
            redirect_stderr(errors),
            self.assertRaises(ValueError),
        ):
            evidence.measure_targets(ROOT, Stage.GREEN)
        execute.assert_called_once()
        self.assertEqual(diagnostic, errors.getvalue())

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

    def test_actual_primitive_projection_rejects_relocation_and_source_changes(self):
        path = "kernel/memory/user_address_space_runtime.c"
        original = fixed_source(str(ROOT), path).decode()
        observed = observe_source(ROOT, path, original, "aggregate")
        self.assertEqual(original, project_observations(ROOT, path, observed, "aggregate"))
        match = re.search(r'MICROS_TEST_WORK_HIT\("mem04-[^"]+",[^;]+;', observed)
        self.assertIsNotNone(match)
        call = match[0]
        changed = (
            observed.replace(call, "", 1),
            observed.replace(call, call + call, 1),
            observed.replace(call, "", 1) + call,
            observed.replace(call, call.replace(", 1);", ", 2);"), 1),
            observed.replace("bitmap[index] = 0;", "bitmap[index] = 1;", 1),
        )
        for text in changed:
            with self.subTest(text=text[-80:]), self.assertRaises(ValueError):
                project_observations(ROOT, path, text, "aggregate")

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

    def test_legacy_native_orchestrator_owns_one_checked_view(self):
        from tests.host import evidence_authority_scope as scope
        from tools import check_selected_ownership as checker

        def observed(root, products):
            self.assertIsNotNone(scope._active)
            self.assertEqual(ROOT, scope._active.root)
            self.assertEqual(ROOT, root)
            self.assertEqual(["fixture"] * 6, products)

        with (
            mock.patch.object(scope, "_active", None),
            mock.patch.object(checker, "require_current_runtime", return_value={}),
            mock.patch.object(checker, "check_native_products", side_effect=observed) as execute,
            redirect_stdout(io.StringIO()),
        ):
            self.assertEqual(0, checker.main(["--native", *(["--executable", "fixture"] * 6)]))
            execute.assert_called_once()
            self.assertIsNone(scope._active)


class SelectedAddressSpaceAuthorityTest(unittest.TestCase):
    def test_private_include_projection_retains_the_added_edge_and_declarations(self):
        from tests.host import kernel_layout_source
        from tests.host.selected_address_space_source import project_include_transports

        logical = "kernel/user_address_space_internal.h"
        path = "kernel/memory/user_address_space_internal.h"
        original = fixed_source(str(ROOT), path).decode()
        current = extended_header(path, original).encode()
        _, _, checksum, before = kernel_layout_source.KERNEL_FILE_FACTS[logical]
        after = project_include_transports(ROOT, logical, current, before, False)
        restored = kernel_layout_source._restored_include_bytes(
            logical, current, checksum, after, required=False,
        )
        self.assertIn(b'#include "kernel/tty_handoff_core.h"\n', restored)
        self.assertIn(b'#include "kernel/vm_handoff_core.h"\n', restored)
        self.assertIn(b"struct micros_user_address_space_path", restored)
        self.assertEqual(before, project_include_transports(ROOT, logical, current, before, True))
        with self.assertRaises(AssertionError):
            kernel_layout_source._restored_include_bytes(logical, current, checksum, before)
        with self.assertRaises(ValueError):
            project_include_transports(ROOT, logical, current + b"\n", before, False)

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

    def test_handoff_dependency_admits_only_the_two_owned_error_assignments(self):
        from tests.host import selected_ownership_source as previous
        from tests.host.selected_address_space_source import extend_handoff_preflight

        path = "kernel/memory/user_address_space_handoff.c"
        original = fixed_source(str(ROOT), path).decode()
        current = extend_handoff_preflight(original)
        statement = "            error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;\n"
        self.assertEqual(2, current.count(statement) - original.count(statement))
        validate_retained_source(ROOT, path, original, Stage.RED)
        validate_retained_source(ROOT, path, current, Stage.GREEN)
        previous.validate_retained_source(ROOT, path, current, Stage.GREEN)
        for changed in (
            original, current.replace(statement, "", 1),
            current.replace(statement, statement.replace("_STATE", "_OWNERSHIP"), 1),
            current.replace("return count == 1;", "return count == 0;", 1),
            current + "\n",
        ):
            with self.subTest(changed=changed[-60:]), self.assertRaises(ValueError):
                validate_retained_source(ROOT, path, changed, Stage.GREEN)

    def test_dependency_declaration_is_not_an_extra_path_allowance(self):
        from tests.host.selected_address_space_source import read_declarations, validate_declarations

        values = read_declarations(ROOT)
        for dependencies in (
            (), ("kernel/memory/user_address_space_mapping.c",),
            (*SOURCE_ROLES["dependent_production_paths"], "kernel/memory/user_address_space_mapping.c"),
        ):
            changed = {**values, "SOURCE_ROLES": {**values["SOURCE_ROLES"], "dependent_production_paths": dependencies}}
            with self.subTest(dependencies=dependencies), self.assertRaises(ValueError):
                validate_declarations(changed)

    def test_stack_admission_owners_are_exact_immutable_inputs_not_production(self):
        from tests.host.selected_address_space_source import read_declarations, validate_declarations

        values = read_declarations(ROOT)
        expected = (
            "tools/stack_bound_bindings.py", "tools/stack_bound_fatal.py",
            "tests/host/test_stack_bound_evidence.py",
        )
        roles = values["SOURCE_ROLES"]
        self.assertEqual(expected, roles.get("stack_admission_inputs", ()))
        self.assertFalse(set(expected).intersection(roles["production_paths"]))
        for paths in ((), expected[:1], (expected[0], expected[0]), (*expected, "kernel/memory/user_address_space_mapping.c")):
            changed = {**values, "SOURCE_ROLES": {**roles, "stack_admission_inputs": paths}}
            with self.subTest(paths=paths), self.assertRaises(ValueError):
                validate_declarations(changed)

    def test_changed_stack_admission_scaffold_cannot_reuse_runtime_evidence(self):
        from tests.host import selected_address_space_source as source

        read = source.read_regular_source
        for changed in (
            "tools/stack_bound_bindings.py", "tools/stack_bound_fatal.py",
            "tests/host/test_stack_bound_evidence.py",
        ):
            def read_changed(root, path):
                return read(root, path) + (b"\n" if path == changed else b"")

            with self.subTest(changed=changed), mock.patch.object(source, "read_regular_source", side_effect=read_changed):
                with self.assertRaisesRegex(ValueError, "stack"):
                    source.validate_scaffold(ROOT)

    def test_complete_predecessor_records_and_fingerprints_are_conserved(self):
        from tests.host.evidence_authority_inventory import current_inventory
        from tests.host.evidence_authority_source import literal_value, single_assignment
        from tests.host.evidence_authority_types import fact_set_digest
        from tests.host.evidence_authority_validation import validate_inventory

        inventory = current_inventory(ROOT)
        self.assertTrue(validate_inventory(inventory).ok)
        prior = tuple(fact for fact in inventory.facts if fact.family != FAMILY)
        affected = tuple(fact for fact in inventory.facts if fact.key in SOURCE_PROJECTION["retirement_keys"])
        self.assertEqual(FROZEN_PREDECESSOR_SHA256, fact_set_digest(prior))
        self.assertEqual(FROZEN_AFFECTED_SHA256, fact_set_digest(affected))
        original = ast.parse(fixed_source(str(ROOT), "tests/host/evidence_authority_inventory.py").decode())
        fingerprints = literal_value(single_assignment(original, "DECLARED_FAMILY_FACT_DIGESTS").value)
        self.assertEqual(
            fingerprints, {name: value for name, value in inventory.declared_family_fact_digests if name != FAMILY},
        )
        self.assertEqual(8, len(inventory.facts) - len(prior))

    def test_uncommitted_declaration_without_terminal_execution_retires_nothing(self):
        from tests.host import selected_address_space_authority as authority

        with (
            mock.patch.object(authority, "committed_claim", return_value=None),
            mock.patch.object(authority, "read_selection", return_value=(None, "")),
            mock.patch.object(authority, "_provisional", None),
        ):
            self.assertEqual(frozenset(), authority.retired_identity_keys(ROOT))

    def test_current_header_admission_is_byte_exact_and_does_not_authorize_legacy_retirement(self):
        from tests.host import selected_ownership_source as previous
        from tests.host.evidence_authority_scope import identity_required

        for path in SOURCE_ROLES["production_paths"]:
            if not path.endswith(".h"):
                continue
            original = fixed_source(str(ROOT), path).decode()
            current = extended_header(path, original)
            validate_retained_source(ROOT, path, current, Stage.GREEN)
            for text in (current + "\n", current.replace("\n#endif\n", "\n#endif /* changed */\n")):
                with self.subTest(path=path, text=text[-45:]), self.assertRaises(ValueError):
                    validate_retained_source(ROOT, path, text, Stage.GREEN)
            with self.subTest(path=path), self.assertRaises(ValueError):
                validate_retained_source(ROOT, path, current, Stage.RED)
            with mock.patch(
                identity_required.__module__ + ".identity_required", return_value=True,
            ), self.assertRaises(ValueError):
                previous.validate_retained_source(ROOT, path, current, Stage.GREEN)

    def test_exact_frozen_adapters_reject_a_partial_or_unrelated_change(self):
        from tests.host.selected_address_space_source import extend_legacy_adapter, project_legacy_adapter

        for path in (
            "tests/host/kernel_layout_source.py",
            "tests/host/selected_ownership_source.py",
            "tests/host/test_kernel_vm_handoff_source_layout.py",
            "tests/host/test_scheduler_source_layout.py",
            "tests/host/test_tty_source_layout.py",
            "tools/generate_selected_ownership_fixture.py",
        ):
            original = fixed_source(str(ROOT), path).decode()
            current = extend_legacy_adapter(path, original)
            self.assertEqual(original, project_legacy_adapter(ROOT, path, current))
            with self.subTest(path=path), self.assertRaises(ValueError):
                project_legacy_adapter(ROOT, path, current + "\n")

    def test_native_recipe_inverse_keeps_full_only_coverage_and_original_bytes(self):
        from tests.host.selected_address_space_source import extend_cmake, native_recipe, project_cmake
        from tests.host.work_count_recipe import project_host_test_commands

        original = fixed_source(str(ROOT), "CMakeLists.txt").decode()
        current = extend_cmake(original)
        self.assertEqual(original, project_cmake(current))
        self.assertEqual(project_host_test_commands(original), project_cmake(project_host_test_commands(current)))
        addition = native_recipe()
        self.assertNotIn("add_dependencies(test-unit-fast", addition)
        for changed in (
            current.replace(addition, addition.replace("LABELS slow", "LABELS fast"), 1),
            current.replace(addition, addition.replace("JOB_POOL mem03_source_proof", ""), 1),
            current.replace(addition, addition + addition, 1),
            current + "\n",
        ):
            with self.subTest(changed=changed[-60:]), self.assertRaises(ValueError):
                project_cmake(changed)
