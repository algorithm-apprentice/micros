"""Actual retained C frames and linked calls from one metadata image."""

from dataclasses import dataclass
import json
from pathlib import Path
import re

from tools.stack_artifact_build import ninja_path
from tools.stack_artifact_files import read_regular
from tools.stack_bound_artifacts import load_c_object
from tools.stack_bound_catalog import DefinedFunction, resolve_linked_calls
from tools.stack_bound_link import FunctionLocation, join_function_locations, text_owners
from tools.stack_bound_objects import function_symbols, load_object
from tools.stack_bound_relocations import verify_linked_object
from tools.stack_bound_types import SourceOrigin, StackEvidenceError, require_path
from tools.user_elf_parse import load_elf
from tools.validation_inputs import _run


@dataclass(frozen=True)
class LinkedCatalog:
    frames: tuple
    definitions: tuple[DefinedFunction, ...]
    calls: tuple
    pending: tuple
    objects: tuple = ()
    origins: tuple[SourceOrigin, ...] = ()


def compiler_dependencies(directory):
    content = _run((ninja_path(directory), "-C", directory, "-t", "deps")).decode()
    result = {}
    selected = None
    for line in content.splitlines():
        header = re.fullmatch(r"(.+): #deps [0-9]+, deps mtime [0-9]+ \((VALID|STALE)\)", line)
        if header:
            selected = header[1]
            if header[2] != "VALID":
                raise StackEvidenceError("metadata compiler dependencies are stale")
            result[selected] = []
        elif line.startswith("    ") and selected is not None:
            path = Path(line[4:])
            result[selected].append(path if path.is_absolute() else Path(directory) / path)
        elif line:
            raise StackEvidenceError("unsupported metadata compiler dependency record")
    return {name: tuple(paths) for name, paths in result.items()}


def image_map_text(directory, relative_image):
    require_path(relative_image)
    directory = Path(directory)
    image_path = directory / relative_image
    map_path = image_path.with_suffix(".map")
    try:
        if relative_image.endswith(".runtime.elf"):
            if relative_image != "applications/micros-application-runtime-probe.runtime.elf":
                raise StackEvidenceError("unreviewed seeded-runtime map alias")
            configured = (directory / "CMakeCache.txt").read_text().splitlines()
            if [line for line in configured if line.startswith("MICROS_BUILD_APPLICATION_IMAGES:")] != [
                "MICROS_BUILD_APPLICATION_IMAGES:BOOL=ON",
            ]:
                raise StackEvidenceError("runtime map alias has the wrong configuration")
            if map_path.exists() or map_path.is_symlink():
                raise StackEvidenceError("runtime copy has an undeclared independent map")
            producer = directory / "applications/micros-application-runtime-probe.elf"
            if read_regular(image_path)[0] != read_regular(producer)[0]:
                raise StackEvidenceError("runtime copy differs from its exact producer")
            map_path = producer.with_suffix(".map")
        return read_regular(map_path)[0].decode("utf-8")
    except (OSError, UnicodeError) as error:
        raise StackEvidenceError(f"linked image lacks its exact map: {error}") from error


def collect_linked_c(root, directory, relative_image, variant):
    root, directory = Path(root), Path(directory)
    image_path = directory / relative_image
    image = load_elf(image_path)
    map_text = image_map_text(directory, relative_image)
    owners = text_owners(map_text)
    objects = {name for name, _ in owners}
    dependencies = compiler_dependencies(directory)
    commands = json.loads((directory / "compile_commands.json").read_text())
    records = []
    locations = []
    for entry in commands:
        if not entry["file"].endswith(".c"):
            continue
        path = Path(entry["output"])
        if not path.is_absolute():
            path = directory / path
        relative = str(path.relative_to(directory))
        if relative not in objects:
            continue
        source = Path(entry["file"])
        if not source.is_absolute():
            source = directory / source
        obj = load_object(path)
        verify_linked_object(obj, relative, image, map_text)
        functions = function_symbols(obj)
        evidence = load_c_object(
            path, source, variant=variant, image=relative_image,
            translation_unit=str(source.relative_to(root)),
            dependencies=dependencies.get(relative, ()),
            compilation_directory=directory,
        )
        records.append((evidence, functions))
        for frame in evidence.frames:
            symbol = functions[frame.function.symbol]
            locations.append(FunctionLocation(
                frame.function, relative, obj.sections[symbol.section_index].name,
                symbol.value, symbol.size,
            ))
    addresses = join_function_locations(tuple(locations), map_text, image.symbols)
    object_by_function = {location.function: location.object_name for location in locations}
    frames = []
    definitions = []
    calls = []
    function_objects = []
    origins = []
    for evidence, functions in records:
        for frame in evidence.frames:
            if frame.function not in addresses:
                continue
            symbol = functions[frame.function.symbol]
            frames.append(frame)
            function_objects.append((frame.function, object_by_function[frame.function]))
            definitions.append(DefinedFunction(
                frame.function, addresses[frame.function], symbol.size, symbol.binding != 0,
            ))
        calls.extend(call for call in evidence.calls if call.caller in addresses)
        for origin in evidence.origins:
            if origin.function in addresses:
                try:
                    source_unit = str(Path(origin.source).relative_to(root))
                except ValueError as error:
                    raise StackEvidenceError("retained function source origin escapes the repository") from error
                origins.append(SourceOrigin(origin.function, source_unit, origin.line))
    if not frames:
        raise StackEvidenceError("metadata image has no retained C frame inventory")
    edges, pending = resolve_linked_calls(image, tuple(calls), tuple(definitions))
    return LinkedCatalog(
        tuple(frames), tuple(definitions), edges, pending, tuple(function_objects), tuple(origins),
    )
