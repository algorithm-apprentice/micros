"""Source/object-bound admission of one immutable, target-test-only table.

This adds no production callback authority and publishes no stack certificate.
The caller supplies a collected catalog, never table names, targets or flags.
"""

from dataclasses import dataclass
from functools import lru_cache
import json
from pathlib import Path
import re
import shlex

from tests.host.c_source_records import (
    c_lexical_text, function_records, matching_lexical, tokens,
)
from tools.stack_artifact_files import read_regular
from tools.stack_bound_artifacts import object_frames
from tools.stack_bound_bindings import anchored_function, reference_source
from tools.stack_bound_flow import function_flow, reachable_instructions
from tools.stack_bound_link import (
    FunctionLocation, direct_call_target, join_function_locations,
)
from tools.stack_bound_machine import branch_offset, is_call, signed
from tools.stack_bound_objects import function_symbols, load_object, relocations
from tools.stack_bound_pointer import indirect_register, memory_action
from tools.stack_bound_switch import add_index, bound_constant, load_sp_slot, store_sp_slot
from tools.stack_bound_types import (
    CallSite, EdgeKind, FunctionKey, StackEvidenceError, SymbolCall,
    require_function, require_path, require_uint64,
)
from tools.stack_bound_values import register_inputs
from tools.user_elf_format import _program_file_bytes
from tools.user_elf_instructions import _instruction_destination
from tools.user_elf_parse import load_elf


_UNIT = "kernel/tests/grant/grant_syscall_test_commands.c"
_COPY_UNIT = "kernel/tests/grant/grant_syscall_test_copy_commands.c"
_HEADER = "kernel/tests/grant/grant_syscall_test_internal.h"
_CALLER = "grant_syscall_test_arm_command"
_TABLE = "command_bindings"
_FLAG = "MICROS_BUILD_GRANT_SYSCALL_TEST"
_VARIANT = "riscv64-grant-syscall-test"
_IMAGE = "kernel/micros.elf"
_STRIDE, _MEMBER, _COUNT = 16, 8, 46


@dataclass(frozen=True)
class CommandTableArtifact:
    """A checked object binding, not linked-frame or production authority."""

    table: object
    entries: tuple
    call_offset: int
    flow: object


@lru_cache(maxsize=32)
def _reference_rows(root):
    source = c_lexical_text(reference_source(str(root), _UNIT))
    declaration = re.search(
        r"\bstatic\s+const\s+struct\s+grant_syscall_command_binding\s+"
        r"command_bindings\s*\[\s*\]\s*=\s*(\{)", source,
    )
    header = c_lexical_text(reference_source(str(root), _HEADER))
    enumeration = re.search(r"\benum\s+grant_syscall_command\s*(\{)", header)
    if declaration is None or enumeration is None:
        raise StackEvidenceError("immutable test-table source stage has no declared table/type")
    opening = declaration.start(1)
    body = source[opening + 1:matching_lexical(source, opening, "{", "}")]
    pairs = tuple(re.findall(
        r"\{\s*(GRANT_COMMAND_[A-Z_]+)\s*,\s*(grant_syscall_test_arm_[a-z_]+)\s*\}", body,
    ))
    reconstructed = "".join("{" + command + "," + name + "}," for command, name in pairs)
    opening = enumeration.start(1)
    enum_body = header[opening + 1:matching_lexical(header, opening, "{", "}")]
    names = tuple(item.strip() for item in enum_body.split(",") if item.strip())
    if (
        len(pairs) != _COUNT or tokens(body) != tokens(reconstructed)
        or not names or names[0] != "GRANT_COMMAND_NONE = 0"
        or any(re.fullmatch(r"GRANT_COMMAND_[A-Z_]+", name) is None for name in names[1:])
        or len(set(names)) != len(names)
    ):
        raise StackEvidenceError("immutable test-table initializer/enum is outside its finite contract")
    values = {name: index for index, name in enumerate(names[1:], 1)}
    owners = {
        unit: function_records(reference_source(str(root), unit)) for unit in (_UNIT, _COPY_UNIT)
    }
    rows = []
    for command, name in pairs:
        units = [unit for unit, records in owners.items() if name in records]
        if command not in values or len(units) != 1:
            raise StackEvidenceError("immutable test-table provider has no unique source owner")
        rows.append((values[command], name, units[0]))
    if {row[0] for row in rows} != set(range(1, _COUNT + 1)):
        raise StackEvidenceError("immutable test-table command inventory is incomplete")
    return tuple(rows)


def _owned_path(root, relative):
    require_path(relative)
    path = Path(root)
    for part in Path(relative).parts:
        path /= part
        if path.is_symlink():
            raise StackEvidenceError("test-table input has a linked source/artifact path")
    return path


def _require_sources(root, key, targets, origins):
    for unit in (_UNIT, _COPY_UNIT, _HEADER):
        actual = read_regular(_owned_path(root, unit))[0].decode("utf-8")
        if tokens(actual) != tokens(reference_source(str(root), unit)):
            raise StackEvidenceError("test-table source-stage type/initializer/provider scope changed")
    for function in (key, *targets):
        record = anchored_function(root, function)
        origin = origins[function]
        reference = c_lexical_text(reference_source(str(root), function.translation_unit))
        occurrences = tuple(re.finditer(r"\b" + re.escape(function.symbol) + r"\s*\(", reference))
        if (
            len(occurrences) != 1 or origin.source != function.translation_unit
            or origin.line != reference[:occurrences[0].start()].count("\n") + 1
        ):
            raise StackEvidenceError("test-table function has a borrowed source/line origin")
        if function == key:
            expression = tokens("command_bindings[index].arm(fixture, &input)")
            body = tokens(record.body)
            if sum(body[index:index + len(expression)] == expression for index in range(len(body))) != 1:
                raise StackEvidenceError("test-table source call occurrence is not complete")


def _table_entries(root, obj):
    tables = tuple(symbol for symbol in obj.symbols if symbol.name == _TABLE)
    if len(tables) != 1:
        raise StackEvidenceError("test table has no unique object symbol")
    table, = tables
    if not 0 < table.section_index < len(obj.sections):
        raise StackEvidenceError("test table has no owned readonly section")
    section = obj.sections[table.section_index]
    if (
        table.binding != 0 or table.symbol_type != 1 or table.value != 0
        or table.size != _STRIDE * _COUNT or section.size != table.size
        or section.name != ".rodata." + _TABLE or section.flags != 2
        or section.section_type != 1 or section.alignment != 8
        or any(symbol.symbol_type == 1 and symbol.section_index == section.index
               and symbol != table for symbol in obj.symbols)
    ):
        raise StackEvidenceError("test table lost its exact immutable range/slot geometry")
    references = relocations(obj, section.index)
    by_address = {address: (kind, target, addend) for address, kind, target, addend in references}
    expected = set(range(_MEMBER, table.size, _STRIDE))
    if len(by_address) != len(references) or set(by_address) != expected:
        raise StackEvidenceError("test-table slot/member relocation inventory is incomplete")
    data = obj.data[section.offset:section.offset + section.size]
    if len(data) != section.size:
        raise StackEvidenceError("test-table readonly bytes are truncated")
    functions = function_symbols(obj)
    entries = []
    for index, (command, name, unit) in enumerate(_reference_rows(root)):
        start = index * _STRIDE
        kind, target, addend = by_address[start + _MEMBER]
        local = unit == _UNIT
        if (
            data[start:start + 4] != command.to_bytes(4, "little")
            or data[start + 4:start + _STRIDE] != bytes(12)
            or kind != 2 or addend != 0 or target.name != name
            or local and (target.binding != 0 or functions.get(name) != target)
            or not local and (
                target.binding != 1 or target.section_index != 0
                or target.symbol_type != 0 or target.value != 0 or target.size != 0
            )
        ):
            raise StackEvidenceError("test-table row bytes/relocation/provider differ from fixed source")
        entries.append((command, target, unit))
    return table, tuple(entries)


def _linear_chain(flow, chain):
    if any(flow.successors[left] != (right,) for left, right in zip(chain, chain[1:])):
        raise StackEvidenceError("test-table load chain has a control-flow bypass")
    predecessors = dict(zip(chain[1:], chain))
    for address, successors in flow.successors.items():
        if any(target in predecessors and predecessors[target] != address for target in successors):
            raise StackEvidenceError("incoming branch bypasses a test-table range/load proof")


def _lookup_chain(obj, symbol, flow, address, member):
    decoded = tuple(flow.instructions)
    position = decoded.index(address)
    if position < 2 or position + 3 >= len(decoded):
        raise StackEvidenceError("test-table lookup lacks its indexed register chain")
    chain = decoded[position - 2:position + 4]
    loaded, scale, high, low, combined, lookup = (flow.instructions[item] for item in chain)
    index = load_sp_slot(*loaded)
    base = high[1] >> 7 & 31
    scaled = scale[1] >> 7 & 31
    lower = [
        item for item in relocations(obj, symbol.section_index)
        if item[0] == symbol.value + chain[3] and item[1] != 51
    ]
    if (
        index is None or index[0] in (0, 1, 2)
        or scale[0] != 4 or scale[1] & 0xfc00707f != 0x1013
        or scale[1] >> 15 & 31 != index[0] or scale[1] >> 20 & 63 != 4
        or base in (0, 1, 2, scaled) or scaled in (0, 1, 2)
        or high[0] != 4 or high[1] & 0x7f != 0x17 or high[1] >> 12
        or low[0] != 4 or low[1] & 0x707f != 0x13 or low[1] >> 20
        or low[1] >> 7 & 31 != base or low[1] >> 15 & 31 != base
        or len(lower) != 1 or lower[0][1] != 24 or lower[0][3] != 0
        or lower[0][2].section_index != symbol.section_index
        or lower[0][2].value != symbol.value + address
        or not add_index(*combined, base, scaled)
    ):
        raise StackEvidenceError("test-table operand lost its exact readonly base/index/stride")
    action = memory_action(*lookup)
    size = 4 if member == 0 else 8
    if action is None or action[:2] != (False, size) or action[3:] != (base, member):
        raise StackEvidenceError("test-table load has the wrong indexed member geometry")
    if action[2] in (0, 1, 2):
        raise StackEvidenceError("test-table load has no usable member register")
    _linear_chain(flow, chain)
    return chain, index[1], action[2]


def _guarded_dispatch(flow, lookups, call):
    command_chain, slot, command_register = lookups[0]
    arm_chain, arm_slot, target_register = lookups[1]
    decoded = tuple(flow.instructions)
    position = decoded.index(command_chain[0])
    if slot != arm_slot or position < 4:
        raise StackEvidenceError("test-table lookups do not share their guarded index")
    guard_chain = decoded[position - 4:position]
    loaded, constant, guard, jump = (flow.instructions[address] for address in guard_chain)
    bounded = load_sp_slot(*loaded)
    bound = bound_constant(*constant)
    if (
        bounded is None or bounded[1] != slot or bound is None or bound[1] != _COUNT - 1
        or guard[0] != 4 or guard[1] & 0x707f != 0x6063
        or guard[1] >> 15 & 31 != bound[0] or guard[1] >> 20 & 31 != bounded[0]
        or bound[0] in (0, bounded[0])
        or branch_offset(*jump) is None or branch_offset(*jump)[1]
        or flow.successors[guard_chain[-1]] != (command_chain[0],)
    ):
        raise StackEvidenceError("test-table index lacks the exact unsigned guarded extent")
    _linear_chain(flow, guard_chain[:3])
    guard_at = guard_chain[2]
    reached = reachable_instructions(flow)
    if guard_at not in reached or call.offset not in reached:
        raise StackEvidenceError("test-table call/guard lacks a reachable owned entry")
    outside, inside = flow.successors[guard_at]
    if inside != guard_chain[3] or call.offset in reachable_instructions(flow, start=outside):
        raise StackEvidenceError("out-of-range test-table index reaches the callback")
    compare_position = decoded.index(command_chain[-1]) + 1
    compare_chain = decoded[compare_position:compare_position + 3]
    if len(compare_chain) != 3:
        raise StackEvidenceError("test-table command comparison is missing")
    loaded_command, compare, match_jump = (flow.instructions[address] for address in compare_chain)
    command_slot = memory_action(*loaded_command)
    if (
        command_slot is None or command_slot[:2] != (False, 4) or command_slot[3] != 2
        or compare[0] != 4 or compare[1] & 0x707f != 0x1063
        or {compare[1] >> 15 & 31, compare[1] >> 20 & 31} != {command_register, command_slot[2]}
        or branch_offset(*match_jump) is None or branch_offset(*match_jump)[1]
        or flow.successors[compare_chain[-1]] != (arm_chain[0],)
        or flow.successors[compare_chain[1]][1] != compare_chain[-1]
    ):
        raise StackEvidenceError("test-table callback lost its exact command-member comparison")
    _linear_chain(flow, (command_chain[-1], *compare_chain[:2]))
    call_position = decoded.index(call.offset)
    tail = decoded[decoded.index(arm_chain[-1]):call_position + 1]
    if len(tail) != 4 or not is_call(*flow.instructions[call.offset]) or (
        indirect_register(*flow.instructions[call.offset]) != target_register
    ):
        raise StackEvidenceError("actual JALR does not consume the indexed arm member register")
    width, word = flow.instructions[call.offset]
    if width == 4 and word >> 20:
        raise StackEvidenceError("indexed test-table call has a nonzero JALR displacement")
    if any(_instruction_destination(word, width) == target_register
           for width, word in (flow.instructions[address] for address in tail[1:-1])):
        raise StackEvidenceError("indexed test-table member register was overwritten before JALR")
    _linear_chain(flow, tail)
    required = (guard_at, *command_chain, *compare_chain[:2], *arm_chain, *tail)
    return guard_chain[0], guard_at, inside, command_slot, required


def _index_updates(flow, slot, command_slot, inputs, guard_at, guard_start):
    decoded = tuple(flow.instructions)
    stack = inputs[guard_at][2]
    stores, command_stores = [], []
    for address, instruction in flow.instructions.items():
        action = memory_action(*instruction)
        if action is None or not action[0]:
            continue
        if action[3] != 2 or address not in inputs or inputs[address][2] != stack:
            raise StackEvidenceError("test-table caller has an unbound memory write/stack alias")
        _, size, register, _, displacement = action
        if displacement < slot + 8 and slot < displacement + size:
            if size != 8 or displacement != slot:
                raise StackEvidenceError("test-table index has a partial/aliased store")
            stores.append((address, register))
        command_displacement = command_slot[4]
        if displacement < command_displacement + 4 and command_displacement < displacement + size:
            command_stores.append((address, size, register, displacement))
    if len(stores) != 2 or len(command_stores) != 1:
        raise StackEvidenceError("test-table index/command store inventory changed")
    initial, increment = stores
    if inputs[initial[0]][initial[1]] != ("scalar", 0):
        raise StackEvidenceError("test-table index is not initialized to zero")
    stored_at, size, register, displacement = command_stores[0]
    if (
        size != 4 or displacement != command_slot[4]
        or inputs[stored_at][register] != ("argument", 3, ())
    ):
        raise StackEvidenceError("test-table comparison borrowed another command argument")
    position = decoded.index(increment[0])
    if position < 2 or position + 1 >= len(decoded):
        raise StackEvidenceError("test-table increment lacks its complete update/recheck chain")
    increment_chain = decoded[position - 2:position + 2]
    loaded, addition, stored, jump = (flow.instructions[address] for address in increment_chain)
    previous = load_sp_slot(*loaded)
    destination = store_sp_slot(*stored)
    width, word = addition
    updates = (
        width == 4 and word & 0x707f == 0x13 and word >> 20 == 1
        and word >> 15 & 31 == increment[1] and word >> 7 & 31 == increment[1]
        or width == 2 and word & 0xe003 == 1 and word >> 7 & 31 == increment[1]
        and signed(((word >> 12) & 1) << 5 | ((word >> 2) & 31), 6) == 1
    )
    if (
        previous is None or previous[1] != slot or destination != (increment[1], slot)
        or increment[1] != previous[0] or not updates
        or branch_offset(*jump) is None or branch_offset(*jump)[1]
        or flow.successors[increment_chain[-1]] != (guard_start,)
    ):
        raise StackEvidenceError("test-table loop lost its exact index increment/recheck")
    _linear_chain(flow, increment_chain)
    return initial[0]


def _prove_loop(obj, symbol, flow, lookups, call):
    guard_start, guard_at, inside, command_slot, required = _guarded_dispatch(flow, lookups, call)
    inputs = register_inputs(obj, symbol)
    initial = _index_updates(flow, lookups[0][1], command_slot, inputs, guard_at, guard_start)
    required = (*required, initial)
    if any(call.offset in reachable_instructions(flow, excluded=address)
           for address in set(required) - {call.offset}):
        raise StackEvidenceError("test-table call bypasses its complete guarded source/load occurrence")
    if (
        inputs[call.offset][10] != ("argument", 0, ())
        or inputs[call.offset][11] is None or inputs[call.offset][11][0] != "stack"
    ):
        raise StackEvidenceError("test-table call lost its fixture/input operands")
    before_call = reachable_instructions(flow, excluded=call.offset, start=inside)
    if any(candidate.offset != call.offset and candidate.offset in before_call
           and call.offset in reachable_instructions(flow, start=candidate.offset)
           for candidate in flow.calls):
        raise StackEvidenceError("an unbound call intervenes in the guarded test-table loop")


def validate_grant_command_table(root, obj, symbol):
    """Validate actual RV64 object evidence using only the fixed Git source pin."""
    if symbol.name != _CALLER or function_symbols(obj).get(_CALLER) != symbol:
        raise StackEvidenceError("test-table caller is not its complete owned function range")
    table, entries = _table_entries(root, obj)
    flow = function_flow(obj, symbol)
    calls = tuple(call for call in flow.calls if call.target_symbol is None)
    if len(calls) != 1 or calls[0].kind is not EdgeKind.CALL:
        raise StackEvidenceError("test-table source/machine call occurrence inventory is incomplete")
    references = tuple(
        item for item in relocations(obj, symbol.section_index)
        if symbol.value <= item[0] < symbol.value + symbol.size
        and item[1] == 23 and item[2] == table and item[3] == 0
    )
    if len(references) != 2:
        raise StackEvidenceError("test-table caller has an incomplete/foreign table operand")
    allowed = {item[0] for item in references}
    for section in obj.sections:
        if not section.flags & 2:
            continue
        for address, kind, target, addend in relocations(obj, section.index):
            if target == table or target.section_index == table.section_index and (
                table.value <= target.value + addend < table.value + table.size
            ):
                if (
                    section.index != symbol.section_index or address not in allowed
                    or kind != 23 or target != table or addend != 0
                ):
                    raise StackEvidenceError("test-table address escapes its two exact readonly lookups")
    lookups = tuple(_lookup_chain(
        obj, symbol, flow, reference[0] - symbol.value, member,
    ) for reference, member in zip(sorted(references), (0, _MEMBER)))
    _prove_loop(obj, symbol, flow, lookups, calls[0])
    return CommandTableArtifact(table, entries, calls[0].offset, flow)


def _catalog_inventory(catalog):
    frames = {item.function: item for item in catalog.frames}
    definitions = {item.function: item for item in catalog.definitions}
    objects = dict(catalog.objects)
    origins = {item.function: item for item in catalog.origins}
    if (
        len(frames) != len(catalog.frames) or len(definitions) != len(catalog.definitions)
        or len(objects) != len(catalog.objects) or len(origins) != len(catalog.origins)
        or any(set(items) != set(frames) for items in (definitions, objects, origins))
    ):
        raise StackEvidenceError("test-table catalog has an incomplete qualified frame/object/origin inventory")
    for key in frames:
        require_function(key)
    return frames, definitions, objects, origins


def _configured_flag(directory):
    content = read_regular(_owned_path(directory, "CMakeCache.txt"))[0].decode("utf-8")
    records = [line for line in content.splitlines() if line.startswith(_FLAG + ":")]
    if records not in ([_FLAG + ":BOOL=ON"], [_FLAG + ":BOOL=OFF"]):
        raise StackEvidenceError("test-table configuration lacks its exact compiled test flag")
    return records == [_FLAG + ":BOOL=ON"]


def _require_compile_flags(root, directory):
    entries = json.loads(read_regular(_owned_path(directory, "compile_commands.json"))[0])
    if type(entries) is not list:
        raise StackEvidenceError("test-table compile inventory is not a complete command list")
    for unit in (_UNIT, _COPY_UNIT):
        source = Path(root).absolute() / unit
        output = Path(directory).absolute() / ("CMakeFiles/micros.dir/" + unit + ".obj")
        selected = [entry for entry in entries if isinstance(entry, dict)
                    and entry.get("file") == str(source)]
        if len(selected) != 1:
            raise StackEvidenceError("test-table source has no unique compiled command")
        entry, = selected
        if ("arguments" in entry) == ("command" in entry):
            raise StackEvidenceError("test-table compiled command has missing/ambiguous argv authority")
        argv = entry.get("arguments")
        if argv is None:
            argv = shlex.split(entry.get("command", ""))
        if (
            type(argv) is not list or any(type(argument) is not str for argument in argv)
            or argv.count("-c") != 1 or argv.count("-o") != 1
            or len(argv) <= max(argv.index("-c"), argv.index("-o")) + 1
        ):
            raise StackEvidenceError("test-table compiled command has no exact source/output operands")
        compiled_output = Path(argv[argv.index("-o") + 1])
        if not compiled_output.is_absolute():
            require_path(str(compiled_output))
            compiled_output = Path(directory).absolute() / compiled_output
        if (
            entry.get("directory") != str(Path(directory).absolute())
            or entry.get("output") != str(output)
            or argv.count("-D" + _FLAG + "=1") != 1
            or any(_FLAG in argument and argument != "-D" + _FLAG + "=1" for argument in argv)
            or argv[argv.index("-c") + 1] != str(source)
            or compiled_output != output
            or any(argument.startswith("@") for argument in argv)
        ):
            raise StackEvidenceError("test-table compiled source/object/flag differs from its exact configuration")


def _linked_table(image, map_text, object_name, artifact, definitions, key):
    section_name = ".rodata." + _TABLE
    marker = object_name + ":(" + section_name + ")"
    lines = [line for line in map_text.splitlines() if marker in line]
    pattern = (
        r"\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+8\s+"
        + re.escape(marker) + r"\s*"
    )
    match = re.fullmatch(pattern, lines[0]) if len(lines) == 1 else None
    if match is None:
        raise StackEvidenceError("test table has no exact readonly linked object-section owner")
    address, load_address, size = (int(match[index], 16) for index in (1, 2, 3))
    if address != load_address or address % 8 or size != artifact.table.size:
        raise StackEvidenceError("test-table linked range/slot geometry differs from its object")
    symbols = tuple(symbol for symbol in image.symbols if (
        symbol.name, symbol.value, symbol.size
    ) == (_TABLE, address, size))
    if len(symbols) != 1 or symbols[0].binding != 0 or symbols[0].symbol_type != 1:
        raise StackEvidenceError("test table has no unique actual linked readonly symbol")
    symbol, = symbols
    if not 0 < symbol.section_index < len(image.sections):
        raise StackEvidenceError("linked test table has no data section")
    section = image.sections[symbol.section_index]
    programs = tuple(program for program in image.programs if program.program_type == 1
                     and program.virtual_address <= address
                     and address + size <= program.virtual_address + program.file_size)
    # Combined .rodata may inherit MERGE/STRINGS labels, never write/execute.
    if (
        section.flags & 7 != 2 or section.flags & ~0x32
        or section.section_type != 1 or len(programs) != 1
        or programs[0].flags != 4 or not section.address <= address
        or address + size > section.address + section.size
    ):
        raise StackEvidenceError("linked test table is not exactly readonly file-backed data")
    data = _program_file_bytes(image, address, size)
    actual_targets = set()
    expected_targets = set()
    for index, (command, target, unit) in enumerate(artifact.entries):
        start = index * _STRIDE
        function = FunctionKey(key.variant, key.image, unit, target.name)
        expected = definitions[function].address
        actual = int.from_bytes(data[start + _MEMBER:start + _STRIDE], "little")
        if data[start:start + 8] != command.to_bytes(4, "little") + bytes(4) or actual != expected:
            raise StackEvidenceError("linked test-table row/target does not equal its exact source/object function")
        actual_targets.add(actual)
        expected_targets.add(expected)
    if actual_targets != expected_targets:
        raise StackEvidenceError("linked test-table target-set equality failed")
    return address


def _linked_code(image, obj, symbol, artifact, address, table_address):
    code = _program_file_bytes(image, address, symbol.size)
    flow = artifact.flow
    references = {}
    for point, kind, target, addend in relocations(obj, symbol.section_index):
        relative = point - symbol.value
        if relative not in flow.instructions:
            continue
        if kind == 51:
            continue
        if relative in references:
            raise StackEvidenceError("test-table caller has ambiguous linked instruction relocations")
        references[relative] = kind, target, addend
    masks = {}
    for point, (kind, target, addend) in references.items():
        width, word = flow.instructions[point]
        actual = int.from_bytes(code[point:point + width], "little")
        if kind in (16, 17, 44, 45):
            branch = branch_offset(width, actual)
            if branch is None or point + branch[0] != flow.successors[point][0]:
                raise StackEvidenceError("linked test-table branch bypasses the object range/load proof")
            masks[point] = {16: 0x01fff07f, 17: 0x00000fff, 44: 0xe383, 45: 0xe003}[kind]
        elif kind == 23:
            lowers = [(offset, record) for offset, record in references.items()
                      if record[0] == 24 and record[1].section_index == symbol.section_index
                      and record[1].value + record[2] == symbol.value + point]
            if len(lowers) != 1:
                raise StackEvidenceError("linked test-table operand lacks its exact lower pair")
            lower, _ = lowers[0]
            low = int.from_bytes(code[lower:lower + 4], "little")
            if target == artifact.table and addend == 0:
                expected = table_address
            elif target.section_index == symbol.section_index and (
                symbol.value <= target.value + addend < symbol.value + symbol.size
            ):
                expected = address + target.value + addend - symbol.value
            else:
                raise StackEvidenceError("linked test-table caller has an unbound PC-relative operand")
            if address + point + signed(actual & 0xfffff000, 32) + signed(low >> 20, 12) != expected:
                raise StackEvidenceError("linked test-table lookup consumed a foreign table/function address")
            masks[point], masks[lower] = 0x00000fff, 0x000fffff
        elif kind in (18, 19):
            candidates = tuple(item for item in image.symbols if item.name == target.name
                               and item.binding == 1 and item.symbol_type == 2 and item.section_index != 0)
            destination, edge = direct_call_target(image, address + point)
            if len(candidates) != 1 or destination != candidates[0].value or edge is not EdgeKind.CALL:
                raise StackEvidenceError("linked compiler-hook call lost its actual function target")
            masks[point], masks[point + 4] = 0x00000fff, 0x000fffff
        elif kind != 24:
            raise StackEvidenceError("test-table caller has an unsupported linked relocation")
    for point, (width, word) in flow.instructions.items():
        actual = int.from_bytes(code[point:point + width], "little")
        mask = masks.get(point, (1 << (width * 8)) - 1)
        if actual & mask != word & mask or point in references and references[point][0] == 24 and point not in masks:
            raise StackEvidenceError("linked test-table instruction/register differs from its actual object")


def _require_linked_targets(root, directory, key, targets, inventory):
    frames, definitions, object_names, _ = inventory
    objects, locations = {}, []
    for function in (key, *targets):
        relative = "CMakeFiles/micros.dir/" + function.translation_unit + ".obj"
        if object_names[function] != relative:
            raise StackEvidenceError("test-table function borrowed another source/object owner")
        if relative not in objects:
            path = _owned_path(directory, relative)
            before = read_regular(path)[0]
            obj = load_object(path)
            if obj.data != before:
                raise StackEvidenceError("test-table object changed while loading")
            objects[relative] = obj, function_symbols(obj), object_frames(obj, function_symbols(obj))
        obj, symbols, sizes = objects[relative]
        symbol = symbols.get(function.symbol)
        frame = frames[function]
        definition = definitions[function]
        require_uint64(frame.bytes_used, "test-table compiler frame")
        if (
            symbol is None or frame.classification != "static" or sizes.get(function.symbol) != frame.bytes_used
            or definition.size != symbol.size or definition.global_linkage != (symbol.binding != 0)
        ):
            raise StackEvidenceError("test-table target/caller lacks its exact object frame/function")
        locations.append(FunctionLocation(
            function, relative, obj.sections[symbol.section_index].name, symbol.value, symbol.size,
        ))
    map_text = read_regular(_owned_path(directory, "kernel/micros.map"))[0].decode("utf-8")
    image_path = _owned_path(directory, key.image)
    before = read_regular(image_path)[0]
    image = load_elf(image_path)
    if image.data != before:
        raise StackEvidenceError("test-table linked image changed while loading")
    addresses = join_function_locations(tuple(locations), map_text, image.symbols)
    if set(addresses) != {key, *targets} or any(
        addresses[function] != definitions[function].address for function in addresses
    ):
        raise StackEvidenceError("test-table target-set lacks exact qualified linked function equality")
    obj, symbols, _ = objects[object_names[key]]
    artifact = validate_grant_command_table(root, obj, symbols[key.symbol])
    table_address = _linked_table(image, map_text, object_names[key], artifact, definitions, key)
    _linked_code(image, obj, symbols[key.symbol], artifact, addresses[key], table_address)
    return artifact


def bind_current_test_tables(root, directory, catalog):
    """Return this bound CallSite and all unrelated unresolved boundaries intact."""
    try:
        inventory = _catalog_inventory(catalog)
        frames, definitions, _, origins = inventory
        active = _configured_flag(directory)
        present = any(key.translation_unit in (_UNIT, _COPY_UNIT) or key.symbol == _CALLER for key in frames)
        if not active:
            if present:
                raise StackEvidenceError("target-test table cannot extend production callback authority")
            return (), catalog.pending
        if not present and not any(key.image == _IMAGE for key in frames):
            return (), catalog.pending
        key = FunctionKey(_VARIANT, _IMAGE, _UNIT, _CALLER)
        targets = tuple(dict.fromkeys(
            FunctionKey(key.variant, key.image, unit, name)
            for _, name, unit in _reference_rows(root)
        ))
        if key not in frames or any(target not in definitions for target in targets):
            raise StackEvidenceError("active test-table caller/provider/frame inventory is incomplete")
        _require_compile_flags(root, directory)
        _require_sources(root, key, targets, origins)
        artifact = _require_linked_targets(root, directory, key, targets, inventory)
        expected = (SymbolCall(key, artifact.call_offset, None, EdgeKind.CALL),)
        actual = tuple(sorted(
            (call for call in catalog.pending if call.caller == key and call.target_symbol is None),
            key=lambda call: call.offset,
        ))
        if actual != expected or any(
            call.caller == key and call.offset == artifact.call_offset for call in catalog.calls
        ):
            raise StackEvidenceError("test-table catalog/source/machine occurrence inventory differs")
        bound = CallSite(key, artifact.call_offset, targets, EdgeKind.CALL)
        remaining = tuple(call for call in catalog.pending if (
            call.caller, call.offset
        ) != (key, artifact.call_offset))
        return (bound,), remaining
    except (OSError, UnicodeError, ValueError, KeyError, IndexError, AssertionError) as error:
        if isinstance(error, StackEvidenceError):
            raise
        raise StackEvidenceError(f"test-table binding failed: {error}") from error
