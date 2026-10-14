"""Private service-ELF byte fixture and validation adapter."""

import struct

from tools import check_user_elf


def align(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def make_string_table(names):
    table = bytearray(b"\0")
    offsets = {"": 0}
    for name in names:
        offsets[name] = len(table)
        table.extend(name.encode("ascii"))
        table.append(0)
    return bytes(table), offsets


def build_elf(
    *,
    data_section_name=".data",
    extra_relocation=False,
    undefined_symbol=False,
):
    base = check_user_elf.USER_BASE
    text_offset = 0x1000
    rodata_offset = 0x2000
    data_offset = 0x3000
    text = bytearray(check_user_elf.PAGE_SIZE)
    words = (
        0x00000193,
        0x00000213,
        0x00000097,
        0x018080E7,
        0x00100073,
        0x0000006F,
        0x00000073,
        0x00008067,
        0x00008067,
    )
    struct.pack_into("<9I", text, 0, *words)
    rodata = bytes([0x5A]) * check_user_elf.PAGE_SIZE
    data_bytes = bytes.fromhex("1122334455667788")

    symbol_names = list(check_user_elf.REQUIRED_SYMBOLS)
    if undefined_symbol:
        symbol_names.append("host_dependency")
    symbol_strings, symbol_name_offsets = make_string_table(symbol_names)

    symbols = [bytes(check_user_elf.SYMBOL.size)]

    def add_symbol(name, section, value, size=0, symbol_type=0):
        symbols.append(
            check_user_elf.SYMBOL.pack(
                symbol_name_offsets[name],
                (
                    check_user_elf.SYMBOL_BIND_GLOBAL << 4
                    | symbol_type
                ),
                0,
                section,
                value,
                size,
            )
        )

    add_symbol("_start", 1, base, 24, 2)
    add_symbol("micros_service_main", 1, base + 32, 4, 2)
    add_symbol("micros_runtime_raw_syscall", 1, base + 24, 8, 2)
    add_symbol("micros_runtime_service_returned", 1, base + 16)
    add_symbol("__micros_user_image_start", 1, base)
    add_symbol("__micros_user_text_start", 1, base)
    add_symbol("__micros_user_text_end", 2, base + 0x1000)
    add_symbol("__micros_user_rodata_start", 2, base + 0x1000)
    add_symbol("__micros_user_rodata_end", 3, base + 0x2000)
    add_symbol("__micros_user_data_start", 3, base + 0x2000)
    add_symbol("__micros_user_data_end", 3, base + 0x2008)
    add_symbol("__micros_user_bss_start", 4, base + 0x2008)
    add_symbol("__micros_user_bss_end", 4, base + 0x3000)
    add_symbol("__micros_user_image_end", 4, base + 0x3000)
    if undefined_symbol:
        add_symbol("host_dependency", 0, 0)
    symbol_table = b"".join(symbols)

    section_names = [
        ".text",
        ".rodata",
        data_section_name,
        ".bss",
        ".symtab",
        ".strtab",
        ".shstrtab",
    ]
    if extra_relocation:
        section_names.insert(4, ".rela.text")
    section_strings, section_name_offsets = make_string_table(section_names)

    cursor = data_offset + len(data_bytes)
    relocation_offset = None
    if extra_relocation:
        cursor = align(cursor, 8)
        relocation_offset = cursor
        cursor += 24
    symbol_offset = align(cursor, 8)
    cursor = symbol_offset + len(symbol_table)
    string_offset = cursor
    cursor += len(symbol_strings)
    section_string_offset = cursor
    cursor += len(section_strings)
    section_offset = align(cursor, 8)

    section_count = 9 if extra_relocation else 8
    symbol_section_index = 6 if extra_relocation else 5
    string_section_index = symbol_section_index + 1
    section_string_index = string_section_index + 1
    total_size = section_offset + section_count * check_user_elf.SECTION_HEADER.size
    image = bytearray(total_size)
    image[text_offset : text_offset + len(text)] = text
    image[rodata_offset : rodata_offset + len(rodata)] = rodata
    image[data_offset : data_offset + len(data_bytes)] = data_bytes
    if extra_relocation:
        image[relocation_offset : relocation_offset + 24] = bytes(24)
    image[symbol_offset : symbol_offset + len(symbol_table)] = symbol_table
    image[string_offset : string_offset + len(symbol_strings)] = symbol_strings
    image[
        section_string_offset : section_string_offset + len(section_strings)
    ] = section_strings

    identification = bytearray(16)
    identification[:4] = b"\x7fELF"
    identification[4] = check_user_elf.ELF_CLASS_64
    identification[5] = check_user_elf.ELF_DATA_LITTLE
    identification[6] = check_user_elf.ELF_VERSION_CURRENT
    check_user_elf.ELF_HEADER.pack_into(
        image,
        0,
        bytes(identification),
        check_user_elf.ELF_TYPE_EXECUTABLE,
        check_user_elf.ELF_MACHINE_RISCV,
        check_user_elf.ELF_VERSION_CURRENT,
        base,
        check_user_elf.ELF_HEADER.size,
        section_offset,
        check_user_elf.ELF_FLAG_RISCV_RVC,
        check_user_elf.ELF_HEADER.size,
        check_user_elf.PROGRAM_HEADER.size,
        3,
        check_user_elf.SECTION_HEADER.size,
        section_count,
        section_string_index,
    )
    programs = (
        (
            check_user_elf.PROGRAM_LOAD,
            check_user_elf.PROGRAM_READ | check_user_elf.PROGRAM_EXECUTE,
            text_offset,
            base,
            base,
            0x1000,
            0x1000,
            0x1000,
        ),
        (
            check_user_elf.PROGRAM_LOAD,
            check_user_elf.PROGRAM_READ,
            rodata_offset,
            base + 0x1000,
            base + 0x1000,
            0x1000,
            0x1000,
            0x1000,
        ),
        (
            check_user_elf.PROGRAM_LOAD,
            check_user_elf.PROGRAM_READ | check_user_elf.PROGRAM_WRITE,
            data_offset,
            base + 0x2000,
            base + 0x2000,
            len(data_bytes),
            0x1000,
            0x1000,
        ),
    )
    for index, program in enumerate(programs):
        check_user_elf.PROGRAM_HEADER.pack_into(
            image,
            check_user_elf.ELF_HEADER.size
            + index * check_user_elf.PROGRAM_HEADER.size,
            *program,
        )

    sections = [
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (
            section_name_offsets[".text"],
            check_user_elf.SECTION_PROGBITS,
            check_user_elf.SECTION_ALLOC | check_user_elf.SECTION_EXECUTE,
            base,
            text_offset,
            0x1000,
            0,
            0,
            4,
            0,
        ),
        (
            section_name_offsets[".rodata"],
            check_user_elf.SECTION_PROGBITS,
            check_user_elf.SECTION_ALLOC,
            base + 0x1000,
            rodata_offset,
            0x1000,
            0,
            0,
            8,
            0,
        ),
        (
            section_name_offsets[data_section_name],
            check_user_elf.SECTION_PROGBITS,
            check_user_elf.SECTION_ALLOC | check_user_elf.SECTION_WRITE,
            base + 0x2000,
            data_offset,
            len(data_bytes),
            0,
            0,
            8,
            0,
        ),
        (
            section_name_offsets[".bss"],
            check_user_elf.SECTION_NOBITS,
            check_user_elf.SECTION_ALLOC | check_user_elf.SECTION_WRITE,
            base + 0x2008,
            data_offset + len(data_bytes),
            0xFF8,
            0,
            0,
            8,
            0,
        ),
    ]
    if extra_relocation:
        sections.append(
            (
                section_name_offsets[".rela.text"],
                check_user_elf.SECTION_RELA,
                0,
                0,
                relocation_offset,
                24,
                symbol_section_index,
                1,
                8,
                24,
            )
        )
    sections.extend(
        (
            (
                section_name_offsets[".symtab"],
                check_user_elf.SECTION_SYMTAB,
                0,
                0,
                symbol_offset,
                len(symbol_table),
                string_section_index,
                1,
                8,
                check_user_elf.SYMBOL.size,
            ),
            (
                section_name_offsets[".strtab"],
                check_user_elf.SECTION_STRTAB,
                0,
                0,
                string_offset,
                len(symbol_strings),
                0,
                0,
                1,
                0,
            ),
            (
                section_name_offsets[".shstrtab"],
                check_user_elf.SECTION_STRTAB,
                0,
                0,
                section_string_offset,
                len(section_strings),
                0,
                0,
                1,
                0,
            ),
        )
    )
    for index, section in enumerate(sections):
        check_user_elf.SECTION_HEADER.pack_into(
            image,
            section_offset + index * check_user_elf.SECTION_HEADER.size,
            *section,
        )
    metadata = {
        "program_offset": check_user_elf.ELF_HEADER.size,
        "section_offset": section_offset,
        "symbol_offset": symbol_offset,
        "raw_symbol_index": 3,
        "text_offset": text_offset,
    }
    return image, metadata


def validation_errors(image):
    return check_user_elf.validate_elf(check_user_elf.parse_elf(bytes(image)))
