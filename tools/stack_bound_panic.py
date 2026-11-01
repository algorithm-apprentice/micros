"""Finite shared panic-entry stack boundary from Accepted ADR-0014/0015."""

from pathlib import Path

from tools.stack_bound_bindings import reference_source
from tools.stack_bound_machine import require_c_frame_movement, transfer
from tools.stack_bound_objects import call_instructions, function_symbols, instructions, relocations, section_bytes
from tools.stack_bound_types import CallSite, EdgeKind, Frame, FunctionKey, StackEvidenceError
from tools.user_elf_instructions import _instruction_destination


def panic_frame_evidence(root, obj, variant, image):
    root = Path(root)
    for unit in ("arch/riscv64/panic.S", "arch/riscv64/panic_context.h"):
        path = root / unit
        if path.is_symlink() or path.read_text() != reference_source(str(root), unit):
            raise StackEvidenceError("panic assembly source/layout stage changed")
    symbols = function_symbols(obj)
    if set(symbols) != {"micros_panic_entry", "micros_trap_panic_entry"}:
        raise StackEvidenceError("panic assembly function inventory changed")
    direct = symbols["micros_panic_entry"]
    shared = symbols["micros_trap_panic_entry"]
    if direct.section_index != shared.section_index:
        raise StackEvidenceError("panic entries lost their shared instruction section")
    section = obj.sections[direct.section_index]
    data = section_bytes(obj, section)
    prefix = tuple(instructions(data[direct.value:direct.value + direct.size], direct.value))
    if (
        len(prefix) != 3
        or prefix[0][1:] != (4, 0x100172f3)
        or prefix[1][1:] != (2, 0x4701)
        or prefix[2][1] != 2 or prefix[2][2] & 0xe003 != 0xa001
    ):
        raise StackEvidenceError("panic direct entry changed its exact CSR/null-trap prefix")
    branches = tuple(
        (address, kind, target, addend)
        for address, kind, target, addend in relocations(obj, section.index)
        if address == prefix[2][0] and kind == 45
    )
    if (
        len(branches) != 1 or branches[0][2].section_index != section.index
        or branches[0][2].value + branches[0][3] != shared.value + 4
    ):
        raise StackEvidenceError("panic direct jump does not enter its exact shared context body")
    decoded = tuple(instructions(data[shared.value:shared.value + shared.size], shared.value))
    stack_writes = tuple(
        (address, width, word) for address, width, word in decoded
        if _instruction_destination(word, width) == 2
    )
    registers = [None] * 32
    registers[0], registers[2] = (False, 0), (True, 0)
    if (
        len(stack_writes) != 1 or stack_writes[0][0] != shared.value + 4
        or transfer(tuple(registers), stack_writes[0][1], stack_writes[0][2], 48)[2] != (True, -48)
    ):
        raise StackEvidenceError("panic shared allocation is not the exact 48-byte context")
    require_c_frame_movement(obj, shared, 48)
    prefix_key = FunctionKey(variant, image, "arch/riscv64/panic.S", direct.name)
    shared_key = FunctionKey(variant, image, "arch/riscv64/panic.S", shared.name)
    calls = call_instructions(obj, shared, shared_key)
    if len(calls) != 1 or calls[0].target_symbol != "micros_panic_report":
        raise StackEvidenceError("panic shared body has an unbound reporter call")
    reporter = FunctionKey(variant, image, "kernel/diagnostic/panic.c", "micros_panic_report")
    return (
        (Frame(prefix_key, 0), Frame(shared_key, 48)),
        (
            CallSite(prefix_key, prefix[2][0] - direct.value, (shared_key,), EdgeKind.TAIL),
            CallSite(shared_key, calls[0].offset, (reporter,), calls[0].kind),
        ),
    )
