"""Observer leaves have an independently declared zero-stack RV64 ABI."""

from dataclasses import replace
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.stack_bound_objects import function_symbols, instructions, load_object, section_bytes


ROOT = Path(__file__).resolve().parents[2]


def observer_object(directory, kernel):
    output = directory / "observer.obj"
    command = [
        os.environ["MICROS_CLANG"], "--target=riscv64-unknown-elf",
        "-march=rv64imac_zicsr_zifencei", "-mabi=lp64", "-mcmodel=medany",
        *(("-DMICROS_STACK_OBSERVER_KERNEL=1",) if kernel else ()),
        "-c", str(ROOT / "tests/stack/observer.S"), "-o", str(output),
    ]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return load_object(output)


class StackObserverAbiTest(unittest.TestCase):
    def test_both_observer_modes_have_zero_frames_and_exact_owned_bss(self):
        from tools.stack_bound_observer import observer_frame_evidence

        with tempfile.TemporaryDirectory(prefix="micros-stack-observer-abi-") as temporary:
            root = Path(temporary)
            for kernel in (False, True):
                obj = observer_object(root, kernel)
                proof = observer_frame_evidence(ROOT, obj, "fixture", "kernel/micros.elf", kernel)
                self.assertEqual({0}, {frame.bytes_used for frame in proof.frames})
                self.assertEqual(2, len(proof.frames))
                self.assertEqual((), proof.calls)
                self.assertEqual(131 if kernel else 1, proof.records)
                exit_symbol = function_symbols(obj)["__cyg_profile_func_exit"]
                section = obj.sections[exit_symbol.section_index]
                self.assertEqual(
                    b"\x67\x80\x00\x00",
                    section_bytes(obj, section)[exit_symbol.value:exit_symbol.value + exit_symbol.size],
                )

    def test_observer_cannot_write_sp_or_replace_maximum_with_a_destructive_swap(self):
        from tools.stack_bound_observer import observer_frame_evidence
        from tools.stack_bound_types import StackEvidenceError

        with tempfile.TemporaryDirectory(prefix="micros-stack-observer-mutation-") as temporary:
            obj = observer_object(Path(temporary), False)
            entry = function_symbols(obj)["__cyg_profile_func_enter"]
            section = obj.sections[entry.section_index]
            code = tuple(instructions(
                section_bytes(obj, section)[entry.value:entry.value + entry.size], entry.value,
            ))
            for point, width, word in (
                code[0],
                next(row for row in code if row[2] & 0x7f == 0x2f and row[2] >> 27 == 0x1c),
            ):
                changed = word & ~(31 << 7) | 2 << 7 if point == code[0][0] else word & ~(31 << 27) | 1 << 27
                data = bytearray(obj.data)
                data[section.offset + point:section.offset + point + width] = changed.to_bytes(width, "little")
                with self.subTest(point=point), self.assertRaises(StackEvidenceError):
                    observer_frame_evidence(ROOT, replace(obj, data=bytes(data)), "fixture", "user.elf", False)

    def test_compiler_hooks_bind_their_exact_function_and_incoming_return_address(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tests.host.test_stack_test_tables import relocation_change
        from tools.stack_bound_objects import relocations
        from tools.stack_bound_observer import compiler_hook_inventory
        from tools.stack_bound_types import StackEvidenceError

        with tempfile.TemporaryDirectory(prefix="micros-stack-compiler-hook-") as temporary:
            _, output = compile_fixture(Path(temporary), (
                "int foreign(int x);\nint foreign(int x) { return x + 1; }\n"
                "int probe(int x);\nint probe(int x) { return foreign(x); }\n"
            ), observed=True)
            obj = load_object(output)
            symbol = function_symbols(obj)["probe"]
            hooks = compiler_hook_inventory(obj, symbol)
            self.assertEqual({"__cyg_profile_func_enter", "__cyg_profile_func_exit"}, {
                hook.target_symbol for hook in hooks
            })
            foreign = obj.symbols.index(function_symbols(obj)["foreign"])
            high, = (
                row for row in relocations(obj, symbol.section_index)
                if row[1] == 23 and row[2] == symbol
                and row[0] < hooks[0].offset
            )
            with self.assertRaises(StackEvidenceError):
                compiler_hook_inventory(relocation_change(
                    obj, symbol.section_index, high[0], target=foreign,
                ), symbol)

    def test_qemu_recipe_preserves_the_exact_legacy_arguments_without_launching(self):
        from unittest.mock import patch
        import shlex
        from tools.stack_bound_observer import qemu_recipe_arguments

        runner = str(ROOT / "tools/run_qemu_smoke.py")
        arguments = ["--qemu", "/fixture/qemu", "--kernel", "/fixture/kernel.elf",
                     "--marker", "legacy marker", "--timeout", "8"]
        command = "cd " + shlex.quote(str(ROOT)) + " && " + shlex.join([
            os.environ["MICROS_PYTHON"], "-E", "-S", "-B", runner, *arguments,
        ])
        with (
            patch("tools.stack_bound_observer.ninja_path", return_value="/fixture/ninja"),
            patch("tools.stack_bound_observer._run", return_value=command.encode()) as metadata,
        ):
            self.assertEqual(tuple(arguments), qemu_recipe_arguments(ROOT, Path("/fixture"), "test-qemu-scheduler"))
        metadata.assert_called_once()

    def test_compiler_trap_has_no_normal_fallthrough_or_invented_exit_hook(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_flow import function_flow
        from tools.stack_bound_observer import compiler_hook_inventory
        from tools.stack_bound_types import StackEvidenceError

        with tempfile.TemporaryDirectory(prefix="micros-stack-trap-terminal-") as temporary:
            _, output = compile_fixture(
                Path(temporary),
                "_Noreturn void fail(void);\n_Noreturn void fail(void) { for (;;) { __builtin_trap(); } }\n",
                observed=True, size_optimized=True,
            )
            obj = load_object(output)
            symbol = function_symbols(obj)["fail"]
            hooks = compiler_hook_inventory(obj, symbol)
            self.assertEqual(("__cyg_profile_func_enter",), tuple(hook.target_symbol for hook in hooks))
            flow = function_flow(obj, symbol)
            point = max(flow.instructions)
            self.assertEqual((2, 0), flow.instructions[point])
            self.assertEqual((), flow.successors[point])
            data = bytearray(obj.data)
            position = obj.sections[symbol.section_index].offset + symbol.value + point
            data[position:position + 2] = b"\x01\x00"
            with self.assertRaises(StackEvidenceError):
                function_flow(replace(obj, data=bytes(data)), symbol)


if __name__ == "__main__":
    unittest.main()
