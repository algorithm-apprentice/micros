"""Join actual Clang .su and relocated .stack_sizes evidence fail-closed."""

import hashlib
import os
from dataclasses import dataclass
from pathlib import Path
import re

from tools.stack_bound_objects import (
    allocatable_object_bytes, call_instructions, function_symbols, load_object,
    relocations, section_bytes,
)
from tools.stack_bound_types import (
    Frame, FunctionKey, ObjectEvidence, SourceOrigin, StackEvidenceError, require_function,
    require_uint64,
)
from tools.stack_bound_machine import require_c_frame_movement
from tools.stack_bound_switch import switch_jumps


@dataclass(frozen=True)
class CompilerUsage:
    bytes_used: int
    source: str
    line: int


def source_spellings(source, compilation_directory):
    source = Path(source).absolute()
    directory = Path(compilation_directory).absolute()
    common = Path(os.path.commonpath((source, directory)))
    return {
        str(source), str(source.resolve()),
        str(source.relative_to(common)),
        str(source.resolve().relative_to(
            Path(os.path.commonpath((source.resolve(), directory.resolve()))),
        )),
    }


def usage_records(content, sources, compilation_directory):
    spellings = {}
    for source in sources:
        for spelling in source_spellings(source, compilation_directory):
            spellings.setdefault(spelling, set()).add(str(Path(source).resolve()))
    result = {}
    for line in content.splitlines():
        fields = line.split("\t")
        if len(fields) != 3:
            raise StackEvidenceError("malformed compiler stack-usage record")
        identity, size, classification = fields
        match = re.fullmatch(r"(.+):([0-9]+):([A-Za-z_][A-Za-z0-9_.$]*)", identity)
        if match is None or len(spellings.get(match[1], ())) != 1:
            raise StackEvidenceError("stack record has an unowned source identity")
        if classification != "static" or not re.fullmatch(r"[0-9]+", size):
            raise StackEvidenceError("stack record is not fixed static usage")
        value = int(size)
        require_uint64(value, "compiler frame size")
        line_number = int(match[2])
        require_uint64(line_number, "compiler source line")
        if match[3] in result:
            raise StackEvidenceError("duplicate compiler frame record")
        result[match[3]] = CompilerUsage(value, next(iter(spellings[match[1]])), line_number)
    if not result:
        raise StackEvidenceError("missing compiler stack-usage records")
    return result


def unsigned_leb(data, start):
    value = 0
    position = start
    while position < len(data) and position - start < 10:
        byte = data[position]
        value |= (byte & 0x7f) << (7 * (position - start))
        position += 1
        if not byte & 0x80:
            require_uint64(value, "object frame size")
            if position - start != max(1, (value.bit_length() + 6) // 7):
                raise StackEvidenceError("noncanonical object frame size")
            return value, position
    raise StackEvidenceError("truncated object stack-size entry")


def object_frames(obj, functions):
    result = {}
    owners = {}
    for name, function in functions.items():
        owners.setdefault((function.section_index, function.value), []).append(name)
    for section in obj.sections:
        if section.name != ".stack_sizes":
            continue
        if section.flags != 0x80 or section.section_type != 1:
            raise StackEvidenceError("stack-size metadata has unreviewed flags")
        references = {}
        for address, kind, target, addend in relocations(obj, section.index):
            if kind != 2 or address in references:
                raise StackEvidenceError("ambiguous stack-size relocation")
            matches = owners.get((target.section_index, target.value + addend), ())
            if len(matches) != 1 or section.link != target.section_index:
                raise StackEvidenceError("stack-size entry has no exact function owner")
            references[address] = matches[0]
        data = section_bytes(obj, section)
        position = 0
        seen = set()
        while position < len(data):
            if len(data) - position < 9 or position not in references:
                raise StackEvidenceError("unowned or truncated stack-size entry")
            if data[position:position + 8] != bytes(8):
                raise StackEvidenceError("stack-size relocation address is not canonical")
            name = references[position]
            seen.add(position)
            size, position = unsigned_leb(data, position + 8)
            if name in result:
                raise StackEvidenceError("duplicate object frame identity")
            result[name] = size
        if seen != set(references):
            raise StackEvidenceError("extra stack-size relocation")
    if not result:
        raise StackEvidenceError("missing object stack-size metadata")
    return result


def load_c_object(
    path, source, *, variant, image, translation_unit,
    dependencies=(), compilation_directory=None,
):
    path = Path(path)
    usage = path.with_suffix(".su")
    if not path.is_file() or path.is_symlink() or usage.is_symlink():
        raise StackEvidenceError("missing or linked compiler stack artifact")
    directory = Path.cwd() if compilation_directory is None else compilation_directory
    obj = load_object(path)
    functions = function_symbols(obj)
    if not functions:
        if (
            any(section.flags & 4 and section.size for section in obj.sections)
            or any(section.name == ".stack_sizes" and section.size for section in obj.sections)
            or usage.exists() and (not usage.is_file() or usage.read_bytes())
        ):
            raise StackEvidenceError("orphan executable or stack metadata in empty C unit")
        return ObjectEvidence(
            (), (), hashlib.sha256(obj.data).hexdigest(),
            hashlib.sha256(b"").hexdigest() if usage.is_file() else None,
        )
    if not usage.is_file():
        raise StackEvidenceError("missing compiler stack-usage artifact")
    usage_data = usage.read_bytes()
    try:
        recorded = usage_records(
            usage_data.decode("utf-8"), (source, *dependencies), directory,
        )
    except UnicodeError as error:
        raise StackEvidenceError("stack-usage metadata is not UTF-8") from error
    produced = object_frames(obj, functions)
    if set(recorded) != set(functions) or {
        name: record.bytes_used for name, record in recorded.items()
    } != produced:
        raise StackEvidenceError("compiler and object stack frame records disagree")
    frames = []
    calls = []
    origins = []
    for name, record in recorded.items():
        size = record.bytes_used
        key = FunctionKey(variant, image, translation_unit, name)
        require_function(key)
        frames.append(Frame(key, size))
        origins.append(SourceOrigin(key, record.source, record.line))
        try:
            switches = switch_jumps(obj, functions[name])
            require_c_frame_movement(obj, functions[name], size, switches)
        except StackEvidenceError as error:
            raise StackEvidenceError(f"{name}: {error}") from error
        calls.extend(call_instructions(obj, functions[name], key, switches))
    return ObjectEvidence(
        tuple(frames), tuple(calls),
        hashlib.sha256(obj.data).hexdigest(),
        hashlib.sha256(usage_data).hexdigest(),
        tuple(origins),
    )
