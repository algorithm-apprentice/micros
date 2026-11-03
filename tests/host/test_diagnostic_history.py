"""Independent PLAT01 declaration, runtime and transition regressions."""

import ast
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from tests.host.diagnostic_history_authority import ORACLE_RULES
from tests.host.diagnostic_history_source import (
    AUTHORITY_PATH, FAMILY, SOURCE_PATH, SOURCE_PROJECTION,
    checked_oracle_rules, declaration_facts,
    project_diagnostic_cmake, project_diagnostic_context, project_diagnostic_gate,
    project_runner_options, project_runner_source,
)
from tests.host.evidence_authority_inventory import current_inventory
from tests.host.evidence_authority_types import AuthorityClass
from tests.host.qemu_smoke_test_fixture import PANIC_OUTPUT, TRAP_CONTEXT_RECORD
from tools import qemu_smoke_arguments, qemu_smoke_cli, qemu_smoke_faults
from tools.qemu_smoke_records import QemuResult, SmokeOutcome


ROOT = Path(__file__).resolve().parents[2]


class DiagnosticHistoryAuthorityTest(unittest.TestCase):
    def test_only_the_eleven_new_production_members_may_be_absent_for_red(self):
        from unittest.mock import patch
        from tests.host import diagnostic_history_authority as authority

        base = authority.base_membership(ROOT)
        expected = set(base) | set(authority.SOURCE_ROLES["candidate_paths"]) | set(
            authority.SOURCE_ROLES["immutable_additions"]
        )
        new_production = set(authority.SOURCE_ROLES["candidate_paths"]) - set(base)
        self.assertEqual(11, len(new_production))
        observed = expected - new_production

        def membership(_, *arguments):
            return b"" if "--others" in arguments else b"".join(
                name.encode() + b"\0" for name in sorted(observed)
            )

        with (
            tempfile.TemporaryDirectory(prefix="micros-plat01-typed-absence-") as directory,
            patch.object(authority, "base_membership", return_value=base),
            patch.object(authority, "git", side_effect=membership),
        ):
            root = Path(directory)
            self.assertEqual(new_production, authority.current_membership(root, allow_absent=True)[2])
            with self.assertRaises(ValueError):
                authority.current_membership(root, allow_absent=False)
            observed.remove("README.md")
            with self.assertRaises(ValueError):
                authority.current_membership(root, allow_absent=True)
            observed.add("README.md")
            path = root / sorted(new_production)[0]
            path.parent.mkdir(parents=True)
            path.touch()
            with self.assertRaises(ValueError):
                authority.current_membership(root, allow_absent=True)

    def test_staged_claim_without_a_terminal_journal_keeps_every_assertion(self):
        from unittest.mock import patch
        from tests.host import diagnostic_history_authority as authority

        def absent_claim(root, *arguments):
            self.assertEqual(ROOT, root)
            self.assertEqual(
                ("ls-tree", "-r", "--name-only", "-z", "HEAD", "--", AUTHORITY_PATH, SOURCE_PATH),
                arguments,
            )
            return b""

        with (
            patch.object(authority, "_provisional", None),
            patch.object(authority, "git", side_effect=absent_claim),
            patch.object(authority, "read_current", return_value=None),
            patch.object(authority, "capture_author_context", side_effect=AssertionError("no author receipt")),
        ):
            for key in SOURCE_PROJECTION["retirement_keys"]:
                self.assertTrue(authority.identity_required(ROOT, key), key)
        with tempfile.TemporaryDirectory(prefix="micros-plat01-no-authority-") as directory:
            self.assertTrue(authority.identity_required(Path(directory), SOURCE_PROJECTION["retirement_keys"][0]))
        self.assertTrue(authority.identity_required(ROOT, "unrelated:permanent:invariant"))

    def test_real_design_plan_registers_all_properties_without_retiring_them(self):
        from tests.host.diagnostic_history_authority import task_definition
        from tests.host.evidence_authority_types import Environment, TransitionState
        from tests.host.evidence_authority_validation import validate_transition

        authority, authorities, plan, registry = task_definition(ROOT)
        candidates = set(dict(authority.unit_input_paths)["B14"])
        immutable = set(authority.command_input_paths) | set(plan.declared_external_inputs)
        for oracle in plan.replacement_oracles:
            immutable.update(oracle.input_paths)
        self.assertEqual(29, len(candidates))
        self.assertFalse(candidates & immutable)
        self.assertEqual(tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",), plan.recertification_units)
        self.assertEqual(20, len(plan.planned_retirement_identity_keys))
        self.assertEqual(4, len(plan.replacement_oracles))
        self.assertEqual({"COPY01-runtime-memory", "PLAT01-diagnostic-history"}, {
            claim.transition_id for claim in registry.claims
        })
        current = tuple((path, "0" * 64) for path in sorted(candidates))
        environment = Environment(
            tuple((path, "0" * 64) for path in sorted(immutable)), current, current,
            tuple((unit, current) for unit in plan.recertification_units),
            authority.base_head, authority.base_tree,
        )
        result = validate_transition(current_inventory(ROOT), authorities, registry, plan, (), environment)
        self.assertTrue(result.ok, result)
        self.assertIs(TransitionState.REPLACEMENT_REGISTERED, result.state)
        self.assertFalse(result.retired_identity_keys)
        self.assertTrue(set(plan.planned_retirement_identity_keys) <= set(result.active_identity_keys))

    def test_inventory_materializes_the_exact_eight_authorities(self):
        expected = {
            AUTHORITY_PATH + ":design-anchor:PLAT01": AuthorityClass.TRANSPORT_IDENTITY,
            AUTHORITY_PATH + ":source-membership:PLAT01": AuthorityClass.PERMANENT_INVARIANT,
            AUTHORITY_PATH + ":artifact-contract:PLAT01-recipes": AuthorityClass.ARTIFACT_CONTRACT,
            SOURCE_PATH + ":source-projection:PLAT01": AuthorityClass.TRANSPORT_IDENTITY,
        }
        for identifier in (
            "plat01-history-storage", "plat01-reader-commands",
            "plat01-forwarded-diagnostics", "plat01-panic-history",
        ):
            expected[AUTHORITY_PATH + ":baseline-oracle:" + identifier] = AuthorityClass.BASELINE_ORACLE
        facts = tuple(fact for fact in current_inventory(ROOT).facts if fact.family == FAMILY)
        self.assertEqual(len(expected), len(facts))
        self.assertEqual(expected, {fact.key: fact.authority_class for fact in facts})

    def test_oracle_coverage_rejects_duplicate_tests_and_uncovered_keys(self):
        identities = SOURCE_PROJECTION["retirement_keys"]
        rows = list(ORACLE_RULES)
        identifier, _, property_name, adr, section, positions = rows[1]
        rows[1] = (identifier, rows[0][1], property_name, adr, section, positions)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            checked_oracle_rules(tuple(rows), identities)
        rows = list(ORACLE_RULES)
        identifier, test, property_name, adr, section, _ = rows[3]
        rows[3] = (identifier, test, property_name, adr, section, (1,))
        with self.assertRaisesRegex(ValueError, "omits"):
            checked_oracle_rules(tuple(rows), identities)
        rows[3] = (identifier, test, property_name, adr, section, (21,))
        with self.assertRaisesRegex(ValueError, "unbound"):
            checked_oracle_rules(tuple(rows), identities)

    def test_literal_declarations_reject_duplicate_or_computed_roles(self):
        with tempfile.TemporaryDirectory(prefix="micros-plat01-declaration-") as directory:
            root = Path(directory)
            for name in (AUTHORITY_PATH, SOURCE_PATH):
                destination = root / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes((ROOT / name).read_bytes())
            source = (root / AUTHORITY_PATH).read_text(encoding="utf-8")
            needle = '    "candidate_paths": (\n'
            duplicate = source.replace(needle, '    "candidate_paths": (),\n' + needle, 1)
            self.assertNotEqual(source, duplicate)
            (root / AUTHORITY_PATH).write_text(duplicate, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate dictionary key"):
                declaration_facts(root, AUTHORITY_PATH)
            (root / AUTHORITY_PATH).write_text(source + "\nSOURCE_ROLES = dict(SOURCE_ROLES)\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "one ordinary SOURCE_ROLES"):
                declaration_facts(root, AUTHORITY_PATH)


class DiagnosticReportTest(unittest.TestCase):
    @staticmethod
    def report(payload, *, prefix=PANIC_OUTPUT, chunk=32):
        result = prefix + f"MICROS_DIAGNOSTIC_HISTORY state=retained bytes=0x{len(payload):016x}\n"
        for offset in range(0, len(payload), chunk):
            result += (
                f"MICROS_DIAGNOSTIC_HISTORY_DATA offset=0x{offset:016x} "
                f"data={payload[offset:offset + chunk].hex()}\n"
            )
        return result

    def history_checker(self):
        checker = getattr(qemu_smoke_faults, "_has_diagnostic_history", None)
        self.assertIsNotNone(checker, "the additional bounded history requirement is not implemented")
        return checker

    def test_retained_empty_and_exact_seeded_histories(self):
        check = self.history_checker()
        seed = bytes(ord("a") + index % 23 for index in range(1999)) + b"\n"
        self.assertTrue(check(self.report(b""), "retained"))
        self.assertFalse(check(self.report(b""), "seeded"))
        self.assertTrue(check(self.report(seed[2:]), "seeded"))
        self.assertFalse(check(self.report(seed[1:]), "seeded"))
        self.assertTrue(check(self.report(bytes((1, 10, 27, 128, 255)), chunk=2), "retained"))
        self.assertTrue(check(self.report(b"tail", prefix=PANIC_OUTPUT + TRAP_CONTEXT_RECORD), "retained"))
        self.assertFalse(check(PANIC_OUTPUT, "retained"))
        self.assertFalse(check(self.report(b"tail"), "unknown"))

    def test_history_rejects_malformed_and_reordered_records(self):
        check = self.history_checker()
        valid = self.report(b"m" * 64)
        header, first, second = valid[len(PANIC_OUTPUT):].splitlines(keepends=True)
        malformed = (
            PANIC_OUTPUT + header + header + first + second,
            PANIC_OUTPUT + header + first,
            valid + second,
            PANIC_OUTPUT + header + first + second.replace("0000000000000020", "0000000000000021"),
            valid.replace("data=6d", "data=6D", 1),
            PANIC_OUTPUT + header + first.replace("data=6d", "data=d", 1) + second,
            self.report(b"m" * 64, chunk=33),
            valid.replace("bytes=0x0000000000000040", "bytes=0x00000000000007cf"),
            valid[:-1],
            header + PANIC_OUTPUT + first + second,
            PANIC_OUTPUT + header + first + "MICROS_BOOT 0.1.0\n" + second,
            valid.replace("state=retained", "state=unavailable"),
            valid.replace("data=6d", "data=MICROS_PANIC", 1),
            valid + "MICROS_BOOT 0.1.0\n",
        )
        for index, value in enumerate(malformed):
            with self.subTest(case=index):
                self.assertFalse(check(value, "retained"))
        self.assertTrue(check(valid, "retained"))

    def test_forwarding_requires_unique_terminated_witness_before_legacy_io(self):
        check = getattr(qemu_smoke_faults, "_has_diagnostic_forwarding", None)
        self.assertIsNotNone(check, "the forwarding order requirement is not implemented")
        marker = (
            "MICROS_DIAGNOSTIC_TEST_PASS transfer=three-pages envelope=irq-diagnostic "
            "output=tty early=drained\n"
        )
        ready = "MICROS_TTY_INPUT_READY\n"
        done = (
            "MICROS_TTY_TEST_PASS handoff=two-phase mapping=exact irq=deferred "
            "input=canonical output=interrupt-driven grants=checked\n"
        )
        self.assertTrue(check(marker + ready + done))
        for value in (
            ready + done, marker + marker + ready + done, ready + marker + done,
            marker + done + ready, marker[:-1], marker[:-1] + ready + done,
        ):
            with self.subTest(output=value):
                self.assertFalse(check(value))

    def test_optional_requirements_preserve_default_arguments(self):
        with tempfile.TemporaryDirectory(prefix="micros-plat01-arguments-") as directory:
            kernel = Path(directory) / "kernel.elf"
            kernel.touch()
            base = ("--kernel", str(kernel), "--marker", "boot")
            arguments = qemu_smoke_arguments.parse_arguments(base)
            self.assertIsNone(getattr(arguments, "require_diagnostic_history", "missing"))
            self.assertIs(getattr(arguments, "require_diagnostic_forwarding", None), False)
            arguments = qemu_smoke_arguments.parse_arguments((
                *base, "--require-diagnostic-history", "seeded", "--require-diagnostic-forwarding",
            ))
            self.assertEqual("seeded", arguments.require_diagnostic_history)
            self.assertTrue(arguments.require_diagnostic_forwarding)

    def test_selected_requirement_reaches_actual_outcome_composition(self):
        with tempfile.TemporaryDirectory(prefix="micros-plat01-composition-") as directory:
            kernel = Path(directory) / "kernel.elf"
            kernel.touch()
            base = (
                "--kernel", str(kernel), "--marker", "MICROS_BOOT 0.1.0",
                "--expect", "panic", "--require-panic-report",
            )
            arguments = qemu_smoke_arguments.parse_arguments(base)
            original = QemuResult(output=PANIC_OUTPUT, return_code=0, timed_out=False)

            def accepted(result):
                return qemu_smoke_cli._matches_observed_run(
                    arguments, result, SmokeOutcome.PANIC, SmokeOutcome.PANIC, None, None,
                )

            self.assertTrue(accepted(original))
            arguments = qemu_smoke_arguments.parse_arguments((*base, "--require-diagnostic-history", "retained"))
            self.assertFalse(accepted(original))
            current = QemuResult(output=self.report(b"tail"), return_code=0, timed_out=False)
            self.assertTrue(accepted(current))


class DiagnosticExecutionTest(unittest.TestCase):
    def evidence(self):
        name = "tests.host.diagnostic_history_evidence"
        self.assertIsNotNone(importlib.util.find_spec(name), "the actual diagnostic evidence executor is absent")
        return importlib.import_module(name)

    def test_baseline_archive_retains_exact_git_blobs_and_ordinary_modes(self):
        evidence = self.evidence()
        with tempfile.TemporaryDirectory(prefix="micros-plat01-fixed-archive-") as directory:
            source = evidence.extract_baseline(ROOT, Path(directory))
            evidence.require_archive(ROOT, source)
            path = source / "README.md"
            path.chmod(0o600)
            with self.assertRaises(ValueError):
                evidence.require_archive(ROOT, source)
            path.chmod(0o644)
            moved = source.with_name("relocated")
            source.rename(moved)
            source.symlink_to(moved, target_is_directory=True)
            with self.assertRaises(ValueError):
                evidence.require_archive(ROOT, source)

    def test_reader_observations_require_two_complete_actual_results(self):
        evidence = self.evidence()
        red = [
            {"command": command, "result": -1, "shape": False, "preserved": True}
            for command in (3, 4)
        ]
        output = "".join(json.dumps(row) + "\n" for row in red)
        self.assertEqual(tuple(red), evidence.reader_observations(output, stage="red"))
        for malformed in (
            output[:-1], output + output, json.dumps(red[0]) + "\n",
            output.replace('"result": -1', '"result": -2', 1),
            output.replace('"shape": false', '"shape": 0', 1),
            output.replace('"preserved": true', '"preserved": false', 1),
            output.replace('"command": 3', '"command": 3, "command": 4', 1),
        ):
            with self.subTest(output=malformed), self.assertRaises(ValueError):
                evidence.reader_observations(malformed, stage="red")
        green = output.replace('"result": -1', '"result": 0').replace(
            '"shape": false', '"shape": true'
        ).replace('"preserved": true', '"preserved": false')
        self.assertEqual((0, 0), tuple(
            row["result"] for row in evidence.reader_observations(green, stage="green")
        ))
        with self.assertRaises(ValueError):
            evidence.reader_observations(green, stage="red")

    def test_missing_new_witness_does_not_turn_legacy_failure_into_red(self):
        evidence = self.evidence()
        with tempfile.TemporaryDirectory(prefix="micros-plat01-legacy-result-") as directory:
            kernel = Path(directory) / "kernel.elf"
            kernel.touch()
            arguments = qemu_smoke_arguments.parse_arguments((
                "--kernel", str(kernel), "--marker", "MICROS_BOOT 0.1.0",
                "--expect", "panic", "--require-panic-report",
            ))
            evidence.require_legacy_result(arguments, QemuResult(PANIC_OUTPUT, 0, False))
            for result in (
                QemuResult(PANIC_OUTPUT, None, True),
                QemuResult(PANIC_OUTPUT, 1, False),
                QemuResult(PANIC_OUTPUT.replace("MICROS_PANIC_HART", "missing"), 0, False),
            ):
                with self.subTest(result=result), self.assertRaises(ValueError):
                    evidence.require_legacy_result(arguments, result)

    def test_four_named_oracles_report_only_discriminating_red_failures(self):
        from tests.host.evidence_authority_types import ObservedResult

        evidence = self.evidence()
        observed = evidence.OracleObservations(
            (), tuple({"command": value, "result": -1, "shape": False, "preserved": True} for value in (3, 4)),
            QemuResult("", 0, False), QemuResult(PANIC_OUTPUT, 0, False),
        )
        results, output = evidence.run_oracles(observed, stage="red")
        self.assertEqual(tuple(row[0] for row in ORACLE_RULES), tuple(row.oracle_id for row in results))
        self.assertEqual({ObservedResult.EXPECTED_FAILURE}, {row.observed for row in results})
        self.assertIn("Ran 4 tests", output)
        with self.assertRaises(ValueError):
            evidence.run_oracles(observed, stage="green")


class DiagnosticJournalTest(unittest.TestCase):
    def driver(self):
        name = "tools.check_diagnostic_history"
        self.assertIsNotNone(importlib.util.find_spec(name), "the three-intent author transaction is absent")
        return importlib.import_module(name)

    def test_driver_admits_only_the_three_documented_intents(self):
        from contextlib import redirect_stderr
        import io

        driver = self.driver()
        for phase in ("red", "green", "recertify"):
            self.assertEqual(phase, driver.parse_arguments(("--" + phase,)).phase)
        for argv in ((), ("--red", "--green"), ("--red", "--source", "/tmp"), ("--rec",)):
            with self.subTest(argv=argv), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                driver.parse_arguments(argv)

    def test_journal_selector_is_byte_bound_and_writer_lock_is_exclusive(self):
        from tests.host import diagnostic_history_authority as authority

        self.driver()
        with tempfile.TemporaryDirectory(prefix="micros-plat01-selector-") as directory:
            root = Path(directory)
            with authority.author_lock(root) as storage:
                journal = authority.begin_attempt(storage)
                value, identity = authority.read_selection(storage)
                self.assertEqual(identity, journal.selector_sha256)
                self.assertEqual(0, value["count"])
                with self.assertRaises(BlockingIOError):
                    with authority.author_lock(root):
                        self.fail("a competing writer entered the journal")
                path = storage / "current.json"
                path.write_bytes(path.read_bytes() + b"\n")
                changed, rewritten = authority.read_selection(storage)
                self.assertEqual(value, changed)
                self.assertNotEqual(journal.selector_sha256, rewritten)

    def model_context(self, root):
        from types import SimpleNamespace
        from tests.host.diagnostic_history_authority import task_definition
        from tests.host.evidence_authority_types import Environment

        root.mkdir(parents=True, exist_ok=True)
        authority, authorities, plan, registry = task_definition(ROOT)
        candidates = tuple(sorted((path, "1" * 64) for path in dict(authority.unit_input_paths)["B14"]))
        immutable = tuple(sorted((path, "0" * 64) for path in (
            *authority.command_input_paths, *plan.declared_external_inputs,
        )))
        environment = Environment(
            immutable, candidates, candidates,
            tuple((unit, candidates) for unit in plan.recertification_units),
            authority.base_head, "a" * 40,
        )
        return SimpleNamespace(
            root=root, authority=authority, authorities=authorities, plan=plan, registry=registry,
            inventory=current_inventory(ROOT), environment=environment, allow_absent=False,
        )

    @staticmethod
    def model_results(_, value, *, phase, attempt):
        from tests.host.evidence_authority_types import ObservedResult, OracleResult, RunStatus

        if value != {"synthetic_phase": phase}:
            raise ValueError("synthetic execution is incomplete")
        observed = ObservedResult.EXPECTED_FAILURE if phase == "red" else ObservedResult.PASS
        return tuple(OracleResult(row[0], RunStatus.RUN, observed, row[2]) for row in ORACLE_RULES)

    def model_append(self, driver, context, journal, phase, *, omitted_unit=None):
        logs = {}
        if phase == "recertify":
            logs = {
                unit: {
                    "version": 1, "unit": unit, "command": list(command),
                    "exit_code": 0, "output": "Synthetic journal model; not author execution.\n",
                }
                for unit, command in context.plan.unit_commands if unit != omitted_unit
            }
        replay = {"synthetic_phase": phase}
        results = self.model_results(context, replay, phase=phase, attempt=journal.attempt)
        return driver.append_phase(context, journal, phase, replay, results, logs)

    def test_journal_model_rejects_raw_rewrites_and_input_drift_before_publication(self):
        from dataclasses import replace
        from unittest.mock import patch
        from tests.host import diagnostic_history_authority as authority

        driver = self.driver()
        with (
            patch.object(driver, "recheck_context"),
            patch("tests.host.diagnostic_history_evidence.validate_execution_log", side_effect=self.model_results),
            tempfile.TemporaryDirectory(prefix="micros-plat01-chain-model-") as directory,
        ):
            for mutation in ("selector", "record", "log", "immutable"):
                with self.subTest(mutation=mutation):
                    context = self.model_context(Path(directory) / mutation)
                    with authority.author_lock(context.root) as storage:
                        journal = self.model_append(driver, context, authority.begin_attempt(storage), "red")
                        if mutation == "immutable":
                            rows = context.environment.immutable_inputs
                            context.environment = replace(
                                context.environment, immutable_inputs=((rows[0][0], "f" * 64), *rows[1:]),
                            )
                        else:
                            path = storage / "current.json" if mutation == "selector" else (
                                journal.directory / ("red.json" if mutation == "record" else "red-execution.log")
                            )
                            path.write_bytes(path.read_bytes() + b"\n")
                        with self.assertRaises(ValueError):
                            self.model_append(driver, context, journal, "green")
                        self.assertEqual(1, authority.read_current(storage)["count"])
                        self.assertFalse((journal.directory / "green.json").exists())

    def test_terminal_model_requires_all_units_and_scoped_suppression_cannot_leak(self):
        from dataclasses import replace
        from unittest.mock import patch
        from tests.host import diagnostic_history_authority as authority
        from tests.host.evidence_authority_types import TransitionState

        driver = self.driver()
        with (
            patch.object(authority, "_provisional", None),
            patch.object(authority, "recheck_context"),
            patch.object(driver, "recheck_context"),
            patch("tests.host.diagnostic_history_evidence.validate_execution_log", side_effect=self.model_results),
            tempfile.TemporaryDirectory(prefix="micros-plat01-terminal-model-") as directory,
        ):
            context = self.model_context(Path(directory))
            with authority.author_lock(context.root) as storage:
                journal = authority.begin_attempt(storage)
                for phase in ("red", "green"):
                    journal = self.model_append(driver, context, journal, phase)
                previous = context.environment
                changed = ((previous.current_candidate_inputs[0][0], "e" * 64), *previous.current_candidate_inputs[1:])
                context.environment = replace(
                    previous, current_candidate_inputs=changed,
                    unit_inputs=tuple((unit, changed) for unit in context.plan.recertification_units),
                    current_tree="b" * 40,
                )
                with self.assertRaises(ValueError):
                    authority.read_journal(context)
                context.environment = previous
                replay = {"synthetic_phase": "recertify"}
                with self.assertRaisesRegex(RuntimeError, "model interruption"):
                    with authority.provisional_retirement(context, journal, replay):
                        with self.assertRaises(ValueError):
                            with authority.provisional_retirement(context, journal, replay):
                                self.fail("nested suppression was admitted")
                        raise RuntimeError("model interruption")
                self.assertIsNone(authority._provisional)
                with self.assertRaises(ValueError):
                    self.model_append(driver, context, journal, "recertify", omitted_unit="B12")
                self.assertEqual(2, authority.read_current(storage)["count"])
                journal = self.model_append(driver, context, journal, "recertify")
                result = authority.validate_journal(context, journal)
                self.assertIs(TransitionState.RETIREMENT_READY, result.state)
                self.assertEqual(set(SOURCE_PROJECTION["retirement_keys"]), set(result.retired_identity_keys))
                packed = {fact.key for fact in context.inventory.facts if fact.family == "scheduler-packed-transport"}
                self.assertEqual(177, len(packed))
                self.assertTrue(packed <= set(result.active_identity_keys))

    def test_failed_terminal_publication_restores_the_exact_prior_selector(self):
        from unittest.mock import patch
        from tests.host import diagnostic_history_authority as authority

        driver = self.driver()
        publish = driver.publish_json
        with (
            patch.object(driver, "recheck_context"),
            patch("tests.host.diagnostic_history_evidence.validate_execution_log", side_effect=self.model_results),
            tempfile.TemporaryDirectory(prefix="micros-plat01-publication-model-") as directory,
        ):
            context = self.model_context(Path(directory))
            with authority.author_lock(context.root) as storage:
                journal = authority.begin_attempt(storage)
                for phase in ("red", "green"):
                    journal = self.model_append(driver, context, journal, phase)
                before = (storage / "current.json").read_bytes()

                def reject_published(path, payload, *, validate=None):
                    def check():
                        validate()
                        if path.name == "current.json" and authority.read_current(storage)["count"] == 3:
                            raise ValueError("synthetic postpublication drift")
                    return publish(path, payload, validate=check)

                with patch.object(driver, "publish_json", side_effect=reject_published):
                    with self.assertRaisesRegex(ValueError, "postpublication drift"):
                        self.model_append(driver, context, journal, "recertify")
                self.assertEqual(before, (storage / "current.json").read_bytes())
                self.assertEqual(journal, authority.read_journal(context))


class DiagnosticSourceTest(unittest.TestCase):
    def test_current_retrigger_transfer_binding_is_complete(self):
        from tests.host.diagnostic_history_source import validate_retrigger_kernel_fixture, validate_retrigger_read_adapter

        validate_retrigger_kernel_fixture((ROOT / "tests/qemu/diagnostic_history_kernel.c").read_text())
        validate_retrigger_read_adapter((ROOT / "kernel/diagnostic/diagnostic_control.c").read_text())

    def test_retrigger_admission_keeps_queued_irq_and_no_copy_before_deferral(self):
        from tests.host import diagnostic_history_source as source_contract

        validate = getattr(source_contract, "validate_retrigger_read_adapter", None)
        validate_fixture = getattr(source_contract, "validate_retrigger_kernel_fixture", None)
        self.assertIsNotNone(validate, "the test-only deferred return has no checked copy boundary")
        self.assertIsNotNone(validate_fixture, "the fixture has no exact queued-IRQ admission")
        site = source_contract.RETRIGGER_READ_BOUNDARY
        source = "static void read_history(void)\n{\n" + site + "\n}\n"
        validate(source)
        for changed in (
            source.replace("MICROS_SYSCALL_ABI_STATE", "MICROS_SYSCALL_ABI_ARGUMENT"),
            source.replace("#ifdef MICROS_BUILD_TTY_SERVICE_TEST\n", "", 1),
            source.replace("    micros_diagnostic_copy_prevalidated(&diagnostic_reader, history, &span);\n", "", 1),
            source.replace("    if (!micros_diagnostic_test_before_read", "    if (micros_diagnostic_test_before_read", 1),
        ):
            with self.assertRaises(AssertionError):
                validate(changed)
        fixture = (ROOT / "tests/qemu/diagnostic_history_kernel.c").read_text()
        validate_fixture(fixture)
        for changed in (
            fixture.replace("endpoint->pending_kernel_events != UINT64_C(0x8)", "endpoint->pending_kernel_events == 0"),
            fixture.replace("state->handoff.deadline_armed || state->handoff.deadline != 0", "false"),
            fixture.replace("        return false;\n", "        return true;\n", 1),
        ):
            self.assertNotEqual(fixture, changed)
            with self.assertRaises(AssertionError):
                validate_fixture(changed)

    def test_scheduler_pump_cannot_move_across_custody_or_selection(self):
        from tests.host import diagnostic_history_source as source_contract

        changes = getattr(source_contract, "SCHEDULER_PUMP_CHANGES", None)
        self.assertIsNotNone(changes, "the two diagnostic pump sites lack exact custody anchors")
        for name, additions in changes.items():
            baseline = subprocess.check_output(
                ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + name),
                cwd=ROOT, timeout=30,
            ).decode("utf-8")
            candidate = baseline
            for added, original in additions:
                self.assertEqual(1, candidate.count(original))
                candidate = candidate.replace(original, added, 1)
            source_contract.validate_legacy_subject(ROOT, name, candidate, required=True)
            call = "    micros_diagnostic_notify_pending(hart, " + (
                "NULL" if name.endswith("scheduler_idle.c") else "frame"
            ) + ");\n"
            moved = candidate.replace(call, "", 1).replace("    if (error != ", call + "    if (error != ", 1)
            self.assertNotEqual(candidate, moved)
            with self.assertRaises(AssertionError):
                source_contract.validate_legacy_subject(ROOT, name, moved, required=True)

    def test_native_dispatch_double_cannot_change_or_hide_an_old_body(self):
        from tests.host.diagnostic_history_source import project_diagnostic_native_fixture

        name = "tests/host/tty_control_syscall_test_controller.c"
        baseline = subprocess.check_output(
            ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + name),
            cwd=ROOT, timeout=30,
        ).decode("utf-8")
        source = (ROOT / name).read_text(encoding="utf-8")
        self.assertEqual(baseline, project_diagnostic_native_fixture(name, source))
        changed = source.replace("    abort();", "    return 0;", 1)
        self.assertNotEqual(baseline, project_diagnostic_native_fixture(name, changed))
        changed = source.replace("fixture->operation_stage != 5", "fixture->operation_stage != 4", 1)
        self.assertNotEqual(baseline, project_diagnostic_native_fixture(name, changed))

    def test_legacy_subject_adapters_preserve_every_original_case(self):
        from tests.host import diagnostic_history_source as source_contract

        changes = getattr(source_contract, "LEGACY_SUBJECT_CHANGES", None)
        validate = getattr(source_contract, "validate_legacy_subject", None)
        self.assertIsNotNone(changes, "legacy target additions have no closed source register")
        self.assertIsNotNone(validate, "legacy cases have no independent conservation check")
        for name, additions in changes.items():
            baseline = subprocess.check_output(
                ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + name),
                cwd=ROOT, timeout=30,
            ).decode("utf-8")
            candidate = baseline
            for added, original in additions:
                self.assertEqual(1, candidate.count(original), original)
                candidate = candidate.replace(original, added, 1)
            with self.subTest(path=name):
                validate(ROOT, name, candidate, required=True)
                validate(ROOT, name, baseline, required=False)
                with self.assertRaises(AssertionError):
                    validate(ROOT, name, baseline, required=True)
                changed = candidate.replace("return false;", "return true;", 1)
                self.assertNotEqual(candidate, changed)
                with self.assertRaises(AssertionError):
                    validate(ROOT, name, changed, required=True)
                incomplete = candidate.replace(additions[0][0], additions[0][1], 1)
                with self.assertRaises(AssertionError):
                    validate(ROOT, name, incomplete, required=False)

    def test_seed_extension_retains_the_original_branch_and_rejects_relocation(self):
        from tests.host.diagnostic_history_source import project_boot_diagnostic_seed

        original = (
            "#ifdef MICROS_BUILD_PANIC_TEST\n"
            '    MICROS_PANIC(boot->hart_id, "intentional-test");\n#endif'
        )
        call = "    micros_diagnostic_test_seed_panic();\n"
        candidate = original.replace('    MICROS_PANIC', call + "    MICROS_PANIC", 1)
        self.assertEqual(original, project_boot_diagnostic_seed(candidate))
        for changed in (
            original, candidate + "\n" + call,
            original.replace("\n#endif", "\n" + call + "#endif"),
            candidate.replace("MICROS_BUILD_PANIC_TEST", "MICROS_BUILD_TRAP_TEST"),
        ):
            with self.subTest(source=changed), self.assertRaises(AssertionError):
                project_boot_diagnostic_seed(changed)

    def test_assertion_adapters_restore_all_original_modules_without_bypass(self):
        from tests.host.diagnostic_history_source import validate_source_adapters

        validate_source_adapters(ROOT)
        name = "tests/host/kernel_layout_source.py"
        source = (ROOT / name).read_text(encoding="utf-8")
        for changed in (
            source.replace("transports, required=required)", "transports, required=False)", 1),
            source.replace("if required and hashlib.sha256(restored)", "if False and hashlib.sha256(restored)", 1),
        ):
            self.assertNotEqual(source, changed)
            with self.assertRaises(AssertionError):
                project_diagnostic_gate(name, changed)

    def test_kernel_extension_keeps_exact_ordinary_owners_and_private_direction(self):
        from tests.host import diagnostic_history_source as source_contract

        validate = getattr(source_contract, "diagnostic_kernel_members", None)
        self.assertIsNotNone(validate, "new diagnostic files lack an explicit closed owner extension")
        members = SOURCE_PROJECTION["new_kernel_members"]
        with tempfile.TemporaryDirectory(prefix="micros-plat01-kernel-members-") as directory:
            root = Path(directory)
            for name in members:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("/* Independent ownership fixture. */\n")
                path.chmod(0o644)
            self.assertEqual((members, ()), validate(root))
            selected = root / "kernel/diagnostic/diagnostic_history_core.c"
            original = selected.read_text()
            selected.write_text('#include "kernel/diagnostic/diagnostic_history.h"\n')
            self.assertTrue(validate(root)[1])
            selected.write_text(original)
            selected.chmod(0o600)
            self.assertTrue(validate(root)[1])
            selected.chmod(0o644)
            selected.unlink()
            self.assertTrue(validate(root)[1])
            selected.symlink_to(root / "kernel/diagnostic/diagnostic_history.c")
            self.assertTrue(validate(root)[1])

    def test_tty_constructor_keeps_original_callback_bindings_and_phase_order(self):
        from unittest.mock import patch
        from tests.host import diagnostic_history_source as source_contract
        from tests.host.c_source_records import function_records
        from tools.stack_bound_bindings import anchored_function, reference_source
        from tools.stack_bound_types import FunctionKey, StackEvidenceError

        changes = getattr(source_contract, "TTY_CONSTRUCTOR_CHANGES", None)
        project = getattr(source_contract, "project_diagnostic_callback_source", None)
        self.assertIsNotNone(changes, "TTY constructor additions lack their fixed insertion register")
        self.assertIsNotNone(project, "TTY constructor has no exact historical inverse")
        unit = "servers/tty/tty_service.c"
        original = reference_source(str(ROOT), unit)
        record = function_records(original)["micros_service_main"]
        body = record.body
        for added, previous in changes:
            self.assertEqual(1, body.count(previous), previous)
            body = body.replace(previous, added, 1)
        candidate = original.replace(
            record.signature + "{" + record.body + "}",
            record.signature + "{" + body + "}", 1,
        )
        self.assertEqual(original, project(unit, candidate))
        with tempfile.TemporaryDirectory(prefix="micros-plat01-tty-constructor-") as directory:
            root = Path(directory)
            path = root / unit
            path.parent.mkdir(parents=True)
            path.write_text(candidate)
            key = FunctionKey("fixture", "tty.elf", unit, "micros_service_main")
            with patch("tools.stack_bound_bindings.reference_source", return_value=original):
                anchored_function(root, key)
                path.write_text(candidate.replace("MICROS_TTY_UART_VIRTUAL_BASE", "0", 1))
                with self.assertRaises(StackEvidenceError):
                    anchored_function(root, key)
        altered = candidate.replace(
            "&tty_uart, &bus, &tty_service.terminal, &diagnostic_effects",
            "&tty_uart, &foreign_bus, &tty_service.terminal, &diagnostic_effects", 1,
        )
        self.assertNotEqual(candidate, altered)
        with self.assertRaises(AssertionError):
            project(unit, altered)

    def test_callback_free_admission_also_checks_every_actual_machine_call(self):
        from types import SimpleNamespace
        from unittest.mock import patch
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_artifacts import object_frames
        from tools.stack_bound_bindings import bind_current_callbacks, reference_source
        from tools.stack_bound_callback_contracts import GROUPS
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import Frame, FunctionKey, StackEvidenceError

        headers = {
            group.header: reference_source(str(ROOT), group.header)
            for group in GROUPS if group.source_prefix == "servers/tty/"
        }
        with tempfile.TemporaryDirectory(prefix="micros-plat01-callback-machine-") as directory:
            root, build = Path(directory) / "source", Path(directory) / "build"
            build.mkdir()
            unit = "servers/tty/tty_diagnostic.c"
            for name, content in (*headers.items(), (unit, "void diagnostic_case(void) {}\n")):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            (build / "CMakeCache.txt").write_text("")

            def pinned(_, name):
                if name in headers:
                    return headers[name]
                raise StackEvidenceError("no historical source for this unit")

            for indirect in (False, True):
                body = "((void (*)(void))0x40000000)();" if indirect else ""
                _, output = compile_fixture(build, "void diagnostic_case(void) { " + body + " }\n")
                obj = load_object(output)
                key = FunctionKey("fixture", "service.elf", unit, "diagnostic_case")
                catalog = SimpleNamespace(
                    frames=(Frame(key, object_frames(obj, function_symbols(obj))["diagnostic_case"]),),
                    definitions=(SimpleNamespace(function=key),), objects=((key, output.name),),
                    origins=(SimpleNamespace(function=key, source=unit),), pending=(), calls=(),
                )
                with patch("tools.stack_bound_bindings.reference_source", side_effect=pinned):
                    if indirect:
                        with self.assertRaises(StackEvidenceError):
                            bind_current_callbacks(root, build, catalog)
                    else:
                        self.assertEqual(((), ()), bind_current_callbacks(root, build, catalog))

    def test_only_declared_callback_free_sources_avoid_absent_historical_blobs(self):
        from unittest.mock import patch
        from tools.stack_bound_bindings import reference_source, require_callback_type_scope
        from tools.stack_bound_callback_contracts import GROUPS
        from tools.stack_bound_types import StackEvidenceError

        group = next(group for group in GROUPS if group.identifier == "tty-uart")
        original_header = reference_source(str(ROOT), group.header)
        with tempfile.TemporaryDirectory(prefix="micros-plat01-callback-scope-") as directory:
            root = Path(directory)
            unit = "servers/tty/tty_diagnostic.c"
            header = "servers/tty/tty_diagnostic.h"
            for name, content in (
                (group.header, original_header),
                (unit, '#include "servers/tty/tty_diagnostic.h"\nint helper(void) { return 0; }\n'),
                (header, "struct diagnostic_fixture { unsigned cursor; };\n"),
                ("servers/tty/undeclared.c", "int other(void) { return 0; }\n"),
            ):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)

            def pinned(_, name):
                if name == group.header:
                    return original_header
                raise StackEvidenceError("no historical source for this unit")

            with patch("tools.stack_bound_bindings.reference_source", side_effect=pinned):
                require_callback_type_scope(root, group, {unit})
                (root / header).write_text("typedef void (*hidden_hook)(void);\n")
                with self.assertRaises(StackEvidenceError):
                    require_callback_type_scope(root, group, {unit})
                (root / header).write_text("struct micros_tty_uart_bus *carrier;\n")
                with self.assertRaises(StackEvidenceError):
                    require_callback_type_scope(root, group, {unit})
                with self.assertRaises(StackEvidenceError):
                    require_callback_type_scope(root, group, {"servers/tty/undeclared.c"})

    def test_existing_tty_owner_admits_only_actual_callback_free_diagnostic_wrappers(self):
        from types import SimpleNamespace
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_artifacts import object_frames
        from tools.stack_bound_bindings import bind_current_callbacks
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import Frame, FunctionKey, StackEvidenceError

        unit = "servers/tty/tty_control.c"
        source = (ROOT / unit).read_text()
        beginning = "micros_runtime_result_t micros_tty_control_diagnostic_read(uint32_t previous)\n{\n"
        indirect = source.replace(
            beginning, beginning + "    ((void (*)(void))(uintptr_t)previous)();\n", 1,
        )
        self.assertNotEqual(source, indirect)
        extra = source + "\nvoid micros_tty_control_diagnostic_unknown(void) {}\n"
        with tempfile.TemporaryDirectory(prefix="micros-plat01-control-callbacks-") as directory:
            build = Path(directory)
            (build / "CMakeCache.txt").write_text("")
            for label, body in (
                ("actual", source), ("indirect", indirect), ("unknown", extra), ("borrowed", source),
            ):
                with self.subTest(case=label):
                    _, output = compile_fixture(build, body)
                    obj = load_object(output)
                    symbols = function_symbols(obj)
                    sizes = object_frames(obj, symbols)
                    keys = tuple(FunctionKey("fixture", "service.elf", unit, name) for name in symbols)
                    catalog = SimpleNamespace(
                        frames=tuple(Frame(key, sizes[key.symbol]) for key in keys),
                        definitions=tuple(SimpleNamespace(function=key) for key in keys),
                        objects=tuple((key, output.name) for key in keys),
                        origins=tuple(SimpleNamespace(
                            function=key,
                            source="servers/tty/tty_service.c" if label == "borrowed"
                                and key.symbol == "micros_tty_control_diagnostic_read" else unit,
                        ) for key in keys),
                        pending=(), calls=(),
                    )
                    if label == "actual":
                        self.assertEqual(((), ()), bind_current_callbacks(ROOT, build, catalog))
                    else:
                        with self.assertRaises(StackEvidenceError):
                            bind_current_callbacks(ROOT, build, catalog)

    def test_runner_and_guard_inverses_preserve_complete_fixed_source(self):
        names = (
            "tools/qemu_smoke_faults.py", "tools/qemu_smoke_arguments.py",
            "tools/qemu_smoke_outcome.py", "tools/qemu_smoke_cli.py",
            "tools/validation_context.py",
        )
        for name in names:
            baseline = subprocess.check_output(
                ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + name),
                cwd=ROOT, timeout=30,
            ).decode("utf-8")
            source = (ROOT / name).read_text(encoding="utf-8")
            projected = project_diagnostic_context(source) if name == names[-1] else project_runner_source(name, source)
            with self.subTest(path=name):
                self.assertEqual(baseline, projected)
                for node in ast.parse(source).body:
                    if isinstance(node, ast.FunctionDef) and name != names[-1]:
                        self.assertLessEqual(node.end_lineno - node.lineno + 1, 120, node.name)

    def test_incomplete_forwarding_or_changed_legacy_body_cannot_project_away(self):
        name = "tools/qemu_smoke_outcome.py"
        source = (ROOT / name).read_text(encoding="utf-8")
        incomplete = source.replace("    require_diagnostic_history=None,\n", "", 1)
        with self.assertRaisesRegex(AssertionError, "incomplete"):
            project_runner_source(name, incomplete)
        changed = source.replace("or result.return_code != 0", "or result.return_code == 0", 1)
        self.assertNotEqual(source, changed)
        self.assertNotEqual(project_runner_source(name, source), project_runner_source(name, changed))

    def test_malformed_runner_source_is_an_explicit_unsupported_context(self):
        from tests.host.diagnostic_history_source import projected_runner_digest
        from tools.validation_inputs import UnsupportedContext

        with tempfile.TemporaryDirectory(prefix="micros-plat01-malformed-runner-") as directory:
            path = Path(directory) / "qemu_smoke_faults.py"
            path.write_text("def incomplete(\n", encoding="utf-8")
            with self.assertRaises(UnsupportedContext):
                projected_runner_digest(path)

    def test_option_observation_adapter_preserves_the_original_gate(self):
        name = "tests/host/test_qemu_runner_source_layout.py"
        baseline = subprocess.check_output(
            ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + name),
            cwd=ROOT, timeout=30,
        ).decode("utf-8")
        self.assertEqual(baseline, project_diagnostic_gate(name, (ROOT / name).read_text(encoding="utf-8")))
        observations = (
            (1, "--require-panic-report"), (1, "--require-diagnostic-history"),
            (1, "--require-diagnostic-forwarding"), (1, "--require-trap-ready"),
        )
        self.assertEqual((observations[0], observations[3]), project_runner_options(observations))
        for changed in (
            (observations[1], observations[0], *observations[2:]),
            (*observations, observations[1]),
            (observations[0], (2, observations[1][1]), *observations[2:]),
        ):
            with self.subTest(observations=changed), self.assertRaises(AssertionError):
                project_runner_options(changed)

    def test_complete_recipe_has_an_exact_inverse(self):
        from tests.host.terminal_failure_source import project_terminal_cmake

        baseline = subprocess.check_output(
            ("git", "show", SOURCE_PROJECTION["source_reference"] + ":CMakeLists.txt"),
            cwd=ROOT, timeout=30,
        ).decode("utf-8")
        source = project_terminal_cmake((ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
        self.assertEqual(baseline, project_diagnostic_cmake(source))
        for changed in (
            source.replace("            --require-diagnostic-forwarding\n", "", 1),
            source.replace("            --require-diagnostic-history\n            retained\n",
                           "            --require-diagnostic-history\n            seeded\n", 1),
            source.replace("PRIVATE MICROS_TEST_WORK_COUNTS=1)", "PRIVATE MICROS_TEST_WORK_COUNTS=0)", 1),
        ):
            with self.subTest(recipe=changed[-100:]), self.assertRaises(AssertionError):
                project_diagnostic_cmake(changed)


class DiagnosticFatalMachineTest(unittest.TestCase):
    def test_thr_observation_is_after_and_dominated_by_the_actual_store(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_bindings import reference_source
        from tools.stack_bound_fatal_effects import uart_machine_effects
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        source = reference_source(str(ROOT), "arch/riscv64/platform.h") + reference_source(str(ROOT), "arch/riscv64/uart.c")
        store = "    uart0[UART_THR] = (uint8_t)character;\n"
        observation = "    micros_diagnostic_test_observe_thr();\n"
        self.assertEqual(1, source.count(store))
        with tempfile.TemporaryDirectory(prefix="micros-plat01-thr-site-") as directory:
            for position in ("after", "before"):
                for observed in (False, True):
                    root = Path(directory) / f"{position}-{observed}"
                    root.mkdir()
                    changed = "void micros_diagnostic_test_observe_thr(void);\n" + source.replace(
                        store, store + observation if position == "after" else observation + store, 1,
                    )
                    _, output = compile_fixture(root, changed, observed=observed)
                    obj = load_object(output)
                    symbol = function_symbols(obj)["uart_write_character"]
                    with self.subTest(position=position, observed=observed):
                        if position == "after":
                            uart_machine_effects(obj, symbol, test_counter=True)
                            with self.assertRaises(StackEvidenceError):
                                uart_machine_effects(obj, symbol)
                        else:
                            with self.assertRaises(StackEvidenceError):
                                uart_machine_effects(obj, symbol, test_counter=True)

    def test_recording_arms_require_the_false_selector_continuation(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools import stack_bound_fatal_effects as effects
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        prove = getattr(effects, "diagnostic_recording_guard", None)
        self.assertIsNotNone(prove, "logical writers have no source-independent branch proof")
        for name, parameter, argument in (
            ("uart_write", "const char *text", "text"),
            ("uart_write_hex64", "uint64_t value", "value"),
        ):
            prefix = (
                "#include <stdint.h>\n#include <stdbool.h>\n"
                "bool uart_panic_mode(void);\nbool foreign_selector(void);\n"
                f"void {name}_direct({parameter});\nvoid {name}_record({parameter});\n"
            )
            correct = (
                f"if (uart_panic_mode()) {{ {name}_direct({argument}); return; }} "
                f"{name}_record({argument});"
            )
            cases = (
                (correct, True),
                (correct.replace("if (uart_panic_mode())", "if (!uart_panic_mode())"), False),
                (correct.replace(" return;", ""), False),
                (correct.replace("uart_panic_mode()", "foreign_selector()"), False),
                (f"{name}_record({argument}); " + correct, False),
            )
            with tempfile.TemporaryDirectory(prefix="micros-plat01-guard-") as directory:
                for index, (body, accepted) in enumerate(cases):
                    for observed in (False, True):
                        root = Path(directory) / f"{index}-{observed}"
                        root.mkdir()
                        _, output = compile_fixture(
                            root, prefix + f"void {name}({parameter}) {{ {body} }}\n", observed=observed,
                        )
                        obj = load_object(output)
                        symbol = function_symbols(obj)[name]
                        with self.subTest(name=name, body=body, observed=observed):
                            if accepted:
                                predicate, excluded, branch = prove(obj, symbol)
                                self.assertLess(predicate, branch)
                                self.assertNotEqual(excluded, branch)
                            else:
                                with self.assertRaises(StackEvidenceError):
                                    prove(obj, symbol)

    def test_panic_selector_uses_only_the_actual_owner_field(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools import stack_bound_fatal_effects as effects
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        prove = getattr(effects, "diagnostic_selector_effects", None)
        self.assertIsNotNone(prove, "the concrete PANIC selector has no machine proof")
        prefix = (
            "#include <stdint.h>\n#include <stdbool.h>\n"
            "struct console { uint64_t magic; unsigned owner; bool quiesced; };\n"
            "static struct console uart_console_state __attribute__((used));\n"
            "static struct console foreign_console __attribute__((used));\n"
        )
        cases = (
            ("return uart_console_state.owner == 3;", True),
            ("return uart_console_state.owner == 2;", False),
            ("return foreign_console.owner == 3;", False),
            ("uart_console_state.owner = 3; return true;", False),
            ('__asm__ volatile("csrsi sstatus, 2"); return uart_console_state.owner == 3;', False),
        )
        with tempfile.TemporaryDirectory(prefix="micros-plat01-selector-") as directory:
            for index, (body, valid) in enumerate(cases):
                for observed in (False, True):
                    root = Path(directory) / f"{index}-{observed}"
                    root.mkdir()
                    source = prefix + "bool uart_panic_mode(void) { " + body + " }\n"
                    _, output = compile_fixture(root, source, observed=observed)
                    obj = load_object(output)
                    symbol = function_symbols(obj)["uart_panic_mode"]
                    console, = (item for item in obj.symbols if item.name == "uart_console_state")
                    with self.subTest(body=body, observed=observed):
                        if valid:
                            proof = prove(obj, symbol, console)
                            self.assertFalse(proof.writes or proof.systems)
                        else:
                            with self.assertRaises(StackEvidenceError):
                                prove(obj, symbol, console)

    def test_thr_observer_proves_exact_unsigned_saturation_and_no_other_effect(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools import stack_bound_fatal_effects as effects
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        prove = getattr(effects, "diagnostic_counter_effects", None)
        self.assertIsNotNone(prove, "the THR observation has no bounded machine-effect proof")
        prefix = (
            "#include <stdint.h>\n"
            "static uint64_t diagnostic_thr_writes __attribute__((used));\n"
            "static uint64_t foreign_counter __attribute__((used));\n"
            "uint64_t fixture_count(void) { return diagnostic_thr_writes; }\n"
        )
        correct = "if (diagnostic_thr_writes != UINT64_MAX) { ++diagnostic_thr_writes; }"
        cases = (
            (correct, True),
            (correct.replace("!=", "=="), False),
            (correct.replace("++diagnostic_thr_writes", "diagnostic_thr_writes += 2"), False),
            (correct.replace("diagnostic_thr_writes", "foreign_counter"), False),
            ("++diagnostic_thr_writes;", False),
            (correct + ' __asm__ volatile("csrsi sstatus, 2");', False),
        )
        with tempfile.TemporaryDirectory(prefix="micros-plat01-counter-") as directory:
            for index, (body, valid) in enumerate(cases):
                for observed in (False, True):
                    root = Path(directory) / f"{index}-{observed}"
                    root.mkdir()
                    source = prefix + "void micros_diagnostic_test_observe_thr(void) { " + body + " }\n"
                    _, output = compile_fixture(root, source, observed=observed)
                    obj = load_object(output)
                    symbol = function_symbols(obj)["micros_diagnostic_test_observe_thr"]
                    counter, = (item for item in obj.symbols if item.name == "diagnostic_thr_writes")
                    with self.subTest(body=body, observed=observed):
                        if valid:
                            proof = prove(obj, symbol, counter)
                            self.assertEqual(1, len(proof.writes))
                            self.assertFalse(proof.systems)
                        else:
                            with self.assertRaises(StackEvidenceError):
                                prove(obj, symbol, counter)

    def test_history_formatter_rejects_foreign_or_unbounded_writes(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools import stack_bound_fatal_effects as effects
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        prove = getattr(effects, "diagnostic_readonly_effects", None)
        self.assertIsNotNone(prove, "history formatting has no closed frame-only write proof")
        prefix = (
            "void uart_write(const char *text);\n"
            "void foreign_call(void);\n"
            "unsigned long foreign_state;\n"
        )
        valid = "char pair[3]; pair[0] = (char)value; pair[1] = 'a'; pair[2] = 0; uart_write(pair);"
        cases = (
            (valid, True),
            ("foreign_state = value; " + valid, False),
            (valid.replace("pair[0]", "pair[value]"), False),
            (valid + ' __asm__ volatile("csrsi sstatus, 2");', False),
            (valid + " foreign_call();", False),
        )
        with tempfile.TemporaryDirectory(prefix="micros-plat01-format-effects-") as directory:
            for index, (body, accepted) in enumerate(cases):
                root = Path(directory) / str(index)
                root.mkdir()
                _, output = compile_fixture(root, prefix + "void write_history_byte(unsigned value) { " + body + " }\n")
                obj = load_object(output)
                symbol = function_symbols(obj)["write_history_byte"]
                with self.subTest(body=body):
                    if accepted:
                        self.assertFalse(prove(obj, symbol).writes)
                    else:
                        with self.assertRaises(StackEvidenceError):
                            prove(obj, symbol)
