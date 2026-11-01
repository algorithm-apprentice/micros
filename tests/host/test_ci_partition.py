"""Parallel placement cannot remove or duplicate canonical validation coverage."""

from dataclasses import replace
from pathlib import Path
import unittest
from unittest.mock import patch

from tools import validation_plan
from tools.stack_bound_build import static_profiles
from tools.stack_bound_scope import expected_images, SCENARIOS


ROOT = Path(__file__).resolve().parents[2]


class CiPartitionTest(unittest.TestCase):
    def test_balanced_parts_preserve_every_profile_image_and_observation(self):
        from tools.ci_partition import target_partition

        parts = target_partition(ROOT)
        profiles = static_profiles(ROOT)
        actual = tuple(profile for part in parts.values() for profile in part)
        self.assertCountEqual(profiles, actual)
        self.assertEqual(len(actual), len({row.configure for row in actual}))
        self.assertEqual(111, sum(len(expected_images(row.configure)) for row in actual))
        self.assertEqual(10, sum(row.configure in SCENARIOS for row in actual))
        weights = [
            sum(len(expected_images(row.configure)) * (2 if row.configure in SCENARIOS else 1) for row in part)
            for part in parts.values()
        ]
        self.assertLessEqual(max(weights) - min(weights), 1)
        with patch("tools.ci_partition.static_profiles", return_value=tuple(reversed(profiles))):
            self.assertEqual(parts, target_partition(ROOT))

    def test_parts_execute_the_full_nonstack_plan_exactly_once(self):
        from tools.ci_partition import PARTS, part_commands

        commands = [command for part in PARTS for command in part_commands(ROOT, part, "BASE")]
        expected = [
            command for command in validation_plan.plan([], "full", "BASE")
            if command != validation_plan.STACK_EVIDENCE
        ]
        self.assertCountEqual(expected, commands)
        self.assertEqual(len(commands), len(set(commands)))
        with self.assertRaises(ValueError):
            part_commands(ROOT, "caller-selected-subset", "BASE")

    def test_unknown_missing_and_duplicated_scope_fails_before_execution(self):
        from tools.ci_partition import target_partition

        profiles = static_profiles(ROOT)
        for changed in (
            profiles[:-1], profiles + profiles[:1],
            (replace(profiles[0], workflow="test-qemu-unregistered"),) + profiles[1:],
        ):
            with (
                self.subTest(profiles=len(changed)),
                patch("tools.ci_partition.static_profiles", return_value=changed),
                self.assertRaises(ValueError),
            ):
                target_partition(ROOT)


if __name__ == "__main__":
    unittest.main()
