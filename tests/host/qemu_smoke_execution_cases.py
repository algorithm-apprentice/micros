import contextlib

import io

import sys

from tools import run_qemu_smoke


class NmSymbolCases:
    def test_parses_exact_defined_symbol(self):
        address = run_qemu_smoke.parse_nm_symbol_address(
            "0000000080202000 T micros_trap_panic_test_fault\n",
            "micros_trap_panic_test_fault",
        )

        self.assertEqual(0x0000000080202000, address)

    def test_rejects_duplicate_defined_symbol(self):
        with self.assertRaises(ValueError):
            run_qemu_smoke.parse_nm_symbol_address(
                "0000000080202000 T trap_fault\n"
                "0000000080203000 t trap_fault\n",
                "trap_fault",
            )



class QemuCommandCases:
    def test_uses_explicit_single_hart_plic_machine(self):
        command = run_qemu_smoke.build_qemu_command(
            qemu="/tools/qemu-system-riscv64",
            kernel="/build/micros.elf",
        )

        self.assertEqual(
            [
                "/tools/qemu-system-riscv64",
                "-machine",
                "virt,aia=none",
                "-cpu",
                "rv64",
                "-smp",
                "1",
                "-m",
                "128M",
                "-display",
                "none",
                "-monitor",
                "none",
                "-serial",
                "stdio",
                "-nic",
                "none",
                "-bios",
                "default",
                "-kernel",
                "/build/micros.elf",
                "-no-reboot",
            ],
            command,
        )

    def test_accepts_explicit_guest_memory_size(self):
        command = run_qemu_smoke.build_qemu_command(
            qemu="/tools/qemu-system-riscv64",
            kernel="/build/micros.elf",
            memory="256M",
        )

        self.assertEqual("256M", command[command.index("-m") + 1])



class QemuSerialInputCases:
    def test_writes_serial_input_once_after_complete_trigger_line(self):
        command = [
            sys.executable,
            "-c",
            (
                "import sys;"
                "sys.stdout.write('READY\\n');"
                "sys.stdout.flush();"
                "data=sys.stdin.buffer.read(3);"
                "sys.stdout.write(data.hex()+'\\n');"
                "sys.stdout.flush()"
            ),
        ]

        result = run_qemu_smoke.run_qemu(
            command,
            2.0,
            serial_input_trigger="READY",
            serial_input=b"x\x7f\r",
        )

        self.assertFalse(result.timed_out)
        self.assertEqual(0, result.return_code)
        self.assertTrue(result.serial_input_sent)
        self.assertEqual("READY\n787f0d\n", result.output)

    def test_default_execution_keeps_stdin_closed(self):
        command = [
            sys.executable,
            "-c",
            (
                "import sys;"
                "data=sys.stdin.buffer.read(1);"
                "sys.stdout.write('EOF\\n' if not data else 'DATA\\n')"
            ),
        ]

        result = run_qemu_smoke.run_qemu(command, 2.0)

        self.assertFalse(result.timed_out)
        self.assertEqual(0, result.return_code)
        self.assertFalse(result.serial_input_sent)
        self.assertEqual("EOF\n", result.output)

    def test_closed_serial_input_pipe_returns_a_normal_result(self):
        command = [
            sys.executable,
            "-c",
            (
                "import os,sys;"
                "os.close(0);"
                "sys.stdout.write('READY\\n');"
                "sys.stdout.flush()"
            ),
        ]

        result = run_qemu_smoke.run_qemu(
            command,
            2.0,
            serial_input_trigger="READY",
            serial_input=b"x",
        )

        self.assertFalse(result.timed_out)
        self.assertEqual(0, result.return_code)
        self.assertFalse(result.serial_input_sent)
        self.assertEqual("READY\n", result.output)

    def test_trigger_requires_an_exact_terminated_line(self):
        self.assertFalse(
            run_qemu_smoke._serial_trigger_observed(
                b"prefix READY\nREADY",
                "READY",
            )
        )
        self.assertTrue(
            run_qemu_smoke._serial_trigger_observed(
                b"prefix READY\nREADY\r\n",
                "READY",
            )
        )



class TapOutputCases:
    def test_pass_includes_machine_readable_outcome(self):
        result = run_qemu_smoke.QemuResult(
            output="MICROS_BOOT 0.1.0\n",
            return_code=0,
            timed_out=False,
        )
        output = io.StringIO()

        with contextlib.redirect_stdout(output):
            exit_code = run_qemu_smoke.print_tap_result(
                run_qemu_smoke.SmokeOutcome.PASS,
                result,
                ["qemu-system-riscv64"],
            )

        self.assertEqual(0, exit_code)
        self.assertIn("# outcome: pass\n", output.getvalue())
