"""Actual fixed-source assembly frames, payload ranges and stack pivots."""

from dataclasses import replace
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.stack_bound_objects import function_symbols, load_object, section_bytes, instructions
from tools.stack_bound_types import StackEvidenceError
from tools.user_elf_instructions import _instruction_destination


ROOT = Path(__file__).resolve().parents[2]


def compile_assembly(directory, unit, flags=frozenset()):
    output = directory / "fixture.obj"
    command = [
        os.environ["MICROS_CLANG"], "--target=riscv64-unknown-elf",
        "-march=rv64imac_zicsr_zifencei", "-mabi=lp64", "-mcmodel=medany",
        "-I" + str(ROOT), *(f"-D{flag}=1" for flag in sorted(flags)),
        "-c", str(ROOT / unit), "-o", str(output),
    ]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return load_object(output)


class StackAssemblyTest(unittest.TestCase):
    def test_trap_snapshot_has_a_distinct_complete_256_byte_return_buffer(self):
        from tools.stack_bound_assembly import assembly_object_evidence

        unit = "arch/riscv64/trap_test.S"
        with tempfile.TemporaryDirectory(prefix="micros-stack-snapshot-") as temporary:
            obj = compile_assembly(Path(temporary), unit)
            evidence = assembly_object_evidence(ROOT, obj, unit, "fixture", "kernel/micros.elf")
            snapshot, = evidence.snapshots
            self.assertEqual(256, snapshot.top - snapshot.bottom)
            self.assertEqual(256, snapshot.bytes_used)
            self.assertNotEqual(snapshot.entry_top, snapshot.top)
            self.assertEqual(
                {"micros_trap_test_entry_stack_top", "micros_trap_test_return_sp"},
                {epoch.target for epoch in evidence.epochs if epoch.owner == "test-snapshot"},
            )

    def test_snapshot_return_cannot_publish_another_stack_address_or_field(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_continuations import prove_snapshot_return

        with tempfile.TemporaryDirectory(prefix="micros-stack-snapshot-return-") as temporary:
            for index, expression in enumerate((
                "frame->sp = (unsigned long)micros_trap_test_return_sp",
                "frame->sp = (unsigned long)micros_trap_test_return_sp + 16",
                "frame->ra = (unsigned long)micros_trap_test_return_sp",
                "frame->sp = (unsigned long)foreign",
            )):
                directory = Path(temporary) / str(index)
                directory.mkdir()
                _, output = compile_fixture(directory, (
                    "struct frame { unsigned long ra, sp; };\n"
                    "extern char micros_trap_test_return_sp[], foreign[];\n"
                    "void prepare(struct frame *frame);\n"
                    "void prepare(struct frame *frame) { " + expression + "; }\n"
                ))
                obj = load_object(output)
                symbol = function_symbols(obj)["prepare"]
                if index == 0:
                    self.assertIsInstance(prove_snapshot_return(obj, symbol), int)
                else:
                    with self.subTest(expression=expression), self.assertRaises(StackEvidenceError):
                        prove_snapshot_return(obj, symbol)

    def test_untyped_user_payload_is_not_omitted_or_classified_as_firmware(self):
        from tools.stack_bound_assembly import assembly_object_evidence

        unit = "arch/riscv64/user_execution_test.S"
        with tempfile.TemporaryDirectory(prefix="micros-stack-asm-payload-") as temporary:
            obj = compile_assembly(Path(temporary), unit)
            evidence = assembly_object_evidence(ROOT, obj, unit, "fixture", "kernel/micros.elf")
            payload, = evidence.payloads
            self.assertEqual(("micros_user_execution_payload_start", 16), (
                payload.function.symbol, payload.frame,
            ))
            self.assertEqual(2, len(payload.ecalls))
            self.assertEqual("user", payload.owner)
            self.assertIn(payload.function, {frame.function for frame in evidence.frames})

    def test_trap_frame_is_not_added_to_the_emergency_pivot(self):
        from tools.stack_bound_assembly import assembly_object_evidence

        unit = "arch/riscv64/trap.S"
        with tempfile.TemporaryDirectory(prefix="micros-stack-asm-trap-") as temporary:
            obj = compile_assembly(Path(temporary), unit)
            evidence = assembly_object_evidence(ROOT, obj, unit, "fixture", "kernel/micros.elf")
            trap, = (row for row in evidence.frames if row.function.symbol == "micros_trap_entry")
            self.assertEqual(288, trap.bytes_used)
            emergency, = (row for row in evidence.epochs if row.owner == "emergency")
            self.assertEqual("micros_trap_nested_panic", emergency.target)
            self.assertEqual(0, emergency.initial_bytes)

    def test_changed_user_pivot_and_unknown_assembly_source_fail(self):
        from tools.stack_bound_assembly import assembly_object_evidence

        unit = "arch/riscv64/user_entry.S"
        with tempfile.TemporaryDirectory(prefix="micros-stack-asm-pivot-") as temporary:
            obj = compile_assembly(Path(temporary), unit)
            symbol = function_symbols(obj)["micros_riscv_enter_user"]
            section = obj.sections[symbol.section_index]
            point, width = next(
                (point, width) for point, width, word in instructions(
                    section_bytes(obj, section)[symbol.value:symbol.value + symbol.size], symbol.value,
                ) if _instruction_destination(word, width) == 2
            )
            data = bytearray(obj.data)
            data[section.offset + point:section.offset + point + width] = (0x00010113).to_bytes(width, "little")
            with self.assertRaises(StackEvidenceError):
                assembly_object_evidence(ROOT, replace(obj, data=bytes(data)), unit, "fixture", "kernel/micros.elf")
            with self.assertRaises(StackEvidenceError):
                assembly_object_evidence(ROOT, obj, "arch/riscv64/unbound.S", "fixture", "kernel/micros.elf")

    def test_nx_transfer_retains_its_16_byte_frame_and_exact_fault_boundary(self):
        from tools.stack_bound_assembly import assembly_object_evidence

        unit = "arch/riscv64/mmu_test.S"
        with tempfile.TemporaryDirectory(prefix="micros-stack-asm-nx-") as temporary:
            obj = compile_assembly(Path(temporary), unit)
            evidence = assembly_object_evidence(ROOT, obj, unit, "fixture", "kernel/micros.elf")
            frame, = (row for row in evidence.frames if row.function.symbol == "micros_mmu_execute_test_trigger")
            self.assertEqual(16, frame.bytes_used)
            fault, = evidence.faults
            self.assertEqual(("micros_mmu_writable_probe", "micros_mmu_execute_resume"), (
                fault.target, fault.continuation,
            ))
            self.assertFalse(any(call.target_symbol is None for call in evidence.calls))

    def test_linker_align_removes_only_declared_nops_and_preserves_function_ownership(self):
        from tools.stack_bound_relocations import linked_offset, verify_linked_object
        from tools.user_elf_parse import load_elf

        with tempfile.TemporaryDirectory(prefix="micros-stack-asm-align-") as temporary:
            directory = Path(temporary)
            obj = compile_assembly(directory, "tests/qemu/bootstrap_probe.S")
            command = [
                os.environ["MICROS_LD_LLD"], "-m", "elf64lriscv", "--no-relax",
                "--entry=micros_bootstrap_probe_report", "-Ttext=0x40000000",
                "--Map=fixture.map", "-o", "fixture.elf", "fixture.obj",
            ]
            result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            image = load_elf(directory / "fixture.elf")
            text = (directory / "fixture.map").read_text()
            owners = verify_linked_object(obj, "fixture.obj", image, text)
            wait = function_symbols(obj)["micros_bootstrap_probe_wait"]
            linked, = (row for row in image.symbols if row.name == wait.name)
            self.assertEqual(linked.value, owners[wait.section_index] + linked_offset(
                obj, wait.section_index, wait.value, owners[wait.section_index],
            ))
            section = obj.sections[wait.section_index]
            data = bytearray(obj.data)
            data[section.offset + 4:section.offset + 6] = b"\x02\x97"
            with self.assertRaises(StackEvidenceError):
                verify_linked_object(replace(obj, data=bytes(data)), "fixture.obj", image, text)

    def test_saved_supervisor_sp_must_name_the_original_live_caller_frame(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_artifacts import object_frames
        from tools.stack_bound_continuations import prove_saved_stack_pair

        with tempfile.TemporaryDirectory(prefix="micros-stack-saved-continuation-") as temporary:
            directory = Path(temporary)
            resume_obj = compile_assembly(directory, "arch/riscv64/user_runtime_test.S")
            resume = function_symbols(resume_obj)["micros_user_runtime_test_supervisor_resume"]
            for index, addition in enumerate(("", "addi %0, %0, 16\\n")):
                root = directory / str(index)
                root.mkdir()
                _, output = compile_fixture(root, (
                    "unsigned long micros_user_runtime_test_saved_sp;\n"
                    "void capture(void);\n"
                    "void capture(void) {\n"
                    f'    __asm__ volatile("mv %0, sp\\n{addition}"\n'
                    '        : "=r"(micros_user_runtime_test_saved_sp));\n'
                    "}\n"
                ))
                obj = load_object(output)
                symbol = function_symbols(obj)["capture"]
                size = object_frames(obj, function_symbols(obj))["capture"]
                if index == 0:
                    pair = prove_saved_stack_pair(
                        obj, symbol, resume_obj, resume, "micros_user_runtime_test_saved_sp", size,
                    )
                    self.assertEqual(size, pair.frame)
                else:
                    with self.assertRaises(StackEvidenceError):
                        prove_saved_stack_pair(
                            obj, symbol, resume_obj, resume, "micros_user_runtime_test_saved_sp", size,
                        )


if __name__ == "__main__":
    unittest.main()
