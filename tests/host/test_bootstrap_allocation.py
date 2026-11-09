"""MEM01 actual-code work, retained-source and authority regressions."""

import ast
import copy
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tests.host import bootstrap_allocation_source as source
from tests.host.evidence_authority_inventory import current_inventory
from tests.host.evidence_authority_types import AuthorityClass, fact_set_digest
from tests.host.work_count_types import Stage


ROOT = Path(__file__).resolve().parents[2]


class BootstrapAllocationAuthorityTest(unittest.TestCase):
    def test_provisional_scope_is_pid_root_bound_and_clears_after_failure(self):
        from types import SimpleNamespace
        from tests.host import bootstrap_allocation_authority as authority

        context = SimpleNamespace(root=ROOT, phase="recertify", candidate=object())
        results = ("checked-green-replay",)
        journal = SimpleNamespace(
            attempt="0" * 32, records=({}, {"event": SimpleNamespace(oracle_results=results)}),
        )
        with (
            patch.object(authority, "_provisional", None),
            patch.object(authority, "validate_journal"),
            patch.object(authority, "recheck_context"),
            patch.object(authority, "committed_claim", return_value=None),
            patch.object(authority, "read_current", return_value=None),
            patch("tests.host.bootstrap_allocation_evidence.validate_execution_log", return_value=results),
            patch("tools.validation_context.capture_candidate", return_value=context.candidate),
        ):
            with self.assertRaises(RuntimeError):
                with authority.provisional_retirement(context, journal, {}):
                    for key in source.SOURCE_PROJECTION["retirement_keys"]:
                        self.assertFalse(authority.identity_required(ROOT, key))
                    with patch.object(authority.os, "getpid", return_value=-1):
                        self.assertTrue(authority.identity_required(ROOT, source.SOURCE_PROJECTION["retirement_keys"][0]))
                    with tempfile.TemporaryDirectory(prefix="micros-mem01-other-root-") as directory:
                        self.assertTrue(authority.identity_required(
                            Path(directory), source.SOURCE_PROJECTION["retirement_keys"][0],
                        ))
                    raise RuntimeError("unit failed")
            self.assertIsNone(authority._provisional)

    def test_fixed_design_plan_has_exact_closure_and_keeps_both_prior_claims(self):
        from tests.host.bootstrap_allocation_authority import task_definition
        from tests.host.evidence_authority_types import Environment, TransitionState
        from tests.host.evidence_authority_validation import validate_transition

        authority, authorities, plan, registry = task_definition(ROOT)
        candidate = set(dict(authority.unit_input_paths)["B14"])
        immutable = set(authority.command_input_paths) | set(plan.declared_external_inputs)
        for oracle in plan.replacement_oracles:
            immutable.update(oracle.input_paths)
        self.assertEqual(set(source.SOURCE_ROLES["candidate_paths"]), candidate)
        self.assertFalse(candidate & immutable)
        self.assertEqual(set(source.SOURCE_PROJECTION["retirement_keys"]), set(plan.planned_retirement_identity_keys))
        self.assertEqual({"COPY01-runtime-memory", "PLAT01-diagnostic-history", "MEM01-bootstrap-allocation"}, {
            claim.transition_id for claim in registry.claims
        })
        current = tuple((path, "0" * 64) for path in sorted(candidate))
        environment = Environment(
            tuple((path, "0" * 64) for path in sorted(immutable)), current, current,
            tuple((unit, current) for unit in plan.recertification_units),
            authority.base_head, authority.base_tree,
        )
        result = validate_transition(current_inventory(ROOT), authorities, registry, plan, (), environment)
        self.assertTrue(result.ok, result)
        self.assertIs(TransitionState.REPLACEMENT_REGISTERED, result.state)
        self.assertFalse(set(plan.planned_retirement_identity_keys) & set(result.retired_identity_keys))

    def test_exact_eight_new_facts_preserve_every_predecessor_fingerprint(self):
        declarations = source.read_declarations(ROOT)
        pins = declarations["DESIGN_PINS"]
        fixed = subprocess.check_output(
            ("git", "show", pins["design_commit"] + ":tests/host/evidence_authority_inventory.py"),
            cwd=ROOT, text=True, timeout=30,
        )
        declaration, = (
            node for node in ast.parse(fixed).body
            if isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == "DECLARED_FAMILY_FACT_DIGESTS"
                for target in node.targets
            )
        )
        inventory = current_inventory(ROOT)
        expected = ast.literal_eval(declaration.value)
        from tests.host.evidence_authority_inventory import historical_object_binding_facts

        historical = historical_object_binding_facts(ROOT, inventory.facts)
        for family, fingerprint in expected.items():
            self.assertEqual(
                fingerprint, fact_set_digest(fact for fact in historical if fact.family == family), family,
            )
        facts = tuple(fact for fact in inventory.facts if fact.family == source.FAMILY)
        self.assertEqual(8, len(facts))
        self.assertEqual(
            {
                source.AUTHORITY_PATH + ":design-anchor:MEM01": AuthorityClass.TRANSPORT_IDENTITY,
                source.SOURCE_PATH + ":source-membership:MEM01": AuthorityClass.PERMANENT_INVARIANT,
                source.SOURCE_PATH + ":artifact-contract:MEM01-recipes": AuthorityClass.ARTIFACT_CONTRACT,
                source.SOURCE_PATH + ":state-work-contract:MEM01": AuthorityClass.PERMANENT_INVARIANT,
                source.SOURCE_PATH + ":source-projection:MEM01": AuthorityClass.TRANSPORT_IDENTITY,
                **{
                    source.AUTHORITY_PATH + ":baseline-oracle:" + row[0]: AuthorityClass.BASELINE_ORACLE
                    for row in declarations["ORACLE_RULES"]
                },
            },
            {fact.key: fact.authority_class for fact in facts},
        )

    def test_declarations_reject_ambiguous_scopes_and_uncovered_retirements(self):
        declarations = source.read_declarations(ROOT)
        for field in ("candidate_paths", "immutable_additions"):
            changed = copy.deepcopy(declarations)
            changed["SOURCE_ROLES"][field] = (*changed["SOURCE_ROLES"][field], changed["SOURCE_ROLES"][field][0])
            with self.subTest(field=field), self.assertRaises(ValueError):
                source.validate_declarations(changed)
        changed = copy.deepcopy(declarations)
        changed["SOURCE_ROLES"]["immutable_additions"] = (
            *changed["SOURCE_ROLES"]["immutable_additions"], changed["SOURCE_ROLES"]["candidate_paths"][0],
        )
        with self.assertRaises(ValueError):
            source.validate_declarations(changed)
        for positions in ((0,), (True,), (6,)):
            changed = copy.deepcopy(declarations)
            row = changed["ORACLE_RULES"][2]
            changed["ORACLE_RULES"] = (*changed["ORACLE_RULES"][:2], (*row[:-1], positions))
            with self.subTest(positions=positions), self.assertRaises(ValueError):
                source.validate_declarations(changed)

    def test_staged_claim_cannot_suppress_any_of_the_six_assertions(self):
        from tests.host import bootstrap_allocation_authority as authority

        with (
            patch.object(authority, "_provisional", None),
            patch.object(authority, "committed_claim", return_value=None),
            patch.object(authority, "read_current", return_value=None),
        ):
            for key in source.SOURCE_PROJECTION["retirement_keys"]:
                self.assertTrue(authority.identity_required(ROOT, key), key)
        with tempfile.TemporaryDirectory(prefix="micros-mem01-no-authority-") as directory:
            for key in source.SOURCE_PROJECTION["retirement_keys"]:
                self.assertTrue(authority.identity_required(Path(directory), key), key)

    def test_changed_or_partial_declarations_cannot_borrow_committed_authority(self):
        from tests.host import bootstrap_allocation_authority as authority

        with tempfile.TemporaryDirectory(prefix="micros-mem01-claim-") as directory:
            root = Path(directory)
            saved = {}
            for name in (source.AUTHORITY_PATH, source.SOURCE_PATH):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                saved[name] = (ROOT / name).read_bytes()
                path.write_bytes(saved[name])

            def committed(_, *arguments):
                if arguments[0] == "ls-tree":
                    return b"".join(name.encode() + b"\0" for name in saved)
                self.assertEqual("show", arguments[0])
                return saved[arguments[1].removeprefix("HEAD:")]

            with patch.object(authority, "git", side_effect=committed):
                self.assertIsNotNone(authority.committed_claim(root))
                path = root / source.AUTHORITY_PATH
                path.write_text(path.read_text().replace("lowest-address-prefix;", "changed-prefix;", 1))
                with self.assertRaises(ValueError):
                    authority.committed_claim(root)
            with patch.object(authority, "git", return_value=source.AUTHORITY_PATH.encode() + b"\0"):
                with self.assertRaises(ValueError):
                    authority.committed_claim(root)


class BootstrapAllocationSourceTest(unittest.TestCase):
    def test_retained_call_shape_accepts_scalar_casts_but_not_indirect_calls(self):
        from tests.host.c_source_records import function_records

        path = "kernel/memory/frame_allocator.c"
        baseline = source.baseline_source(ROOT, path)
        body = function_records(baseline)["micros_frame_allocator_allocate"].body
        self.assertEqual(set(), source._function_calls(body))
        with self.assertRaises(ValueError):
            source._function_calls("((void (*)(void))target)();")

    def test_only_the_two_assertion_owners_receive_the_exact_six_key_policy(self):
        for path in source.SOURCE_ROLES["candidate_paths"][-2:]:
            fixed = subprocess.check_output(
                ("git", "show", source.SOURCE_PROJECTION["source_reference"] + ":" + path),
                cwd=ROOT, text=True, timeout=30,
            )
            current = source.extend_gate_adapter(path, fixed)
            with self.subTest(path=path):
                self.assertEqual(fixed, source.project_gate_adapter(path, current))
                with self.assertRaises(ValueError):
                    source.extend_gate_adapter(path, current)
        path = "tests/host/kernel_layout_source.py"
        altered = current = source.extend_gate_adapter(
            path, subprocess.check_output(
                ("git", "show", source.SOURCE_PROJECTION["source_reference"] + ":" + path),
                cwd=ROOT, text=True, timeout=30,
            ),
        )
        altered = altered.replace(
            'root, "tests/host/kernel_layout_source.py:data-entry:BOUNDED_FRAME_VALIDATION_SHA256",',
            'root, "tests/host/kernel_layout_source.py:data-entry:BASE_FRAME_VALIDATION_SHA256",',
        )
        self.assertNotEqual(current, altered)
        with self.assertRaises(ValueError):
            source.project_gate_adapter(path, altered)

    def test_native_recipe_inverse_keeps_every_predecessor_byte(self):
        fixed = subprocess.check_output(
            ("git", "show", source.SOURCE_PROJECTION["source_reference"] + ":CMakeLists.txt"),
            cwd=ROOT, text=True, timeout=30,
        )
        current = source.extend_cmake(fixed)
        self.assertEqual(fixed, source.project_cmake(current))
        for changed in (
            current.replace("MICROS_TEST_BOOTSTRAP_MEMORY_COUNTS=1", "MICROS_TEST_BOOTSTRAP_MEMORY_COUNTS=0", 1),
            current + "\nset(MICROS_UNDECLARED_INPUT ON)\n",
            current.replace("    set(MEM01_NATIVE_ROOT", "    set(MEM01_OTHER_ROOT", 1),
        ):
            with self.subTest(change=changed != current), self.assertRaises(ValueError):
                source.project_cmake(changed)

    def test_native_dependencies_cover_every_fixed_source_and_header_explicitly(self):
        import re
        import shlex
        from tools.generate_bootstrap_allocation_fixture import fixed_inputs

        recipe = source.native_recipe()
        block = re.search(r"    add_custom_command\(\n(.*?)\n    \)", recipe, re.S).group(1)
        declarations = {}
        for line in block.splitlines():
            arguments = shlex.split(line)
            if arguments and arguments[0] in ("OUTPUT", "BYPRODUCTS", "DEPFILE", "DEPENDS"):
                self.assertNotIn(arguments[0], declarations)
                declarations[arguments[0]] = arguments[1:]
        self.assertNotIn("DEPFILE", declarations)
        self.assertIn("${MEM01_NATIVE_INPUTS}", declarations["DEPENDS"])
        self.assertIn("${MEM01_NATIVE_HEADERS}", declarations["DEPENDS"])
        header_block = re.search(r"file\(GLOB_RECURSE MEM01_NATIVE_HEADERS\n(.*?)\n    \)", recipe, re.S).group(1)
        patterns = re.findall(r'"\$\{CMAKE_CURRENT_SOURCE_DIR\}/([^"]+)"', header_block)
        observed = {
            str(path.relative_to(ROOT))
            for pattern in patterns
            for path in (ROOT / Path(pattern).parent).rglob(Path(pattern).name)
        }
        inputs = fixed_inputs(ROOT)
        self.assertTrue({name for name in inputs if name.endswith(".h")} <= observed)
        source_block = re.search(r"set\(MEM01_NATIVE_INPUTS\n(.*?)\n    \)", recipe, re.S).group(1)
        self.assertEqual({name for name in inputs if name.endswith(".c")}, set(shlex.split(source_block)))

    def test_bootstrap_member_inverse_requires_both_fixture_owners_and_keeps_unknowns(self):
        from tests.host import test_kernel_bootstrap_source_layout as bootstrap

        addition = set(source.SOURCE_ROLES["immutable_additions"][:2])
        retained = {"tests/host/bootstrap_manifest_test.c", "tests/host/bootstrap_unowned.c"}
        self.assertEqual(retained, source.project_bootstrap_family_members(retained | addition))
        for missing in addition:
            with self.subTest(missing=missing), self.assertRaises(ValueError):
                source.project_bootstrap_family_members((retained | addition) - {missing})
        original = bootstrap.kernel_layout_source.glob_paths
        unknown = "tests/host/bootstrap_allocation_unowned.c"

        def extra_member(root, pattern):
            paths = tuple(original(root, pattern))
            return (*paths, root / unknown) if pattern == "tests/host/bootstrap*.[ch]" else paths

        with patch.object(bootstrap.kernel_layout_source, "glob_paths", side_effect=extra_member):
            self.assertEqual({unknown}, bootstrap.discovered_family_paths() - bootstrap.accepted_family_paths())

    def test_bootstrap_discovery_inverse_restores_the_original_complete_gate(self):
        path = "tests/host/test_kernel_bootstrap_source_layout.py"
        fixed = subprocess.check_output(
            ("git", "show", source.SOURCE_PROJECTION["source_reference"] + ":" + path),
            cwd=ROOT, text=True, timeout=30,
        )
        current = (ROOT / path).read_text()
        self.assertEqual(fixed, source.project_gate_adapter(path, current))
        for changed in (
            current.replace("    return project_bootstrap_family_members({", "    return set({", 1),
            current.replace('"tests/host/bootstrap*.[ch]"', '"tests/host/bootstrap*.c"', 1),
            current.replace("    from tests.host.bootstrap_allocation_source", "from tests.host.bootstrap_allocation_source", 1),
        ):
            with self.subTest(change=changed != current), self.assertRaises(ValueError):
                source.project_gate_adapter(path, changed)

    def test_native_archive_keeps_exact_git_modes_and_original_header_layout(self):
        from tools.generate_bootstrap_allocation_fixture import fixed_inputs

        inputs = fixed_inputs(ROOT)
        path = "include/micros/frame_allocator.h"
        row = subprocess.check_output(
            ("git", "ls-tree", source.SOURCE_PROJECTION["source_reference"], "--", path),
            cwd=ROOT, text=True, timeout=30,
        )
        self.assertEqual(int(row.split()[0], 8) & 0o777, inputs[path][1])
        self.assertEqual(source.baseline_source(ROOT, path).encode(), inputs[path][0])
        self.assertNotIn(b"allocation_cursor", inputs[path][0])

    def test_retained_original_definitions_cannot_change(self):
        path = "kernel/memory/frame_allocator.c"
        baseline = source.baseline_source(ROOT, path)
        source.validate_retained_source(ROOT, path, baseline, stage=Stage.RED)
        changed = baseline.replace("right > UINT64_MAX - left", "right >= UINT64_MAX - left", 1)
        self.assertNotEqual(baseline, changed)
        with self.assertRaises(ValueError):
            source.validate_retained_source(ROOT, path, changed, stage=Stage.RED)

    def test_native_observations_inverse_to_the_actual_selected_bodies(self):
        for path in source.RECIPE_ADMISSION["generated_subjects"]:
            baseline = source.baseline_source(ROOT, path)
            observed = source.observe_source(path, baseline, stage=Stage.RED)
            with self.subTest(path=path):
                self.assertEqual(baseline, source.project_observations(path, observed, stage=Stage.RED))
                with self.assertRaises(ValueError):
                    source.project_observations(path, observed + "\nint unowned_state;\n", stage=Stage.RED)

    def test_native_observations_reject_relocation_wrong_dimensions_and_missing_reads(self):
        path = "kernel/memory/frame_allocator.c"
        baseline = source.baseline_source(ROOT, path)
        observed = source.observe_source(path, baseline, stage=Stage.RED)
        call = (
            'MICROS_TEST_WORK_HIT("allocate-bit-read", MICROS_WORK_OWNER_KERNEL_MEMORY, '
            'MICROS_WORK_KIND_BITMAP_WORD_LOAD, 1);'
        )
        self.assertEqual(1, observed.count(call))
        for changed in (
            observed.replace(call, "", 1),
            observed.replace(call, call.replace("BITMAP_WORD_LOAD", "INDEX_PROBE"), 1),
            observed.replace(call, call + call, 1),
            observed.replace(call, "", 1).replace(
                "size_t range_index;", "size_t range_index;" + call, 1,
            ),
        ):
            with self.subTest(change=changed != observed), self.assertRaises(ValueError):
                source.project_observations(path, changed, stage=Stage.RED)


class BootstrapAllocationNativeTest(unittest.TestCase):
    def test_fixed_runtime_demonstrates_all_three_actual_work_divergences(self):
        from types import SimpleNamespace
        from tests.host.bootstrap_allocation_evidence import (
            measure_native, native_log, run_oracles, validate_execution_log,
        )

        measured = measure_native(ROOT, Stage.RED)
        results, output = run_oracles(measured)
        self.assertEqual(3, len(results))
        self.assertEqual({"EXPECTED_FAILURE"}, {item.observed.value for item in results})
        context = SimpleNamespace(root=ROOT, native_compiler=measured.compiler, files=measured.fixture_inputs)
        replay = native_log(measured, results, output, phase="red", attempt="0" * 32)
        self.assertEqual(results, validate_execution_log(context, replay, phase="red", attempt="0" * 32))
        for field in ("source_inputs", "fixture_inputs", "generated_sources"):
            changed = copy.deepcopy(replay)
            path = next(iter(changed["native"][field]))
            changed["native"][field][path]["sha256"] = "0" * 64
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_execution_log(context, changed, phase="red", attempt="0" * 32)
        for field, value in (
            ("output", "\n".join(measured.output.splitlines()[:-1]) + "\n"),
            ("observed", False),
            ("command", [measured.compiler["path"], "unreviewed.c"]),
        ):
            changed = copy.deepcopy(replay)
            changed["native"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_execution_log(context, changed, phase="red", attempt="0" * 32)

    def test_actual_current_runtime_passes_work_semantics_and_rollback(self):
        from tests.host.bootstrap_allocation_evidence import measure_native, run_oracles

        measured = measure_native(ROOT, Stage.GREEN)
        results, _ = run_oracles(measured)
        self.assertEqual(3, len(results))
        self.assertEqual({"PASS"}, {item.observed.value for item in results})

    def test_ordinary_native_execution_contains_no_observation_state(self):
        from tests.host.bootstrap_allocation_evidence import measure_native

        measured = measure_native(ROOT, Stage.GREEN, observed=False)
        self.assertTrue(measured.records)
        self.assertTrue(all(record.capture is None for record in measured.records))
        self.assertFalse(measured.observation_symbols)
