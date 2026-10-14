import struct

from tests.host.user_elf_test_fixture import align, build_elf, make_string_table
from tools.user_elf_format import (
    ElfSymbol,
    SECTION_HEADER,
    SYMBOL,
    SYMBOL_BIND_GLOBAL,
    USER_BASE,
)
from tools.user_elf_parse import parse_elf


def _application_symbols(image):
    symbols = [
        symbol for symbol in image.symbols
        if symbol.name.startswith("__micros_user_")
    ]
    functions = (
        ("_start", 0, 28),
        ("micros_application_start", 36, 4),
        ("micros_application_main", 40, 4),
        ("micros_runtime_raw_syscall", 28, 8),
        ("micros_application_start_returned", 20, 8),
        ("micros_application_startup_failed", 44, 8),
        ("micros_application_exit_returned", 52, 8),
    )
    for name, offset, size in functions:
        symbols.append(ElfSymbol(name, SYMBOL_BIND_GLOBAL, 2, 1, USER_BASE + offset, size))
    for name, offset, size in (
        ("micros_application_pm_endpoint", 8, 4),
        ("micros_application_vfs_endpoint", 12, 4),
        ("micros_application_pid", 16, 8),
        ("micros_application_page_size", 24, 8),
    ):
        symbols.append(
            ElfSymbol(name, SYMBOL_BIND_GLOBAL, 1, 4, USER_BASE + 0x2000 + offset, size)
        )
    symbols.append(
        ElfSymbol("__micros_application_stack_top", SYMBOL_BIND_GLOBAL, 0, 0xFFF1,
                  0x80000000, 0)
    )
    return tuple(symbols)


def _encode_application_symbols(data, image, symbols):
    strings, offsets = make_string_table([symbol.name for symbol in symbols])
    table = bytearray(SYMBOL.size)
    for symbol in symbols:
        table.extend(SYMBOL.pack(
            offsets[symbol.name],
            symbol.binding << 4 | symbol.symbol_type,
            0,
            symbol.section_index,
            symbol.value,
            symbol.size,
        ))
    symbol_offset = align(len(data), 8)
    data.extend(bytes(symbol_offset - len(data)))
    data.extend(table)
    string_offset = len(data)
    data.extend(strings)
    section_offset = align(len(data), 8)
    data.extend(bytes(section_offset - len(data)))
    for section in image.sections:
        record = list(SECTION_HEADER.unpack_from(
            image.data,
            image.header.section_offset + section.index * SECTION_HEADER.size,
        ))
        if section.name == ".symtab":
            record[4:6] = (symbol_offset, len(table))
        elif section.name == ".strtab":
            record[4:6] = (string_offset, len(strings))
        data.extend(SECTION_HEADER.pack(*record))
    struct.pack_into("<Q", data, 40, section_offset)
    return data


def build_application_elf(*, data_section_name=".data"):
    data, metadata = build_elf(data_section_name=data_section_name)
    image = parse_elf(bytes(data))
    words = (
        0x00000193, 0x00000213, 0x00010513, 0x00000097, 0x018080E7,
        0x00100073, 0x0000006F, 0x00000073, 0x00008067,
        0x00008067, 0x00008067,
        0x00100073, 0x0000006F, 0x00100073, 0x0000006F,
    )
    struct.pack_into("<15I", data, metadata["text_offset"], *words)
    return _encode_application_symbols(data, image, _application_symbols(image))
