"""MEM03 independent selected-work and complete-diagnostic evidence."""

import ast
import copy
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from types import ModuleType, SimpleNamespace
import unittest
from unittest import mock

from tests.host.evidence_authority_inventory import current_inventory
from tests.host.evidence_authority_source import literal_value, single_assignment
from tests.host.evidence_authority_types import fact_set_digest
from tests.host import selected_ownership_source as source
from tests.host.work_count_types import Stage


ROOT = Path(__file__).resolve().parents[2]


class SelectedOwnershipAuthorityTest(unittest.TestCase):
    def test_source_view_dispatches_selected_keys_to_the_actual_cold_provider(self):
        from tests.host import evidence_authority_scope, selected_ownership_authority

        key = source.SOURCE_PROJECTION["retirement_keys"][0]
        with (
            mock.patch.object(evidence_authority_scope, "_active", None),
            mock.patch.object(selected_ownership_authority, "_provisional", None),
            mock.patch.object(selected_ownership_authority, "identity_required", return_value=False) as cold,
        ):
            self.assertFalse(evidence_authority_scope.identity_required(ROOT, key))
        cold.assert_called_once_with(ROOT, key)

    def test_terminal_query_does_not_repeat_verified_journal_replay(self):
        from contextlib import ExitStack
        from tests.host import selected_ownership_authority as authority
        from tests.host.evidence_authority_types import Environment

        environment = Environment((), (), (), (), "a" * 40, "b" * 40)
        context = SimpleNamespace(root=ROOT, environment=environment)
        selector = {"attempt": "0" * 32, "count": 3, "sha256": "c" * 64}
        records = [
            ({"environment": environment}, {"sha256": value * 64})
            for value in ("a", "b", "c")
        ]
        with tempfile.TemporaryDirectory(prefix="micros-mem03-replay-") as directory, ExitStack() as patches:
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
            self.assertEqual(1, verify.call_count)

    def test_uncommitted_and_foreign_process_scopes_cannot_retire_assertions(self):
        from tests.host import selected_ownership_authority as authority

        with (
            mock.patch.object(authority, "committed_claim", return_value=None),
            mock.patch.object(authority, "read_selection", return_value=(None, "")),
            mock.patch.object(authority, "_provisional", None),
        ):
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

    def test_exact_plan_closes_40_assertions_without_immutable_overlap(self):
        from tests.host import selected_ownership_authority as authority

        task, authorities, plan, registry = authority.task_definition(ROOT)
        inventory = current_inventory(ROOT)
        facts = {fact.key: fact for fact in inventory.facts}
        candidates = set()
        for key in plan.affected_identity_keys:
            candidates.update((facts[key].owner_path, *facts[key].payload_paths))
        for _, paths in task.unit_input_paths:
            candidates.update(paths)
        immutable = {*task.command_input_paths, *plan.declared_external_inputs}
        for oracle in plan.replacement_oracles:
            immutable.update(oracle.input_paths)
        self.assertEqual(set(source.SOURCE_ROLES["candidate_paths"]), candidates)
        self.assertEqual(set(), candidates & immutable)
        self.assertEqual(40, len(plan.planned_retirement_identity_keys))
        self.assertEqual((25, 5, 10), tuple(len(oracle.replaces_identity_keys) for oracle in plan.replacement_oracles))
        self.assertEqual(1476, len(authority.base_membership(ROOT)))
        self.assertEqual(5, len(registry.claims))
        self.assertEqual(5, len(authorities))

    def test_eight_independent_facts_preserve_every_predecessor(self):
        declaration = source.read_declarations(ROOT)
        direct = tuple(
            fact for owner in (source.AUTHORITY_PATH, source.SOURCE_PATH)
            for fact in source.declaration_facts(ROOT, owner)
        )
        self.assertEqual(8, len(direct))
        self.assertEqual(
            "473d2640bd3d9a756183d51b2d6d6ad31aa890f9f8efeebdf1d76d42d514ed2c",
            fact_set_digest(direct),
        )
        fixed = subprocess.check_output(
            (
                "git", "show",
                declaration["DESIGN_PINS"]["runtime_reference"]
                + ":tests/host/evidence_authority_inventory.py",
            ),
            cwd=ROOT, text=True, timeout=30,
        )
        previous = literal_value(single_assignment(
            ast.parse(fixed), "DECLARED_FAMILY_FACT_DIGESTS",
        ).value)
        inventory = current_inventory(ROOT)
        from tests.host.evidence_authority_inventory import historical_object_binding_facts

        historical = historical_object_binding_facts(ROOT, inventory.facts)
        for family, fingerprint in previous.items():
            with self.subTest(family=family):
                self.assertEqual(
                    fingerprint, fact_set_digest(fact for fact in historical if fact.family == family),
                )
        observed = tuple(fact for fact in inventory.facts if fact.family == source.FAMILY)
        self.assertEqual(8, len(observed), "MEM03 independent evidence family is not registered")
        self.assertEqual(direct, observed)


class SelectedOwnershipNativeTest(unittest.TestCase):
    def test_launcher_rejects_consumed_environment_before_interpreter_exec(self):
        from tests.host.c_source_records import function_records
        from tests.host.runtime_memory_evidence import native_compiler

        text = (ROOT / "tests/host/selected_ownership_test.c").read_text()
        launcher = function_records(text)["launch_native_products"]
        compiler = native_compiler(ROOT)
        with tempfile.TemporaryDirectory(prefix="micros-mem03-launcher-") as temporary:
            directory = Path(temporary)
            executable = directory / "launcher"
            probe = directory / "launcher.c"
            probe.write_text(
                "#include <stddef.h>\n#include <stdio.h>\n"
                "#include <stdlib.h>\n#include <unistd.h>\n"
                + launcher.signature + "{" + launcher.body + "}\n"
                + "int main(int argc, char **argv)\n{\n"
                "    if (argc == 6) {\n"
                "        return launch_native_products(argc, argv);\n"
                "    }\n"
                '    puts("interpreter execution reached");\n'
                "    return 37;\n}\n"
            )
            built = subprocess.run(
                (
                    compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                    "-DMICROS_SELECTED_OWNERSHIP_PYTHON=" + json.dumps(str(executable)),
                    '-DMICROS_SELECTED_OWNERSHIP_DRIVER="unused.py"',
                    "-DMICROS_SELECTED_OWNERSHIP_ROOT=" + json.dumps(str(directory)),
                    str(probe), "-o", str(executable),
                ),
                cwd=ROOT, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(0, built.returncode, built.stdout + built.stderr)
            for value in (None, "", str(executable)):
                environment = dict(os.environ)
                environment.pop("__PYVENV_LAUNCHER__", None)
                if value is not None:
                    environment["__PYVENV_LAUNCHER__"] = value
                with self.subTest(launcher_environment=value):
                    result = subprocess.run(
                        (str(executable),) * 6, env=environment,
                        stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=10,
                    )
                    if value:
                        self.assertEqual(2, result.returncode)
                        self.assertEqual("", result.stdout)
                        self.assertIn("__PYVENV_LAUNCHER__ is unsupported", result.stderr)
                    else:
                        self.assertEqual(37, result.returncode)
                        self.assertEqual("interpreter execution reached\n", result.stdout)
                        self.assertEqual("", result.stderr)

    def test_current_only_guards_compile_against_independently_declared_interfaces(self):
        from tests.host.selected_ownership_evidence import check_current_fixture_syntax

        check_current_fixture_syntax(ROOT)

    def test_fixed_source_executes_all_slices_and_ordinary_control(self):
        from tests.host.evidence_authority_types import ObservedResult
        from tests.host.selected_ownership_evidence import (
            execution_log, measure_complete_helpers, measure_native,
            run_oracles, validate_execution_log, work_failures,
        )

        measured = measure_native(ROOT, Stage.RED)
        self.assertEqual(6, len(measured))
        self.assertEqual(495, sum(len(item["observations"]) for item in measured.values()))
        self.assertEqual((), measured["ordinary"]["observation_symbols"])
        for item in measured.values():
            self.assertEqual(99, len(item["cases"]))
            self.assertEqual((0, 0), (item["build_exit"], item["execution_exit"]))
        failures = work_failures(measured)
        self.assertEqual(
            {"mem03-selected-record-work", "mem03-runtime-owner-work", "mem03-consumer-audit-boundaries"},
            set(failures),
        )
        self.assertTrue(all(failures.values()))
        audits = source.measure_audit_boundaries(ROOT, Stage.RED)
        helpers = measure_complete_helpers(ROOT, Stage.RED)
        results, output = run_oracles(ROOT, measured, audits, helpers)
        self.assertEqual((ObservedResult.EXPECTED_FAILURE,) * 3, tuple(result.observed for result in results))
        context = SimpleNamespace(
            root=ROOT, files=measured["ordinary"]["fixture_inputs"],
            native_compiler=measured["ordinary"]["compiler"], native_nm=measured["ordinary"]["nm"],
        )
        replay = execution_log(measured, audits, helpers, results, output, phase="red", attempt="a" * 32)
        self.assertEqual(
            results, validate_execution_log(context, replay, phase="red", attempt="a" * 32),
        )
        mutations = (
            lambda value: value["native"].pop("ordinary"),
            lambda value: value["native"]["ordinary"]["observation_symbols"].append("micros_test_work_record"),
            lambda value: value["native"]["aggregate"]["command"].remove("-DMICROS_SELECTED_OWNERSHIP_BASELINE=1"),
            lambda value: value["native"]["aggregate"]["source_inputs"].pop("kernel/memory/frame_ownership.c"),
            lambda value: value["native"]["aggregate"]["generated_sources"].pop("kernel/memory/frame_ownership.c"),
            lambda value: value["audits"]["complete"].pop(),
            lambda value: value.pop("helpers"),
            lambda value: value["helpers"]["digests"].update(validate_active_frames="0" * 64),
            lambda value: value["helpers"]["command"].remove("-P"),
            lambda value: value["helpers"]["source_inputs"].pop("kernel/memory/frame_ownership_validation.c"),
            lambda value: value["results"][0].update(observed=ObservedResult.PASS.value),
            lambda value: value.update(attempt="b" * 32),
        )
        for index, mutate in enumerate(mutations):
            changed = copy.deepcopy(replay)
            mutate(changed)
            with self.subTest(replay=index), self.assertRaises(ValueError):
                validate_execution_log(context, changed, phase="red", attempt="a" * 32)


class SelectedOwnershipObservationTest(unittest.TestCase):
    def test_complete_source_census_binds_guarded_and_ordinary_audits(self):
        original, retained, migrated = source.declared_audit_boundaries(str(ROOT))
        self.assertEqual((26, 8, 18), tuple(sum(row[4] for row in rows) for rows in (original, retained, migrated)))
        self.assertEqual(18, len({row[0] for row in original}))
        actual = source.measure_audit_boundaries(ROOT, Stage.RED)
        self.assertEqual(original, actual["complete"])
        self.assertEqual((), actual["migrated"])
        with self.assertRaises(ValueError):
            source._audit_calls(
                "void test(void) { audit = micros_frame_ownership_validate; audit(ownership); }",
                "kernel/memory/frame_ownership.c", source.COMPLETE_AUDITS,
            )

    def test_fixed_sites_recover_every_original_native_source_in_each_slice(self):
        declarations = source.original_observations(str(ROOT))
        self.assertEqual(173, sum(len(sites) for sites in declarations.values()))
        for path in source.RECIPE_ADMISSION["native_source_members"]:
            original = source.baseline_source(str(ROOT), path)
            for channel in source.STATE_WORK_CONTRACT["channels"]:
                with self.subTest(path=path, channel=channel):
                    observed = source.observe_source(ROOT, path, original, Stage.RED, channel)
                    self.assertEqual(
                        original, source.project_observations(ROOT, path, observed, Stage.RED, channel),
                    )

    def test_projection_rejects_missing_duplicate_moved_and_foreign_work(self):
        path = "kernel/memory/frame_ownership_validation.c"
        original = source.baseline_source(str(ROOT), path)
        channel = "active-frame"
        observed = source.observe_source(ROOT, path, original, Stage.RED, channel)
        first = observed.index("MICROS_TEST_WORK_HIT(")
        end = observed.index(";", first) + 1
        call = observed[first:end]
        without = observed[:first] + observed[end:]
        mutations = (
            without, observed[:first] + call + observed[first:],
            without + call, observed.replace(call, call.replace("RECORD_VISIT", "INDEX_PROBE"), 1),
            observed.replace("struct micros_frame_owner observed;", "struct micros_frame_owner observed = {0};", 1),
        )
        for mutation in mutations:
            with self.subTest(mutation=mutations.index(mutation)):
                with self.assertRaises(ValueError):
                    source.project_observations(ROOT, path, mutation, Stage.RED, channel)
        for invalid in ("ordinary", "active", "", None):
            with self.subTest(channel=invalid), self.assertRaises(ValueError):
                source.observe_source(ROOT, path, original, Stage.RED, invalid)
        with self.assertRaises(ValueError):
            source.project_observations(ROOT, path, observed, Stage.RED, "aggregate")

    def test_current_helper_inventory_is_independent_and_extra_work_is_not_hidden(self):
        declarations = source.current_observations(str(ROOT))
        self.assertEqual(179, sum(map(len, declarations.values())))
        path = "kernel/memory/frame_ownership_validation.c"
        original = source.baseline_source(str(ROOT), path)
        statement = "observed = ownership->owners[frame_index];"
        mutations = (
            original.replace(statement, statement + statement, 1),
            original.replace(statement, "for (size_t hidden = 0; hidden < 8; ++hidden) { (void)hidden; }" + statement, 1),
            original.replace(statement, "const struct micros_frame_owner *hidden = ownership->owners; (void)hidden;" + statement, 1),
        )
        for index, changed in enumerate(mutations):
            with self.subTest(mutation=index), self.assertRaises(ValueError):
                source._validate_observation_coverage(ROOT, path, changed, Stage.RED)
        unchanged = "kernel/memory/frame_ownership_vm_freeze.c"
        from tests.host import evidence_authority_scope
        import tests.host as package
        from tests.host.selected_address_space_authority import git

        reference = "abe05cdd7bced5e77e86c1ec12754d017f920cab"
        self.assertEqual(
            "2c34dac3c259a4975d1c213e3e14c18d8a075658",
            git(ROOT, "rev-parse", reference + "^{tree}").decode().strip(),
        )
        inputs = {}

        def fixed(path):
            if path not in inputs:
                inputs[path] = git(ROOT, "show", reference + ":" + path)
            return inputs[path]

        policies = {}
        for name in ("selected_ownership_source", "selected_address_space_source"):
            path = "tests/host/" + name + ".py"
            policy = ModuleType("historical_" + name)
            policy.__file__ = str(ROOT / path)
            exec(compile(fixed(path), policy.__file__, "exec"), policy.__dict__)
            policies[name] = policy
        claims = []
        retired = set()
        names = (
            "runtime_memory", "diagnostic_history", "bootstrap_allocation",
            "kernel_mapping", "selected_ownership", "selected_address_space",
        )
        for name in names:
            tree = ast.parse(fixed("tests/host/" + name + "_authority.py"))
            claim = literal_value(single_assignment(tree, "COMPLETED_CLAIM").value)
            claims.append(claim[1])
            if name == "runtime_memory":
                retired.add(claim[2])
            else:
                tree = ast.parse(fixed("tests/host/" + name + "_source.py"))
                projection = literal_value(single_assignment(tree, "SOURCE_PROJECTION").value)
                self.assertFalse(retired.intersection(projection["retirement_keys"]))
                retired.update(projection["retirement_keys"])
        self.assertEqual(["COPY01", "PLAT01", "MEM01", "MEM02", "MEM03", "MEM04"], claims)
        self.assertEqual(103, len(retired))
        historical = policies["selected_ownership_source"]
        next_policy = policies["selected_address_space_source"]
        text = fixed(unchanged).decode()

        def historical_identity(root, key):
            self.assertEqual(ROOT, Path(root).resolve())
            return key not in retired

        with (
            mock.patch.object(source, "observe_source", side_effect=AssertionError("historical freeze used live current policy")),
            mock.patch.object(source, "project_observations", side_effect=AssertionError("historical freeze used live current policy")),
            mock.patch.object(evidence_authority_scope, "_active", None),
            mock.patch.object(evidence_authority_scope, "identity_required", side_effect=historical_identity),
            mock.patch.object(evidence_authority_scope, "_providers", side_effect=AssertionError("historical freeze consulted current authority")),
            mock.patch.dict(sys.modules, {"tests.host.selected_address_space_source": next_policy}),
            mock.patch.object(package, "selected_address_space_source", next_policy),
        ):
            for channel in historical.STATE_WORK_CONTRACT["channels"]:
                observed = historical.observe_source(ROOT, unchanged, text, Stage.GREEN, channel)
                self.assertEqual(text, historical.project_observations(ROOT, unchanged, observed, Stage.GREEN, channel))
            observed = historical.observe_source(ROOT, unchanged, text, Stage.GREEN, "aggregate")
            first = observed.index("MICROS_TEST_WORK_HIT(")
            end = observed.index(";", first) + 1
            call = observed[first:end]
            without = observed[:first] + observed[end:]
            for changed in (
                without, observed[:first] + call + observed[first:], without + call,
                observed.replace(call, call.replace("MICROS_WORK_OWNER_KERNEL_MEMORY", "MICROS_WORK_OWNER_KERNEL_OBJECTS"), 1),
            ):
                with self.subTest(historical_mutation=changed), self.assertRaises(ValueError):
                    historical.project_observations(ROOT, unchanged, changed, Stage.GREEN, "aggregate")


class SelectedOwnershipIntegrationTest(unittest.TestCase):
    def test_native_orchestration_is_full_only_and_labeled_slow(self):
        recipe = source.native_recipe()
        dependencies = dict(re.findall(r"add_dependencies\((test-unit(?:-fast)?) ([^)]+)\)", recipe))
        self.assertEqual({"test-unit"}, set(dependencies))
        self.assertEqual(set(source.RECIPE_ADMISSION["native_targets"]), set(dependencies["test-unit"].split()))
        self.assertIn("set_tests_properties(selected-ownership PROPERTIES LABELS slow)", recipe)

    def test_existing_generators_track_the_new_source_authority_inputs(self):
        recipe = source.native_recipe()
        for owner in ("MEM01", "MEM02"):
            command = re.search(
                r"add_custom_command\(\s*OUTPUT\s+\$\{" + owner + r"_NATIVE_OUTPUTS\}\s+APPEND\s+"
                r"DEPENDS\s+([^)]*)\)",
                recipe,
            )
            self.assertIsNotNone(command, owner + " generation must not reuse stale admission code")
            self.assertEqual(
                {
                    "tests/host/selected_ownership_source.py",
                    "tests/host/selected_ownership_authority.py",
                    "tests/host/evidence_authority_scope.py",
                    "tools/validation_context.py",
                    "tools/validation_inputs.py",
                },
                set(command.group(1).split()),
            )

    def test_native_generators_serialize_their_shared_index_metadata_only(self):
        from tests.host.selected_address_space_source import project_cmake

        recipe = project_cmake((ROOT / "CMakeLists.txt").read_text())
        self.assertTrue(
            "set_property(GLOBAL APPEND PROPERTY JOB_POOLS mem03_source_proof=1)" in recipe,
            "memory fixture generation requires its depth-one source-proof pool",
        )
        for owner in ("MEM01", "MEM02", "MEM03"):
            command = re.search(
                r"add_custom_command\(\s*OUTPUT\s+\$\{" + owner + r"_NATIVE_OUTPUTS\}\s*\n"
                r"(.*?)\n    \)",
                recipe, re.DOTALL,
            )
            self.assertIsNotNone(command, owner)
            self.assertIn("JOB_POOL mem03_source_proof", command.group(1))
        self.assertEqual(3, recipe.count("JOB_POOL mem03_source_proof"))
        self.assertNotRegex(recipe, r"JOB_POOL_(?:COMPILE|LINK)\s+mem03_source_proof")

    def test_scaffold_restores_exact_legacy_adapters_and_the_complete_previous_recipe(self):
        from tests.host.common_scheduler_return_source import project_cmake as project_common
        from tests.host.selected_address_space_source import project_legacy_adapter as project_next

        source.validate_scaffold(ROOT)
        for path in source.SOURCE_ROLES["fixed_adapters"]:
            original = source.baseline_adapter_source(str(ROOT), path)
            actual = project_next(ROOT, path, (ROOT / path).read_text())
            self.assertEqual(source.extend_legacy_adapter(path, original), actual)
            self.assertEqual(original, source.project_legacy_adapter(path, actual))
            with self.subTest(path=path), self.assertRaises(ValueError):
                source.project_legacy_adapter(path, actual + "\nunowned_policy = True\n")
            self.assertEqual(
                source.baseline_source(str(ROOT), path),
                source._project_validation_prerequisite(path, original),
            )
        path = "tests/host/test_kernel_bootstrap_source_layout.py"
        original = source.baseline_source(str(ROOT), path)
        actual = (ROOT / path).read_text()
        self.assertEqual(source.extend_bootstrap_readonly_fixture(original), actual)
        self.assertEqual(original, source.project_legacy_adapter(path, actual))
        with self.assertRaises(ValueError):
            source.project_legacy_adapter(
                path, actual.replace('"micros_frame_ownership_runtime_validate_metadata"',
                                     '"micros_frame_ownership_runtime_validate_metadata", "micros_process_create"', 1),
            )
        original = subprocess.check_output(
            ("git", "show", source.SOURCE_PROJECTION["adapter_reference"] + ":CMakeLists.txt"),
            cwd=ROOT, text=True, timeout=30,
        )
        current = (ROOT / "CMakeLists.txt").read_text()
        from tests.host.qemu_transcript_source import project_transcript_cmake

        self.assertEqual(source.extend_cmake(original), project_transcript_cmake(project_common(current)))
        self.assertEqual(original, source.project_cmake(current))
        with self.assertRaises(ValueError):
            source.project_cmake(current.replace("MICROS_SELECTED_OWNERSHIP_LAUNCHER=1", "MICROS_SELECTED_OWNERSHIP_LAUNCHER=0", 1))

    def test_single_ctest_tracks_all_six_products_with_the_unchanged_manifest_policy(self):
        from tools.validation_build_manifest import native_outputs
        from tools.validation_inputs import UnsupportedContext

        recipe = source.native_recipe()
        self.assertEqual(
            ["selected-ownership"],
            re.findall(r"add_test\(\s*NAME\s+([A-Za-z0-9_-]+)", recipe),
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "build"
            directory.mkdir()
            (root / "CMakeLists.txt").write_text(recipe)
            products = tuple(directory / name for name in source.RECIPE_ADMISSION["native_targets"])
            for path in products:
                path.write_bytes(b"fixture-output")
            command = [str(products[-1]), *map(str, products[:-1])]
            slow = [{"name": "LABELS", "value": ["slow"]}]
            inventory = {"tests": [{"name": "selected-ownership", "command": command, "properties": slow}]}
            with mock.patch("tests.host.work_count_recipe.native_test_declarations", return_value=[]):
                self.assertEqual(set(products), set(native_outputs(root, directory, inventory)))
                inventory["tests"][0]["properties"] = []
                with self.assertRaisesRegex(UnsupportedContext, "slow CTest"):
                    native_outputs(root, directory, inventory)
                inventory["tests"][0]["properties"] = slow
                inventory["tests"][0]["command"] = [*command, "--native"]
                with self.assertRaises(UnsupportedContext):
                    native_outputs(root, directory, inventory)

    def test_native_adapters_do_not_admit_changes_while_exact_keys_remain_active(self):
        from tests.host import (
            bootstrap_allocation_source, evidence_authority_scope,
            kernel_mapping_source, selected_ownership_authority,
        )

        path = "kernel/memory/frame_ownership_validation.c"
        original = source.baseline_source(str(ROOT), path)
        changed = original.replace("observed = ownership->owners[frame_index];", "observed = ownership->owners[0];", 1)
        self.assertNotEqual(original, changed)
        with (
            mock.patch.object(evidence_authority_scope, "_active", None),
            mock.patch.object(evidence_authority_scope, "identity_required", return_value=True),
            mock.patch.object(evidence_authority_scope, "_providers", side_effect=AssertionError(
                "synthetic active-key model consulted live authority providers",
            )),
            mock.patch.object(selected_ownership_authority, "retired_identity_keys", return_value=frozenset()),
        ):
            bootstrap_allocation_source.validate_retained_source(ROOT, path, original, stage=Stage.GREEN)
            kernel_mapping_source.validate_retained_source(ROOT, path, original, Stage.GREEN)
            with self.assertRaises(ValueError):
                bootstrap_allocation_source.validate_retained_source(ROOT, path, changed, stage=Stage.GREEN)
            with self.assertRaises(ValueError):
                kernel_mapping_source.validate_retained_source(ROOT, path, changed, Stage.GREEN)
            self.assertEqual(
                frozenset(), source.native_assertion_admission(ROOT, "MEM02", path, changed, Stage.GREEN),
            )


class SelectedOwnershipTokenScopeTest(unittest.TestCase):
    def setUp(self):
        from tests.host import evidence_authority_scope

        patch = mock.patch.object(evidence_authority_scope, "_active", None)
        patch.start()
        self.addCleanup(patch.stop)

    @classmethod
    def setUpClass(cls):
        from tests.host import test_frame_validation_hot_path as guard
        from tools.generate_selected_ownership_fixture import prepare_sources

        fixed = source.baseline_source(str(ROOT), "kernel/memory/frame_ownership_validation.c")
        with tempfile.TemporaryDirectory(prefix="micros-mem03-helper-scope-") as temporary:
            prepare_sources(ROOT, Path(temporary), Stage.RED, "ordinary")
            with mock.patch.object(guard, "ROOT", Path(temporary)):
                cls.preprocessed = guard.clang_preprocessed(fixed)

    def test_original_prefix_values_and_independent_exact_helper_values_are_preserved(self):
        from tests.host import test_frame_validation_hot_path as guard

        for name, expected in (
            ("validate_active_frames", guard.ACTIVE_TOKENS_SHA256),
            ("inactive_state_is_zero", guard.INACTIVE_TOKENS_SHA256),
        ):
            with self.subTest(helper=name):
                self.assertEqual(expected, guard.function_token_digest(self.preprocessed, name))
                source.assert_complete_helper_tokens(self.preprocessed, name)

    def test_only_exact_retirement_permits_unrelated_prefix_changes(self):
        from tests.host import selected_ownership_authority as authority
        from tests.host import test_frame_validation_hot_path as guard

        changed = self.preprocessed.replace(
            "void micros_frame_ownership_clear_bytes(",
            "void prefix_only_probe(void);\nvoid micros_frame_ownership_clear_bytes(", 1,
        )
        self.assertNotEqual(changed, self.preprocessed)
        for name in ("validate_active_frames", "inactive_state_is_zero"):
            source.assert_complete_helper_tokens(changed, name)
        with mock.patch.object(authority, "identity_required", return_value=True), self.assertRaises(AssertionError):
            guard.assert_word_bounded_scans(changed)
        with mock.patch.object(authority, "identity_required", return_value=False):
            guard.assert_word_bounded_scans(changed)
        for active_key in source.SOURCE_PROJECTION["complete_prefix_keys"]:
            with (
                self.subTest(active=active_key),
                mock.patch.object(authority, "identity_required", side_effect=lambda root, key: key == active_key),
                self.assertRaises(AssertionError),
            ):
                guard.assert_word_bounded_scans(changed)
        changed = self.preprocessed.replace("*owner = observed;", "*owner = observed; (void)physical_address;", 1)
        self.assertNotEqual(changed, self.preprocessed)
        with mock.patch.object(authority, "identity_required", return_value=False):
            guard.assert_word_bounded_scans(changed)
        changed = self.preprocessed.replace(
            "static enum micros_frame_ownership_error validate_active_frames(",
            "static void prefix_only_helper(void) { }\n"
            "static enum micros_frame_ownership_error validate_active_frames(", 1,
        )
        self.assertNotEqual(changed, self.preprocessed)
        with mock.patch.object(authority, "identity_required", return_value=False):
            guard.assert_word_bounded_scans(changed)

    def test_exact_helper_checks_reject_missing_duplicate_signature_and_body_changes(self):
        from tests.host.c_source_records import function_records

        records = function_records(self.preprocessed)
        for name in ("validate_active_frames", "inactive_state_is_zero"):
            record = records[name]
            fragment = record.signature + "{" + record.body + "}"
            mutations = (
                self.preprocessed.replace(fragment, "", 1),
                self.preprocessed + "\n" + fragment,
                self.preprocessed.replace(fragment, fragment.replace("static ", "", 1), 1),
                self.preprocessed.replace(fragment, fragment.replace("word_index", "changed_word_index"), 1),
            )
            for index, changed in enumerate(mutations):
                with self.subTest(helper=name, mutation=index), self.assertRaises(AssertionError):
                    source.assert_complete_helper_tokens(changed, name)


class SelectedOwnershipProtocolTest(unittest.TestCase):
    def transcript(self, mutate=None):
        from tests.host.selected_ownership_evidence import expected_cases, expected_guards

        lines = []
        for case in expected_cases():
            lines.append("MICROS_MEM03_CASE " + json.dumps(case))
            observation = {
                "version": 1, "reserved0": 0, "reserved1": 0,
                "producer_label": source.STATE_WORK_CONTRACT["producer_base"] + 1,
                "capture_label": case["case"], "owner_mask": source.STATE_WORK_CONTRACT["owner_mask"],
                "counts": [[0] * 14 for _ in range(27)],
            }
            if mutate is not None and case["case"] == 1:
                mutate(observation)
            lines.append("MICROS_MEM03_WORK " + json.dumps(observation))
        lines.extend("MICROS_MEM03_GUARD " + guard + " PASS" for guard in expected_guards(Stage.RED))
        lines.append("MICROS_MEM03_SEMANTICS_PASS cases=99")
        return "\n".join(lines) + "\n"

    def test_snapshot_schema_is_complete_and_bound_to_external_stage_and_channel(self):
        from tests.host.selected_ownership_evidence import parse_native_output

        output = self.transcript()
        cases, work = parse_native_output(output, Stage.RED, "aggregate")
        self.assertEqual((99, 99), (len(cases), len(work)))
        for stage, channel in ((Stage.GREEN, "aggregate"), (Stage.RED, "active-frame"), (Stage.RED, "ordinary")):
            with self.subTest(stage=stage, channel=channel), self.assertRaises(ValueError):
                parse_native_output(output, stage, channel)
        mutations = (
            lambda value: value.pop("reserved0"),
            lambda value: value.update(reserved1=1),
            lambda value: value.update(capture_label=True),
            lambda value: value.update(owner_mask=1),
            lambda value: value["counts"].pop(),
            lambda value: value["counts"][2].pop(),
            lambda value: value["counts"][2].__setitem__(1, -1),
            lambda value: value["counts"][2].__setitem__(1, 1 << 64),
            lambda value: value["counts"][2].__setitem__(1, False),
            lambda value: value["counts"][0].__setitem__(1, 1),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(mutation=index), self.assertRaises(ValueError):
                parse_native_output(self.transcript(mutate), Stage.RED, "aggregate")

    def test_record_order_duplicates_and_component_contamination_are_rejected(self):
        from tests.host.selected_ownership_evidence import parse_native_output

        lines = self.transcript().splitlines(keepends=True)
        for index, changed in enumerate((
            [lines[1], lines[0], *lines[2:]],
            [lines[0], *lines[2:]],
            [lines[0], lines[1], lines[1], *lines[2:]],
            [lines[0], lines[0], *lines[1:]],
            [*lines[:-2], lines[-1]],
            [*lines[:-1], lines[-2], lines[-1]],
        )):
            with self.subTest(order=index), self.assertRaises(ValueError):
                parse_native_output("".join(changed), Stage.RED, "aggregate")
        original = self.transcript()
        producer = source.STATE_WORK_CONTRACT["producer_base"] + 1
        component = original.replace(str(producer), str(producer + 1))
        parse_native_output(component, Stage.RED, "active-frame")
        lines = component.splitlines(keepends=True)
        record = json.loads(lines[1].removeprefix("MICROS_MEM03_WORK "))
        record["counts"][2][4] = 1
        lines[1] = "MICROS_MEM03_WORK " + json.dumps(record) + "\n"
        with self.assertRaises(ValueError):
            parse_native_output("".join(lines), Stage.RED, "active-frame")


if __name__ == "__main__":
    unittest.main()
