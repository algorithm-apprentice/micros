from contextlib import redirect_stderr, redirect_stdout
import io
import importlib
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from tools import qemu_smoke_cli
from tools.qemu_smoke_records import QemuResult
from tests.host import test_common_scheduler_return as common_fixture
from tests.host import test_kernel_mapping as mapping_fixture
from tests.host import test_selected_release_handoff as release_fixture
from tests.host import test_selected_ipc_validation as ipc_fixture
from tests.host.common_scheduler_return_evidence import TARGET_MARKERS
from tests.host.work_count_types import Stage
from tests.host.build_source_records import cmake_command, cmake_commands


class CurrentTargetEvidenceTest(unittest.TestCase):
    def test_platform_controllers_have_only_current_execution(self):
        for owner in ("tools.check_terminal_failures", "tools.check_panic_traces"):
            controller = importlib.import_module(owner)
            with self.subTest(owner=owner), patch.object(controller, "execute", return_value=0) as execute:
                self.assertEqual(0, controller.main([]))
                execute.assert_called_once_with()
            with self.subTest(owner=owner), patch.object(controller, "execute") as execute:
                with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    controller.main(["--baseline"])
                execute.assert_not_called()

    def test_cmake_witness_owners_cannot_borrow_prefixed_target_names(self):
        prefix = "add_custom_target(test-qemu-panic-trace COMMAND unrelated)"
        exact = "add_custom_target(test-qemu-panic COMMAND required)"
        self.assertEqual(exact, cmake_command(prefix + "\n" + exact, "add_custom_target", "test-qemu-panic"))
        self.assertEqual((exact,), cmake_commands(prefix + "\n" + exact, "add_custom_target", "test-qemu-panic"))
        with self.assertRaises(AssertionError):
            cmake_command(prefix, "add_custom_target", "test-qemu-panic")

    def test_required_workflows_select_their_current_witness_contract(self):
        source = (Path(__file__).resolve().parents[2] / "CMakeLists.txt").read_text()
        for target, scope in (
            ("test-qemu-mmu", "kernel-mapping"),
            ("test-qemu-user-address-space", "user-address-space"),
            ("test-qemu-address-space-handoff", "address-space-handoff"),
            ("test-qemu-scheduler", "scheduler-return"),
            ("test-qemu-user-runtime", "runtime-memory"),
            ("test-qemu-panic", "diagnostic-history"),
            ("test-qemu-ipc-syscall", "selected-ipc"),
        ):
            command = cmake_command(source, "add_custom_target", target).split()
            with self.subTest(target=target):
                self.assertEqual(1, command.count("--require-current-evidence"))
                self.assertEqual(scope, command[command.index("--require-current-evidence") + 1])

    def test_runtime_scope_checks_the_linked_image_from_the_owned_build(self):
        root = Path(__file__).resolve().parents[2]
        kernel = root / "build/riscv64-user-runtime-test/kernel/micros.elf"
        result = QemuResult("current runtime passed\n", 0, False)
        with patch("tests.host.runtime_memory_evidence.prove_linked_runtime") as prove:
            qemu_smoke_cli._validate_current_evidence("runtime-memory", result, kernel)
            prove.assert_called_once_with(root, directory=kernel.parent.parent)
        with patch(
            "tests.host.runtime_memory_evidence.prove_linked_runtime",
            side_effect=ValueError("linked runtime changed"),
        ), self.assertRaisesRegex(ValueError, "linked runtime changed"):
            qemu_smoke_cli._validate_current_evidence("runtime-memory", result, kernel)

    def test_diagnostic_storage_binds_unique_aligned_bss_and_image_identity(self):
        from tests.host import diagnostic_history_evidence as evidence

        data = b"owned ELF fixture"
        section = SimpleNamespace(address=0x1000, size=12012, section_type=8, flags=3)
        symbol = SimpleNamespace(
            name="diagnostic_history", value=0x1000, size=12012,
            binding=0, symbol_type=1, section_index=1,
        )
        image = SimpleNamespace(data=data, sections=(None, section), symbols=(symbol,))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "kernel.elf"
            path.write_bytes(data)
            with patch.object(evidence, "load_elf", return_value=image):
                evidence.validate_history_storage(path)
                for symbols in (
                    (), (symbol, symbol),
                    (SimpleNamespace(**{**vars(symbol), "size": 12008}),),
                    (SimpleNamespace(**{**vars(symbol), "value": 0x1001}),),
                    (SimpleNamespace(**{**vars(symbol), "binding": 1}),),
                ):
                    image.symbols = symbols
                    with self.subTest(symbols=symbols), self.assertRaises(ValueError):
                        evidence.validate_history_storage(path)
                image.symbols = (symbol,)
                image.data = b"foreign image"
                with self.assertRaisesRegex(ValueError, "changed"):
                    evidence.validate_history_storage(path)

    def test_diagnostic_scope_requires_panic_and_seeded_report_options(self):
        from tools.qemu_smoke_arguments import parse_arguments

        common = [
            "--kernel", str(Path(__file__)), "--marker", "MICROS_BOOT",
            "--require-current-evidence", "diagnostic-history",
        ]
        required = ["--expect", "panic", "--require-panic-report", "--require-diagnostic-history", "seeded"]
        self.assertEqual("diagnostic-history", parse_arguments(common + required).require_current_evidence)
        for options in ([], ["--expect", "panic"], required[:-2]):
            with self.subTest(options=options), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                parse_arguments(common + options)

    def test_address_space_scope_requires_the_current_release_witnesses(self):
        output = release_fixture.user_target_output(Stage.GREEN).replace("# ", "")
        result = QemuResult(output, 0, False)
        qemu_smoke_cli._validate_current_evidence("user-address-space", result, Path("unused"))
        with self.assertRaises(ValueError):
            qemu_smoke_cli._validate_current_evidence(
                "user-address-space",
                QemuResult(output.replace("MEM05_RELEASE", "UNOWNED_RELEASE"), 0, False),
                Path("unused"),
            )

    def test_mapping_scope_requires_bound_largest_leaf_evidence(self):
        fixture = mapping_fixture.KernelMappingTargetRecordTest()
        current, binding, _, _, _ = fixture.fixture(largest=True)
        old, _, _, _, _ = fixture.fixture(largest=False)
        with patch("tests.host.kernel_mapping_evidence.bind_target_image", return_value=binding) as bind:
            qemu_smoke_cli._validate_current_evidence("kernel-mapping", current, Path("owned.elf"))
            bind.assert_called_once_with(Path("owned.elf"))
            with self.assertRaises(ValueError):
                qemu_smoke_cli._validate_current_evidence("kernel-mapping", old, Path("owned.elf"))

    def invoke_scheduler(self, output, *, changed_image=False):
        with tempfile.TemporaryDirectory(prefix="micros-current-target-") as directory:
            kernel = Path(directory) / "kernel.elf"
            kernel.write_bytes(b"owned target fixture")
            arguments = [
                "--kernel", str(kernel), "--require-current-evidence", "scheduler-return",
                *(item for marker in TARGET_MARKERS for item in ("--marker", marker)),
            ]

            def execute(*args, **kwargs):
                if changed_image:
                    kernel.write_bytes(b"changed target fixture")
                return QemuResult(output, 0, False)

            stream, errors = io.StringIO(), io.StringIO()
            with patch.object(qemu_smoke_cli, "run_qemu", side_effect=execute) as run:
                with redirect_stdout(stream), redirect_stderr(errors):
                    status = qemu_smoke_cli.main(arguments)
            run.assert_called_once()
            return status, stream.getvalue(), errors.getvalue()

    def test_complete_current_scheduler_witness_uses_the_single_owned_run(self):
        output = common_fixture.ContextCaptureProtocolTest.transcript().replace("# ", "")
        status, stdout, stderr = self.invoke_scheduler(output)
        self.assertEqual((0, ""), (status, stderr))
        self.assertIn("ok 1 - QEMU smoke", stdout)

    def test_selected_ipc_requires_same_guest_work_and_unchanged_image(self):
        complete = ipc_fixture.target_transcript().replace("# ", "")
        missing = ipc_fixture.TARGET_PASS + "\n"
        for output, changed, success in ((complete, False, True), (missing, False, False), (complete, True, False)):
            with self.subTest(changed=changed, complete=output == complete), tempfile.TemporaryDirectory() as directory:
                kernel = Path(directory) / "kernel.elf"
                kernel.write_bytes(b"owned IPC image")

                def execute(*args, **kwargs):
                    if changed:
                        kernel.write_bytes(b"changed IPC image")
                    return QemuResult(output, 0, False)

                with patch.object(qemu_smoke_cli, "run_qemu", side_effect=execute) as run:
                    with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                        result = qemu_smoke_cli.main([
                            "--kernel", str(kernel), "--marker", ipc_fixture.TARGET_PASS,
                            "--require-current-evidence", "selected-ipc",
                        ])
                run.assert_called_once()
                self.assertEqual(success, result == 0)
        with patch.object(qemu_smoke_cli, "run_qemu") as run:
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                qemu_smoke_cli.main([
                    "--kernel", str(Path(__file__)), "--marker", ipc_fixture.TARGET_PASS,
                    "--require-current-evidence", "selected-ipc", "--expect", "panic",
                ])
            run.assert_not_called()

    def test_basic_pass_marker_cannot_replace_current_work_or_image_identity(self):
        complete = common_fixture.ContextCaptureProtocolTest.transcript().replace("# ", "")
        missing = "\n".join(TARGET_MARKERS) + "\n"
        invalid = complete.replace("valid=0x0000000000000001", "valid=0x0000000000000000")
        for output, changed in ((missing, False), (invalid, False), (complete, True)):
            with self.subTest(changed_image=changed, missing=output == missing):
                status, stdout, stderr = self.invoke_scheduler(output, changed_image=changed)
                self.assertNotEqual(0, status)
                self.assertIn("not ok 1", stdout)
                self.assertNotIn("\nok 1", "\n" + stdout)
                self.assertIn("current target evidence", stderr)

    def test_unknown_scope_is_rejected_before_guest_execution(self):
        errors = io.StringIO()
        with patch.object(qemu_smoke_cli, "run_qemu") as run:
            with redirect_stderr(errors), self.assertRaises(SystemExit) as stopped:
                qemu_smoke_cli.main([
                    "--kernel", str(Path(__file__)), "--marker", "PASS",
                    "--require-current-evidence", "unowned",
                ])
        self.assertEqual(2, stopped.exception.code)
        run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
