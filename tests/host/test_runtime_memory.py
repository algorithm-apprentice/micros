"""Independent runtime memory semantics, work and authority controls."""

import ast
import copy
from contextlib import contextmanager, nullcontext, redirect_stderr, redirect_stdout
from dataclasses import asdict, replace
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

from tests.host.work_count_types import Kind, Owner, Stage


ROOT = Path(__file__).resolve().parents[2]


class RuntimeMemoryBaselineTest(unittest.TestCase):
    def test_pinned_red_measures_the_actual_byte_loop_and_fails_both_word_oracles(self):
        from tests.host.runtime_memory_evidence import (
            assert_aligned_work, measure_native, probe,
        )

        measured = measure_native(ROOT, Stage.RED)
        for operation, unit, byte in (
            ("copy", Kind.COPY_UNIT, Kind.COPIED_BYTE),
            ("fill", Kind.CLEAR_UNIT, Kind.CLEARED_BYTE),
        ):
            with self.subTest(operation=operation):
                row = probe(measured, operation, 64, 0, 0)
                self.assertEqual(64, row.capture.snapshot.counts[Owner.RUNTIME][unit])
                self.assertEqual(64, row.capture.snapshot.counts[Owner.RUNTIME][byte])
                with self.assertRaisesRegex(AssertionError, "aligned word work"):
                    assert_aligned_work(measured, operation)

    def test_disabled_fixture_preserves_all_residue_and_boundary_cases_without_capture(self):
        from tests.host.runtime_memory_evidence import measure_native, probe

        measured = measure_native(ROOT, Stage.RED, observed=False)
        probe(measured, "copy", 4096, 7, 5)
        for destination in range(8):
            for source in range(8):
                for length in (0, 7, 8, 9, 65, 4096):
                    with self.subTest(destination=destination, source=source, length=length):
                        row = probe(measured, "copy", length, destination, source)
                        self.assertIsNone(row.capture)
        for destination in range(4):
            for length in (0, 11, 12, 13, 4096):
                self.assertIsNone(probe(measured, "fill", length, destination, 0).capture)

    def test_record_shape_cannot_borrow_numeric_equality_or_foreign_work(self):
        from tests.host.runtime_memory_evidence import decode_measurements, measure_native

        measured = measure_native(ROOT, Stage.RED)
        records = [json.loads(line) for line in measured.output.splitlines()]
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

    def test_protected_boundaries_null_zero_and_typed_objects(self):
        from tests.host.runtime_memory_evidence import measure_access

        measured = measure_access(ROOT, Stage.RED)
        self.assertIs(True, json.loads(measured.output)["address_orders"])

    def test_green_source_admission_rejects_the_original_and_red_only_sites(self):
        from tests.host.runtime_memory_authority import (
            baseline_memory, red_translation_unit, validate_green_source,
        )

        for source in (baseline_memory(ROOT).decode(), red_translation_unit(ROOT)):
            with self.assertRaises(ValueError):
                validate_green_source(source)

    def test_complete_geometry_oracle_rejects_byte_work_beyond_the_two_aligned_cases(self):
        from tests.host.runtime_memory_evidence import assert_geometry_work, measure_native

        measured = measure_native(ROOT, Stage.RED)
        with self.assertRaisesRegex(AssertionError, "complete aligned intervals"):
            assert_geometry_work(measured)

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
            evidence.measure_native(ROOT, Stage.RED)


class RuntimeMemoryWorkTest(unittest.TestCase):
    def measurement(self):
        from tests.host.runtime_memory_evidence import measure_native

        return getattr(self, "supplied_measurement", None) or measure_native(ROOT, Stage.GREEN)

    def test_copy_aligned_work(self):
        from tests.host.runtime_memory_evidence import assert_aligned_work

        assert_aligned_work(self.measurement(), "copy")

    def test_fill_aligned_work(self):
        from tests.host.runtime_memory_evidence import assert_aligned_work

        assert_aligned_work(self.measurement(), "fill")


class RuntimeMemoryGreenTest(unittest.TestCase):
    def test_complete_geometry_semantics_modes_and_exact_access_ranges(self):
        from tests.host.runtime_memory_evidence import assert_geometry_work, measure_access, measure_native

        measured = measure_native(ROOT, Stage.GREEN)
        disabled = measure_native(ROOT, Stage.GREEN, observed=False)
        assert_geometry_work(measured)
        self.assertEqual(tuple(replace(row, capture=None) for row in measured.records), disabled.records)
        measure_access(ROOT, Stage.GREEN)

    def test_actual_production_body_lowers_to_wide_transfers_in_every_retained_mode(self):
        from tests.host.runtime_memory_evidence import target_transfer_proofs

        target_transfer_proofs(ROOT, Stage.GREEN)


class RuntimeMemoryArtifactTest(unittest.TestCase):
    def test_exact_inline_primitives_have_real_nonstack_word_transfers(self):
        from tests.host.runtime_memory_evidence import target_transfer_proofs

        proofs = target_transfer_proofs(ROOT, Stage.GREEN, fixture=True)
        self.assertEqual({"debug", "size"}, set(proofs))
        for proof in proofs.values():
            self.assertGreater(proof["copy"], 0)
            self.assertGreater(proof["fill"], 0)

    def test_original_stack_spills_cannot_impersonate_data_word_transfers(self):
        from tests.host.runtime_memory_evidence import target_transfer_proofs

        with self.assertRaisesRegex(ValueError, "wide data transfer"):
            target_transfer_proofs(ROOT, Stage.RED)

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


class RuntimeMemoryAuthorityTest(unittest.TestCase):
    def test_exact_assertion_adapter_preserves_the_complete_original_gate_ast(self):
        from tests.host.runtime_memory_authority import IDENTITY_OWNER, validate_gate_adapter

        validate_gate_adapter(ROOT, (ROOT / IDENTITY_OWNER).read_text())

    def test_adapter_projection_cannot_hide_a_wider_mask_partial_adapter_or_other_mutation(self):
        from tests.host.runtime_memory_authority import IDENTITY_OWNER, validate_gate_adapter

        source = (ROOT / IDENTITY_OWNER).read_text()
        for old, new in (
            ("memory_required = memory_identity_required(REPOSITORY_ROOT)", "memory_required = False"),
            ('if path == "lib/runtime/memory.c" and not memory_required:', 'if path.endswith(".c") and not memory_required:'),
            ("            FIXTURE_PATH,\n", "            FIXTURE_PATH,\n            FIXTURE_PATH,\n"),
            ("self.assertEqual((23, 11),", "self.assertEqual((23, 10),"),
            ("project_terminal_workflows(validation_plan.QEMU_WORKFLOWS)", "validation_plan.QEMU_WORKFLOWS"),
            ("project_terminal_workflows(validation_plan.affected_workflows([path]))", "project_terminal_workflows(())"),
        ):
            with self.subTest(mutation=new), self.assertRaises(ValueError):
                validate_gate_adapter(ROOT, source.replace(old, new))

    def test_new_family_preserves_all_prior_authority_and_the_retained_memory_fact(self):
        from tests.host.evidence_authority import AuthorityClass, validate_inventory
        from tests.host.evidence_authority_inventory import current_inventory
        from tests.host.evidence_authority_types import fact_set_digest
        from tests.host.runtime_memory_authority import DESIGN_COMMIT, MEMORY_IDENTITY
        from tests.host.test_evidence_authority import prior_binding_facts
        from tools.validation_inputs import _run

        inventory = current_inventory(ROOT)
        result = validate_inventory(inventory)
        self.assertTrue(result.ok, result)
        source = _run((
            "git", "show", DESIGN_COMMIT + ":tests/host/evidence_authority_inventory.py",
        ), cwd=ROOT).decode()
        statement, = (
            node for node in ast.parse(source).body
            if isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == "DECLARED_FAMILY_FACT_DIGESTS"
                for target in node.targets
            )
        )
        historical = prior_binding_facts(inventory.facts)
        for family, expected in ast.literal_eval(statement.value).items():
            self.assertEqual(expected, fact_set_digest(fact for fact in historical if fact.family == family))
        existing, = (fact for fact in inventory.facts if fact.key == MEMORY_IDENTITY)
        self.assertIs(AuthorityClass.TRANSPORT_IDENTITY, existing.authority_class)
        added = tuple(fact for fact in inventory.facts if fact.family == "runtime-memory-baseline")
        self.assertEqual(3, len(added))
        self.assertEqual(2, sum(fact.authority_class is AuthorityClass.BASELINE_ORACLE for fact in added))

    def test_real_design_binding_cannot_retire_a_new_claim_without_events(self):
        from tests.host.evidence_authority import Environment, TransitionState, validate_transition
        from tests.host.evidence_authority_inventory import current_inventory
        from tests.host.runtime_memory_authority import CANDIDATE_PATHS, MEMORY_IDENTITY, task_definition

        authority, plan, registry = task_definition(ROOT)
        immutable = set(plan.declared_external_inputs) | set(authority.command_input_paths)
        for oracle in plan.replacement_oracles:
            immutable.update(oracle.input_paths)
        entries = lambda paths: tuple((path, hashlib.sha256(path.encode()).hexdigest()) for path in sorted(paths))
        candidate = entries(CANDIDATE_PATHS)
        environment = Environment(
            entries(immutable), candidate, candidate,
            tuple((unit, candidate) for unit in plan.recertification_units),
            authority.base_head, authority.base_tree,
        )
        result = validate_transition(
            current_inventory(ROOT), {authority.task_id: authority}, registry, plan, (), environment,
        )
        self.assertTrue(result.ok, result)
        self.assertIs(TransitionState.REPLACEMENT_REGISTERED, result.state)
        self.assertIn(MEMORY_IDENTITY, result.active_identity_keys)
        self.assertFalse(result.retired_identity_keys)

    def test_design_binding_rejects_borrowed_or_incomplete_declarations(self):
        from tests.host import runtime_memory_authority as authority

        read = authority.design_blob
        original = read(ROOT, authority.DESIGN_DOCUMENT)
        for old, new in (
            (b"copy01-copy-aligned-word-work", b"unrelated-oracle"),
            (b"| B08 |", b"| B13 |"),
        ):
            def altered(root, path):
                return original.replace(old, new) if path == authority.DESIGN_DOCUMENT else read(root, path)

            with patch.object(authority, "design_blob", side_effect=altered), self.assertRaises(ValueError):
                authority.task_definition(ROOT)

    def test_closed_input_membership_cannot_omit_or_add_an_author_input(self):
        from tests.host import runtime_memory_authority as authority

        expected = authority.design_membership(ROOT)
        run = authority._run
        for selected in (expected, expected[:-1], (*expected, "tests/host/foreign_oracle.py")):
            def execute(argv, **kwargs):
                if argv[:2] == ("git", "ls-files"):
                    return b"\0".join(path.encode() for path in selected) + b"\0"
                return run(argv, **kwargs)

            with patch.object(authority, "_run", side_effect=execute):
                if selected == expected:
                    self.assertEqual(expected, authority.checked_membership(ROOT))
                else:
                    with self.assertRaises(ValueError):
                        authority.checked_membership(ROOT)

    def test_staged_claim_is_not_prior_committed_retirement_and_cannot_widen_it(self):
        from tests.host.evidence_authority_inventory import current_inventory
        from tests.host.runtime_memory_authority import DESIGN_COMMIT, memory_identity_required

        inventory = current_inventory(ROOT)
        with (
            tempfile.TemporaryDirectory(prefix="micros-copy-claim-") as temporary,
            patch("tests.host.evidence_authority_inventory.current_inventory", return_value=inventory),
        ):
            root = Path(temporary) / "checkout"
            subprocess.run(
                ["git", "clone", "--shared", "--quiet", "--no-checkout", str(ROOT), str(root)],
                check=True, capture_output=True,
            )
            subprocess.run(
                ["git", "checkout", "--quiet", "--detach", DESIGN_COMMIT],
                cwd=root, check=True, capture_output=True,
            )
            path = root / "tests/host/runtime_memory_authority.py"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((ROOT / "tests/host/runtime_memory_authority.py").read_bytes())
            subprocess.run(["git", "add", "tests/host/runtime_memory_authority.py"], cwd=root, check=True)
            self.assertTrue(memory_identity_required(root))
            subprocess.run([
                "git", "-c", "user.name=micros test", "-c", "user.email=micros-test@example.invalid",
                "commit", "--quiet", "-m",
                "fixture: committed COPY01 registry\n\nCo-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>",
            ], cwd=root, check=True, capture_output=True)
            self.assertFalse(memory_identity_required(root))
            path.write_text(path.read_text().replace(
                "STABLE_HASHES/lib/runtime/memory.c", "STABLE_HASHES/lib/runtime/memory.h",
            ))
            with self.assertRaises(ValueError):
                memory_identity_required(root)


class RuntimeMemoryRecordTest(unittest.TestCase):
    def test_event_decode_keeps_exact_types_and_rejects_unknown_or_incomplete_fields(self):
        from tests.host import evidence_authority as core
        from tests.host.runtime_memory_authority import decode_event

        inventory = core.synthetic_inventory()
        plan = core.synthetic_plan(inventory)
        environment = core.synthetic_environment(plan)
        event = core.synthetic_red_event(plan, environment)
        value = json.loads(json.dumps(asdict(event)))
        self.assertEqual(event, decode_event(value))
        for mutation in ("extra", "enum", "missing", "scalar-command", "unknown-oracle-field"):
            altered = copy.deepcopy(value)
            if mutation == "extra":
                altered["status"] = "PASS"
            elif mutation == "enum":
                altered["kind"] = "COMPLETED"
            elif mutation == "missing":
                del altered["immutable_oracle_projection"]
            elif mutation == "scalar-command":
                altered["command"] = "fake successful command"
            else:
                altered["oracle_results"][0]["borrowed"] = True
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                decode_event(altered)

    def test_phase_record_is_bound_to_attempt_and_original_complete_log_bytes(self):
        from tests.host import evidence_authority as core
        from tests.host.runtime_memory_authority import read_phase_record

        inventory = core.synthetic_inventory()
        plan = core.synthetic_plan(inventory)
        environment = core.synthetic_environment(plan)
        event = core.synthetic_red_event(plan, environment)
        with tempfile.TemporaryDirectory(prefix="micros-copy-record-") as temporary:
            directory = Path(temporary)
            log = directory / "red-native.log"
            original = b"unit codec fixture, not runtime execution"
            log.write_bytes(original)
            record = {
                "version": 1, "attempt": "a" * 32, "phase": "red",
                "previous": "", "event": asdict(event), "environment": asdict(environment),
                "logs": {"native": {"size": len(original), "sha256": hashlib.sha256(original).hexdigest()}},
            }
            path = directory / "red.json"
            path.write_text(json.dumps(record))
            parsed, _ = read_phase_record(directory, "red", "a" * 32, "")
            self.assertEqual(event, parsed["event"])
            log.write_bytes(b"replaced")
            with self.assertRaises(ValueError):
                read_phase_record(directory, "red", "a" * 32, "")
            log.write_bytes(original)
            for mutation in ("attempt", "unknown", "boolean-size", "missing-log"):
                altered = copy.deepcopy(record)
                if mutation == "attempt":
                    altered["attempt"] = "b" * 32
                elif mutation == "unknown":
                    altered["trusted"] = True
                elif mutation == "boolean-size":
                    altered["logs"]["native"]["size"] = True
                else:
                    altered["logs"] = {}
                path.write_text(json.dumps(altered))
                with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                    read_phase_record(directory, "red", "a" * 32, "")


class RuntimeMemoryJournalTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from tests.host import runtime_memory_evidence as evidence

        cls.measured = evidence.measure_native(ROOT, Stage.RED)
        cls.outcomes, output = evidence.run_oracles(cls.measured)
        cls.replay = evidence.native_log(cls.measured, cls.outcomes, output)

    @contextmanager
    def fixture(self):
        from tests.host import runtime_memory_authority as authority
        from tests.host.evidence_authority_inventory import current_inventory
        from tests.host.evidence_authority_types import Environment
        from tools import check_runtime_memory as driver

        task, plan, registry = authority.task_definition(ROOT)
        entries = lambda paths: tuple((path, hashlib.sha256(path.encode()).hexdigest()) for path in sorted(paths))
        immutable = entries({*task.command_input_paths, *plan.declared_external_inputs})
        candidate = entries(authority.CANDIDATE_PATHS)
        environment = Environment(
            immutable, candidate, candidate,
            tuple((unit, candidate) for unit in plan.recertification_units),
            task.base_head, task.base_tree,
        )
        context = authority.AuthorContext(
            ROOT, None, task, plan, registry, current_inventory(ROOT), environment, {}, self.measured.compiler,
        )
        with tempfile.TemporaryDirectory(prefix="micros-copy-journal-model-") as temporary:
            directory = Path(temporary)
            with (
                # Models cannot borrow the real B14 author scope that is testing them.
                patch.object(authority, "_provisional", None),
                patch.object(authority, "journal_directory", return_value=directory),
                patch.object(authority, "recheck_context"),
                patch.object(driver, "recheck_context"),
            ):
                yield authority, driver, context, directory

    def test_real_red_captures_round_trip_without_retiring_the_identity(self):
        from tests.host.evidence_authority_types import TransitionState

        with self.fixture() as (authority, driver, context, directory):
            first = authority.begin_attempt(directory)
            recorded = driver.append_phase(context, first, "red", self.replay, self.outcomes, {})
            self.assertEqual(recorded, authority.read_journal(context))
            result = authority.validate_journal(context, recorded)
            self.assertIs(TransitionState.CORRECTION_ACTIVE, result.state)
            self.assertIn(authority.MEMORY_IDENTITY, result.active_identity_keys)
            restarted = authority.begin_attempt(directory)
            self.assertNotEqual(first.attempt, restarted.attempt)
            self.assertEqual(0, authority.read_current(directory)["count"])

    def test_false_or_incidental_red_does_not_become_expected_divergence(self):
        from tests.host import runtime_memory_evidence as evidence

        with patch.object(evidence, "assert_aligned_work"), self.assertRaises(ValueError):
            evidence.run_oracles(self.measured)
        with patch.object(evidence, "assert_aligned_work", side_effect=ValueError("broken measurement")), self.assertRaises(ValueError):
            evidence.run_oracles(self.measured)

    def test_resealed_wrong_command_tree_missing_result_and_log_claim_are_rejected(self):
        from tests.host.evidence_authority_types import seal_event
        from tools.stack_bound_manifest import encoded

        with self.fixture() as (authority, driver, context, directory):
            first = authority.begin_attempt(directory)
            recorded = driver.append_phase(context, first, "red", self.replay, self.outcomes, {})
            path = recorded.directory / "red.json"
            original = json.loads(path.read_bytes())
            native = json.loads((recorded.directory / "red-native.log").read_bytes())
            selector = authority.read_current(directory)
            for mutation in ("command", "tree", "missing-result", "invented-red"):
                altered = copy.deepcopy(original)
                log = copy.deepcopy(native)
                if mutation == "command":
                    altered["event"]["command"].append("--borrowed")
                elif mutation == "tree":
                    altered["event"]["candidate_tree"] = "f" * 40
                elif mutation == "missing-result":
                    altered["event"]["oracle_results"].pop()
                else:
                    lines = [json.loads(line) for line in log["output"].splitlines()]
                    row, = (
                        item for item in lines if item["operation"] == "copy" and item["length"] == 64
                        and item["destination_offset"] == item["source_offset"] == 0
                    )
                    row["capture"]["snapshot"]["counts"][Owner.RUNTIME][Kind.COPY_UNIT] = 8
                    log["output"] = "\n".join(json.dumps(item) for item in lines) + "\n"
                raw_log = encoded(log)
                (recorded.directory / "red-native.log").write_bytes(raw_log)
                altered["logs"]["native"] = {"size": len(raw_log), "sha256": hashlib.sha256(raw_log).hexdigest()}
                altered["event"] = asdict(seal_event(authority.decode_event(altered["event"])))
                data = encoded(altered)
                path.write_bytes(data)
                (directory / "current.json").write_bytes(encoded({
                    **selector, "sha256": hashlib.sha256(data).hexdigest(),
                }))
                with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                    authority.read_journal(context)

    def test_post_publication_recheck_failure_does_not_select_the_new_event(self):
        with self.fixture() as (authority, driver, context, directory):
            first = authority.begin_attempt(directory)

            def changed(_):
                if authority.read_current(directory)["count"] == 1:
                    raise ValueError("source changed after selector publication")

            with patch.object(driver, "recheck_context", side_effect=changed), self.assertRaisesRegex(
                ValueError, "after selector publication",
            ):
                driver.append_phase(context, first, "red", self.replay, self.outcomes, {})
            self.assertEqual(0, authority.read_current(directory)["count"])
            self.assertEqual((), authority.read_journal(context).records)

    def test_publication_rejects_equivalent_record_bytes_before_and_after_selection(self):
        from tests.host import runtime_memory_evidence as evidence

        measured = evidence.measure_native(ROOT, Stage.GREEN)
        outcomes, output = evidence.run_oracles(measured)
        green = evidence.native_log(measured, outcomes, output)
        for phase in ("red", "green"):
            for boundary in ("before", "after"):
                with self.subTest(phase=phase, boundary=boundary), self.fixture() as (
                    authority, driver, context, directory,
                ):
                    journal = authority.begin_attempt(directory)
                    if phase == "green":
                        journal = driver.append_phase(context, journal, "red", self.replay, self.outcomes, {})
                    count = len(journal.records)
                    publish = driver.publish_json
                    changed = []

                    def rewrite():
                        if not changed:
                            path = journal.directory / "red.json"
                            path.write_bytes(path.read_bytes() + b" \n")
                            changed.append(True)

                    def intercept(path, payload, *, validate=None):
                        if path.name != "current.json":
                            return publish(path, payload, validate=validate)
                        if boundary == "before":
                            rewrite()

                        def guard():
                            if boundary == "after" and authority.read_current(directory)["count"] == count + 1:
                                rewrite()
                            validate()

                        return publish(path, payload, validate=guard)

                    with patch.object(driver, "publish_json", side_effect=intercept), self.assertRaises(ValueError):
                        driver.append_phase(
                            context, journal, phase, self.replay if phase == "red" else green,
                            self.outcomes if phase == "red" else outcomes, {},
                        )
                    self.assertTrue(changed)
                    self.assertEqual(count, authority.read_current(directory)["count"])

    def test_provisional_scope_clears_on_failure_and_preserves_every_other_stable_artifact(self):
        from tests.host import runtime_memory_authority as authority
        from tests.host import runtime_memory_evidence as evidence
        from tests.host import test_trap_syscall_source_layout as layout

        with self.fixture() as (_, _, context, directory):
            journal = authority.Journal(
                directory, "a" * 32, ({}, {"event": SimpleNamespace(oracle_results=self.outcomes)}), "",
            )
            pid = os.getpid()
            with (
                patch.object(authority, "validate_journal"),
                patch.object(evidence, "validate_native_log", return_value=self.outcomes),
                patch.object(authority, "committed_claim", return_value=None),
            ):
                with self.assertRaisesRegex(RuntimeError, "unit failure"):
                    with authority.provisional_retirement(context, journal, {}):
                        self.assertFalse(authority.memory_identity_required(ROOT))
                        with patch.object(authority.os, "getpid", return_value=pid + 1):
                            self.assertTrue(authority.memory_identity_required(ROOT))
                        case = layout.TrapSyscallSourceLayoutTest()
                        read = case.required_text
                        with patch.object(case, "required_text", side_effect=lambda path: (
                            read(path) + "\n" if path == "lib/runtime/memory.h" else read(path)
                        )), self.assertRaises(AssertionError):
                            case.test_reviewed_eleven_headers_and_twelve_cohesive_artifacts_are_byte_exact()
                        raise RuntimeError("unit failure")
                self.assertTrue(authority.memory_identity_required(ROOT))

    def test_journal_models_do_not_borrow_or_clear_a_surrounding_author_scope(self):
        from tests.host import runtime_memory_authority as authority

        surrounding = (os.getpid(), SimpleNamespace(root=ROOT))
        with (
            patch.object(authority, "_provisional", surrounding),
            patch.object(authority, "committed_claim", return_value=None),
        ):
            with self.fixture():
                self.assertTrue(authority.memory_identity_required(ROOT))
            self.assertIs(surrounding, authority._provisional)


class RuntimeMemoryDriverTest(unittest.TestCase):
    def test_cli_rejects_incomplete_ambiguous_and_caller_supplied_authority(self):
        from tools.check_runtime_memory import parse_arguments

        for arguments in (
            (), ("--red", "--green"), ("--success",),
            ("--red", "--source", "other.c"), ("--recertify", "--command", "true"),
        ):
            with self.subTest(arguments=arguments), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                parse_arguments(arguments)

    def test_exact_invocation_accepts_only_the_validated_runtime_launcher_handoff(self):
        import sys
        from tests.host.runtime_memory_authority import runtime_image
        from tools import check_runtime_memory as driver

        metadata = driver.require_current_runtime()
        selected = os.environ["MICROS_PYTHON"]
        command = (selected, "-E", "-S", "-B", "tools/check_runtime_memory.py", "--red")
        for executable in (selected, runtime_image(metadata)["path"]):
            with patch.object(sys, "orig_argv", (executable, *command[1:])):
                driver.require_invocation("red", metadata)
        for altered in (
            ("/tmp/foreign-python", *command[1:]),
            (selected, "-E", "-B", *command[4:]),
            (*command[:-1], "--green"),
        ):
            with patch.object(sys, "orig_argv", altered), self.assertRaises(ValueError):
                driver.require_invocation("red", metadata)

    def test_writer_lock_excludes_concurrent_attempts_and_linked_storage(self):
        from tests.host.runtime_memory_authority import author_lock

        with tempfile.TemporaryDirectory(prefix="micros-copy-lock-") as temporary:
            root = Path(temporary)
            with author_lock(root):
                with self.assertRaises(BlockingIOError), author_lock(root):
                    self.fail("concurrent COPY01 writer admitted")
            other = root / "linked"
            other.mkdir()
            (other / "build").symlink_to(root / "build", target_is_directory=True)
            with self.assertRaises(ValueError), author_lock(other):
                self.fail("linked COPY01 storage admitted")

    def test_stale_author_inputs_stop_before_native_execution(self):
        from tools import check_runtime_memory as driver

        with (
            patch.object(driver, "author_lock", return_value=nullcontext(ROOT)),
            patch.object(driver, "capture_author_context", return_value=object()),
            patch.object(driver, "committed_claim", return_value=None),
            patch.object(driver, "read_journal", side_effect=ValueError("changed Green source")),
            patch.object(driver, "measure_native") as native,
            self.assertRaisesRegex(ValueError, "changed Green source"),
        ):
            driver.execute(ROOT, "recertify")
        native.assert_not_called()

    def test_failed_real_unit_result_clears_scope_and_cannot_publish_terminal_success(self):
        from tests.host.runtime_memory_authority import task_definition
        from tools import check_runtime_memory as driver

        _, plan, _ = task_definition(ROOT)
        context = SimpleNamespace(plan=plan)
        journal = SimpleNamespace(attempt="a" * 32, records=({}, {}))
        active = []

        @contextmanager
        def scope(*_):
            active.append(True)
            try:
                yield
            finally:
                active.clear()

        with (
            patch.object(driver, "author_lock", return_value=nullcontext(ROOT)),
            patch.object(driver, "capture_author_context", return_value=context),
            patch.object(driver, "committed_claim", return_value=None),
            patch.object(driver, "read_journal", return_value=journal),
            patch.object(driver, "measure_native"),
            patch.object(driver, "assert_geometry_work"),
            patch.object(driver, "run_oracles", return_value=((), "unit control fixture")),
            patch.object(driver, "native_log", return_value={}),
            patch.object(driver, "recheck_context"),
            patch.object(driver, "provisional_retirement", side_effect=scope),
            patch.object(driver.subprocess, "run", return_value=SimpleNamespace(
                returncode=7, stdout="fixture: the required command failed\n",
            )) as unit,
            patch.object(driver, "append_phase") as publish,
            redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()),
            self.assertRaisesRegex(ValueError, "B05 recertification failed"),
        ):
            driver.execute(ROOT, "recertify")
        self.assertEqual([], active)
        self.assertEqual(1, unit.call_count)
        self.assertEqual(plan.unit_commands[0][1], unit.call_args.args[0])
        publish.assert_not_called()


if __name__ == "__main__":
    unittest.main()
