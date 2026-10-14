import struct

from tools.user_elf_format import (
    ELF_HEADER,
    PROGRAM_HEADER,
    ElfFormatError,
    ElfImage,
    PAGE_SIZE,
    PROGRAM_EXECUTE,
    PROGRAM_LOAD,
    PROGRAM_READ,
    PROGRAM_WRITE,
    SECTION_ALLOC,
    SECTION_WRITE,
    SYMBOL_BIND_GLOBAL,
    SYMBOL_UNDEFINED,
    USER_BASE,
    _bounded_slice,
    _defined_symbols,
    _program_file_bytes,
)
from tools.user_elf_instructions import _instruction_writes, _sign_extend
from tools.user_elf_parse import _parse_header, _parse_programs
from tools.user_elf_policy import (
    _validate_symbol_boundaries,
    _validate_symbol_load_ranges,
    _validate_symbol_section_ranges,
)
from tools.user_elf_structure import (
    _validate_header,
    _validate_programs,
    _validate_sections,
)


FILE_SIZE_MAX = 131072
IMAGE_PAGE_MAX = 15
STACK_TOP = 0x80000000
LINKER_SYMBOLS = (
    "__micros_user_image_start", "__micros_user_text_start",
    "__micros_user_text_end", "__micros_user_rodata_start",
    "__micros_user_rodata_end", "__micros_user_data_start",
    "__micros_user_data_end", "__micros_user_bss_start",
    "__micros_user_bss_end", "__micros_user_image_end",
)
FUNCTION_SYMBOLS = (
    "_start", "micros_application_start", "micros_application_main",
    "micros_runtime_raw_syscall", "micros_application_start_returned",
    "micros_application_startup_failed", "micros_application_exit_returned",
)
STARTUP_GLOBALS = (
    ("micros_application_pm_endpoint", 4),
    ("micros_application_vfs_endpoint", 4),
    ("micros_application_pid", 8),
    ("micros_application_page_size", 8),
)
REQUIRED_SYMBOLS = (
    *FUNCTION_SYMBOLS, *(name for name, _ in STARTUP_GLOBALS),
    "__micros_application_stack_top", *LINKER_SYMBOLS,
)
BREAKPOINT_LOOP = b"\x73\x00\x10\x00\x6f\x00\x00\x00"
RAW_SYSCALL = b"\x73\x00\x00\x00\x67\x80\x00\x00"


def _runtime_header_errors(image):
    errors = _validate_header(image)
    if len(image.data) < ELF_HEADER.size:
        return errors + ["application ELF header is truncated"]
    if image.data[7] != 0:
        errors.append("application ELF OS ABI must be ELFOSABI_SYSV")
    if image.data[8] != 0:
        errors.append("application ELF ABI version must be zero")
    if any(image.data[9:16]):
        errors.append("application ELF identification padding must be zero")
    if image.header.program_offset != ELF_HEADER.size:
        errors.append("application program-header offset must be 64")
    if image.header.program_entry_size != PROGRAM_HEADER.size:
        errors.append("application program-header entry size must be 56")
    if image.header.program_count != 3 or len(image.programs) != 3:
        errors.append("application program-header count must be three")
    if len(image.data) > FILE_SIZE_MAX:
        errors.append("application ELF file exceeds 131072 bytes")
    return errors


def _runtime_program_errors(image):
    errors, loads = _validate_programs(image)
    expected_flags = (
        PROGRAM_READ | PROGRAM_EXECUTE,
        PROGRAM_READ,
        PROGRAM_READ | PROGRAM_WRITE,
    )
    previous_end = USER_BASE
    previous_file_end = 0
    pages = 0
    for index, program in enumerate(image.programs):
        if (
            index >= len(expected_flags)
            or program.program_type != PROGRAM_LOAD
            or program.flags != expected_flags[index]
        ):
            errors.append("application PT_LOAD order and permission classes are invalid")
        if (
            program.virtual_address % PAGE_SIZE
            or program.physical_address % PAGE_SIZE
            or program.offset % PAGE_SIZE
            or program.memory_size % PAGE_SIZE
        ):
            errors.append(f"application PT_LOAD {index} is not page-closed")
        if program.virtual_address != previous_end:
            errors.append("application PT_LOAD mappings must be contiguous in header order")
        previous_end = program.virtual_address + program.memory_size
        if program.offset < previous_file_end:
            errors.append("application PT_LOAD file ranges are not ordered and disjoint")
        previous_file_end = program.offset + program.file_size
        if previous_end > 0xFFFFFFFFFFFFFFFF or previous_file_end > 0xFFFFFFFFFFFFFFFF:
            errors.append("application PT_LOAD range overflows uint64")
        if program.program_type == PROGRAM_LOAD:
            pages += program.memory_size // PAGE_SIZE
    if pages > IMAGE_PAGE_MAX or previous_end > USER_BASE + IMAGE_PAGE_MAX * PAGE_SIZE:
        errors.append("application image exceeds 15 image pages")
    return errors, loads


def _application_symbol_inventory(image, errors):
    symbols, duplicates = _defined_symbols(image)
    for name in sorted(duplicates):
        errors.append(f"duplicate defined application symbol {name}")
    for name in REQUIRED_SYMBOLS:
        if name not in symbols:
            errors.append(f"missing required application symbol {name}")
    undefined = sorted({
        symbol.name for symbol in image.symbols
        if symbol.name and symbol.section_index == SYMBOL_UNDEFINED
    })
    for name in undefined:
        errors.append(f"undefined symbol {name}")
    if "micros_bootstrap_service_config" in symbols:
        errors.append("application must not contain a bootstrap service configuration")
    return symbols


def _startup_global_errors(image, symbols, loads):
    errors = []
    if len(loads) != 3:
        return errors
    writable = loads[2]
    ranges = []
    for name, size in STARTUP_GLOBALS:
        symbol = symbols[name]
        end = symbol.value + symbol.size
        section = (
            image.sections[symbol.section_index]
            if symbol.section_index < len(image.sections) else None
        )
        if (
            symbol.binding != SYMBOL_BIND_GLOBAL or symbol.symbol_type != 1
            or symbol.size != size or symbol.value < writable.virtual_address
            or end > writable.virtual_address + writable.memory_size
            or section is None
            or section.flags & (SECTION_ALLOC | SECTION_WRITE)
                != SECTION_ALLOC | SECTION_WRITE
            or symbol.value < section.address
            or end > section.address + section.size
        ):
            errors.append(f"application startup global {name} has an invalid writable extent")
        ranges.append((symbol.value, end))
    previous = None
    for begin, end in sorted(ranges):
        if previous is not None and begin < previous:
            errors.append("application startup globals overlap")
        previous = end
    return errors


def _application_symbol_errors(image, loads):
    errors = []
    symbols = _application_symbol_inventory(image, errors)
    if any(name not in symbols for name in REQUIRED_SYMBOLS):
        return errors, symbols
    boundaries = _validate_symbol_boundaries(image, symbols, errors)
    _validate_symbol_load_ranges(loads, boundaries, errors)
    _validate_symbol_section_ranges(image, boundaries, errors)
    if symbols["__micros_application_stack_top"].value != STACK_TOP:
        errors.append("application external stack top must equal 0x80000000")
    if len(loads) == 3:
        text = loads[0]
        for name in FUNCTION_SYMBOLS:
            symbol = symbols[name]
            if (
                symbol.binding != SYMBOL_BIND_GLOBAL or symbol.symbol_type != 2
                or symbol.size == 0 or symbol.value < text.virtual_address
                or symbol.value + symbol.size > text.virtual_address + text.memory_size
            ):
                errors.append(f"application function {name} is not closed in text")
    errors.extend(_startup_global_errors(image, symbols, loads))
    return errors, symbols


def _fixed_instruction_errors(image, symbol, expected, description):
    errors = []
    if symbol.size != len(expected):
        errors.append(f"{description} has an invalid size")
    try:
        actual = _program_file_bytes(image, symbol.value, len(expected))
    except ElfFormatError as error:
        errors.append(str(error))
    else:
        if actual != expected:
            errors.append(f"{description} has noncanonical instructions")
    return errors


def _application_instruction_errors(image, symbols, loads):
    errors = []
    if any(name not in symbols for name in REQUIRED_SYMBOLS):
        return errors
    errors.extend(_fixed_instruction_errors(
        image, symbols["micros_runtime_raw_syscall"], RAW_SYSCALL, "raw syscall stub",
    ))
    for name in (
        "micros_application_start_returned", "micros_application_startup_failed",
        "micros_application_exit_returned",
    ):
        errors.extend(_fixed_instruction_errors(
            image, symbols[name], BREAKPOINT_LOOP, "application fatal breakpoint loop",
        ))
    start = symbols["_start"]
    if start.size != 28:
        errors.append("application startup must be exactly 28 bytes")
    try:
        startup = _program_file_bytes(image, start.value, 28)
    except ElfFormatError as error:
        errors.append(str(error))
    else:
        words = struct.unpack("<7I", startup)
        if words[:3] != (0x00000193, 0x00000213, 0x00010513):
            errors.append("application startup must clear gp/tp and pass original sp in a0")
        if symbols["micros_application_start_returned"].value != start.value + 20:
            errors.append("application startup return label must follow the C-entry call")
        auipc, jalr = words[3:5]
        if (
            auipc & 0x7F != 0x17 or (auipc >> 7) & 0x1F != 1
            or jalr & 0x7F != 0x67 or (jalr >> 7) & 0x1F != 1
            or (jalr >> 15) & 0x1F != 1 or (jalr >> 12) & 0x7 != 0
        ):
            errors.append("application startup must call its C entry directly")
        else:
            target = (
                start.value + 12 + _sign_extend(auipc & 0xFFFFF000, 32)
                + _sign_extend(jalr >> 20, 12)
            ) & ~1
            if target != symbols["micros_application_start"].value:
                errors.append("application startup call target is not micros_application_start")
    executable = [program for program in loads
                  if program.flags == PROGRAM_READ | PROGRAM_EXECUTE]
    if len(executable) == 1:
        program = executable[0]
        try:
            data = _bounded_slice(image.data, program.offset, program.file_size, "text bytes")
            writes = [item for item in _instruction_writes(program.virtual_address, data)
                      if item[1] in (3, 4)]
        except ElfFormatError as error:
            errors.append(str(error))
        else:
            if writes != [(start.value, 3), (start.value + 4, 4)]:
                errors.append("gp or tp is written outside application startup initialization")
    return errors


def validate_elf(image):
    errors = _runtime_header_errors(image)
    program_errors, loads = _runtime_program_errors(image)
    errors.extend(program_errors)
    errors.extend(_validate_sections(image, loads))
    symbol_errors, symbols = _application_symbol_errors(image, loads)
    errors.extend(symbol_errors)
    errors.extend(_application_instruction_errors(image, symbols, loads))
    return errors


def validate_seeded_elf(image, runtime_bytes):
    errors = validate_elf(image)
    if errors:
        return ["checked application ELF is invalid: " + error for error in errors]
    try:
        header = _parse_header(runtime_bytes)
        programs = _parse_programs(runtime_bytes, header)
    except (ElfFormatError, struct.error) as error:
        return ["seeded application ELF is malformed: " + str(error)]
    runtime = ElfImage(runtime_bytes, header, programs, (), ())
    errors.extend(_runtime_header_errors(runtime))
    program_errors, _ = _runtime_program_errors(runtime)
    errors.extend(program_errors)
    if runtime_bytes[:ELF_HEADER.size] != image.data[:ELF_HEADER.size]:
        errors.append("seeded application ELF header differs from checked link ELF")
    size = image.header.program_count * PROGRAM_HEADER.size
    begin = image.header.program_offset
    if runtime_bytes[begin:begin + size] != image.data[begin:begin + size]:
        errors.append("seeded application program headers differ from checked link ELF")
    for program in image.programs:
        begin = program.offset
        end = begin + program.file_size
        if runtime_bytes[begin:end] != image.data[begin:end]:
            errors.append(f"seeded application PT_LOAD bytes differ at header {program.index}")
    return errors
