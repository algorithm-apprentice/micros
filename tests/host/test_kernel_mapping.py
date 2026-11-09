"""MEM02 independent authority, native work and target evidence regressions."""

import ast
import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from tests.host.evidence_authority_inventory import current_inventory
from tests.host.evidence_authority_types import AuthorityClass, fact_set_digest


ROOT = Path(__file__).resolve().parents[2]


class KernelMappingAuthorityTest(unittest.TestCase):
    def test_provisional_scope_is_pid_root_bound_and_clears_after_failure(self):
        from types import SimpleNamespace
        from unittest.mock import patch
        from tests.host import kernel_mapping_authority as authority
        from tests.host.kernel_mapping_source import SOURCE_PROJECTION

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
            patch("tests.host.kernel_mapping_evidence.validate_execution_log", return_value=results),
            patch("tools.validation_context.capture_candidate", return_value=context.candidate),
        ):
            with self.assertRaises(RuntimeError):
                with authority.provisional_retirement(context, journal, {}):
                    for key in SOURCE_PROJECTION["retirement_keys"]:
                        self.assertFalse(authority.identity_required(ROOT, key))
                    with patch.object(authority.os, "getpid", return_value=-1):
                        self.assertTrue(authority.identity_required(ROOT, SOURCE_PROJECTION["retirement_keys"][0]))
                    with tempfile.TemporaryDirectory(prefix="micros-mem02-other-root-") as directory:
                        self.assertTrue(authority.identity_required(Path(directory), SOURCE_PROJECTION["retirement_keys"][0]))
                    raise RuntimeError("unit failed")
            self.assertIsNone(authority._provisional)

    def test_changed_or_partial_declarations_cannot_borrow_committed_authority(self):
        from unittest.mock import patch
        from tests.host import kernel_mapping_authority as authority
        from tests.host import kernel_mapping_source as source

        with tempfile.TemporaryDirectory(prefix="micros-mem02-claim-") as directory:
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
                return saved[arguments[1].removeprefix("HEAD:")]

            with patch.object(authority, "git", side_effect=committed):
                self.assertIsNotNone(authority.committed_claim(root))
                path = root / source.AUTHORITY_PATH
                path.write_text(path.read_text().replace("largest-contained-1g-2m-4k-leaves;", "changed-leaves;", 1))
                with self.assertRaises(ValueError):
                    authority.committed_claim(root)
            with patch.object(authority, "git", return_value=source.AUTHORITY_PATH.encode() + b"\0"):
                with self.assertRaises(ValueError):
                    authority.committed_claim(root)

    def test_fixed_plan_has_disjoint_inputs_and_preserves_every_prior_claim(self):
        from tests.host.kernel_mapping_authority import task_definition
        from tests.host.kernel_mapping_source import SOURCE_PROJECTION, SOURCE_ROLES
        from tests.host.evidence_authority_types import Environment, TransitionState
        from tests.host.evidence_authority_validation import validate_transition

        authority, authorities, plan, registry = task_definition(ROOT)
        candidate = set(dict(authority.unit_input_paths)["B14"])
        immutable = set(authority.command_input_paths) | set(plan.declared_external_inputs)
        for oracle in plan.replacement_oracles:
            immutable.update(oracle.input_paths)
        self.assertEqual(set(SOURCE_ROLES["candidate_paths"]), candidate)
        self.assertFalse(candidate & immutable)
        self.assertEqual(set(SOURCE_PROJECTION["retirement_keys"]), set(plan.planned_retirement_identity_keys))
        self.assertEqual(
            {"COPY01-runtime-memory", "PLAT01-diagnostic-history", "MEM01-bootstrap-allocation", "MEM02-kernel-mapping"},
            {claim.transition_id for claim in registry.claims},
        )
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

    def test_uncommitted_claim_cannot_suppress_an_original_mapping_assertion(self):
        from unittest.mock import patch
        from tests.host import kernel_mapping_authority as authority
        from tests.host.kernel_mapping_source import SOURCE_PROJECTION

        with (
            patch.object(authority, "_provisional", None),
            patch.object(authority, "committed_claim", return_value=None),
            patch.object(authority, "read_current", return_value=None),
        ):
            for key in SOURCE_PROJECTION["retirement_keys"]:
                self.assertTrue(authority.identity_required(ROOT, key))

    def test_eight_independent_facts_match_the_merged_contract(self):
        inventory = current_inventory(ROOT)
        facts = tuple(
            fact for fact in inventory.facts
            if fact.family == "kernel-mapping-baseline"
        )
        self.assertEqual(8, len(facts))
        source = "tests/host/kernel_mapping_source.py:"
        authority = "tests/host/kernel_mapping_authority.py:"
        self.assertEqual(
            {
                authority + "design-anchor:MEM02": AuthorityClass.TRANSPORT_IDENTITY,
                source + "source-membership:MEM02": AuthorityClass.PERMANENT_INVARIANT,
                source + "artifact-contract:MEM02-recipes": AuthorityClass.ARTIFACT_CONTRACT,
                source + "state-work-contract:MEM02": AuthorityClass.PERMANENT_INVARIANT,
                source + "source-projection:MEM02": AuthorityClass.TRANSPORT_IDENTITY,
                authority + "baseline-oracle:mem02-large-leaf-topology": AuthorityClass.BASELINE_ORACLE,
                authority + "baseline-oracle:mem02-direct-table-work": AuthorityClass.BASELINE_ORACLE,
                authority + "baseline-oracle:mem02-target-mmu-baseline": AuthorityClass.BASELINE_ORACLE,
            },
            {fact.key: fact.authority_class for fact in facts},
        )
        self.assertEqual(
            "f2388c75814e42ff852e397a35e37bddf80f171f65074ade752dae10e6ddff92",
            fact_set_digest(facts),
        )
        original = subprocess.check_output(
            (
                "git", "show",
                "e7d936d6d26e372a8794bc5fd8feb6d976fcd529:"
                "tests/host/evidence_authority_inventory.py",
            ),
            cwd=ROOT, text=True, timeout=30,
        )
        declaration, = (
            node for node in ast.parse(original).body
            if isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name)
                and target.id == "DECLARED_FAMILY_FACT_DIGESTS"
                for target in node.targets
            )
        )
        from tests.host.evidence_authority_inventory import historical_object_binding_facts

        historical = historical_object_binding_facts(ROOT, inventory.facts)
        for family, expected in ast.literal_eval(declaration.value).items():
            with self.subTest(family=family):
                self.assertEqual(
                    expected,
                    fact_set_digest(
                        fact for fact in historical if fact.family == family
                    ),
                )

    def test_declarations_reject_ambiguous_scopes_and_uncovered_retirements(self):
        from tests.host import kernel_mapping_source as source

        original = source.read_declarations(ROOT)
        for field in ("candidate_paths", "immutable_additions"):
            changed = copy.deepcopy(original)
            values = changed["SOURCE_ROLES"][field]
            changed["SOURCE_ROLES"][field] = (*values, values[0])
            with self.subTest(field=field), self.assertRaises(ValueError):
                source.validate_declarations(changed)
        for positions in ((True,), (3,), (0, 0), ()):
            changed = copy.deepcopy(original)
            row = changed["ORACLE_RULES"][2]
            changed["ORACLE_RULES"] = (*changed["ORACLE_RULES"][:2], (*row[:-1], positions))
            with self.subTest(positions=positions), self.assertRaises(ValueError):
                source.validate_declarations(changed)
        changed = copy.deepcopy(original)
        changed["ORACLE_RULES"] = tuple(
            (*row[:-1], (0,)) for row in changed["ORACLE_RULES"]
        )
        with self.assertRaises(ValueError):
            source.validate_declarations(changed)


class KernelMappingSourceTest(unittest.TestCase):
    def test_current_source_keeps_retained_scans_publication_and_private_ownership(self):
        from tests.host import kernel_mapping_source as source
        from tests.host.work_count_types import Stage

        for path in source.SOURCE_ROLES["candidate_paths"][:3]:
            current = (ROOT / path).read_text()
            with self.subTest(path=path):
                source.validate_retained_source(ROOT, path, current, Stage.GREEN)
        path = "kernel/memory/address_space.c"
        current = (ROOT / path).read_text()
        with self.assertRaises(ValueError):
            source.validate_retained_source(
                ROOT, path, current.replace("kernel_address_space.initialized = true;", "kernel_address_space.initialized = false;", 1),
                Stage.GREEN,
            )
        for path in source.RECIPE_ADMISSION["generated_subjects"]:
            current = (ROOT / path).read_text()
            observed = source.observe_source(ROOT, path, current, Stage.GREEN)
            with self.subTest(path=path):
                self.assertEqual(current, source.project_observations(ROOT, path, observed, Stage.GREEN))

    def test_native_mmu_recipe_restores_the_complete_predecessor_and_keeps_budgets(self):
        from tests.host import kernel_mapping_source as source
        from tests.host.selected_ownership_source import project_cmake as project_selected_cmake
        from tests.host.work_count_recipe import project_host_test_commands

        original = subprocess.check_output(
            ("git", "show", source.SOURCE_PROJECTION["source_reference"] + ":CMakeLists.txt"),
            cwd=ROOT, text=True, timeout=30,
        )
        current = (ROOT / "CMakeLists.txt").read_text()
        self.assertEqual(
            project_host_test_commands(project_selected_cmake(current)),
            source.extend_cmake(original),
        )
        self.assertEqual(original, source.project_cmake(current))
        changed = current.replace(source.mmu_recipe_extension(), "", 1)
        with self.assertRaises(ValueError):
            source.project_cmake(changed)
        changed = current.replace("    add_test(NAME kernel-mapping COMMAND kernel_mapping_test)", "", 1)
        with self.assertRaises(ValueError):
            source.project_cmake(changed)
        changed = current.replace("            --timeout\n            8", "            --timeout\n            80", 1)
        with self.assertRaises(ValueError):
            source.project_cmake(changed)

    def test_exact_mmu_extensions_preserve_all_original_source_and_reject_tampering(self):
        from tests.host import kernel_mapping_source as source

        for path in source.RECIPE_ADMISSION["qemu_fixture_paths"]:
            original = source.baseline_source(str(ROOT), path)
            current = (ROOT / path).read_text()
            with self.subTest(path=path):
                self.assertEqual(original, source.project_mmu_source(path, current))
                self.assertEqual(original, source.project_mmu_source(path, original))
                with self.assertRaises(ValueError):
                    source.project_mmu_source(path, current + "\n")
        assembly = source.baseline_source(str(ROOT), "arch/riscv64/mmu_test.S")
        self.assertEqual(
            (ROOT / "arch/riscv64/mmu_test.S").read_text(),
            source.extend_mmu_assembly(assembly),
        )
        current = (ROOT / "kernel/tests/memory/mmu_test.c").read_text()
        with self.assertRaises(ValueError):
            source.project_mmu_source(
                "kernel/tests/memory/mmu_test.c",
                current.replace("test_trap_count != 2", "test_trap_count != 1", 1),
            )

    def test_fixed_source_observations_round_trip_at_actual_primitive_sites(self):
        from tests.host import kernel_mapping_source as source
        from tests.host.work_count_types import Stage

        count = 0
        for path in source.RECIPE_ADMISSION["generated_subjects"]:
            original = source.baseline_source(str(ROOT), path)
            observed = source.observe_source(ROOT, path, original, Stage.RED)
            with self.subTest(path=path):
                self.assertEqual(
                    original,
                    source.project_observations(ROOT, path, observed, Stage.RED),
                )
            count += len(source.observation_sites(ROOT, path, Stage.RED))
        self.assertEqual(45, count)

    def test_relocated_or_changed_observation_cannot_borrow_original_source(self):
        from tests.host import kernel_mapping_source as source
        from tests.host.work_count_lexical import function_spans
        from tests.host.work_count_types import Stage

        path = "kernel/memory/address_space.c"
        original = source.baseline_source(str(ROOT), path)
        observed = source.observe_source(ROOT, path, original, Stage.RED)
        site = source.observation_sites(ROOT, path, Stage.RED)[0]
        call = source._observation_call(site)
        missing = observed.replace(call, "", 1)
        start = function_spans(missing)[site[0]].body_start
        moved = missing[:start] + call + missing[start:]
        for changed in (
            missing, moved, observed.replace(call, call + call, 1),
            observed.replace("table->entries[index] = 0;", "table->entries[index] = 1;", 1),
        ):
            with self.subTest(changed=changed != observed), self.assertRaises(ValueError):
                source.project_observations(ROOT, path, changed, Stage.RED)
        with self.assertRaises(ValueError):
            source.observe_source(ROOT, path, original, Stage.FIXTURE)


class KernelMappingNativeTest(unittest.TestCase):
    def test_actual_current_mapper_passes_semantics_work_and_observation_absence(self):
        from tests.host.kernel_mapping_evidence import KernelMappingOracles, decode_measurements, measure_native
        from tests.host.work_count_types import Stage

        measured = measure_native(ROOT, Stage.GREEN)
        ordinary = measure_native(ROOT, Stage.GREEN, observed=False)
        self.assertEqual(85, len(measured.records))
        self.assertEqual(
            [(row.case, row.operation, row.tables, row.middle_tables, row.leaves) for row in measured.records],
            [(row.case, row.operation, row.tables, row.middle_tables, row.leaves) for row in ordinary.records],
        )
        self.assertEqual((), ordinary.observation_symbols)
        lines = measured.output.splitlines()
        with self.assertRaises(ValueError):
            decode_measurements("\n".join(lines[:-1]) + "\n", Stage.GREEN, observed=True)
        changed = json.loads(lines[1])
        changed["capture"]["snapshot"]["counts"][2][5] = -1
        with self.assertRaises(ValueError):
            decode_measurements(
                "\n".join((lines[0], json.dumps(changed), *lines[2:])) + "\n",
                Stage.GREEN, observed=True,
            )
        changed = json.loads(lines[1])
        changed["capture"]["snapshot"]["capture_label"] = 2
        with self.assertRaises(ValueError):
            decode_measurements(
                "\n".join((lines[0], json.dumps(changed), *lines[2:])) + "\n",
                Stage.GREEN, observed=True,
            )
        for method in ("test_large_leaf_topology", "test_direct_table_work"):
            case = KernelMappingOracles(method)
            case.measured = measured
            result = unittest.TestResult()
            case.run(result)
            with self.subTest(oracle=method):
                self.assertFalse(result.errors, result.errors)
                self.assertFalse(result.failures, result.failures)
                self.assertEqual(1, result.testsRun)

    def test_actual_fixed_mapper_preserves_semantics_but_fails_both_native_oracles(self):
        from tests.host.kernel_mapping_evidence import KernelMappingOracles, measure_native
        from tests.host.work_count_types import Stage

        measured = measure_native(ROOT, Stage.RED)
        self.assertEqual(85, len(measured.records))
        for method in ("test_large_leaf_topology", "test_direct_table_work"):
            case = KernelMappingOracles(method)
            case.measured = measured
            result = unittest.TestResult()
            case.run(result)
            with self.subTest(oracle=method):
                self.assertEqual(1, result.testsRun)
                self.assertFalse(result.errors)
                self.assertFalse(result.skipped)
                self.assertTrue(result.failures)


class KernelMappingTargetRecordTest(unittest.TestCase):
    def fixture(self, *, largest):
        from tests.host.qemu_smoke_test_fixture import FRAME_ALLOCATOR_OUTPUT, FRAME_ALLOCATOR_READY_RECORD, MMU_TEST_PASS
        from tools.qemu_smoke_records import QemuResult

        binding = {"linker_bounds": {
            "__kernel_start": 0x80200000, "__kernel_text_start": 0x80200000,
            "__kernel_text_end": 0x80201000, "__kernel_rodata_start": 0x80201000,
            "__kernel_rodata_end": 0x80202000, "__kernel_writable_start": 0x80202000,
            "__kernel_writable_end": 0x80203000, "__kernel_end": 0x80203000,
        }}
        output = FRAME_ALLOCATOR_OUTPUT.replace(
            FRAME_ALLOCATOR_READY_RECORD,
            "MICROS_FRAME_ALLOCATOR_READY managed=0x0000000000007dfd free=0x0000000000007dfd\n",
        )
        tables = 7 if largest else 69
        leaves = (516, 62, 0) if largest else (32260, 0, 0)
        observation = (
            f"MICROS_KERNEL_MAPPING_OBSERVATION version=1 tables=0x{tables:016x} "
            f"leaves-4k=0x{leaves[0]:016x} leaves-2m=0x{leaves[1]:016x} "
            f"leaves-1g=0x{leaves[2]:016x} largest={int(largest)}\n"
        )
        guard = "MICROS_KERNEL_MAPPING_GUARDS aliases=2m,1g misaligned=rejected resources=preserved\n"
        passing = (
            "MICROS_KERNEL_MAPPING_TEST_PASS topology=largest aliases=2m,1g "
            "misaligned=rejected resources=preserved\n"
        )
        output += (
            f"MICROS_MMU_READY mode=sv39 root=0x0000000080203000 tables=0x{tables:016x}\n"
            f"MICROS_FRAME_OWNERSHIP_READY owned=0x{tables:016x} kernel-tables=0x{tables:016x} phase=bootstrap\n"
            + observation + guard + (passing if largest else "") + MMU_TEST_PASS
            + "MICROS_TTY_CONTROLLER_TEST_PASS mapping=exact priority=disabled enable=disabled "
            "context=supervisor seie=independent\n"
        )
        return QemuResult(output, 0, False), binding, observation, guard, passing

    def test_synthetic_protocol_controls_distinguish_baseline_from_corrected_topology(self):
        from tests.host.kernel_mapping_evidence import decode_target_output

        for largest in (False, True):
            result, binding, _, _, _ = self.fixture(largest=largest)
            observed = decode_target_output(result, binding)
            with self.subTest(largest=largest):
                self.assertIs(largest, observed["largest"])
                self.assertEqual(7, observed["expected_tables"])
                self.assertEqual((516, 62, 0), observed["expected_leaves"])

    def test_records_cannot_replace_original_guards_or_borrow_other_topology(self):
        from dataclasses import replace
        from tests.host.kernel_mapping_evidence import decode_target_output

        result, binding, observation, guard, passing = self.fixture(largest=True)
        corruptions = (
            result.output.replace(guard, ""),
            result.output.replace(observation, observation * 2),
            result.output.replace(observation + guard, guard + observation),
            result.output.replace(observation, observation.rstrip("\n")),
            result.output.replace("traps=0x0000000000000002", "traps=0x0000000000000003"),
            result.output.replace("leaves-2m=0x000000000000003e", "leaves-2m=0x000000000000003f"),
            result.output + passing,
        )
        for output in corruptions:
            with self.subTest(output=output), self.assertRaises(ValueError):
                decode_target_output(replace(result, output=output), binding)
        for changed in (replace(result, timed_out=True), replace(result, return_code=1)):
            with self.assertRaises(ValueError):
                decode_target_output(changed, binding)
        baseline, binding, _, guard, passing = self.fixture(largest=False)
        forged = baseline.output.replace("largest=0", "largest=1").replace(guard, guard + passing)
        with self.assertRaises(ValueError):
            decode_target_output(replace(baseline, output=forged), binding)
