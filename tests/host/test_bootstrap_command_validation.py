import copy
from pathlib import Path
import unittest

from tests.host.bootstrap_command_evidence import (
    COMMANDS, DIMENSIONS, FIELDS, OWNER_MASK, PASS_LINES, PRODUCERS,
    checked_terminal_prefix, parse_target_output, target_failures,
)
from tests.host.bootstrap_command_source import SITES, validate_site_source, validate_target_observations
from tests.host.c_source_records import function_records
from tests.host.work_count_types import Kind, Owner


def transcript(scope, mutate=None):
    captures = []
    for index, (command, service, role) in enumerate(COMMANDS[scope]):
        captured = command in (2, 4)
        counts = [[0] * len(Kind) for _ in Owner]
        counts[Owner.KERNEL_EXECUTION][Kind.INDEX_PROBE] = int(command in (1, 2))
        counts[Owner.KERNEL_MEMORY][Kind.RANGE_VISIT] = int(scope == "tty-bootstrap-command" and (service == 4 or command == 4))
        counts[Owner.KERNEL_MEMORY][Kind.INDEX_PROBE] = (
            2 * counts[Owner.KERNEL_EXECUTION][Kind.INDEX_PROBE]
            + counts[Owner.KERNEL_MEMORY][Kind.RANGE_VISIT]
        )
        captures.append({
            "index": index, "version": 1, "reserved0": 0, "producer": PRODUCERS[scope],
            "capture": index + 1, "owner_mask": OWNER_MASK, "reserved1": 0,
            "command": command, "service": service, "role": role, "thread_slot": 0, "thread_generation": 1,
            "return_kind": int(captured), "result": command if captured else 0, "counts": counts,
        })
    summary = {"complete": 1, "captures": len(captures), "attempts": len(captures), "rejected": 0, "audits": 0}
    if mutate:
        mutate(captures, summary)

    def line(marker, fields):
        return "# " + marker + " " + " ".join(key + "=0x" + format(value, "016x") for key, value in fields.items())

    lines = ["# " + PASS_LINES[scope]]
    for row in captures:
        lines.append(line("MICROS_BOOT02_CAPTURE", {name: row[name] for name in FIELDS}))
        for owner, counts in enumerate(row["counts"]):
            lines.append(line("MICROS_BOOT02_CAPTURE_COUNTS", {
                "capture": row["capture"], "owner": owner, **dict(zip(DIMENSIONS, counts, strict=True)),
            }))
    lines.append(line("MICROS_BOOT02_CAPTURE_END", summary))
    return "\n".join(lines) + "\n"


class BootstrapCommandValidationTest(unittest.TestCase):
    def test_selected_metadata_is_not_a_complete_frame_audit(self):
        root = Path(__file__).resolve().parents[2]
        records = function_records((root / "kernel/memory/frame_ownership_validation.c").read_text())
        self.assertNotIn("MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL",
                         records["micros_frame_ownership_validate_metadata"].body)
        self.assertIn("MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL",
                      records["micros_frame_ownership_validate"].body)
        runtime = function_records((root / "kernel/memory/bootstrap_memory.c").read_text())
        self.assertNotIn("MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL",
                         runtime["micros_frame_ownership_runtime_validate_metadata"].body)
        self.assertIn("MICROS_WORK_KIND_INDEX_PROBE",
                      runtime["micros_frame_ownership_runtime_validate_metadata"].body)

    def test_serial_crlf_preserves_the_complete_evidence(self):
        for scope in COMMANDS:
            with self.subTest(scope=scope):
                lf = parse_target_output(transcript(scope), 0, scope)
                crlf = parse_target_output(transcript(scope).replace("\n", "\r\n"), 0, scope)
                self.assertEqual(lf, crlf)

    def test_complete_real_fixture_command_and_role_memberships(self):
        for scope in COMMANDS:
            with self.subTest(scope=scope):
                measured = parse_target_output(transcript(scope), 0, scope)
                self.assertEqual(len(COMMANDS[scope]), len(measured[0]))
                self.assertEqual((), target_failures(measured, scope))

    def test_terminal_composition_preserves_prior_stack_and_semantic_bytes(self):
        from tools.stack_bound_reports import legacy_transcript
        from tools.stack_bound_types import StackEvidenceError

        for scope in COMMANDS:
            lines = transcript(scope).splitlines(keepends=True)
            prefix = lines[0][2:] + "MICROS_STACK_OBSERVATION retained-by-stack-owner\r\n"
            tail = "".join(line[2:] for line in lines[1:])
            with self.subTest(scope=scope):
                self.assertEqual(prefix, checked_terminal_prefix(prefix + tail, 0, scope))
                self.assertEqual(lines[0][2:], legacy_transcript(checked_terminal_prefix(prefix + tail, 0, scope)))
                for damaged in (
                    prefix + tail + "MICROS_STACK_OBSERVATION late\n",
                    prefix + tail + "MICROS_SEMANTIC_RECORD late\n",
                    prefix + tail.replace("MICROS_BOOT02_CAPTURE_COUNTS", "MICROS_UNKNOWN_COUNTS", 1),
                ):
                    with self.assertRaises(ValueError):
                        checked_terminal_prefix(damaged, 0, scope)
                wrong = next(value for value in COMMANDS if value != scope)
                with self.assertRaises(ValueError):
                    checked_terminal_prefix(prefix + tail, 0, wrong)
                with self.assertRaises(ValueError):
                    checked_terminal_prefix(prefix + tail, 1, scope)
                with self.assertRaises(StackEvidenceError):
                    legacy_transcript(checked_terminal_prefix(
                        prefix + "MICROS_SEMANTIC_RECORD late\n" + tail, 0, scope,
                    ))
                audited = transcript(scope, lambda rows, total: total.update(audits=1))
                with self.assertRaises(ValueError):
                    checked_terminal_prefix("".join(line[2:] for line in audited.splitlines(keepends=True)), 0, scope)

    def test_old_audits_and_repeated_contexts_fail_independent_limits(self):
        for scope in COMMANDS:
            captures, summary = parse_target_output(transcript(scope), 0, scope)
            for kind in ("audit", "context", "point", "metadata"):
                changed, totals = copy.deepcopy(captures), dict(summary)
                if kind == "audit":
                    changed[0]["counts"][Owner.KERNEL_BOOT] = tuple(
                        1 if index == Kind.COMPLETE_VALIDATOR_CALL else value
                        for index, value in enumerate(changed[0]["counts"][Owner.KERNEL_BOOT])
                    )
                    totals["audits"] = 1
                elif kind == "context":
                    changed[0]["counts"][Owner.KERNEL_EXECUTION] = tuple(
                        6 if index == Kind.INDEX_PROBE else value
                        for index, value in enumerate(changed[0]["counts"][Owner.KERNEL_EXECUTION])
                    )
                elif kind == "point":
                    changed[0]["counts"][Owner.KERNEL_MEMORY] = tuple(
                        2 if index == Kind.RANGE_VISIT else value
                        for index, value in enumerate(changed[0]["counts"][Owner.KERNEL_MEMORY])
                    )
                else:
                    changed[0]["counts"][Owner.KERNEL_MEMORY] = tuple(
                        3 if index == Kind.INDEX_PROBE else value
                        for index, value in enumerate(changed[0]["counts"][Owner.KERNEL_MEMORY])
                    )
                with self.subTest(scope=scope, kind=kind):
                    self.assertTrue(target_failures((changed, totals), scope))

    def test_incomplete_borrowed_and_wrong_semantic_records_fail(self):
        for scope in COMMANDS:
            mutations = (
                lambda rows, total: rows.pop(),
                lambda rows, total: rows[0].update(service=63),
                lambda rows, total: rows[0].update(role=128),
                lambda rows, total: rows[0].update(producer=1),
                lambda rows, total: rows[1].update(capture=1),
                lambda rows, total: rows[1].update(thread_generation=2),
                lambda rows, total: rows[-1].update(return_kind=0),
                lambda rows, total: rows[0]["counts"].pop(),
                lambda rows, total: total.update(complete=0),
                lambda rows, total: total.update(attempts=1),
            )
            for mutation in mutations:
                with self.subTest(scope=scope, mutation=mutation), self.assertRaises(ValueError):
                    parse_target_output(transcript(scope, mutation), 0, scope)
            with self.assertRaises(ValueError):
                parse_target_output(transcript(scope), 1, scope)
            with self.assertRaises(ValueError):
                parse_target_output(transcript(scope)[:-1], 0, scope)

    def test_current_sites_recipe_and_safe_terminal_report_boundaries(self):
        root = Path(__file__).resolve().parents[2]
        validate_target_observations(root)
        for path, name, label, _, _ in SITES:
            text = (root / path).read_text()
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate_site_source(path, text.replace('"boot02-' + label + '"', '"foreign-site"', 1))
