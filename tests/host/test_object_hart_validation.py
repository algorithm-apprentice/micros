import ast
from copy import deepcopy
import json
import os
from pathlib import Path
import pprint
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tests.host.object_hart_validation_evidence import (
    check_semantics, expected_cases, fixed_native_inputs, parse_native_output, producer_label,
)
from tests.host.object_hart_validation_source import (
    CURRENT_HELPERS, MUTABLE_FUNCTIONS, baseline_adapter_source, current_function_contracts, declared_header,
    expected_skeleton, extend_cmake, extend_legacy_adapter, native_recipe, observe_fixed_source,
    project_cmake, project_fixed_observations, project_legacy_adapter,
    validate_observation_contract, validate_retained_source,
)
from tests.host.work_count_types import Kind, Owner


ROOT = Path(__file__).resolve().parents[2]


class ObjectHartAuthorityTest(unittest.TestCase):
    def test_complete_plan_covers_the_existing_validator_neighbor_identity(self):
        from tests.host.object_hart_validation_authority import ORACLE_RULES
        from tests.host.object_hart_validation_source import SOURCE_PROJECTION

        key = "tests/host/test_kernel_object_source_layout.py:data-entry:STABLE_HASHES/kernel/scheduler_core_validate.c"
        keys = SOURCE_PROJECTION["retirement_keys"]
        self.assertIn(key, keys)
        owners = [
            row[0] for row in ORACLE_RULES
            if key in {keys[index] for index in row[5]}
        ]
        self.assertEqual(["obj01-direct-current-ownership"], owners)

    def test_selected_records_and_new_predicates_preserve_the_complete_predecessor(self):
        from tests.host.evidence_authority_inventory import current_inventory
        from tests.host.evidence_authority_types import fact_set_digest
        from tests.host.object_hart_validation_authority import ORACLE_RULES, _oracle_partition, predecessor_records
        from tests.host.object_hart_validation_source import NATIVE_FAMILY, SOURCE_PROJECTION, candidate_paths

        inventory = current_inventory(ROOT)
        prior = predecessor_records(ROOT, inventory)
        selected = tuple(sorted(
            (fact for fact in prior if fact.key in SOURCE_PROJECTION["retirement_keys"]), key=lambda fact: fact.key,
        ))
        self.assertEqual(SOURCE_PROJECTION["affected_facts_sha256"], fact_set_digest(selected))
        self.assertEqual(set(candidate_paths()), {path for fact in selected for path in (fact.owner_path, *fact.payload_paths)})
        self.assertEqual({row[0]: row[5] for row in ORACLE_RULES}, _oracle_partition(selected))
        self.assertEqual(set(range(len(selected))), {index for row in ORACLE_RULES for index in row[5]})
        native = tuple(fact for fact in inventory.facts if fact.family == NATIVE_FAMILY)
        self.assertEqual(468, len(native))
        self.assertEqual(5851, len(prior))

    def test_native_declaration_rejects_missing_duplicate_borrowed_and_unknown_policies(self):
        from tests.host.evidence_authority_source import literal_value, single_assignment
        from tests.host import object_hart_validation_source as source

        tree = ast.parse((ROOT / source.BINDINGS_PATH).read_text())
        reference = literal_value(single_assignment(tree, "BINDING_REFERENCE").value)
        rows = literal_value(single_assignment(tree, "NATIVE_SOURCE_BINDINGS").value)
        original_reader = source.regular_source
        with tempfile.TemporaryDirectory(prefix="micros-obj01-bindings-") as directory:
            fixture = Path(directory) / "bindings.py"

            def read_declared(root, path):
                return fixture if path == source.BINDINGS_PATH else original_reader(ROOT, path)

            def write(values, pins=reference):
                fixture.write_text(
                    "BINDING_REFERENCE = " + pprint.pformat(pins)
                    + "\nNATIVE_SOURCE_BINDINGS = " + pprint.pformat(values) + "\n"
                )

            with patch.object(source, "regular_source", side_effect=read_declared), patch.object(source, "native_assertion_records", return_value=rows):
                write(rows)
                self.assertEqual(468, len(source.native_binding_facts(ROOT, source.NATIVE_FAMILY, source.BINDINGS_PATH)))
                changed_subject = (rows[0][0], "kernel/memory/frame_allocator.c", *rows[0][2:])
                changed_enforcer = (*rows[0][:2], ("tests/host/object_hart_validation_evidence.py",), rows[0][3])
                changed_policy = (*rows[0][:3], {**rows[0][3], "comparison": "unknown"})
                for values in (
                    rows[:-1], (*rows, rows[0]), (changed_subject, *rows[1:]),
                    (changed_enforcer, *rows[1:]), (changed_policy, *rows[1:]),
                ):
                    write(values)
                    with self.assertRaises(ValueError):
                        source.native_binding_facts(ROOT, source.NATIVE_FAMILY, source.BINDINGS_PATH)
                write(rows, (("policy", "0" * 40, reference[0][2]), reference[1]))
                with self.assertRaises(ValueError):
                    source.native_binding_facts(ROOT, source.NATIVE_FAMILY, source.BINDINGS_PATH)

    def test_staged_declarations_alone_do_not_retire_source_authority(self):
        from tests.host import object_hart_validation_authority as authority
        from tests.host.object_hart_validation_source import SOURCE_PROJECTION

        with (
            patch.object(authority, "_provisional", None),
            patch.object(authority, "committed_claim", return_value=None),
            patch.object(authority, "read_selection", return_value=(None, "")),
        ):
            self.assertEqual(frozenset(), authority.retired_identity_keys(ROOT))
            self.assertTrue(authority.identity_required(ROOT, SOURCE_PROJECTION["retirement_keys"][0]))


class ObjectHartCurrentContractTest(unittest.TestCase):
    def test_selected_failure_guards_preserve_result_and_invariant_errors(self):
        from tests.host import object_hart_validation_source as source

        propagate = getattr(source, "object_failure_is_propagated", None)
        self.assertIsNotNone(propagate, "selected metadata failure guards need their typed current check")
        invariant = "return MICROS_KERNEL_OBJECT_ERROR_INVARIANT;"
        enum_guard = (
            "error = selected(objects, &thread);"
            "if (error != MICROS_KERNEL_OBJECT_OK || !thread->scheduler_assigned) {"
            + invariant + "}"
        )
        bool_guard = "if (other || !selected(thread) || (left && right)) {" + invariant + "}"
        for body, success in (
            (enum_guard, "MICROS_KERNEL_OBJECT_OK"),
            (enum_guard.replace(invariant, "return error;"), "MICROS_KERNEL_OBJECT_OK"),
            ("if (!selected(thread)) {" + invariant + "}", "true"),
            (bool_guard, "true"),
        ):
            with self.subTest(body=body):
                self.assertTrue(propagate(body, "selected", success))
        for body, success in (
            (enum_guard.replace(" || ", " && "), "MICROS_KERNEL_OBJECT_OK"),
            (enum_guard.replace(
                "error != MICROS_KERNEL_OBJECT_OK || !thread->scheduler_assigned",
                "!thread->scheduler_assigned || error != MICROS_KERNEL_OBJECT_OK",
            ), "MICROS_KERNEL_OBJECT_OK"),
            (enum_guard.replace("!=", "=="), "MICROS_KERNEL_OBJECT_OK"),
            (enum_guard.replace("if (", "error = MICROS_KERNEL_OBJECT_OK; if ("), "MICROS_KERNEL_OBJECT_OK"),
            (enum_guard.replace(invariant, "return MICROS_KERNEL_OBJECT_OK;"), "MICROS_KERNEL_OBJECT_OK"),
            (enum_guard.replace(invariant, "publish(thread); " + invariant), "MICROS_KERNEL_OBJECT_OK"),
            (bool_guard.replace(" || ", " && "), "true"),
            ("if ((!selected(thread) || other) && enabled) {" + invariant + "}", "true"),
            (bool_guard.replace(invariant, "return true;"), "true"),
            (bool_guard, "MICROS_SCHEDULER_OK"),
            (enum_guard, "true"),
        ):
            with self.subTest(body=body, success=success):
                self.assertFalse(propagate(body, "selected", success))

    def test_geometry_parameter_adaptation_is_bound_in_the_fixed_caller_primitive(self):
        from tests.host.c_source_records import function_records
        from tests.host.object_hart_validation_source import CURRENT_SIGNATURES, primitive_shape

        path = "kernel/objects/kernel_object_validation.c"
        name = "kernel_object_validation_thread_slots_match"
        original = function_records(fixed_native_inputs(ROOT)[path].decode())[name].body
        before = "kernel_object_validation_thread_context_matches(objects, thread, index)"
        after = "kernel_object_validation_thread_context_matches(thread, index, geometry)"
        self.assertIn("struct kernel_object_stack_geometry *geometry", CURRENT_SIGNATURES[name])
        self.assertEqual(1, original.count(before))
        declared_caller = original.replace(before, after, 1)
        self.assertEqual(
            primitive_shape(path, name, declared_caller),
            current_function_contracts(str(ROOT))[path + "/" + name][3],
        )

    def test_legacy_observations_retain_sites_across_the_declared_caller_adaptation(self):
        from tests.host.c_source_records import function_records
        from tests.host import object_hart_validation_source as source, selected_ownership_source as legacy
        from tests.host.work_count_types import Stage

        path = "kernel/objects/kernel_object_validation.c"
        name = "kernel_object_validation_thread_slots_match"
        original = fixed_native_inputs(ROOT)[path].decode()
        before = "kernel_object_validation_thread_context_matches(objects, thread, index)"
        after = "kernel_object_validation_thread_context_matches(thread, index, geometry)"
        self.assertEqual(1, original.count(before))
        declared = original.replace(before, after, 1)
        with (
            patch.object(source, "_current_authority", return_value=True),
            patch.object(source, "validate_retained_source"),
        ):
            sites = legacy._observation_bindings(ROOT, path, declared, Stage.GREEN, "aggregate")
            self.assertTrue(any(site[0] == name and site[7] == "loop-entry" for site, _, _ in sites))
            observed = source.legacy_observe_source(ROOT, "MEM03", path, declared, Stage.GREEN)
            for site, _, _ in sites:
                self.assertEqual(1, observed.count(legacy._observation_call(site)))
            body = function_records(declared)[name].body
            for changed in (
                original,
                declared.replace(body, body.replace("++index", "index += 2", 1), 1),
                declared.replace(
                    body, body.replace("objects->processes[thread->owner.slot].generation",
                                       "objects->processes[thread->owner.slot + 1].generation", 1), 1,
                ),
            ):
                self.assertNotEqual(declared, changed)
                with self.assertRaises(ValueError):
                    source.legacy_observe_source(ROOT, "MEM03", path, changed, Stage.GREEN)

    def test_independent_current_contracts_reject_the_fixed_predecessor(self):
        contracts = current_function_contracts(str(ROOT))
        expected = {
            path + "/" + name
            for declaration in (MUTABLE_FUNCTIONS, CURRENT_HELPERS)
            for path, names in declaration.items() for name in names
        }
        self.assertEqual(expected, set(contracts))
        fixed = fixed_native_inputs(ROOT)
        for path, raw in fixed.items():
            with self.subTest(path=path):
                validate_retained_source(ROOT, path, raw.decode(), "baseline")
                if path in MUTABLE_FUNCTIONS or path == "kernel/scheduler/scheduler_core_private.h":
                    with self.assertRaises(ValueError):
                        validate_retained_source(ROOT, path, raw.decode(), "current")
                else:
                    validate_retained_source(ROOT, path, raw.decode(), "current")
        for path in MUTABLE_FUNCTIONS:
            self.assertTrue(expected_skeleton(ROOT, path))
        header = declared_header(ROOT)
        validate_retained_source(ROOT, "kernel/scheduler/scheduler_core_private.h", header, "current")
        self.assertNotIn("scheduler_core_queue_enqueue_tail(", header)
        with self.assertRaises(ValueError):
            validate_retained_source(
                ROOT, "kernel/scheduler/scheduler_core_private.h",
                header.replace("uint8_t priority", "uint16_t priority", 1), "current",
            )

    def test_empty_work_and_typed_handle_literal_coverage_remain_closed(self):
        from tests.host.c_source_records import function_records

        fixed = fixed_native_inputs(ROOT)
        path = "kernel/objects/kernel_object_state.c"
        name = "kernel_object_thread_scheduler_metadata_is_zero"
        body = function_records(fixed[path].decode())[name].body
        validate_observation_contract(ROOT, path, name, body, "baseline")
        with self.assertRaises(ValueError):
            validate_observation_contract(
                ROOT, path, name, body + "\nfor (index = 0; index < 128; ++index) {}\n", "baseline",
            )
        path = "kernel/scheduler/scheduler_core_return_plan.c"
        name = "micros_hart_plan_user_return"
        body = function_records(fixed[path].decode())[name].body
        validate_observation_contract(ROOT, path, name, body, "baseline")
        changed = body.replace("objects->threads[selected_slot].generation", "objects->threads[selected_slot + 1].generation")
        self.assertNotEqual(body, changed)
        with self.assertRaises(ValueError):
            validate_observation_contract(ROOT, path, name, changed, "baseline")

    def test_exact_legacy_adapters_recover_fixed_policy_without_accepting_other_edits(self):
        for path in (
            "tests/host/selected_ownership_source.py",
            "tests/host/selected_release_handoff_source.py",
            "tests/host/test_kernel_object_source_layout.py",
            "tests/host/test_scheduler_source_layout.py",
            "tests/host/test_endpoint_source_layout.py",
            "tools/generate_kernel_mapping_fixture.py",
            "tools/generate_selected_release_handoff_fixture.py",
            "tools/stack_bound_fatal.py",
        ):
            with self.subTest(path=path):
                original = baseline_adapter_source(str(ROOT), path)
                expected = extend_legacy_adapter(ROOT, path, original)
                self.assertEqual(expected, (ROOT / path).read_text())
                self.assertEqual(original, project_legacy_adapter(ROOT, path, expected))
                with self.assertRaises(ValueError):
                    project_legacy_adapter(ROOT, path, expected + "\n# unrelated source mutation\n")

    def test_six_product_recipe_is_full_only_and_preserves_every_previous_byte(self):
        from tests.host.work_count_recipe import project_host_test_commands

        original = baseline_adapter_source(str(ROOT), "CMakeLists.txt")
        expected = extend_cmake(original)
        self.assertTrue(expected == (ROOT / "CMakeLists.txt").read_text(), "OBJ01 exact six-product recipe is absent or altered")
        self.assertEqual(original, project_cmake(expected))
        self.assertEqual(project_host_test_commands(original), project_cmake(project_host_test_commands(expected)))
        recipe = native_recipe()
        self.assertNotIn("test-unit-fast", recipe)
        self.assertEqual(6, recipe.count("    add_executable("))
        for before, after in (
            ("PROPERTIES LABELS slow", "PROPERTIES LABELS fast"),
            ("add_dependencies(test-unit ", "add_dependencies(test-unit-fast "),
            ("-fsanitize=address,undefined", "-fsanitize=address"),
            ("MICROS_OBJ01_NATIVE_STAGE=1", "MICROS_OBJ01_NATIVE_STAGE=0"),
        ):
            altered = original.replace(
                '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n',
                recipe.replace(before, after, 1)
                + '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n',
                1,
            )
            with self.subTest(mutation=after), self.assertRaises(ValueError):
                project_cmake(altered)


class ObjectHartNativeProtocolTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.semantic_rows = tuple({
            **row, "version": 1, "preserved": 1, "outputs_preserved": 1,
            "before_state": 1, "after_state": 1, "before_output": 2, "after_output": 2,
            "value": row["harts"] - 1 if row["variant"] == "last-hart"
            else int(row["variant"] == "assigned-current") if row["operation"] == "is-current"
            else (1 << 64) - 1,
        } for row in expected_cases("baseline"))
        row = cls.semantic_rows[0]
        snapshot = {
            "version": 1, "reserved0": 0, "producer": producer_label("baseline", "aggregate"),
            "case": 1, "owner_mask": 24, "reserved1": 0, "counts": [[0] * len(Kind) for _ in Owner],
        }
        case = "MICROS_OBJ01_CASE " + json.dumps(row) + "\n"
        cls.ordinary_output = case + "MICROS_OBJ01_NATIVE_PASS cases=1\n"
        cls.work_output = case + "MICROS_OBJ01_WORK " + json.dumps(snapshot) + "\nMICROS_OBJ01_NATIVE_PASS cases=1\n"

    def test_partial_or_misattributed_work_cannot_supply_a_complete_native_product(self):
        output = self.work_output
        parse_native_output(output, "aggregate")
        lines = output.splitlines(keepends=True)
        work_index = next(index for index, line in enumerate(lines) if line.startswith("MICROS_OBJ01_WORK "))
        original = json.loads(lines[work_index].removeprefix("MICROS_OBJ01_WORK "))
        mutations = (
            ("producer", 2), ("case", 2), ("reserved0", 1), ("reserved1", 1),
            ("owner_mask", 1 << Owner.KERNEL_OBJECTS), ("version", True),
            ("counts", original["counts"][:-1]),
        )
        for field, value in mutations:
            with self.subTest(field=field):
                mutated = deepcopy(original)
                mutated[field] = value
                changed = list(lines)
                changed[work_index] = "MICROS_OBJ01_WORK " + json.dumps(mutated) + "\n"
                with self.assertRaises(ValueError):
                    parse_native_output("".join(changed), "aggregate")
        for changed in (
            "".join(lines[:work_index] + lines[work_index + 1:]),
            "".join(lines[:work_index] + [lines[work_index]] + lines[work_index:]),
            output[:-1],
            self.ordinary_output,
        ):
            with self.assertRaises(ValueError):
                parse_native_output(changed, "aggregate")

    def test_baseline_projection_rejects_source_or_observation_relocation(self):
        path = "kernel/objects/kernel_object_hart.c"
        original = fixed_native_inputs(ROOT)[path].decode()
        observed = observe_fixed_source(ROOT, path, original, "aggregate")
        self.assertEqual(original, project_fixed_observations(ROOT, path, observed, "aggregate"))
        with self.assertRaises(ValueError):
            observe_fixed_source(ROOT, path, original.replace("++index", "index += 2", 1), "aggregate")
        first = observed.index("MICROS_TEST_WORK_HIT(")
        end = observed.index(";", first) + 1
        annotation = observed[first:end]
        for altered in (
            observed[:first] + observed[end:],
            observed[:first] + observed[end:] + "\n" + annotation,
            observed.replace("MICROS_WORK_KIND_INDEX_PROBE", "MICROS_WORK_KIND_RECORD_VISIT", 1),
        ):
            with self.assertRaises(ValueError):
                project_fixed_observations(ROOT, path, altered, "aggregate")

    def test_semantic_corpus_rejects_missing_cases_and_changed_failure_state(self):
        rows = self.semantic_rows
        check_semantics(rows, "baseline")
        with self.assertRaises(ValueError):
            check_semantics(rows[:-1], "baseline")
        for field, value in (("after_state", 0), ("outputs_preserved", 0), ("result", 0)):
            changed = deepcopy(rows)
            index = next(index for index, row in enumerate(changed) if row["result"] == 8)
            changed[index][field] = value
            with self.assertRaises(ValueError):
                check_semantics(changed, "baseline")

    def test_direct_oracle_requires_the_unsupported_current_red_witness(self):
        from tests.host.object_hart_validation_evidence import (
            SCHEDULER_OPERATIONS, WORK_ORACLES, _require_discriminating_red,
        )

        rows = expected_cases("baseline")
        failures = {name: [] for name in WORK_ORACLES}

        def add(group, metric, predicate):
            row = next(row for row in rows if predicate(row))
            failures[group].append((row["case"], metric, 1, 0))

        for operation in SCHEDULER_OPERATIONS:
            add(WORK_ORACLES[0], "complete-validators", lambda row: row["operation"] == operation)
        add(WORK_ORACLES[0], "selected-indices", lambda row: row["operation"] == "ipc-batch" and row["requests"] == 128)
        add(WORK_ORACLES[1], "geometry-comparisons",
            lambda row: row["operation"] == "validate" and row["variant"] == "regular" and row["threads"] == 128 and row["harts"] == 8)
        for operation in ("hart-context", "is-current", "capture"):
            add(WORK_ORACLES[2], "direct-records", lambda row: row["operation"] == operation and row["harts"] == 8)
        products = {"ordinary": {"records": rows}}
        with self.assertRaises(ValueError):
            _require_discriminating_red(products, failures)
        add(WORK_ORACLES[2], "unsupported-current-diagnostic",
            lambda row: row["operation"] == "validate" and row["variant"] == "unscheduled-current")
        _require_discriminating_red(products, failures)


class ObjectHartRetainedWorkTest(unittest.TestCase):
    def test_retained_suite_covers_complete_affected_modules_without_duplicates(self):
        from tools import check_object_hart_validation as driver
        from tools.run_host_tests import select_suite, test_cases

        select = getattr(driver, "retained_host_suite", None)
        self.assertIsNotNone(select, "B14 needs its complete explicit affected-source selection")
        actual = [case.id() for case in test_cases(select(ROOT))]
        self.assertEqual(len(actual), len(set(actual)))
        required = {
            case.id() for case in test_cases(select_suite(ROOT, names=(
                "test_kernel_object_source_layout", "test_panic_trace", "test_stack_source_projection",
            )))
        }
        self.assertTrue(required <= set(actual), sorted(required - set(actual)))

    def test_terminal_replay_uses_bound_inputs_without_authorizing_its_opening_view(self):
        from types import SimpleNamespace

        from tests.host import evidence_authority_scope as scope
        from tests.host import object_hart_validation_evidence as evidence, object_hart_validation_source as source
        from tools import generate_object_hart_validation_fixture as generator
        from tools.stack_artifact_files import read_regular

        replay_sources = getattr(evidence, "_replay_sources", None)
        self.assertIsNotNone(replay_sources, "terminal replay needs independently bound raw inputs")
        path = evidence.NATIVE_SOURCES[0]
        audit_paths = {*source.AUDIT_SOURCES, "kernel/scheduler/scheduler_core_validate.c"}
        paths = tuple(sorted({path, *audit_paths}))
        sources = {name: read_regular(ROOT / name)[0] for name in paths}
        identities = {name: read_regular(ROOT / name)[1] for name in paths}
        context = SimpleNamespace(root=ROOT, files=identities)
        snapshot = SimpleNamespace(signature="fixed", files=sources)

        def observe(root, name, text, channel, stage):
            return "/* protocol observation */\n" + text

        class AfterAudits(Exception):
            pass

        def verify(root):
            bound = replay_sources(context, paths)
            inputs, outputs = generator.source_bundle(
                root, "current", "aggregate", current_sources={path: bound[path]},
            )
            self.assertEqual({path: identities[path]}, inputs)
            self.assertEqual(observe(root, path, sources[path].decode(), "aggregate", "current").encode(), outputs[path])
            audits = source.measure_audit_boundaries(
                root, "current", current_sources={name: bound[name] for name in audit_paths},
            )
            products = {channel: {"records": [], "work": []} for channel in ("ordinary", "aggregate", "stack")}
            attempt = "a" * 32
            replay = {
                "version": 1, "phase": "green", "attempt": attempt,
                "native": {"baseline": products, "current": products},
                "audits": audits, "results": [], "oracle_output": "protocol model",
            }
            with (
                patch.object(evidence, "fixed_native_inputs", return_value={path: sources[path]}),
                patch.object(evidence, "_validate_native_replay") as replay_native,
                patch.object(evidence, "work_failures", return_value={}),
                patch.object(evidence, "_require_discriminating_red"),
                patch.object(evidence, "compare_source_stages", side_effect=AfterAudits),
                self.assertRaises(AfterAudits),
            ):
                evidence.validate_execution_log(
                    context, replay, phase="green", attempt=attempt, reference={"baseline": products},
                )
            self.assertEqual({path: sources[path]}, replay_native.call_args_list[1].kwargs["current_sources"])
            return scope.VerifiedAuthority(frozenset({"fixture:retired"}), ())

        with (
            patch.object(scope, "_active", None),
            patch.object(scope, "_capture_snapshot", return_value=snapshot),
            patch.object(scope, "_verify_authorities", side_effect=verify) as prove,
            patch.object(generator, "fixed_native_inputs", return_value={path: sources[path]}),
            patch.object(generator, "validate_retained_source"),
            patch.object(generator, "observe_source", side_effect=observe),
            patch.object(source, "observe_source", side_effect=observe),
        ):
            with scope.checked_source_view(ROOT):
                self.assertFalse(scope.identity_required(ROOT, "fixture:retired"))
                inputs, _ = generator.source_bundle(ROOT, "current", "aggregate")
                self.assertEqual({path: identities[path]}, inputs)
                prove.assert_called_once_with(ROOT)
            with self.assertRaises(ValueError):
                generator.source_bundle(ROOT, "current", "aggregate", current_sources={})
        stale = SimpleNamespace(
            root=ROOT, files={**identities, path: {**identities[path], "sha256": "0" * 64}},
        )
        with self.assertRaises(ValueError):
            replay_sources(stale, paths)

    def test_retained_native_proof_is_bounded_closed_and_fail_closed(self):
        from types import SimpleNamespace
        from unittest.mock import Mock

        from tests.host import evidence_authority_scope as scope
        from tools import check_object_hart_validation as driver

        for failure in ("none", "native", "proof"):
            with self.subTest(failure=failure):
                close_check = Mock(side_effect=ValueError("changed proof") if failure == "proof" else None)
                proof = scope.VerifiedAuthority(frozenset({"fixture:retired"}), (close_check,))
                snapshot = SimpleNamespace(signature="fixed", files={"fixture.c": b"int value;\n"})

                def native_work(root):
                    for _ in range(3):
                        self.assertFalse(scope.identity_required(root, "fixture:retired"))
                        self.assertEqual(b"int value;\n", scope.source_bytes(root, root / "fixture.c"))
                    if failure == "native":
                        raise ValueError("failed native work")
                    return "retained native PASS"

                def command(arguments, **kwargs):
                    self.assertTrue(scope.identity_required(ROOT, "fixture:retired"))
                    return subprocess.CompletedProcess(arguments, 1, stdout="bounded command stop")

                with (
                    patch.object(scope, "_active", None),
                    patch.object(scope, "_capture_snapshot", return_value=snapshot),
                    patch.object(scope, "_verify_authorities", return_value=proof) as verify,
                    patch.object(scope, "_cold_identity_required", return_value=True),
                    patch.object(driver, "retained_memory_work", side_effect=native_work),
                    patch.object(driver.subprocess, "run", side_effect=command) as execute,
                ):
                    if failure == "none":
                        status, output = driver.retained_checks(ROOT)
                        self.assertEqual(1, status)
                        self.assertIn("retained native PASS", output)
                        close_check.assert_called_once_with()
                        execute.assert_called_once()
                    else:
                        reason = "failed native work" if failure == "native" else "changed proof"
                        with self.assertRaisesRegex(ValueError, reason):
                            driver.retained_checks(ROOT)
                        execute.assert_not_called()
                    verify.assert_called_once_with(ROOT)
                    self.assertTrue(scope.identity_required(ROOT, "fixture:retired"))


class ObjectHartLauncherTest(unittest.TestCase):
    def test_actual_launcher_closes_python_prefix_root_and_preexec_environment(self):
        from tests.host.c_source_records import function_records
        from tests.host.runtime_memory_evidence import native_compiler

        record = function_records((ROOT / "tests/host/object_hart_validation_test.c").read_text())["launch_native_products"]
        compiler = native_compiler(ROOT)
        with tempfile.TemporaryDirectory(prefix="micros-obj01-launcher-") as directory:
            directory = Path(directory).resolve()
            child, launcher = directory / "child", directory / "launcher"
            child_source = directory / "child.c"
            child_source.write_text(
                "#include <stdio.h>\n#include <unistd.h>\n"
                "int main(int argc, char **argv) {\n"
                "    char path[4096]; if (getcwd(path, sizeof(path)) == NULL) return 3;\n"
                "    puts(path); for (int index = 1; index < argc; ++index) puts(argv[index]); return 0;\n}\n"
            )
            source = directory / "launcher.c"
            source.write_text(
                "#include <stdio.h>\n#include <stdlib.h>\n#include <unistd.h>\n"
                "#define MICROS_OBJECT_HART_VALIDATION_PYTHON " + json.dumps(str(child)) + "\n"
                "#define MICROS_OBJECT_HART_VALIDATION_DRIVER " + json.dumps(str(ROOT / "tools/check_object_hart_validation.py")) + "\n"
                "#define MICROS_OBJECT_HART_VALIDATION_ROOT " + json.dumps(str(ROOT)) + "\n"
                + record.signature + "{" + record.body + "}\n"
                "int main(int argc, char **argv) { return argc == 6 ? launch_native_products(argc, argv) : 2; }\n"
            )
            for input_path, output_path in ((child_source, child), (source, launcher)):
                completed = subprocess.run(
                    (compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", str(input_path), "-o", str(output_path)),
                    capture_output=True, text=True, timeout=30,
                )
                self.assertEqual((0, ""), (completed.returncode, completed.stderr), completed.stdout)
            products = ("baseline-ordinary", "baseline-aggregate", "baseline-stack", "current-aggregate", "current-stack")
            environment = dict(os.environ)
            environment.pop("__PYVENV_LAUNCHER__", None)
            completed = subprocess.run(
                (str(launcher), *products), cwd=directory, env=environment,
                capture_output=True, text=True, timeout=30,
            )
            expected = [str(ROOT), "-E", "-S", "-B", str(ROOT / "tools/check_object_hart_validation.py"), "--native"]
            for product in (*products, str(launcher)):
                expected.extend(("--executable", product))
            self.assertEqual((0, ""), (completed.returncode, completed.stderr))
            self.assertEqual(expected, completed.stdout.splitlines())
            completed = subprocess.run(
                (str(launcher), *products), cwd=directory, env={**environment, "__PYVENV_LAUNCHER__": "unsupported"},
                capture_output=True, text=True, timeout=30,
            )
            self.assertEqual((2, ""), (completed.returncode, completed.stdout))
            self.assertEqual("__PYVENV_LAUNCHER__ is unsupported\n", completed.stderr)


if __name__ == "__main__":
    unittest.main()
