"""Hosted CI must bind supported tools and the event to one clean candidate."""

from contextlib import ExitStack
import copy
import os
from pathlib import Path
from unittest.mock import patch
import unittest

from tools.validation_context import Candidate
from tools.validation_inputs import UnsupportedContext


ROOT = Path(__file__).resolve().parents[2]


class CiEnvironmentTest(unittest.TestCase):
    def setUp(self):
        self.candidate = Candidate({"root": str(ROOT)}, {
            "head": "a" * 40, "tree": "c" * 40, "source_sha256": "d" * 64, "missing_paths": [],
        }, True)
        self.environment = {
            **os.environ,
            "GITHUB_ACTIONS": "true", "GITHUB_EVENT_NAME": "pull_request",
            "GITHUB_SHA": "a" * 40, "CI_BASE": "b" * 40,
            "GITHUB_RUN_ID": "42", "GITHUB_RUN_ATTEMPT": "1",
        }
        self.tools = {
            "compiler": {"version": "Homebrew clang version 23.1.4\n"},
            "linker": {"version": "LLD 23.1.4 (compatible with GNU linkers)\n"},
            "cmake": {"version": "cmake version 4.4.5\n"},
            "ninja": {"version": "1.13.2\n"},
            "llvm-nm": {"version": "LLVM version 23.1.4\n"},
            "llvm-readelf": {"version": "LLVM version 23.1.4\n"},
            "actionlint": {"version": "v1.7.12\n"},
        }

    def context(self):
        stack = ExitStack()
        stack.enter_context(patch("tools.ci_environment.require_current_runtime", return_value={"validated": True}))
        stack.enter_context(patch("tools.ci_environment.platform.system", return_value="Darwin"))
        stack.enter_context(patch("tools.ci_environment.platform.machine", return_value="arm64"))
        stack.enter_context(patch("tools.ci_environment.platform.mac_ver", return_value=("26.6.2", (), "")))
        stack.enter_context(patch("tools.ci_environment.capture_candidate", return_value=self.candidate))
        stack.enter_context(patch("tools.ci_environment.collect_tools", return_value=self.tools))
        stack.enter_context(patch("tools.ci_environment.shutil.which", return_value="/fixture/qemu"))
        stack.enter_context(patch("tools.ci_environment.resolve_default_firmware", return_value={"pinned": True}))
        stack.enter_context(patch("tools.ci_environment._run", side_effect=lambda argv, **_: (
            b"false\n" if "--is-shallow-repository" in argv
            else b"" if "cat-file" in argv else ("b" * 40 + "\n").encode()
        )))
        return stack

    def test_preflight_binds_event_source_history_base_and_existing_firmware_owner(self):
        from tools.ci_environment import collect_environment

        with self.context():
            record = collect_environment(ROOT, self.environment)
        self.assertEqual(self.candidate.source, record["candidate"])
        self.assertEqual("b" * 40, record["base"])
        self.assertEqual({"pinned": True}, record["firmware"])
        self.assertEqual(("42", "1"), (record["event"]["run_id"], record["event"]["attempt"]))

    def test_homebrew_linker_banner_retains_the_same_major_version_contract(self):
        from tools.ci_environment import require_tool_versions

        self.tools["linker"]["version"] = "Homebrew LLD 23.1.4 (compatible with GNU linkers)\n"
        require_tool_versions(self.tools)

    def test_wrong_host_event_dirty_source_and_malformed_base_fail_before_tools(self):
        from tools.ci_environment import CiEnvironmentError, collect_environment

        for mutation in ("host", "architecture", "os", "event", "sha", "base", "zero-base", "run", "dirty"):
            with self.subTest(mutation=mutation), self.context(), ExitStack() as changes:
                environment = dict(self.environment)
                if mutation == "host":
                    changes.enter_context(patch("tools.ci_environment.platform.system", return_value="Linux"))
                elif mutation == "architecture":
                    changes.enter_context(patch("tools.ci_environment.platform.machine", return_value="x86_64"))
                elif mutation == "os":
                    changes.enter_context(patch("tools.ci_environment.platform.mac_ver", return_value=("27.0", (), "")))
                elif mutation == "event":
                    environment["GITHUB_EVENT_NAME"] = "pull_request_target"
                elif mutation == "sha":
                    environment["GITHUB_SHA"] = "e" * 40
                elif mutation == "base":
                    environment["CI_BASE"] = "--help"
                elif mutation == "zero-base":
                    environment["CI_BASE"] = "0" * 40
                elif mutation == "run":
                    environment["GITHUB_RUN_ID"] = "not-a-run"
                else:
                    changes.enter_context(patch(
                        "tools.ci_environment.capture_candidate",
                        return_value=Candidate({}, self.candidate.source, False),
                    ))
                with patch("tools.ci_environment.collect_tools") as tools, self.assertRaises(CiEnvironmentError):
                    collect_environment(ROOT, environment)
                tools.assert_not_called()

    def test_missing_history_and_unsupported_compiler_cannot_reach_firmware_introspection(self):
        from tools.ci_environment import CiEnvironmentError, collect_environment

        for mutation in ("shallow", "history", "compiler", "linker"):
            with self.subTest(mutation=mutation), self.context(), ExitStack() as changes:
                if mutation == "shallow":
                    changes.enter_context(patch("tools.ci_environment._run", return_value=b"true\n"))
                elif mutation == "history":
                    changes.enter_context(patch("tools.ci_environment._run", side_effect=UnsupportedContext("missing history")))
                else:
                    tools = copy.deepcopy(self.tools)
                    tools[mutation]["version"] = tools[mutation]["version"].replace("23.1.4", "24.0.0")
                    changes.enter_context(patch("tools.ci_environment.collect_tools", return_value=tools))
                with (
                    patch("tools.ci_environment.resolve_default_firmware") as firmware,
                    self.assertRaises((CiEnvironmentError, UnsupportedContext)),
                ):
                    collect_environment(ROOT, self.environment)
                firmware.assert_not_called()

    def test_source_drift_and_firmware_rejection_cannot_publish_a_preflight_record(self):
        from tools.ci_environment import CiEnvironmentError, collect_environment
        from tools.stack_bound_types import StackEvidenceError

        with self.context(), patch(
            "tools.ci_environment.resolve_default_firmware", side_effect=StackEvidenceError("unbound firmware"),
        ), self.assertRaises(StackEvidenceError):
            collect_environment(ROOT, self.environment)
        changed = Candidate({}, {**self.candidate.source, "tree": "e" * 40}, True)
        with self.context(), patch(
            "tools.ci_environment.capture_candidate", side_effect=(self.candidate, changed),
        ), self.assertRaises(CiEnvironmentError):
            collect_environment(ROOT, self.environment)


if __name__ == "__main__":
    unittest.main()
