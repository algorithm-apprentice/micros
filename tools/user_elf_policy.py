"""Service ELF policy owner assigned by ADR-0063."""

from __future__ import annotations

import struct

from tools.user_elf_format import (
    PROGRAM_LOAD,
    PROGRAM_EXECUTE,
    PROGRAM_READ,
    SYMBOL_BIND_GLOBAL,
    SYMBOL_UNDEFINED,
    PAGE_SIZE,
    USER_BASE,
    MINIMUM_STACK_BOTTOM,
    ElfFormatError,
    ElfImage,
    _bounded_slice,
    _defined_symbols,
    _program_file_bytes,
    _section_by_name,
)

from tools.user_elf_instructions import (
    _sign_extend,
    _instruction_writes,
)


REQUIRED_SYMBOLS = (
    "_start",
    "micros_service_main",
    "micros_runtime_raw_syscall",
    "micros_runtime_service_returned",
    "__micros_user_image_start",
    "__micros_user_text_start",
    "__micros_user_text_end",
    "__micros_user_rodata_start",
    "__micros_user_rodata_end",
    "__micros_user_data_start",
    "__micros_user_data_end",
    "__micros_user_bss_start",
    "__micros_user_bss_end",
    "__micros_user_image_end",
)


def _validate_symbols(image: ElfImage, loads):
    errors = []
    symbols, duplicates = _defined_symbols(image)
    for name in sorted(duplicates):
        errors.append(f"duplicate defined symbol {name}")
    for name in REQUIRED_SYMBOLS:
        if name not in symbols:
            errors.append(f"missing required symbol {name}")
    undefined = sorted(
        {
            symbol.name
            for symbol in image.symbols
            if symbol.name and symbol.section_index == SYMBOL_UNDEFINED
        }
    )
    for name in undefined:
        errors.append(f"undefined symbol {name}")
    if any(name not in symbols for name in REQUIRED_SYMBOLS):
        return errors, symbols
    boundaries = _validate_symbol_boundaries(image, symbols, errors)
    _validate_symbol_load_ranges(loads, boundaries, errors)
    _validate_symbol_section_ranges(image, boundaries, errors)
    return errors, symbols


def _validate_symbol_boundaries(image: ElfImage, symbols, errors):
    if symbols["_start"].binding != SYMBOL_BIND_GLOBAL:
        errors.append("_start must be a global symbol")
    if (
        image.header.entry != symbols["_start"].value
        or symbols["_start"].value != USER_BASE
    ):
        errors.append("_start and e_entry must both equal the user base")
    boundaries = {
        name: symbols[name].value
        for name in REQUIRED_SYMBOLS
        if name.startswith("__micros_user_")
    }
    aligned = (
        "__micros_user_image_start",
        "__micros_user_text_start",
        "__micros_user_text_end",
        "__micros_user_rodata_start",
        "__micros_user_rodata_end",
        "__micros_user_data_start",
        "__micros_user_bss_end",
        "__micros_user_image_end",
    )
    for name in aligned:
        if boundaries[name] % PAGE_SIZE != 0:
            errors.append(f"symbol {name} must be page-aligned")
    if (
        boundaries["__micros_user_image_start"] != USER_BASE
        or boundaries["__micros_user_text_start"] != USER_BASE
    ):
        errors.append("user image and text must start at the user base")
    if not (
        boundaries["__micros_user_text_end"]
        == boundaries["__micros_user_rodata_start"]
        < boundaries["__micros_user_rodata_end"]
        == boundaries["__micros_user_data_start"]
        <= boundaries["__micros_user_data_end"]
        == boundaries["__micros_user_bss_start"]
        < boundaries["__micros_user_bss_end"]
        == boundaries["__micros_user_image_end"]
    ):
        errors.append("user linker boundaries are not ordered and closed")
    if boundaries["__micros_user_text_start"] >= boundaries[
        "__micros_user_text_end"
    ]:
        errors.append("user text range is empty")
    if boundaries["__micros_user_image_end"] > MINIMUM_STACK_BOTTOM:
        errors.append("user image overlaps the minimum external stack")
    return boundaries


def _validate_symbol_load_ranges(loads, boundaries, errors):
    if len(loads) == 3:
        expected_ranges = (
            (
                boundaries["__micros_user_text_start"],
                boundaries["__micros_user_text_end"],
            ),
            (
                boundaries["__micros_user_rodata_start"],
                boundaries["__micros_user_rodata_end"],
            ),
            (
                boundaries["__micros_user_data_start"],
                boundaries["__micros_user_image_end"],
            ),
        )
        for index, (program, expected) in enumerate(
            zip(loads, expected_ranges)
        ):
            if (
                program.virtual_address != expected[0]
                or program.virtual_address + program.memory_size
                    != expected[1]
            ):
                errors.append(
                    f"PT_LOAD {index} does not close its linker range"
                )
        writable_file_end = (
            loads[2].virtual_address + loads[2].file_size
        )
        if writable_file_end != boundaries["__micros_user_bss_start"]:
            errors.append(
                "writable PT_LOAD file range must end at BSS start"
            )


def _validate_symbol_section_ranges(image: ElfImage, boundaries, errors):
    required_sections = {
        ".text": (
            boundaries["__micros_user_text_start"],
            boundaries["__micros_user_text_end"],
        ),
        ".rodata": (
            boundaries["__micros_user_rodata_start"],
            boundaries["__micros_user_rodata_end"],
        ),
        ".data": (
            boundaries["__micros_user_data_start"],
            boundaries["__micros_user_data_end"],
        ),
        ".bss": (
            boundaries["__micros_user_bss_start"],
            boundaries["__micros_user_bss_end"],
        ),
    }
    for name, expected in required_sections.items():
        section = _section_by_name(image, name)
        if (
            section is not None
            and (
                section.address != expected[0]
                or section.address + section.size != expected[1]
            )
        ):
            errors.append(
                f"{name} does not match its exported linker range"
            )
    bss = _section_by_name(image, ".bss")
    if (
        bss is not None
        and (
            bss.address != boundaries["__micros_user_bss_start"]
            or bss.address + bss.size
                != boundaries["__micros_user_bss_end"]
        )
    ):
        errors.append(".bss does not match the exported BSS range")


def _validate_instructions(image: ElfImage, symbols, loads):
    errors = []
    if any(name not in symbols for name in REQUIRED_SYMBOLS):
        return errors
    raw = symbols["micros_runtime_raw_syscall"]
    if raw.size != 8:
        errors.append("raw syscall stub must be exactly eight bytes")
    else:
        try:
            raw_bytes = _program_file_bytes(image, raw.value, raw.size)
        except ElfFormatError as error:
            errors.append(str(error))
        else:
            if raw_bytes != b"\x73\x00\x00\x00\x67\x80\x00\x00":
                errors.append("raw syscall stub must be exact ecall; ret")
    returned = symbols["micros_runtime_service_returned"]
    try:
        return_bytes = _program_file_bytes(image, returned.value, 8)
    except ElfFormatError as error:
        errors.append(str(error))
    else:
        if return_bytes != b"\x73\x00\x10\x00\x6f\x00\x00\x00":
            errors.append(
                "service-return path must be uncompressed ebreak and loop"
            )
    start = symbols["_start"].value
    try:
        startup = _program_file_bytes(image, start, 24)
    except ElfFormatError as error:
        errors.append(str(error))
    else:
        words = struct.unpack("<6I", startup)
        if words[0] != 0x00000193 or words[1] != 0x00000213:
            errors.append("startup must initialize gp and tp to zero")
        if returned.value != start + 16:
            errors.append("service-return label must immediately follow call")
        auipc = words[2]
        jalr = words[3]
        if (
            auipc & 0x7F != 0x17
            or (auipc >> 7) & 0x1F != 1
            or jalr & 0x7F != 0x67
            or (jalr >> 7) & 0x1F != 1
            or (jalr >> 15) & 0x1F != 1
        ):
            errors.append("startup must call micros_service_main directly")
        else:
            upper = _sign_extend(auipc & 0xFFFFF000, 32)
            lower = _sign_extend(jalr >> 20, 12)
            target = (start + 8 + upper + lower) & ~1
            if target != symbols["micros_service_main"].value:
                errors.append("startup call target is not micros_service_main")
    executable_loads = [
        program
        for program in loads
        if program.flags == PROGRAM_READ | PROGRAM_EXECUTE
    ]
    if len(executable_loads) == 1:
        text = executable_loads[0]
        try:
            text_bytes = _bounded_slice(
                image.data,
                text.offset,
                text.file_size,
                "executable PT_LOAD bytes",
            )
            writes = [
                item
                for item in _instruction_writes(
                    text.virtual_address,
                    text_bytes,
                )
                if item[1] in (3, 4)
            ]
        except ElfFormatError as error:
            errors.append(str(error))
        else:
            if writes != [(start, 3), (start + 4, 4)]:
                errors.append(
                    "gp or tp is written outside startup initialization"
                )
    return errors


def resident_page_count(image: ElfImage, stack_page_count: int):
    if stack_page_count < 0:
        raise ValueError("stack page count must not be negative")
    load_pages = sum(
        (program.memory_size + PAGE_SIZE - 1) // PAGE_SIZE
        for program in image.programs
        if program.program_type == PROGRAM_LOAD
    )
    return load_pages + stack_page_count


def validate_resident_page_limit(
    image: ElfImage,
    stack_page_count: int,
    resident_page_limit: int,
):
    if resident_page_limit <= 0:
        raise ValueError("resident page limit must be positive")
    count = resident_page_count(image, stack_page_count)
    if count > resident_page_limit:
        return [
            f"resident page count {count} exceeds limit "
            f"{resident_page_limit}"
        ]
    return []
