"""CI binds committed source, hosted identity and role-specific tool authority."""

from contextlib import ExitStack
from pathlib import Path
import os
import sys
import unittest
from unittest.mock import patch

from tools.validation_context import Candidate
from tools.validation_inputs import UnsupportedContext


ROOT = Path(__file__).resolve().parents[2]


class CiEnvironmentTest(unittest.TestCase):
    def setUp(self):
        self.candidate = Candidate({"root": str(ROOT)}, {
            "head": "a" * 40, "tree": "c" * 40, "source_sha256": "d" * 64, "missing_paths": [],
        }, True)
        self.environment = {
            **os.environ, "GITHUB_ACTIONS": "true", "GITHUB_EVENT_NAME": "pull_request",
            "GITHUB_SHA": "a" * 40, "CI_BASE": "b" * 40, "CI_PART": "target-1",
            "GITHUB_RUN_ID": "42", "GITHUB_RUN_ATTEMPT": "1",
            "GITHUB_REPOSITORY": "algorithm-apprentice/micros",
            "GITHUB_WORKFLOW_REF": "algorithm-apprentice/micros/.github/workflows/ci.yml@refs/pull/1/merge",
            "GITHUB_WORKFLOW_SHA": "a" * 40,
        }
        self.environment.pop("CTEST_PARALLEL_LEVEL", None)

    def git(self, argv, **_):
        if "--is-shallow-repository" in argv:
            return b"false\n"
        if "cat-file" in argv:
            return b""
        if "show" in argv:
            return b"workflow"
        if "config" in argv:
            return b"https://github.com/algorithm-apprentice/micros\n"
        return (("c" if "HEAD^{tree}" in argv else "b") * 40 + "\n").encode()

    def context(self):
        stack = ExitStack()
        stack.enter_context(patch("tools.ci_environment.require_current_runtime", return_value={"checked": True}))
        stack.enter_context(patch("tools.ci_environment.platform.system", return_value="Darwin"))
        stack.enter_context(patch("tools.ci_environment.platform.machine", return_value="arm64"))
        stack.enter_context(patch("tools.ci_environment.platform.mac_ver", return_value=("26.6.2", (), "")))
        stack.enter_context(patch("tools.ci_environment.capture_candidate", return_value=self.candidate))
        stack.enter_context(patch("tools.ci_environment.read_regular", return_value=(b"workflow", {})))
        stack.enter_context(patch("tools.ci_environment.collect_tools", return_value={"checked": True}))
        stack.enter_context(patch("tools.ci_environment.require_tool_versions"))
        stack.enter_context(patch("tools.ci_environment.shutil.which", return_value="/fixture/qemu"))
        stack.enter_context(patch("tools.ci_environment.resolve_default_firmware", return_value={"pinned": True}))
        stack.enter_context(patch("tools.ci_environment._run", side_effect=self.git))
        return stack

    def test_producer_binds_event_committed_tree_workflow_and_original_context(self):
        from tools.ci_environment import collect_environment

        with self.context():
            record = collect_environment(ROOT, self.environment)
        self.assertEqual(self.candidate.source, record["candidate"])
        self.assertEqual(self.candidate.repository, record["repository_context"])
        self.assertEqual("b" * 40, record["base"])
        self.assertEqual({"pinned": True}, record["firmware"])
        self.assertEqual("target-1", record["part"])

    def test_dirty_or_staged_replacement_cannot_borrow_the_event_commit(self):
        from tools.ci_environment import collect_environment

        for candidate in (
            Candidate({}, self.candidate.source, False),
            Candidate({}, {**self.candidate.source, "tree": "e" * 40}, True),
            Candidate({}, {**self.candidate.source, "head": "e" * 40}, True),
        ):
            with self.context(), patch("tools.ci_environment.capture_candidate", return_value=candidate):
                with patch("tools.ci_environment.collect_tools") as tools, self.assertRaises(ValueError):
                    collect_environment(ROOT, self.environment)
                tools.assert_not_called()

    def test_unknown_role_run_base_host_and_missing_history_fail_closed(self):
        from tools.ci_environment import collect_environment

        for key, value in (
            ("CI_PART", "subset"), ("GITHUB_EVENT_NAME", "pull_request_target"),
            ("GITHUB_RUN_ID", "00"), ("CI_BASE", "--help"), ("CI_BASE", "0" * 40),
            ("GITHUB_WORKFLOW_REF", "foreign/repo/.github/workflows/ci.yml@main"),
        ):
            with self.subTest(key=key, value=value), self.context(), self.assertRaises(ValueError):
                collect_environment(ROOT, {**self.environment, key: value})
        for target, value in (("system", "Linux"), ("machine", "x86_64")):
            with self.context(), patch("tools.ci_environment.platform." + target, return_value=value):
                with self.assertRaises(ValueError):
                    collect_environment(ROOT, self.environment)
        with self.context(), patch("tools.ci_environment._run", return_value=b"true\n"):
            with self.assertRaises(ValueError):
                collect_environment(ROOT, self.environment)
        with self.context(), patch("tools.ci_environment._run", side_effect=UnsupportedContext("missing history")):
            with self.assertRaises(UnsupportedContext):
                collect_environment(ROOT, self.environment)

    def test_collector_has_no_target_tool_or_firmware_execution_authority(self):
        from tools.ci_environment import collect_environment

        with self.context(), patch("tools.ci_environment.resolve_default_firmware") as firmware:
            record = collect_environment(ROOT, {**self.environment, "CI_PART": "collector"}, role="collector")
        self.assertIsNone(record["firmware"])
        firmware.assert_not_called()

    def test_native_concurrency_is_explicit_and_source_drift_prevents_publication(self):
        from tools.ci_environment import collect_environment

        native = {**self.environment, "CI_PART": "native"}
        with self.context(), self.assertRaises(ValueError):
            collect_environment(ROOT, native)
        with self.context():
            collect_environment(ROOT, {**native, "CTEST_PARALLEL_LEVEL": "3"})
        changed = Candidate({}, {**self.candidate.source, "tree": "e" * 40}, True)
        with self.context(), patch("tools.ci_environment.capture_candidate", side_effect=(self.candidate, changed)):
            with self.assertRaises(ValueError):
                collect_environment(ROOT, self.environment)

    def test_supported_tool_families_include_homebrew_lld_but_not_another_major(self):
        from tools.ci_environment import require_tool_versions

        versions = {
            "compiler": "Homebrew clang version 23.1.4\n",
            "linker": "Homebrew LLD 23.1.4 (compatible with GNU linkers)\n",
            "cmake": "cmake version 4.4.5\n",
            "ninja": "1.13.2\n",
            "llvm-nm": "llvm-nm, compatible with GNU nm\nHomebrew LLVM version 23.1.4\n",
            "llvm-readelf": "Homebrew LLVM version 23.1.4\n",
            "actionlint": "1.7.12\n",
        }
        tools = {name: {"version": value} for name, value in versions.items()}
        require_tool_versions(tools)
        for name, version in (
            ("compiler", "Homebrew clang version 22.0.0"),
            ("linker", "Homebrew LLD 22.0.0"), ("cmake", "cmake version 3.24.9"),
            ("ninja", "unknown"), ("llvm-nm", "Homebrew LLVM version 22.0.0"),
            ("llvm-readelf", "Homebrew LLVM version 22.0.0"), ("actionlint", ""),
        ):
            with self.subTest(name=name), self.assertRaises((ValueError, UnsupportedContext)):
                require_tool_versions({**tools, name: {"version": version}})

    def test_replaced_executable_cannot_publish_its_old_tool_identity(self):
        from tools.ci_environment import tool_record

        with (
            patch("tools.ci_environment._file_identity", side_effect=({"sha256": "before"}, {"sha256": "after"})),
            patch("tools.ci_environment._run", return_value=b"version"),
            self.assertRaises(ValueError),
        ):
            tool_record(str(Path(sys.executable).resolve()))


if __name__ == "__main__":
    unittest.main()
