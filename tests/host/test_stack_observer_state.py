"""Native generation/retention rules execute the actual observer C helper."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class StackObserverStateTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="micros-observer-state-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.executable = Path(cls.temporary.name) / "observer-state"
        compiler = shutil.which("cc")
        if compiler is None:
            raise AssertionError("native C compiler is unavailable")
        command = [
            compiler, "-std=c17", "-Wall", "-Wextra", "-Werror",
            "-DMICROS_STACK_OBSERVER_KERNEL=1", "-I" + str(ROOT), "-I" + str(ROOT / "include"),
            str(ROOT / "tests/stack/observer.c"),
            str(ROOT / "tests/host/stack_observer_fixture.c"), "-o", str(cls.executable),
        ]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_case(self, case):
        result = subprocess.run(
            [str(self.executable), case], capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        return result.stdout

    def test_reuse_keeps_old_and_new_generations_as_separate_immutable_records(self):
        output = self.run_case("reuse")
        self.assertIn("generation=0x0000000000000001 samples=0x0000000000000001 use=0x0000000000000200", output)
        self.assertIn("generation=0x0000000000000002 samples=0x0000000000000002 use=0x0000000000000300", output)
        self.assertEqual(2, output.count("MICROS_STACK_OBSERVATION"))

    def test_counter_failure_is_sticky_even_when_wrapped_samples_are_zero(self):
        self.run_case("sticky")


if __name__ == "__main__":
    unittest.main()
