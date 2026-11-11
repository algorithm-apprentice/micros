"""Host selection and progress contracts from ADR-0105."""

from contextlib import contextmanager, redirect_stdout
import io
from pathlib import Path
import re
import shlex
import unittest
from unittest import mock
from types import SimpleNamespace

from tests.host.build_source_records import cmake_command


ROOT = Path(__file__).resolve().parents[2]
FAST_MODULES = (
    "test_check_docs", "test_check_elf_sections", "test_embed_ramfs_seed",
    "test_python_host_runtime", "test_stack_reports", "test_stack_observer_state",
    "test_validation_evidence", "test_source_snapshot", "test_host_test_runner",
)


def example_case(identity, action=lambda: None):
    class Example(unittest.TestCase):
        def id(self):
            return identity

        def runTest(self):
            action()

    return Example()


class NativeTierSelectionTest(unittest.TestCase):
    def test_native_tiers_do_not_dispatch_identical_python_discovery(self):
        source = (ROOT / "CMakeLists.txt").read_text()
        selections = []
        for name in ("test-unit-fast", "test-unit"):
            start = re.search(r"\badd_custom_target\s*\(\s*" + re.escape(name) + r"(?=\s|\))", source)
            self.assertIsNotNone(start, name)
            target = cmake_command(source[start.start():], "add_custom_target", name)
            command = re.search(
                r'COMMAND\s+"\$\{MICROS_PYTHON\}"\s+(.*?)'
                r"(?=\n\s*(?:COMMAND|DEPENDS|WORKING_DIRECTORY|USES_TERMINAL)\b)",
                target, re.DOTALL,
            )
            self.assertIsNotNone(command, name)
            selections.append(tuple(shlex.split(command.group(1))))
        self.assertNotEqual(
            *selections, "fast and full must not both execute complete Python discovery",
        )

    def test_partial_mixed_or_changed_native_invocations_are_rejected(self):
        from tests.host.work_count_recipe import validate_host_test_commands

        source = (ROOT / "CMakeLists.txt").read_text()
        validate_host_test_commands(source)
        mutations = (
            source.replace("tools/run_host_tests.py", "tools/other.py", 1),
            source.replace("--tier\n            fast", "--tier\n            full", 1),
            source.replace("--tier\n            fast", "--tier\n            fast\n            --list", 1),
            source + cmake_command(source, "add_custom_target", "test-unit-fast"),
        )
        for changed in mutations:
            with self.subTest(source=changed[-60:]), self.assertRaises(AssertionError):
                validate_host_test_commands(changed)


class HostTestRunnerTest(unittest.TestCase):
    def setUp(self):
        from tools import run_host_tests

        self.runner = run_host_tests
        self.patches = []
        self.addCleanup(self.stop_patches)

    def patch(self, name, **options):
        patcher = mock.patch.object(self.runner, name, **options)
        self.patches.append(patcher)
        return patcher.start()

    def stop_patches(self):
        for patcher in reversed(self.patches):
            patcher.stop()

    def loader(self, cases):
        return SimpleNamespace(
            errors=[],
            discover=mock.Mock(return_value=unittest.TestSuite(cases)),
            loadTestsFromName=mock.Mock(side_effect=lambda name: unittest.TestSuite([
                example_case(name + ".Case.test_one"),
            ])),
        )

    def test_actual_runner_owns_current_bytes_without_a_retirement_view(self):
        from tests.host import source_snapshot

        def body():
            self.assertIsNotNone(source_snapshot._active, "runner did not own a current source snapshot")
            self.assertEqual(
                (ROOT / "README.md").read_bytes(),
                source_snapshot.source_bytes(ROOT, ROOT / "README.md"),
            )

        output = io.StringIO()
        with source_snapshot.isolated_source_view():
            status = self.runner.run_suite(unittest.TestSuite([example_case("current_source", body)]), ROOT, stream=output)
        self.assertEqual(0, status, output.getvalue())

    def test_fast_loads_only_the_closed_unit_modules(self):
        loader = self.loader([])
        with mock.patch.object(self.runner.unittest, "TestLoader", return_value=loader):
            suite = self.runner.select_suite(ROOT, tier="fast")
        self.assertEqual(
            [name + ".Case.test_one" for name in FAST_MODULES],
            [case.id() for case in self.runner.test_cases(suite)],
        )
        self.assertEqual([mock.call(name) for name in FAST_MODULES], loader.loadTestsFromName.call_args_list)
        loader.discover.assert_not_called()

    def test_full_keeps_discovery_order_and_does_not_filter_new_modules(self):
        cases = [example_case("test_new.Z.test_one"), example_case("test_existing.A.test_two")]
        loader = self.loader(cases)
        with mock.patch.object(self.runner.unittest, "TestLoader", return_value=loader):
            suite = self.runner.select_suite(ROOT, tier="full")
        self.assertEqual(cases, list(self.runner.test_cases(suite)))
        loader.discover.assert_called_once_with(str(ROOT / "tests/host"), pattern="test_*.py")
        loader.loadTestsFromName.assert_not_called()

    def test_empty_duplicate_and_failed_discovery_are_not_usable_selections(self):
        for cases, errors in (
            ([], []),
            ([example_case("duplicate"), example_case("duplicate")], []),
            ([example_case("test_one")], ["import failed"]),
        ):
            loader = self.loader(cases)
            loader.errors = errors
            with self.subTest(cases=len(cases), errors=errors):
                with mock.patch.object(self.runner.unittest, "TestLoader", return_value=loader):
                    with self.assertRaises(ValueError):
                        self.runner.select_suite(ROOT, tier="full")

    def test_each_named_selection_must_supply_runnable_tests(self):
        for selection in (
            {"tier": "fast"},
            {"names": ("test_check_docs", "test_stack_reports")},
        ):
            loader = self.loader([])
            loader.loadTestsFromName.side_effect = lambda name: unittest.TestSuite(
                [] if name == "test_check_docs" else [example_case(name + ".Case.test_one")]
            )
            with self.subTest(selection=selection):
                with mock.patch.object(self.runner.unittest, "TestLoader", return_value=loader):
                    with self.assertRaisesRegex(ValueError, "empty"):
                        self.runner.select_suite(ROOT, **selection)

    def test_focused_selection_cannot_import_outside_repository_tests(self):
        loader = self.loader([])
        with mock.patch.object(self.runner.unittest, "TestLoader", return_value=loader):
            for name in ("os.system", "../test_check_docs", "test_check_docs:main", "test_missing"):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    self.runner.select_suite(ROOT, names=(name,))
        loader.loadTestsFromName.assert_not_called()

    def test_listing_does_not_run_tests_or_open_a_source_view(self):
        suite = unittest.TestSuite([example_case("test_one", lambda: self.fail("test ran while listing"))])
        self.patch("require_current_runtime", return_value={})
        self.patch("select_suite", return_value=suite)
        view = self.patch("checked_source_view", side_effect=AssertionError("listing opened source proof"))
        output = io.StringIO()
        with redirect_stdout(output):
            self.assertEqual(0, self.runner.main(["--tier", "full", "--list"]))
        self.assertIn("test_one", output.getvalue())
        view.assert_not_called()

    def test_progress_precedes_test_work_and_success_follows_view_close(self):
        output = io.StringIO()
        now = {"value": 0.0}

        def body():
            self.assertIn("HOST_TEST_START 1/1 test_one", output.getvalue())
            self.assertNotIn("\nOK\n", output.getvalue())
            now["value"] = 3.5

        @contextmanager
        def view(root, **options):
            yield
            self.assertNotIn("\nOK\n", output.getvalue())
            output.write("VIEW_CLOSED\n")

        self.patch("checked_source_view", side_effect=view)
        status = self.runner.run_suite(
            unittest.TestSuite([example_case("test_one", body)]),
            ROOT, stream=output, clock=lambda: now["value"],
        )
        self.assertEqual(0, status)
        self.assertIn("seconds=3.500", output.getvalue())
        self.assertLess(output.getvalue().index("VIEW_CLOSED"), output.getvalue().index("\nOK\n"))

    def test_failed_scope_close_cannot_publish_an_ok_summary(self):
        @contextmanager
        def view(root, **options):
            yield
            raise ValueError("tool input changed")

        self.patch("checked_source_view", side_effect=view)
        output = io.StringIO()
        status = self.runner.run_suite(
            unittest.TestSuite([example_case("test_one")]), ROOT, stream=output,
        )
        self.assertNotEqual(0, status)
        self.assertNotIn("\nOK\n", output.getvalue())
        self.assertIn("tool input changed", output.getvalue())

    def test_skipped_selected_test_is_not_a_passing_aggregate(self):
        @contextmanager
        def view(root, **options):
            yield

        def skip():
            raise unittest.SkipTest("required case was not executed")

        self.patch("checked_source_view", side_effect=view)
        output = io.StringIO()
        self.assertNotEqual(0, self.runner.run_suite(
            unittest.TestSuite([example_case("test_one", skip)]), ROOT, stream=output,
        ))
        self.assertNotIn("\nOK\n", output.getvalue())

    def test_test_fixture_cleanup_and_interrupt_failures_cannot_publish_success(self):
        @contextmanager
        def view(root, **options):
            try:
                yield
            finally:
                released.append(True)

        def fail(error):
            raise error

        self.patch("checked_source_view", side_effect=view)
        for phase in ("assertion", "error", "setup", "cleanup", "interrupt"):
            with self.subTest(phase=phase):
                released = []
                error = (
                    KeyboardInterrupt() if phase == "interrupt"
                    else AssertionError("case failed") if phase == "assertion"
                    else RuntimeError("case failed")
                )
                action = lambda: fail(error)
                case = example_case("test_one", action if phase not in ("setup", "cleanup") else lambda: None)
                if phase == "setup":
                    case.setUp = action
                elif phase == "cleanup":
                    case.addCleanup(action)
                output = io.StringIO()
                status = self.runner.run_suite(unittest.TestSuite([case]), ROOT, stream=output)
                self.assertEqual(130 if phase == "interrupt" else 1, status)
                self.assertEqual([True], released)
                self.assertNotIn("\nOK\n", output.getvalue())
