import pathlib
import re
import unittest
from unittest import mock

from tools import validation_build_impact
from tools import validation_plan


ROOT = pathlib.Path(__file__).resolve().parents[2]


def timeout_edit(content, target, value):
    start = re.search(r"add_custom_target\(\s+" + target + r"\s", content).start()
    timeout = re.search(r"--timeout\s+([0-9]+)", content[start:])
    first = start + timeout.start(1)
    last = start + timeout.end(1)
    return content[:first] + str(value) + content[last:]


class ValidationBuildImpactTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")

    def test_panic_timeout_has_one_explicit_owner(self):
        edited = timeout_edit(self.original, "test-qemu-panic", 12)
        self.assertEqual(
            ("test-qemu-panic",),
            validation_build_impact.classify(self.original, edited),
        )

    def test_smoke_timeout_is_owned_without_an_enclosing_guard(self):
        edited = timeout_edit(self.original, "test-qemu-smoke", 12)
        self.assertEqual(
            ("test-qemu-smoke",),
            validation_build_impact.classify(self.original, edited),
        )

    def test_shared_scheduler_target_owns_both_distinct_workflows(self):
        edited = timeout_edit(self.original, "test-qemu-scheduler-invalid-context", 12)
        self.assertEqual(
            (
                "test-qemu-scheduler-invalid-outgoing",
                "test-qemu-scheduler-invalid-next",
            ),
            validation_build_impact.classify(self.original, edited),
        )

    def test_multiple_owned_bodies_union_without_duplicates(self):
        edited = timeout_edit(self.original, "test-qemu-panic", 12)
        edited = timeout_edit(edited, "test-qemu-smoke", 14)
        self.assertEqual(
            {"test-qemu-smoke", "test-qemu-panic"},
            set(validation_build_impact.classify(self.original, edited)),
        )

    def test_unchanged_file_has_no_verified_edit(self):
        self.assertIsNone(validation_build_impact.classify(self.original, self.original))

    def test_every_unlisted_target_stays_shared(self):
        for target in ("test-qemu-trap", "test-qemu-scheduler"):
            with self.subTest(target=target):
                edited = timeout_edit(self.original, target, 12)
                self.assertIsNone(validation_build_impact.classify(self.original, edited))

    def test_invalid_timeout_tokens_stay_shared(self):
        for value in ("0", "601", "08", "-1", "8.5", '"12"', "${OTHER}", "8;9"):
            with self.subTest(value=value):
                edited = timeout_edit(self.original, "test-qemu-panic", value)
                self.assertIsNone(validation_build_impact.classify(self.original, edited))

    def test_mutable_position_is_the_only_allowed_byte_change(self):
        edited = timeout_edit(self.original, "test-qemu-panic", 12)
        for extra in (
            edited + "\n",
            edited.replace("project(micros", "project(other", 1),
            edited.replace("if(MICROS_BUILD_PANIC_TEST)", "if(OTHER)", 1),
            edited.replace("reason=intentional-test", "reason=other", 1),
            edited.replace("USES_TERMINAL", "VERBATIM\n        USES_TERMINAL", 1),
        ):
            with self.subTest(extra=extra[-100:]):
                self.assertIsNone(validation_build_impact.classify(self.original, extra))

    def test_before_snapshot_must_also_match_the_fixed_template(self):
        original = self.original.replace("reason=intentional-test", "reason=other", 1)
        edited = timeout_edit(original, "test-qemu-panic", 12)
        self.assertIsNone(validation_build_impact.classify(original, edited))

    def test_comments_quotes_and_hidden_commands_cannot_spoof_boundaries(self):
        edited = timeout_edit(self.original, "test-qemu-panic", 12)
        for payload in (
            '# add_custom_target(test-qemu-panic --timeout 12)\n',
            'message(") add_custom_target(test-qemu-panic --timeout 12)")\n',
            "#[=[\n)=]\n]=]\n",
            'execute_process(COMMAND "unexpected")\n',
            "include(other.cmake)\n",
        ):
            with self.subTest(payload=payload):
                self.assertIsNone(validation_build_impact.classify(self.original, edited + payload))

    def test_ambiguous_duplicate_definition_stays_shared(self):
        edited = timeout_edit(self.original, "test-qemu-panic", 12)
        duplicate = "\nadd_custom_target(test-qemu-panic COMMAND echo duplicate)\n"
        self.assertIsNone(
            validation_build_impact.classify(self.original + duplicate, edited + duplicate)
        )

    def test_identical_outside_changes_do_not_admit_an_unknown_scope(self):
        for before, after in (
            ("function(wrapper)\n", "\nendfunction()\n"),
            ("if(OTHER)\n", "\nendif()\n"),
            ("#[=[\n", "\n]=]\n"),
        ):
            original = before + self.original + after
            edited = timeout_edit(original, "test-qemu-panic", 12)
            self.assertIsNone(validation_build_impact.classify(original, edited))

    def test_malformed_or_unclosed_syntax_stays_shared(self):
        edited = timeout_edit(self.original, "test-qemu-panic", 12)
        for suffix in ('\nmessage("unclosed)\n', "\nif(OTHER)\n", "\n)\n"):
            with self.subTest(suffix=suffix):
                self.assertIsNone(
                    validation_build_impact.classify(self.original + suffix, edited + suffix)
                )

    def test_separate_git_layers_union_both_owned_edits(self):
        first = timeout_edit(self.original, "test-qemu-panic", 12)
        second = timeout_edit(first, "test-qemu-scheduler-invalid-context", 14)
        snapshots = [
            validation_build_impact.Snapshot("100644", content)
            for content in (self.original, first, second, second)
        ]
        self.assertEqual(
            {
                "test-qemu-panic",
                "test-qemu-scheduler-invalid-outgoing",
                "test-qemu-scheduler-invalid-next",
            },
            set(validation_build_impact.classify_layers(snapshots)),
        )

    def test_later_revert_cannot_hide_shared_earlier_layer(self):
        changed = self.original + "\nset(UNREVIEWED ON)\n"
        snapshots = [
            validation_build_impact.Snapshot("100644", content)
            for content in (self.original, changed, self.original, self.original)
        ]
        self.assertIsNone(validation_build_impact.classify_layers(snapshots))

    def test_mode_missing_and_unmerged_snapshots_cannot_be_owned(self):
        edited = timeout_edit(self.original, "test-qemu-panic", 12)
        for unsupported in (
            validation_build_impact.Snapshot("100755", edited),
            validation_build_impact.Snapshot("120000", edited),
            None,
        ):
            with self.subTest(unsupported=unsupported):
                baseline = validation_build_impact.Snapshot("100644", self.original)
                self.assertIsNone(
                    validation_build_impact.classify_layers(
                        (baseline, unsupported, baseline, baseline)
                    )
                )

    @mock.patch("tools.validation_build_impact.discover")
    def test_verified_panic_plan_keeps_native_smoke_and_all_diffs(self, discover):
        discover.return_value = ("test-qemu-panic",)
        self.assertEqual(
            [
                validation_plan.UNIT_FULL,
                validation_plan.SMOKE,
                validation_plan.workflow("test-qemu-panic"),
                *validation_plan.diff_commands("custom-base"),
            ],
            validation_plan.repository_plan("/repo", ["CMakeLists.txt"], "pr", "custom-base"),
        )
        discover.assert_called_once_with("/repo", "custom-base")

    @mock.patch("tools.validation_build_impact.discover")
    def test_unknown_other_path_cannot_be_hidden_by_owned_cmake(self, discover):
        discover.return_value = ("test-qemu-panic",)
        self.assertEqual(
            validation_plan.plan([], "full"),
            validation_plan.repository_plan(
                "/repo", ["CMakeLists.txt", "kernel/unmapped.c"], "pr"
            ),
        )

    @mock.patch("tools.validation_build_impact.discover")
    def test_explicit_cmake_without_verified_change_remains_full(self, discover):
        discover.return_value = None
        self.assertEqual(
            validation_plan.plan([], "full"),
            validation_plan.repository_plan("/repo", ["CMakeLists.txt"], "pr"),
        )

    def test_explicit_full_and_non_cmake_plans_do_not_narrow(self):
        for paths, tier in (
            (["CMakeLists.txt"], "full"),
            (["kernel/ipc.c"], "pr"),
            (["docs/testing-strategy.md"], "fast"),
        ):
            with self.subTest(paths=paths, tier=tier):
                self.assertEqual(
                    validation_plan.plan(paths, tier),
                    validation_plan.repository_plan("/repo", paths, tier),
                )

    @mock.patch("tools.validation_build_impact._worktree_snapshot")
    @mock.patch("tools.validation_build_impact._git_snapshot")
    @mock.patch("tools.validation_build_impact._git")
    def test_discovery_uses_merge_base_head_index_and_worktree(self, git, snapshot, worktree):
        old = validation_build_impact.Snapshot("100644", self.original)
        new = validation_build_impact.Snapshot(
            "100644", timeout_edit(self.original, "test-qemu-panic", 12)
        )
        git.return_value = "fixed-merge-base\n"
        snapshot.side_effect = (old, old, new)
        worktree.return_value = new
        self.assertEqual(("test-qemu-panic",), validation_build_impact.discover("/repo", "base"))
        git.assert_called_once_with("/repo", "merge-base", "base", "HEAD")
        self.assertEqual(
            [
                mock.call("/repo", "fixed-merge-base"),
                mock.call("/repo", "HEAD"),
                mock.call("/repo", None),
            ],
            snapshot.call_args_list,
        )
        worktree.assert_called_once_with("/repo")

    @mock.patch("tools.validation_build_impact.subprocess.run")
    def test_git_blob_reader_preserves_every_line_ending_byte(self, run):
        def completed(*arguments, **options):
            content = b"line\r\nnext\r\n"
            stdout = content.decode().replace("\r\n", "\n") if options.get("text") else content
            return mock.Mock(stdout=stdout)

        run.side_effect = completed
        self.assertEqual("line\r\nnext\r\n", validation_build_impact._git("/repo", "show", "blob"))

    def test_line_ending_changes_outside_body_stay_shared(self):
        edited = timeout_edit(self.original, "test-qemu-panic", 12).replace("\n", "\r\n")
        self.assertIsNone(validation_build_impact.classify(self.original, edited))


if __name__ == "__main__":
    unittest.main()
