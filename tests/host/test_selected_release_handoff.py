"""Independent MEM05 evidence and authority regressions."""

from copy import deepcopy
from contextlib import ExitStack, nullcontext, redirect_stderr, redirect_stdout
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from tests.host.selected_release_handoff_evidence import (
    CASE_FIELDS, expected_cases, measure_native_channel, parse_target_output,
    replacement_failures, target_failures, work_failures,
)
from tests.host import selected_release_handoff_source as source
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
        from tools.generate_selected_release_handoff_fixture import fixed_inputs

        path = "kernel/memory/user_address_space_runtime.c"
        declared = source.fixed_source(str(ROOT), path).decode()
        for before, after in source.SOURCE_PROJECTION["skeleton_edits"][path]:
            self.assertEqual(1, declared.count(before))
            declared = declared.replace(before, after, 1)
        includes = "\n".join(line for line in declared.splitlines() if line.startswith("#include "))
        probe = includes + (
            "\nint main(void) {\n"
            "    return micros_frame_ownership_check_selection((const struct micros_frame_selection *)0, 0);\n"
            "}\n"
        )
        compiler = native_compiler(ROOT)
        with tempfile.TemporaryDirectory(prefix="micros-mem05-private-interface-") as directory:
            directory = Path(directory)
            for name, (raw, _) in fixed_inputs(str(ROOT)).items():
                if not name.endswith(".h"):
                    continue
                target = directory / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.declared_header(ROOT, name).encode() if name in source.HEADER_PATHS else raw)
            checked = subprocess.run(
                (compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                 "-I", str(directory), "-I", str(directory / "include"), "-fsyntax-only", "-x", "c", "-"),
                cwd=directory, input=probe, capture_output=True, text=True, timeout=30, check=False,
            )
        self.assertEqual((0, ""), (checked.returncode, checked.stderr), checked.stdout + checked.stderr)

    def test_existing_fixtures_compile_with_the_complete_declared_current_interfaces(self):
        from tests.host.selected_release_handoff_evidence import validate_legacy_fixture_syntax

        validate_legacy_fixture_syntax(ROOT)

    def test_all_declared_headers_preserve_the_fixed_predecessor(self):
        for path in source.HEADER_PATHS:
            with self.subTest(path=path):
                original = source.fixed_header(str(ROOT), path)
                current = source.declared_header(ROOT, path)
                self.assertNotEqual(original, current)
                self.assertEqual(original, source.project_header(ROOT, path, original))
                self.assertEqual(original, source.project_header(ROOT, path, current))
                with self.assertRaises(ValueError):
                    source.project_header(ROOT, path, current + "\n")

    def test_partial_or_foreign_interface_change_is_not_admitted(self):
        path = "include/micros/frame_ownership.h"
        current = source.declared_header(ROOT, path)
        for index, changed in enumerate((
            current.replace("uint32_t previous;", "uint64_t previous;", 1),
            current.replace("const struct micros_frame_selection *selection", "const uint64_t *selection", 1),
        )):
            with self.subTest(mutation=index), self.assertRaises(ValueError):
                source.project_header(ROOT, path, changed)
        with self.assertRaises(ValueError):
            source.declared_header(ROOT, "include/micros/user_address_space.h")

    def test_native_recipe_preserves_every_predecessor_byte_and_full_only_scope(self):
        from tests.host.selected_address_space_source import project_cmake as project_previous

        original = source.fixed_source(str(ROOT), "CMakeLists.txt").decode()
        current = source.extend_cmake(original)
        self.assertEqual(original, source.project_cmake(current))
        self.assertEqual(project_previous(original), project_previous(current))
        self.assertIn("set_tests_properties(selected-release-handoff PROPERTIES LABELS slow)", current)
        for changed in (
            current.replace("LABELS slow)\n    add_dependencies(test-unit selected_release", "LABELS fast)\n    add_dependencies(test-unit selected_release"),
            current.replace("JOB_POOL mem03_source_proof", "JOB_POOL unowned", 1),
            current.replace("    add_dependencies(test-unit selected_release_handoff", "    add_dependencies(test-unit-fast selected_release_handoff"),
        ):
            with self.subTest(mutation=changed), self.assertRaises(ValueError):
                source.project_cmake(changed)

    def test_native_recipe_composes_with_the_exact_host_runner_inverse(self):
        from tests.host.work_count_recipe import project_host_test_commands, project_root_recipe

        original = source.fixed_source(str(ROOT), "CMakeLists.txt").decode()
        current = source.extend_cmake(original)
        expected = project_host_test_commands(original, require_current=True)
        projected = project_host_test_commands(current, require_current=True)
        self.assertEqual(expected, source.project_cmake(projected))
        self.assertEqual(project_root_recipe(original), project_root_recipe(current))
        for changed in (
            projected + "\n",
            projected.replace("JOB_POOL mem03_source_proof", "JOB_POOL unowned", 1),
            projected.replace("    add_dependencies(test-unit selected_release_handoff",
                              "    add_dependencies(test-unit-fast selected_release_handoff"),
        ):
            with self.subTest(mutation=changed), self.assertRaises(ValueError):
                source.project_cmake(changed)


class SelectedReleaseHandoffDeclarationTest(unittest.TestCase):
    def test_scaffold_rejects_changed_frozen_and_nonmutable_inputs(self):
        source.validate_scaffold(ROOT)
        read = source.read_scaffold_input
        for selected in (
            "CMakeLists.txt",
            "tests/host/kernel_mapping_source.py",
            "tests/host/memory_native_source_bindings.py",
            "kernel/tests/memory/address_space_handoff_test_runtime.c",
            "kernel/tests/memory/user_address_space_test_validation.c",
            "tests/host/evidence_authority_inventory.py",
            "tests/host/evidence_authority_scope.py",
            "tests/host/frame_ownership_test_model.c",
        ):
            def changed(root, path):
                data = read(root, path)
                return data + b"\n" if path == selected else data

            with self.subTest(path=selected), mock.patch.object(source, "read_scaffold_input", side_effect=changed):
                with self.assertRaises(ValueError):
                    source.validate_scaffold(ROOT)

    def test_only_the_four_owned_audits_have_declared_replacement_boundaries(self):
        expected = source.declared_audit_boundaries(str(ROOT))
        observed = source.measure_audit_boundaries(ROOT, Stage.RED)
        self.assertEqual((20, 16, 4, 4), tuple(len(expected[name]) for name in ("original", "retained", "migrated", "plans")))
        self.assertEqual(expected["retained"], observed["retained"])
        self.assertEqual(expected["migrated"], observed["migrated"])
        self.assertEqual(1, len(observed["plans"]))
        with self.assertRaises(ValueError):
            source._call_rows(
                "kernel/memory/user_address_space.c",
                "void test(void) { callback = micros_user_address_space_collect_bootstrap_release_locked; }",
                ("micros_user_address_space_collect_bootstrap_release_locked",),
            )

    def test_literal_records_have_the_exact_finite_source_roles(self):
        values = source.read_declarations(ROOT)
        facts = (
            *source.declaration_facts(ROOT, source.AUTHORITY_PATH),
            *source.declaration_facts(ROOT, source.SOURCE_PATH),
        )
        self.assertEqual(8, len(facts))
        self.assertEqual(8, len({fact.key for fact in facts}))
        self.assertEqual(30, len(source.candidate_paths()))
        self.assertEqual((41, 21, 20), tuple(len(row[5]) for row in values["ORACLE_RULES"]))
        self.assertTrue(all(fact.family == source.FAMILY for fact in facts))

    def test_missing_duplicate_or_overlapping_declarations_fail(self):
        original = source.read_declarations(ROOT)
        mutations = []
        changed = deepcopy(original)
        changed["SOURCE_ROLES"]["production_paths"] = changed["SOURCE_ROLES"]["production_paths"][:-1]
        mutations.append(changed)
        changed = deepcopy(original)
        keys = changed["SOURCE_PROJECTION"]["retirement_keys"]
        changed["SOURCE_PROJECTION"]["retirement_keys"] = (*keys[:-1], keys[0])
        mutations.append(changed)
        changed = deepcopy(original)
        changed["SOURCE_ROLES"]["immutable_adapters"] = (
            changed["SOURCE_ROLES"]["production_paths"][0],
            *changed["SOURCE_ROLES"]["immutable_adapters"][1:],
        )
        mutations.append(changed)
        changed = deepcopy(original)
        changed["SOURCE_ROLES"]["dependent_target_paths"] = changed["SOURCE_ROLES"]["dependent_target_paths"][1:]
        mutations.append(changed)
        changed = deepcopy(original)
        del changed["SOURCE_PROJECTION"]["target_adapter_sha256"]["kernel/tests/memory/address_space_handoff_test_runtime.c"]
        mutations.append(changed)
        changed = deepcopy(original)
        changed["STATE_WORK_CONTRACT"]["owner_mask"] ^= 1
        mutations.append(changed)
        changed = deepcopy(original)
        helpers = changed["SOURCE_PROJECTION"]["current_helpers"]
        helpers["kernel/objects/kernel_objects.c"] = helpers["kernel/memory/frame_ownership.c"]
        mutations.append(changed)
        changed = deepcopy(original)
        helpers = changed["SOURCE_PROJECTION"]["current_helpers"]["kernel/memory/frame_ownership.c"]
        signature, _, shape = helpers["selection_header_is_valid"]
        helpers["selection_header_is_valid"] = (signature, ("undeclared", "undeclared"), shape)
        mutations.append(changed)
        changed = deepcopy(original)
        extras = changed["SOURCE_PROJECTION"]["extra_current_sites"]
        key = "kernel/memory/frame_ownership.c/selection_header_is_valid"
        extras[key] = ((*extras[key][0][:-1], True),)
        mutations.append(changed)
        for index, value in enumerate(mutations):
            with self.subTest(mutation=index), self.assertRaises(ValueError):
                source.validate_declarations(value)

    def test_inventory_registers_each_independently_declared_record(self):
        from tests.host.evidence_authority_inventory import current_inventory

        inventory = current_inventory(ROOT)
        expected = (
            *source.declaration_facts(ROOT, source.AUTHORITY_PATH),
            *source.declaration_facts(ROOT, source.SOURCE_PATH),
        )
        observed = tuple(fact for fact in inventory.facts if fact.family == source.FAMILY)
        self.assertEqual(expected, observed)

    def test_plan_is_bound_to_the_merged_design(self):
        from tests.host.selected_release_handoff_authority import task_definition

        authority, _, plan, registry = task_definition(ROOT)
        self.assertEqual(("B02-F006", "B02-F007"), authority.finding_ids)
        self.assertEqual(source.SOURCE_PROJECTION["retirement_keys"], plan.planned_retirement_identity_keys)
        self.assertEqual(13, len(authority.unit_input_paths))
        self.assertTrue(all(paths == source.candidate_paths() for _, paths in authority.unit_input_paths))
        self.assertEqual(7, len(registry.claims))

    def test_checked_source_provider_does_not_retire_an_uncommitted_plan(self):
        from tests.host.evidence_authority_scope import _providers
        from tests.host import selected_release_handoff_authority as authority

        self.assertIn(authority, _providers())
        with (
            mock.patch.object(authority, "_provisional", None),
            mock.patch.object(authority, "committed_claim", return_value=None),
            mock.patch.object(authority, "read_selection", return_value=(None, "")),
        ):
            self.assertEqual(frozenset(), authority.retired_identity_keys(ROOT))


class SelectedReleaseHandoffSourceTest(unittest.TestCase):
    def test_current_unchanged_subjects_keep_their_complete_fixed_bindings(self):
        paths = (
            *(path for path in source.RECIPE_ADMISSION["native_source_members"]
              if path not in source.SOURCE_ROLES["production_paths"]),
            *source.OBSERVED_HEADERS,
        )
        for path in paths:
            with self.subTest(path=path):
                original = source.fixed_source(str(ROOT), path).decode()
                before = source.baseline_observations(str(ROOT), path)
                current = source._current_observations(ROOT, path, original)
                self.assertEqual(set(before), set(current))
                for name, sites in before.items():
                    self.assertEqual(
                        tuple(site[1:] for site in sorted(sites, key=lambda row: row[5])),
                        tuple(site[1:] for site in current[name]),
                    )
                observed = source.observe_source(ROOT, path, original, "aggregate", Stage.GREEN)
                self.assertEqual(original, source.project_observations(ROOT, path, observed, "aggregate", Stage.GREEN))

    def test_source_stage_admission_cannot_substitute_old_code_or_unowned_inputs(self):
        path = "kernel/memory/frame_ownership.c"
        original = source.fixed_source(str(ROOT), path).decode()
        source.validate_retained_source(ROOT, path, original, Stage.RED)
        with self.assertRaises(ValueError):
            source.validate_retained_source(ROOT, path, original + "\n", Stage.RED)
        with self.assertRaises(ValueError):
            source.validate_retained_source(ROOT, path, original, Stage.GREEN)
        header = "include/micros/frame_ownership.h"
        source.validate_retained_source(ROOT, header, source.declared_header(ROOT, header), Stage.GREEN)
        with self.assertRaises(ValueError):
            source.validate_retained_source(ROOT, header, source.fixed_header(str(ROOT), header), Stage.GREEN)
        retained = "kernel/objects/kernel_objects.c"
        raw = source.fixed_source(str(ROOT), retained).decode()
        source.validate_retained_source(ROOT, retained, raw, Stage.GREEN)
        with self.assertRaises(ValueError):
            source.validate_retained_source(ROOT, retained, raw + "\n", Stage.GREEN)
        with self.assertRaises(ValueError):
            source.validate_retained_source(ROOT, "kernel/unowned.c", original, Stage.GREEN)

    def test_array_declarations_do_not_become_observed_variable_length_arrays(self):
        from tests.host.runtime_memory_evidence import native_compiler

        body = (
            "uint64_t observed_counts[8];\n"
            "observed_counts[0] = 1;\n"
            '_Static_assert(sizeof(observed_counts) == 64, "fixed native storage");\n'
            "return observed_counts[0] == 1 ? 0 : 1;\n"
        )
        observed = source._render_body(
            body, source._body_sites("kernel/memory/frame_ownership_validation.c", "main", body),
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

    def test_complete_fixed_inventory_and_independent_helper_shapes_are_closed(self):
        source.validate_primitive_declarations(ROOT)

    def test_declared_helpers_bind_all_primitive_placements_and_callers(self):
        from tests.host.c_source_records import function_records

        path = "kernel/memory/frame_ownership.c"
        name = "micros_frame_selection_clear"
        signature, _, shape = source.SOURCE_PROJECTION["current_helpers"][path][name]
        body = (
            "size_t position; size_t word_index;\n"
            "if (micros_frame_ownership_check_selection(selection, MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES)"
            " != MICROS_FRAME_OWNERSHIP_OK) { return false; }\n"
            + shape + "\nselection->word_count = 0; selection->frame_count = 0; return true;\n"
        )

        def record(value, declaration=signature):
            return function_records(declaration + " {\n" + value + "\n}")[name]

        source.validate_helper_contract(ROOT, path, name, record(body))
        source.validate_helper_contract(ROOT, path, name, record("size_t scalar = 0; (void)scalar;\n" + body))
        store = "selection->bitmap[word_index] = 0;"
        for changed in (
            body.replace(store, "", 1), body.replace(store, store + store, 1),
            body.replace(store, "", 1) + store,
            body.replace("selection->words[position] = 0;", "selection->positions[position] = 0;", 1),
            body + "for (size_t hidden = 0; hidden < 8; ++hidden) {}",
            body.replace("micros_frame_ownership_check_selection(", "undeclared_preflight("),
            "#if 0\n" + body + "#endif\n",
        ):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                source.validate_helper_contract(ROOT, path, name, record(changed))
        with self.assertRaises(ValueError):
            source.validate_helper_contract(ROOT, path, name, record(body, signature.replace("bool ", "int ", 1)))
        with self.assertRaises(ValueError):
            source.validate_helper_contract(ROOT, path, "unknown_helper", record(body))

    def test_scalar_header_probe_cannot_disappear_or_be_duplicated(self):
        from tests.host.c_source_records import function_records

        path = "kernel/memory/frame_ownership.c"
        name = "selection_header_is_valid"
        signature, _, shape = source.SOURCE_PROJECTION["current_helpers"][path][name]
        body = "if (selection == NULL) { return false; }\n" + shape
        record = function_records(signature + " {\n" + body + "\n}")[name]
        source.validate_helper_contract(ROOT, path, name, record)
        for changed in (body.replace(shape, "return true;"), body + shape):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                record = function_records(signature + " {\n" + changed + "\n}")[name]
                source.validate_helper_contract(ROOT, path, name, record)

    def test_consumed_link_binding_keeps_bounds_before_record_access(self):
        from tests.host.c_source_records import function_records

        path = "kernel/memory/frame_ownership.c"
        name = "index_owner_matches_slot"
        signature, _, shape = source.SOURCE_PROJECTION["current_helpers"][path][name]
        body = (
            "struct micros_frame_owner owner;\n"
            + shape.replace("{}", "{ return false; }")
            + "return micros_frame_owner_is_valid(owner) && micros_frame_owner_kind_is_process_bound(owner.kind)"
            " && owner.slot == slot;\n"
        )
        source.validate_helper_contract(ROOT, path, name, function_records(signature + "{" + body + "}")[name])
        store = "owner = ownership->owners[encoded - 1];"
        moved = body.replace(store, "", 1).replace("if (encoded", store + "\nif (encoded", 1)
        for changed in (
            moved, body.replace(store, store + store),
            body.replace("owners[encoded - 1]", "owners[encoded]"),
            body.replace(store, "if (slot == 0) { " + store + " }"),
        ):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                source.validate_helper_contract(ROOT, path, name, function_records(signature + "{" + changed + "}")[name])


class SelectedReleaseHandoffLegacyTest(unittest.TestCase):
    def test_related_private_includes_keep_exact_legacy_transport(self):
        for logical, line, added in (
            ("kernel/user_address_space.c", 7, "kernel/kernel_object_runtime_internal.h"),
            ("kernel/user_address_space_runtime.c", 1, "kernel/frame_ownership_internal.h"),
        ):
            previous = ((
                line, "kernel/user_address_space_internal.h",
                '#include "kernel/user_address_space_internal.h"\n',
                '#include "kernel/memory/user_address_space_internal.h"\n',
            ),)
            with self.subTest(path=logical), mock.patch.object(source, "_current_authority", return_value=False):
                self.assertEqual(previous, source.project_include_transports(ROOT, logical, b"unit source", previous, False))
            with (
                mock.patch.object(source, "_current_authority", return_value=True),
                mock.patch.object(source, "validate_retained_source") as validate,
            ):
                current = source.project_include_transports(ROOT, logical, b"unit source", previous, False)
                self.assertEqual(previous, current[:1])
                self.assertEqual((8, added), current[1][:2])
                validate.assert_called_once_with(ROOT, "kernel/memory/" + Path(logical).name, "unit source", Stage.GREEN)
                with self.assertRaises(AssertionError):
                    source.project_include_transports(ROOT, logical, b"unit source", (), False)

    def test_exact_legacy_adapters_preserve_all_predecessor_bytes(self):
        paths = (
            *(path for path in source.SOURCE_ROLES["fixed_adapters"] if path.endswith(".py")),
            "tests/host/test_kernel_address_space_source_layout.py",
        )
        for path in paths:
            with self.subTest(path=path):
                original = source.baseline_adapter_source(str(ROOT), path)
                expected = source.extend_legacy_adapter(ROOT, path, original)
                current = (ROOT / path).read_text()
                self.assertEqual(expected, current)
                self.assertEqual(original, source.project_legacy_adapter(ROOT, path, current))
                with self.assertRaises(ValueError):
                    source.project_legacy_adapter(ROOT, path, current + "\n")
                with self.assertRaises(ValueError):
                    source.extend_legacy_adapter(ROOT, path, original + "\n")

    def test_current_headers_cross_only_verified_legacy_admission(self):
        from tests.host import evidence_authority_scope as scope
        from tests.host import selected_address_space_source as mem04
        from tests.host import selected_ownership_source as mem03

        selected = frozenset(source.SOURCE_PROJECTION["retirement_keys"])
        header = "include/micros/frame_ownership.h"
        declared = source.declared_header(ROOT, header)
        with mock.patch.object(scope, "identity_required", return_value=True):
            self.assertEqual(frozenset(), source.native_assertion_admission(ROOT, "MEM03", header, declared, Stage.GREEN))
            for backend in (mem03, mem04):
                with self.subTest(backend=backend.__name__), self.assertRaises(ValueError):
                    backend.validate_retained_source(ROOT, header, declared, Stage.GREEN)
        with mock.patch.object(scope, "identity_required", side_effect=lambda root, key: key not in selected):
            self.assertEqual(
                frozenset({"MEM03/header/" + header}),
                source.native_assertion_admission(ROOT, "MEM03", header, declared, Stage.GREEN),
            )
            for backend in (mem03, mem04):
                backend.validate_retained_source(ROOT, header, declared, Stage.GREEN)
                for changed in (declared + "\n", source.fixed_header(str(ROOT), header)):
                    with self.subTest(backend=backend.__name__, changed=changed), self.assertRaises(ValueError):
                        backend.validate_retained_source(ROOT, header, changed, Stage.GREEN)
            self.assertEqual(frozenset(), source.native_assertion_admission(ROOT, "MEM03", header, declared, Stage.RED))
            path = "kernel/memory/frame_ownership.c"
            with self.assertRaises(ValueError):
                source.legacy_replaced_functions(ROOT, "MEM03", path, source.fixed_source(str(ROOT), path).decode(), Stage.GREEN)

    def test_observation_replacement_preserves_every_unselected_legacy_contract(self):
        from tests.host import selected_address_space_source as mem04

        prefix = (
            "tests/host/selected_ownership_observation_bindings.py:native-source-admission:"
            "MEM03/observation/"
        )
        observed = set()
        with mock.patch.object(source, "_current_authority", return_value=True), \
                mock.patch.object(source, "validate_retained_source") as admission:
            for path in source.SOURCE_PROJECTION["mutable_functions"]:
                new = set(source.SOURCE_PROJECTION["current_helpers"].get(path, ()))
                removed = set(source.SOURCE_PROJECTION["removed_functions"].get(path, ()))
                names = source.legacy_replaced_functions(ROOT, "MEM03", path, "unit selector input", Stage.GREEN)
                observed.update(prefix + path + "/" + name for name in names - new - removed)
                protected = set(mem04.SOURCE_PROJECTION["new_helpers"].get(path, ()))
                protected.update(mem04.SOURCE_PROJECTION["mutable_functions"].get(path, ()))
                self.assertFalse(protected & source.legacy_replaced_functions(
                    ROOT, "MEM04", path, "unit selector input", Stage.GREEN,
                ))
            self.assertEqual(
                {key for key in source.SOURCE_PROJECTION["retirement_keys"] if key.startswith(prefix)},
                observed,
            )
            self.assertEqual(2 * len(source.SOURCE_PROJECTION["mutable_functions"]), admission.call_count)

    def test_added_legacy_work_keeps_existing_channel_ownership(self):
        for category, kind, expected in (
            ("frames", "RECORD_VISIT", "path"),
            ("plans", "PTE_VISIT", "path"),
            ("plans", "COPY_UNIT", "path"),
            ("frames", "INDEX_PROBE", "context"),
            ("frames", "BITMAP_WORD_LOAD", "context"),
            ("plans", "BITMAP_WORD_LOAD", "context"),
            ("plans", "CLEAR_UNIT", "context"),
            ("context", "INDEX_PROBE", "context"),
        ):
            site = ("unit-site", "wrap", category, "KERNEL_MEMORY", kind, 0, 1, "i", (), 1)
            with self.subTest(category=category, kind=kind):
                self.assertTrue(source._legacy_site_selected("MEM04", expected, site))
                self.assertFalse(source._legacy_site_selected("MEM04", "context" if expected == "path" else "path", site))

    def test_added_legacy_work_obeys_the_original_mem03_slice_contract(self):
        from tests.host.selected_ownership_evidence import _validate_snapshot
        from tests.host.selected_ownership_source import STATE_WORK_CONTRACT as previous

        sites = tuple(
            site
            for path, functions in source.SOURCE_PROJECTION["current_helpers"].items()
            for name, (_, _, body) in functions.items()
            for site in source._body_sites(path, name, body)
        )
        for channel in previous["channels"]:
            counts = [[0 for _ in Kind] for _ in Owner]
            for site in sites:
                if source._legacy_site_selected("MEM03", channel, site):
                    counts[Owner[site[3]]][Kind[site[4]]] += 1
            if channel == "aggregate":
                self.assertGreater(counts[Owner.KERNEL_MEMORY][Kind.INDEX_PROBE], 0)
            value = {
                "version": 1, "reserved0": 0, "reserved1": 0,
                "producer_label": previous["producer_base"] + previous["channels"].index(channel) + 1
                + previous["current_producer_offset"],
                "capture_label": 1, "owner_mask": previous["owner_mask"], "counts": counts,
            }
            with self.subTest(channel=channel):
                _validate_snapshot(value, Stage.GREEN, channel, 1)

    def test_legacy_comparison_and_completed_store_observations_are_retained(self):
        from tests.host import selected_address_space_source as mem04

        path, name = "kernel/memory/user_address_space_runtime.c", "sample"
        body = "\nbitmap[0] = 0;\nreturn bitmap[0] == 0;\n"
        text = "static int sample(unsigned *bitmap) {" + body + "}\n"
        sites = mem04._explicit_sites(path, name, body, (
            ("unit-store", "after", "context", "CLEAR_UNIT", "bitmap[0] = 0;", ()),
            ("unit-compare", "compare", "path", "COMPARISON", "bitmap[0] == 0", ()),
        ))
        with mock.patch.object(mem04, "observation_sites", return_value={name: sites}):
            retained = source._legacy_retained_sites(ROOT, "MEM04", path, text, "aggregate")
        rendered = source._render_body(body, retained[name])
        for site in sites:
            self.assertEqual(1, rendered.count(mem04._observation_call(site)))
        self.assertLess(rendered.index("bitmap[0] = 0;"), rendered.index('"unit-store"'))
        self.assertLess(rendered.index('"unit-store"'), rendered.index("return"))
        with self.assertRaises(ValueError):
            source._render_body(body, (("unknown", "unknown", "context", "KERNEL_MEMORY", "CLEAR_UNIT", 0, 0, "", (), 1),))

    def test_extracted_legacy_units_keep_the_exact_value_wrapper(self):
        from tools import generate_kernel_mapping_fixture as mem02
        from tools import generate_selected_ownership_fixture as mem03
        from tests.host import selected_address_space_source as mem04

        text = source.LEGACY_VALUE_PREFIX + "unit-source"
        for builder, arguments, owner, definitions in (
            (mem02._bootstrap_unit, (text, True), mem02, "_definitions"),
            (mem03._bootstrap_unit, (text, Stage.GREEN), mem03, "_definitions"),
            (mem04.bind_native, ("kernel/memory/bootstrap_memory.c", text, Stage.GREEN), mem04, "definitions"),
        ):
            with self.subTest(builder=builder.__module__), mock.patch.object(owner, definitions, return_value="unit-body"):
                self.assertEqual(1, builder(*arguments).count(source.LEGACY_VALUE_PREFIX))
        self.assertEqual("", source.legacy_binding_prefix("unobserved-source"))
        for changed in (
            source.LEGACY_VALUE_PREFIX * 2,
            source.LEGACY_VALUE_PREFIX.replace("(owner), (kind), 1", "(owner), (kind), 2"),
            "MICROS_MEM05_OBSERVE_VALUE(unit-site, 0, 0, 1)",
        ):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                source.legacy_binding_prefix(changed)

    def test_current_audit_and_scratch_views_require_verified_current_source(self):
        from tests.host import selected_address_space_source as mem04

        with mock.patch.object(source, "_current_authority", return_value=False):
            self.assertEqual(mem04.declared_audit_boundaries(ROOT)["retained"], source.legacy_expected_audits(ROOT))
            self.assertIsNone(source.legacy_audit_boundaries(ROOT, Stage.GREEN))
            self.assertFalse(source.current_scratch_storage(ROOT))
        with mock.patch.object(source, "_current_authority", return_value=True):
            self.assertEqual(source.declared_audit_boundaries(ROOT)["retained"], source.legacy_expected_audits(ROOT))
            self.assertIsNone(source.legacy_audit_boundaries(ROOT, Stage.RED))
            with mock.patch.object(
                source, "regular_source",
                side_effect=lambda root, path: SimpleNamespace(
                    read_text=lambda: source.fixed_source(str(ROOT), path).decode(),
                ),
            ):
                with self.assertRaisesRegex(ValueError, "unowned complete audit/private-plan caller"):
                    source.legacy_audit_boundaries(ROOT, Stage.GREEN)
                with self.assertRaises(ValueError):
                    source.current_scratch_storage(ROOT)


class SelectedReleaseHandoffTargetTest(unittest.TestCase):
    def test_failed_capture_exposes_owned_output_without_rerunning_qemu(self):
        from tests.host import selected_release_handoff_evidence as evidence

        output = "MICROS_TEST_FAILURE handoff-stage=0x0000000000000005\n"
        result = subprocess.CompletedProcess((), 2, output, "")
        errors = io.StringIO()
        with (
            mock.patch.object(evidence, "_target_inputs", return_value={}),
            mock.patch.object(evidence.subprocess, "run", return_value=result) as execute,
            redirect_stdout(io.StringIO()), redirect_stderr(errors), self.assertRaises(ValueError),
        ):
            evidence.measure_targets(ROOT, Stage.GREEN)
        self.assertEqual(1, execute.call_count)
        self.assertEqual(output, errors.getvalue())

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
    def test_fixed_foreign_generation_count_keeps_its_historical_target_scope(self):
        from tests.host.runtime_memory_evidence import native_compiler
        from tools.generate_selected_release_handoff_fixture import fixed_inputs

        members = (
            "kernel/memory/frame_allocator.c", "kernel/memory/frame_ownership.c",
            "kernel/memory/frame_ownership_validation.c", "kernel/memory/frame_ownership_bootstrap.c",
        )
        program = r'''
#include <inttypes.h>
#include <stdio.h>
#include "micros/frame_ownership.h"

static struct micros_frame_allocator allocator;
static struct micros_frame_ownership ownership;

int main(void)
{
    const struct micros_physical_range range = {UINT64_C(0x100000), 65 * MICROS_FRAME_SIZE};
    const struct micros_frame_owner owner = {1, 0, MICROS_FRAME_OWNER_PROCESS_USER, 0};
    const struct micros_process_handle exact = {0, 1};
    const struct micros_process_handle foreign = {0, 2};
    uint64_t address = UINT64_MAX;
    uint64_t exact_count = UINT64_MAX;
    uint64_t foreign_count = UINT64_MAX;
    enum micros_frame_ownership_error exact_result;
    enum micros_frame_ownership_error foreign_result;

    if (micros_frame_allocator_initialize(&allocator, &range, 1, NULL, 0) != MICROS_FRAME_ALLOCATOR_OK
        || micros_frame_ownership_initialize(&ownership, &allocator) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_allocate(&ownership, owner, &address) != MICROS_FRAME_OWNERSHIP_OK
        || address != range.base) {
        return 2;
    }
    ownership.handoff_targets[0] = MICROS_FRAME_HANDOFF_TARGET_COUNT;
    exact_result = micros_frame_ownership_count_process(&ownership, exact, &exact_count);
    foreign_result = micros_frame_ownership_count_process(&ownership, foreign, &foreign_count);
    printf("%d %" PRIu64 " %d %" PRIu64 "\n",
           (int)exact_result, exact_count, (int)foreign_result, foreign_count);
    return 0;
}
'''
        compiler = native_compiler(ROOT)
        with tempfile.TemporaryDirectory(prefix="micros-mem05-count-scope-") as temporary:
            directory = Path(temporary)
            for path, (raw, _) in fixed_inputs(str(ROOT)).items():
                if path.endswith(".h") or path in members:
                    target = directory / path
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(raw)
            probe = directory / "probe.c"
            probe.write_text(program)
            executable = directory / "probe"
            command = (
                compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                "-Wmissing-prototypes", "-Wstrict-prototypes", "-fno-builtin",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-I", str(directory), "-I", str(directory / "include"),
                *(str(directory / path) for path in members), str(probe), "-o", str(executable),
            )
            built = subprocess.run(command, capture_output=True, text=True, timeout=30, check=False)
            self.assertEqual((0, ""), (built.returncode, built.stderr), built.stdout + built.stderr)
            executed = subprocess.run((str(executable),), capture_output=True, text=True, timeout=30, check=False)
            self.assertEqual((0, ""), (executed.returncode, executed.stderr))
            self.assertEqual("14 18446744073709551615 0 0\n", executed.stdout)

    def test_current_fixture_compiles_against_independently_declared_interfaces(self):
        from tests.host.selected_release_handoff_evidence import validate_current_fixture_syntax

        result = validate_current_fixture_syntax(ROOT)
        self.assertEqual(2, len(result["commands"]))
        self.assertEqual(set(source.HEADER_PATHS), set(result["interfaces"]))

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

    def test_fixed_source_executes_all_slices_and_ordinary_control(self):
        from tests.host.selected_release_handoff_evidence import _require_discriminating_red, _validate_native_replay

        measured = {}
        for channel in ("aggregate", "frames", "plans", "context", "ordinary"):
            try:
                measured[channel] = measure_native_channel(ROOT, Stage.RED, channel)
            except ValueError as error:
                self.fail("A required actual-source slice could not execute: " + str(error))
        ordinary = measured["ordinary"]
        self.assertEqual(128, len(ordinary["rows"]))
        self.assertEqual((), ordinary["observation_symbols"])
        for channel, result in measured.items():
            with self.subTest(channel=channel):
                self.assertEqual(ordinary["rows"], result["rows"])
                self.assertEqual(214, len(result["source_inputs"]))
                self.assertEqual((0, 0), (result["build_exit"], result["execution_exit"]))
                self.assertEqual(0 if channel == "ordinary" else 128, len(result["observations"]))
        for index, row in enumerate(ordinary["rows"]):
            captures = {channel: measured[channel]["observations"][index]["counts"]
                        for channel in ("frames", "plans", "context")}
            summed = tuple(tuple(sum(captures[channel][owner][kind] for channel in captures)
                                 for kind in Kind) for owner in Owner)
            self.assertEqual(summed, measured["aggregate"]["observations"][index]["counts"])
            if row["operation"] == "release-set" and row["selected"] == 0:
                failures = work_failures({key: row[key] for key in CASE_FIELDS}, captures)
                self.assertTrue(any(failure[0] == "bitmap-word-loads" for failure in failures))
        failures = replacement_failures(measured)
        self.assertTrue(all(failures.values()))
        _require_discriminating_red(measured, failures)
        incomplete = {**failures, "owner-index": tuple(
            row for row in failures["owner-index"]
            if expected_cases()[row[0] - 1]["operation"] != "count-process"
        )}
        with self.assertRaises(ValueError):
            _require_discriminating_red(measured, incomplete)
        replay = json.loads(json.dumps(measured))
        context = SimpleNamespace(
            root=ROOT, native_compiler=ordinary["compiler"], native_nm=ordinary["nm"],
            files=ordinary["fixture_inputs"],
        )
        self.assertEqual(failures, _validate_native_replay(context, replay, Stage.RED))
        for field, value in (("nm_exit", 1), ("nm_output", "0000 T unrelated_symbol\n")):
            changed = deepcopy(replay)
            changed["aggregate"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                _validate_native_replay(context, changed, Stage.RED)
        wanted = {
            "owner-index": "owner-record-visits",
            "selected-release": "selected-semantics",
            "wired-handoff": "process-thread-record-visits",
        }
        for group, property_name in wanted.items():
            self.assertTrue(any(failure[1] == property_name for failure in failures[group]), group)
        changed = deepcopy(measured)
        for channel in ("frames", "plans", "context"):
            for observation in changed[channel]["observations"]:
                observation["producer_label"] += 16
        with self.assertRaises(ValueError):
            replacement_failures(changed)
        changed = deepcopy(measured)
        changed["frames"]["observations"], changed["plans"]["observations"] = (
            changed["plans"]["observations"], changed["frames"]["observations"],
        )
        with self.assertRaises(ValueError):
            replacement_failures(changed)
        changed = deepcopy(measured)
        changed["ordinary"]["execution_exit"] = 1
        with self.assertRaises(ValueError):
            replacement_failures(changed)


class SelectedReleaseHandoffAuthorityTest(unittest.TestCase):
    def test_terminal_query_replays_one_verified_journal_only(self):
        from tests.host import selected_release_handoff_authority as authority
        from tests.host.evidence_authority_types import Environment

        environment = Environment((), (), (), (), "a" * 40, "b" * 40)
        context = SimpleNamespace(root=ROOT, environment=environment)
        selector = {"attempt": "0" * 32, "count": 3, "sha256": "c" * 64}
        records = [
            ({"environment": environment}, {"sha256": value * 64})
            for value in ("a", "b", "c")
        ]
        with tempfile.TemporaryDirectory(prefix="micros-mem05-replay-") as directory, ExitStack() as patches:
            path = Path(directory)
            (path / selector["attempt"]).mkdir()
            for name, value in {
                "_provisional": None,
                "committed_claim": mock.Mock(return_value=None),
                "read_selection": mock.Mock(return_value=(selector, "d" * 64)),
                "journal_directory": mock.Mock(return_value=path),
                "read_phase_record": mock.Mock(side_effect=records),
                "capture_author_context": mock.Mock(return_value=context),
                "recheck_context": mock.Mock(),
            }.items():
                patches.enter_context(mock.patch.object(authority, name, value))
            verify = patches.enter_context(mock.patch.object(
                authority, "validate_journal", return_value=SimpleNamespace(active_identity_keys=()),
            ))
            self.assertFalse(authority.identity_required(ROOT, source.SOURCE_PROJECTION["retirement_keys"][0]))
            verify.assert_called_once()

    def test_incomplete_or_foreign_transactions_cannot_retire_assertions(self):
        from tests.host import selected_release_handoff_authority as authority

        with (
            mock.patch.object(authority, "committed_claim", return_value=None),
            mock.patch.object(authority, "read_selection", return_value=(None, "")) as selector,
            mock.patch.object(authority, "_provisional", None),
            mock.patch.object(authority, "read_verified_journal") as replay,
        ):
            self.assertEqual(frozenset(), authority.retired_identity_keys(ROOT))
            for count in (0, 1, 2):
                selector.return_value = ({"attempt": "a" * 32, "count": count, "sha256": "b" * 64}, "c" * 64)
                self.assertEqual(frozenset(), authority.retired_identity_keys(ROOT))
            context = SimpleNamespace(root=ROOT, candidate="candidate")
            with mock.patch.object(authority, "_provisional", (os.getpid() + 1, context)):
                self.assertEqual(frozenset(), authority.retired_identity_keys(ROOT))
            with (
                mock.patch.object(authority, "_provisional", (os.getpid(), context)),
                mock.patch("tools.validation_context.capture_candidate", return_value="changed"),
                self.assertRaises(ValueError),
            ):
                authority.retired_identity_keys(ROOT)
            replay.assert_not_called()


class SelectedReleaseHandoffControllerTest(unittest.TestCase):
    def test_retained_runner_keeps_the_fixed_predecessor_selection(self):
        from tools import check_selected_address_space as previous
        from tools import check_selected_release_handoff as current
        from tools import run_host_tests

        with (
            mock.patch.object(run_host_tests, "select_suite", return_value=mock.sentinel.suite) as select,
            mock.patch.object(run_host_tests, "run_suite", return_value=0) as execute,
        ):
            previous.retained_checks(ROOT)
            original = select.call_args.kwargs["names"]
            select.reset_mock()
            status, _ = current.retained_checks(ROOT)
            self.assertEqual(0, status)
            self.assertEqual((*original, "test_selected_release_handoff"), select.call_args.kwargs["names"])
            self.assertEqual(2, execute.call_count)

    def test_failed_native_products_cannot_print_success_or_open_an_author_attempt(self):
        from tools import check_selected_release_handoff as driver

        output = io.StringIO()
        arguments = ["--native", *(word for index in range(5) for word in ("--executable", "/fixture/" + str(index)))]
        with (
            mock.patch.object(driver, "require_current_runtime", return_value={}),
            mock.patch.object(driver, "checked_source_view", side_effect=lambda root: nullcontext()),
            mock.patch.object(driver, "check_native_products", side_effect=ValueError("native failure")),
            mock.patch.object(driver, "author_lock") as author,
            redirect_stdout(output),
            self.assertRaisesRegex(ValueError, "native failure"),
        ):
            driver.main(arguments)
        self.assertEqual("", output.getvalue())
        author.assert_not_called()

    def test_author_invocation_requires_the_exact_interpreter_prefix_and_phase(self):
        from tools import check_selected_release_handoff as driver
        from tests.host import diagnostic_history_authority

        command = [
            "/fixture/python", "-E", "-S", "-B", "tools/check_selected_release_handoff.py", "--red",
        ]
        with (
            mock.patch.dict(driver.os.environ, {"MICROS_PYTHON": command[0]}),
            mock.patch.object(diagnostic_history_authority, "runtime_image", return_value={"path": "/fixture/python-real"}),
            mock.patch.object(driver.sys, "orig_argv", command),
        ):
            driver.require_invocation("red", {})
            for changed in (
                ["/foreign/python", *command[1:]], [command[0], *command[2:]],
                [*command[:-1], "--green"], [*command, "--unexpected"],
            ):
                with self.subTest(arguments=changed), mock.patch.object(driver.sys, "orig_argv", changed):
                    with self.assertRaises(ValueError):
                        driver.require_invocation("red", {})


if __name__ == "__main__":
    unittest.main()
