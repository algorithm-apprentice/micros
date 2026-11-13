"""Current QEMU runner ownership, public records and discovery contracts."""

import ast
from dataclasses import FrozenInstanceError
import importlib
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest

from tests.host.source_snapshot import source_bytes
from tools import validation_plan
from tools.python_host_runtime import python_command


ROOT = Path(__file__).resolve().parents[2]
RUNNER_PATH = "tools/run_qemu_smoke.py"
RECORD_EXPORTS = (
    "PANIC_MARKER", "FAILURE_MARKER", "TRAP_READY_MARKER", "TRAP_TEST_MARKER", "TRAP_TEST_PASS",
    "TIMER_TEST_MARKER", "TIMER_TEST_PASS", "FRAME_ALLOCATOR_READY_MARKER", "FRAME_ALLOCATOR_TEST_MARKER",
    "FRAME_ALLOCATOR_READY_PATTERN", "FRAME_ALLOCATOR_TEST_PASS", "MMU_READY_MARKER", "MMU_TEST_MARKER",
    "MMU_READY_PATTERN", "MMU_TEST_PASS", "FRAME_OWNERSHIP_READY_MARKER", "FRAME_OWNERSHIP_TEST_MARKER",
    "FRAME_OWNERSHIP_READY_PATTERN", "FRAME_OWNERSHIP_TEST_PASS", "USER_ADDRESS_SPACE_TEST_MARKER",
    "USER_ADDRESS_SPACE_TEST_PASS", "USER_EXECUTION_TEST_MARKER", "USER_EXECUTION_TEST_PASS",
    "SCHEDULER_TEST_MARKER", "SCHEDULER_TEST_PASS", "ENDPOINT_TEST_MARKER", "ENDPOINT_TEST_PASS",
    "IPC_TEST_MARKER", "IPC_ADDRESS_SPACES_MARKER", "IPC_ADDRESS_SPACES_PASS", "IPC_TEST_PASS",
    "OBJECTS_READY_MARKER", "OBJECT_MODEL_TEST_MARKER", "NESTED_TRAP_TEST_MARKER",
    "OBJECTS_READY_PATTERN", "OBJECT_MODEL_TEST_PASS", "NESTED_TRAP_TEST_PASS",
    "FDT_COUNTS_PATTERN", "FDT_EVENT_NAMES", "FDT_RANGE_PATTERN", "PANIC_CORE_PATTERNS",
    "TRAP_CONTEXT_PATTERN", "VM_SELF_FAULT_PATTERN", "QemuResult", "SmokeOutcome", "_split_output_records",
)
OWNER_IMPORTS = {
    "qemu_smoke_records": {"enum", "re", "dataclasses", "typing"},
    "qemu_smoke_platform": {"tools.qemu_smoke_records"},
    "qemu_smoke_objects": {"tools.qemu_smoke_records", "tools.qemu_smoke_platform"},
    "qemu_smoke_faults": {"re", "pathlib", "tools.qemu_smoke_records"},
    "qemu_smoke_command": {"subprocess"},
    "qemu_smoke_execution": {"os", "selectors", "subprocess", "time", "tools.qemu_smoke_records"},
    "qemu_smoke_outcome": {
        "re", "shlex", "tools.qemu_smoke_records", "tools.qemu_smoke_platform",
        "tools.qemu_smoke_objects", "tools.qemu_smoke_faults",
    },
    "qemu_smoke_arguments": {"argparse", "os", "re", "pathlib"},
    "qemu_smoke_cli": {
        "sys", "pathlib", "tools.stack_artifact_files", "tools.qemu_smoke_records",
        "tools.qemu_smoke_platform", "tools.qemu_smoke_command", "tools.qemu_smoke_execution",
        "tools.qemu_smoke_outcome", "tools.qemu_smoke_arguments",
        "tests.host.kernel_mapping_evidence", "tests.host.common_scheduler_return_evidence",
        "tests.host.selected_release_handoff_evidence", "tests.host.work_count_types",
        "tests.host.runtime_memory_evidence", "tests.host.diagnostic_history_evidence",
        "tests.host.selected_ipc_validation_evidence",
        "tests.host.bootstrap_command_evidence", "tests.host.bootstrap_command_source",
        "tests.host.indexed_bootstrap_evidence",
        "tools.stack_bound_manifest",
        "tools.qemu_smoke_faults",
    },
}
CASE_OWNERS = {
    "qemu_smoke_classification_cases": ("SmokeClassificationCases",),
    "qemu_smoke_panic_cases": ("PanicReportCases",),
    "qemu_smoke_platform_cases": ("PlatformReportCases",),
    "qemu_smoke_memory_cases": ("MemoryReportCases",),
    "qemu_smoke_object_cases": ("ObjectReportCases",),
    "qemu_smoke_execution_cases": ("NmSymbolCases", "QemuCommandCases", "QemuSerialInputCases", "TapOutputCases"),
}


def check_imports(owner, source):
    for node in ast.walk(ast.parse(source)):
        if isinstance(node, ast.Import):
            names = {alias.name for alias in node.names}
        elif isinstance(node, ast.ImportFrom):
            if node.level or any(alias.name == "*" for alias in node.names):
                raise AssertionError("runner imports must have explicit package ownership")
            names = {node.module}
        else:
            continue
        if not names <= OWNER_IMPORTS[owner]:
            raise AssertionError(f"runner dependency crosses its responsibility: {owner}: {names}")


class QemuRunnerSourceLayoutTest(unittest.TestCase):
    def test_direct_cli_help_preserves_import_bootstrap_outside_repository(self):
        with TemporaryDirectory() as directory:
            result = subprocess.run(
                python_command(sys.executable, str(ROOT / RUNNER_PATH), "--help"),
                cwd=directory, capture_output=True, text=True, timeout=10,
            )
        self.assertEqual((0, ""), (result.returncode, result.stderr))
        self.assertIn("Run the deterministic micros QEMU boot smoke test.", result.stdout)

    def test_current_private_imports_preserve_responsibility_direction(self):
        for owner in OWNER_IMPORTS:
            with self.subTest(owner=owner):
                check_imports(owner, source_bytes(ROOT, ROOT / "tools" / (owner + ".py")).decode())

    def test_private_imports_reject_backedges_and_execution_in_report_owners(self):
        for owner, source in (
            ("qemu_smoke_records", "from tools.qemu_smoke_cli import main"),
            ("qemu_smoke_faults", "import subprocess"),
            ("qemu_smoke_platform", "from tools.run_qemu_smoke import QemuResult"),
            ("qemu_smoke_objects", "from tools.qemu_smoke_records import *"),
        ):
            with self.subTest(owner=owner), self.assertRaises(AssertionError):
                check_imports(owner, source)

    def test_facade_reexports_canonical_records_and_immutable_result(self):
        from tools import qemu_smoke_records, run_qemu_smoke

        for name in RECORD_EXPORTS:
            self.assertIs(getattr(qemu_smoke_records, name), getattr(run_qemu_smoke, name), name)
        value = run_qemu_smoke.QemuResult("exact", 0, False)
        self.assertFalse(value.serial_input_sent)
        self.assertEqual(("output", "return_code", "timed_out", "serial_input_sent"), tuple(value.__dataclass_fields__))
        with self.assertRaises(FrozenInstanceError):
            value.output = "changed"
        self.assertEqual(
            ("pass", "failure", "panic", "unexpected-exit", "timeout"),
            tuple(value.value for value in run_qemu_smoke.SmokeOutcome),
        )

    def test_case_mixins_have_one_discovery_owner(self):
        aggregate = importlib.import_module("tests.host.test_run_qemu_smoke")
        for owner, classes in CASE_OWNERS.items():
            module = importlib.import_module("tests.host." + owner)
            for name in classes:
                case = getattr(module, name)
                self.assertFalse(issubclass(case, unittest.TestCase))
                owners = {
                    value for value in vars(aggregate).values()
                    if isinstance(value, type) and issubclass(value, unittest.TestCase) and case in value.__mro__
                }
                self.assertEqual(1, len(owners), name)

    def test_current_runner_paths_keep_complete_validation_ownership(self):
        for owner in OWNER_IMPORTS:
            path = "tools/" + owner + ".py"
            for tier in ("fast", "pr", "full"):
                self.assertEqual(
                    validation_plan.plan([RUNNER_PATH], tier, "main"),
                    validation_plan.plan([path], tier, "main"), path,
                )
        self.assertIn("tests/host/test_qemu_runner_source_layout.py", validation_plan.HOST_UNIT_ONLY_INPUTS)


if __name__ == "__main__":
    unittest.main()
