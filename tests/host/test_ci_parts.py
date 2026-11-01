"""Only a complete same-attempt union can satisfy the parallel CI gate."""

import copy
import json
from pathlib import Path
import tempfile
import unittest

from tests.host.test_ci_records import context
from tools.ci_partition import PARTS, part_commands, target_partition
from tools.ci_records import file_inventory
from tools.stack_bound_scope import SCENARIOS, expected_images


ROOT = Path(__file__).resolve().parents[2]


def model_parts():
    partition = target_partition(ROOT)
    result = []
    for part in PARTS:
        current = context(part)
        commands = [
            {"argv": list(command), "exit_code": 0, "duration_ns": 1, "log": f"logs/{index:03d}.log"}
            for index, command in enumerate(part_commands(ROOT, part, current["base"]))
        ]
        profiles = [{
            "profile": profile.configure,
            "static": {"profile": profile.configure, "images": [
                {"image": image} for image in expected_images(profile.configure)
            ]},
            "observed": None if profile.configure not in SCENARIOS else {
                "profile": profile.configure, "gap": SCENARIOS[profile.configure].gap, "outcome": "PASS",
            },
        } for profile in partition.get(part, ())]
        result.append({
            "part": part, "before": current, "commands": commands, "profile_records": profiles,
            "result_identity": {"size": 1, "sha256": "f" * 64}, "files": {},
        })
    return result


class CiPartsTest(unittest.TestCase):
    def test_native_part_requires_exact_successful_commands_and_file_inventory(self):
        from tools.ci_parts import validate_part

        current = context("native")
        with tempfile.TemporaryDirectory(prefix="micros-ci-native-part-") as temporary:
            directory = Path(temporary)
            (directory / "logs").mkdir()
            (directory / "diagnostics").mkdir()
            (directory / "diagnostics/bootstrap.log").write_bytes(b"setup")
            (directory / "diagnostics/actionlint.log").write_bytes(b"")
            (directory / "environment.json").write_text(json.dumps(current))
            commands = []
            for index, command in enumerate(part_commands(ROOT, "native", current["base"])):
                name = f"logs/{index:03d}.log"
                (directory / name).write_bytes(b"complete command output")
                commands.append({"argv": list(command), "exit_code": 0, "duration_ns": 1, "log": name})
            result = {
                "version": 1, "status": "PART_COMPLETE", "part": "native",
                "before": current, "after": current, "commands": commands, "profiles": [],
                "files": file_inventory(directory),
            }
            path = directory / "result.json"
            path.write_text(json.dumps(result))
            validate_part(ROOT, directory, current, "native")
            for mutation in ("failed", "missing", "attempt", "extra"):
                changed = copy.deepcopy(result)
                if mutation == "failed":
                    changed["commands"][0]["exit_code"] = 1
                elif mutation == "missing":
                    changed["commands"].pop()
                elif mutation == "attempt":
                    changed["before"]["event"]["attempt"] = "2"
                else:
                    changed["unexpected"] = True
                path.write_text(json.dumps(changed))
                with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                    validate_part(ROOT, directory, current, "native")

    def test_complete_union_distinguishes_executed_commands_from_distributed_stack_intent(self):
        from tools.ci_parts import assemble_completion

        parts = model_parts()
        result = assemble_completion(ROOT, context("collector"), parts)
        self.assertEqual(47, len(result["executed_commands"]))
        self.assertEqual(1, len(result["distributed_gates"]))
        self.assertEqual(42, len(result["stack"]["static"]))
        self.assertEqual(111, sum(len(row["images"]) for row in result["stack"]["static"]))
        self.assertEqual(10, len(result["stack"]["certificates"]))
        self.assertEqual("BLOCKED", result["stack"]["future_admission"]["outcome"])

    def test_missing_duplicate_foreign_or_underdeclared_parts_cannot_make_the_gate_green(self):
        from tools.ci_parts import assemble_completion

        original = model_parts()
        cases = [original[:-1], original + original[:1]]
        missing = copy.deepcopy(original)
        missing[1]["profile_records"].pop()
        cases.append(missing)
        foreign = copy.deepcopy(original)
        foreign[1]["before"]["event"]["attempt"] = "2"
        cases.append(foreign)
        failed = copy.deepcopy(original)
        failed[0]["commands"][0]["exit_code"] = 1
        cases.append(failed)
        for parts in cases:
            with self.subTest(parts=len(parts)), self.assertRaises(ValueError):
                assemble_completion(ROOT, context("collector"), parts)


if __name__ == "__main__":
    unittest.main()
