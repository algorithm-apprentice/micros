#!/usr/bin/env -S python3.15 -E -S -B

"""Explicit host tiers with checked source views and live test progress."""

import argparse
from contextlib import contextmanager
from pathlib import Path
import re
import sys
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tests.host.evidence_authority_scope import checked_source_view
from tools.python_host_runtime import require_current_runtime


FAST_MODULES = (
    "test_check_docs", "test_check_elf_sections", "test_embed_ramfs_seed",
    "test_python_host_runtime", "test_stack_reports", "test_stack_observer_state",
    "test_validation_evidence", "test_evidence_authority_scope", "test_host_test_runner",
)


@contextmanager
def _host_imports(root):
    previous = sys.path[:]
    paths = (str(root / "tests/host"), str(root))
    sys.path[:] = [*paths, *(path for path in previous if path not in paths)]
    try:
        yield
    finally:
        sys.path[:] = previous


def test_cases(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from test_cases(item)
        else:
            yield item


def _validate_selection(suite):
    cases = tuple(test_cases(suite))
    if not cases:
        raise ValueError("host test selection is empty")
    names = tuple(case.id() for case in cases)
    if len(set(names)) != len(names):
        raise ValueError("host test selection contains duplicate identities")
    return cases


def _test_name(root, name):
    if not re.fullmatch(r"test_[A-Za-z0-9_]+(?:\.[A-Za-z_][A-Za-z0-9_]*)*", name):
        raise ValueError("focused names must identify repository host tests")
    path = root / "tests/host" / (name.split(".", 1)[0] + ".py")
    if not path.is_file() or path.resolve().parent != (root / "tests/host").resolve():
        raise ValueError("host test module is missing or outside its repository namespace")


def select_suite(root, *, tier=None, names=()):
    root = Path(root).resolve()
    if (tier is None) == (not names) or tier not in (None, "fast", "full"):
        raise ValueError("select exactly one host tier or focused test list")
    loader = unittest.TestLoader()
    with _host_imports(root):
        if tier == "full":
            suite = loader.discover(str(root / "tests/host"), pattern="test_*.py")
        else:
            selected = FAST_MODULES if tier == "fast" else names
            for name in selected:
                _test_name(root, name)
            suite = unittest.TestSuite()
            for name in selected:
                named_suite = loader.loadTestsFromName(name)
                _validate_selection(named_suite)
                suite.addTest(named_suite)
    if loader.errors:
        raise ValueError("host test discovery failed:\n" + "\n".join(loader.errors))
    _validate_selection(suite)
    return suite


class _Progress(unittest.TestResult):
    def __init__(self, total, stream, clock):
        super().__init__()
        self.total, self.stream, self.clock = total, stream, clock
        self.started, self.outcomes, self.completed = {}, {}, []

    def emit(self, message):
        self.stream.write(message + "\n")
        self.stream.flush()

    def startTest(self, test):
        super().startTest(test)
        self.started[id(test)] = self.clock()
        self.outcomes[id(test)] = "NOT_RUN"
        self.emit(f"HOST_TEST_START {self.testsRun}/{self.total} {test.id()}")

    def stopTest(self, test):
        elapsed = self.clock() - self.started.pop(id(test))
        outcome = self.outcomes.pop(id(test))
        self.completed.append((test.id(), outcome, elapsed))
        self.emit(f"HOST_TEST_END {self.testsRun}/{self.total} {test.id()} {outcome} seconds={elapsed:.3f}")
        super().stopTest(test)

    def addSuccess(self, test):
        super().addSuccess(test)
        self.outcomes[id(test)] = "PASS"

    def addFailure(self, test, error):
        super().addFailure(test, error)
        self.outcomes[id(test)] = "FAIL"

    def addError(self, test, error):
        super().addError(test, error)
        self.outcomes[id(test)] = "ERROR"

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self.outcomes[id(test)] = "SKIP"

    def addExpectedFailure(self, test, error):
        super().addExpectedFailure(test, error)
        self.outcomes[id(test)] = "EXPECTED_FAILURE"

    def addUnexpectedSuccess(self, test):
        super().addUnexpectedSuccess(test)
        self.outcomes[id(test)] = "UNEXPECTED_SUCCESS"

    def addSubTest(self, test, subtest, error):
        super().addSubTest(test, subtest, error)
        if error is not None:
            self.outcomes[id(test)] = "FAIL" if issubclass(error[0], test.failureException) else "ERROR"


def run_suite(suite, root, *, stream=None, clock=time.monotonic):
    stream = sys.stdout if stream is None else stream
    cases = _validate_selection(suite)
    result = _Progress(len(cases), stream, clock)
    started = clock()
    try:
        with _host_imports(Path(root).resolve()), checked_source_view(
            root, report=lambda phase: result.emit("HOST_SOURCE_VIEW " + phase),
        ):
            result.startTestRun()
            try:
                suite(result)
            finally:
                result.stopTestRun()
    except KeyboardInterrupt:
        result.emit("HOST_TEST_ABORT interrupted; no successful aggregate")
        return 130
    except Exception as error:
        result.emit(f"HOST_TEST_ERROR {type(error).__name__}: {error}")
        return 2
    for test, detail in (*result.failures, *result.errors, *result.expectedFailures):
        result.emit(f"\n{test.id()}\n{detail}")
    for name, outcome, elapsed in sorted(result.completed, key=lambda row: (-row[2], row[0]))[:10]:
        result.emit(f"HOST_TEST_SLOW {name} {outcome} seconds={elapsed:.3f}")
    passed = (
        result.wasSuccessful() and not result.skipped and not result.expectedFailures
        and result.testsRun == len(cases) and len(result.completed) == len(cases)
        and all(outcome == "PASS" for _, outcome, _ in result.completed)
    )
    result.emit(f"\nRan {result.testsRun} tests in {clock() - started:.3f}s")
    result.emit("\nOK" if passed else (
        f"\nFAILED (failures={len(result.failures)}, errors={len(result.errors)}, "
        f"skipped={len(result.skipped)}, not_run={len(cases) - result.testsRun})"
    ))
    return 0 if passed else 1


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--tier", choices=("fast", "full"), action="append")
    mode.add_argument("--test", action="append")
    parser.add_argument("--list", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.tier is not None and len(arguments.tier) != 1:
        parser.error("select the host tier only once")
    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)
    try:
        if Path.cwd().resolve() != ROOT:
            raise ValueError("run host tests from the documented repository root")
        require_current_runtime()
        tier = arguments.tier[0] if arguments.tier else None
        suite = select_suite(ROOT, tier=tier, names=tuple(arguments.test or ()))
        if arguments.list:
            for case in test_cases(suite):
                print(case.id())
            return 0
        return run_suite(suite, ROOT)
    except (OSError, ValueError, RuntimeError) as error:
        print("HOST_TEST_ERROR " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
