import struct
import unittest

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


class UserElfCheckerTest(unittest.TestCase):
    def test_accepts_exact_freestanding_shape(self):
        image, _ = build_elf()

        parsed = check_user_elf.parse_elf(bytes(image))

        self.assertEqual([], check_user_elf.validate_elf(parsed))
        self.assertEqual(check_user_elf.USER_BASE, parsed.header.entry)

    def test_enforces_complete_resident_page_budget(self):
        image, _ = build_elf()
        parsed = check_user_elf.parse_elf(bytes(image))

        self.assertEqual(
            4,
            check_user_elf.resident_page_count(
                parsed,
                stack_page_count=1,
            ),
        )
        self.assertEqual(
            [],
            check_user_elf.validate_resident_page_limit(
                parsed,
                stack_page_count=1,
                resident_page_limit=4,
            ),
        )
        self.assertEqual(
            ["resident page count 4 exceeds limit 3"],
            check_user_elf.validate_resident_page_limit(
                parsed,
                stack_page_count=1,
                resident_page_limit=3,
            ),
        )

    def test_rejects_header_identity_flags_and_entry(self):
        cases = (
            (16, "<H", 3, "ET_EXEC"),
            (18, "<H", 62, "EM_RISCV"),
            (24, "<Q", check_user_elf.USER_BASE + 4, "entry"),
            (48, "<I", 0x2, "unsupported"),
        )
        for offset, field_format, value, expected in cases:
            with self.subTest(offset=offset):
                image, _ = build_elf()
                struct.pack_into(field_format, image, offset, value)
                self.assertIn(expected, "\n".join(validation_errors(image)))

    def test_rejects_forbidden_program_headers_and_bad_load_shape(self):
        for program_type, name in (
            (check_user_elf.PROGRAM_INTERP, "PT_INTERP"),
            (check_user_elf.PROGRAM_DYNAMIC, "PT_DYNAMIC"),
            (check_user_elf.PROGRAM_TLS, "PT_TLS"),
        ):
            with self.subTest(program_type=name):
                image, metadata = build_elf()
                struct.pack_into(
                    "<I",
                    image,
                    metadata["program_offset"],
                    program_type,
                )
                errors = "\n".join(validation_errors(image))
                self.assertIn(name, errors)
                self.assertIn("exactly three PT_LOAD", errors)

        mutations = (
            (4, "<I", 7, "permission class"),
            (24, "<Q", check_user_elf.USER_BASE + 0x1000, "p_paddr"),
            (48, "<Q", 16, "alignment"),
        )
        for relative_offset, field_format, value, expected in mutations:
            with self.subTest(relative_offset=relative_offset):
                image, metadata = build_elf()
                struct.pack_into(
                    field_format,
                    image,
                    metadata["program_offset"] + relative_offset,
                    value,
                )
                self.assertIn(expected, "\n".join(validation_errors(image)))

    def test_rejects_load_overlap_size_and_range(self):
        image, metadata = build_elf()
        second = (
            metadata["program_offset"]
            + check_user_elf.PROGRAM_HEADER.size
        )
        struct.pack_into(
            "<Q",
            image,
            second + 16,
            check_user_elf.USER_BASE + 0x800,
        )
        struct.pack_into(
            "<Q",
            image,
            second + 24,
            check_user_elf.USER_BASE + 0x800,
        )
        self.assertIn("overlap", "\n".join(validation_errors(image)))

        image, metadata = build_elf()
        struct.pack_into(
            "<Q",
            image,
            metadata["program_offset"] + 32,
            0x1001,
        )
        self.assertIn(
            "p_filesz greater than p_memsz",
            "\n".join(validation_errors(image)),
        )

        image, metadata = build_elf()
        struct.pack_into(
            "<Q",
            image,
            metadata["program_offset"] + 8,
            len(image) + 0x1000,
        )
        self.assertIn(
            "file range is truncated",
            "\n".join(validation_errors(image)),
        )

        image, metadata = build_elf()
        third = (
            metadata["program_offset"]
            + 2 * check_user_elf.PROGRAM_HEADER.size
        )
        struct.pack_into(
            "<Q",
            image,
            third + 16,
            check_user_elf.USER_END,
        )
        struct.pack_into(
            "<Q",
            image,
            third + 24,
            check_user_elf.USER_END,
        )
        self.assertIn(
            "outside the user window",
            "\n".join(validation_errors(image)),
        )

    def test_rejects_forbidden_allocatable_runtime_sections(self):
        forbidden = (
            ".dynamic",
            ".dynsym",
            ".got",
            ".preinit_array",
            ".init_array",
            ".fini_array",
            ".ctors",
            ".dtors",
            ".tdata",
            ".tbss",
            ".sdata",
            ".sbss",
            ".stack",
            ".eh_frame",
            ".gcc_except_table",
            ".asan_globals",
            ".llvm_prf_data",
            ".gnu.version_r",
        )
        for section_name in forbidden:
            with self.subTest(section_name=section_name):
                image, _ = build_elf(data_section_name=section_name)
                errors = "\n".join(validation_errors(image))
                self.assertIn(
                    f"forbidden section {section_name}",
                    errors,
                )
                self.assertIn("missing required section .data", errors)

    def test_rejects_relocation_sections(self):
        image, _ = build_elf(extra_relocation=True)
        errors = "\n".join(validation_errors(image))
        self.assertIn("relocation section .rela.text is forbidden", errors)

    def test_rejects_undefined_symbols(self):
        image, _ = build_elf(undefined_symbol=True)

        self.assertIn(
            "undefined symbol host_dependency",
            "\n".join(validation_errors(image)),
        )

    def test_rejects_bss_or_allocatable_section_mismatch(self):
        image, metadata = build_elf()
        bss_header = (
            metadata["section_offset"]
            + 4 * check_user_elf.SECTION_HEADER.size
        )
        struct.pack_into(
            "<Q",
            image,
            bss_header + 8,
            check_user_elf.SECTION_ALLOC,
        )
        self.assertIn(
            "required section .bss has the wrong shape",
            "\n".join(validation_errors(image)),
        )

        image, metadata = build_elf()
        rodata_header = (
            metadata["section_offset"]
            + 2 * check_user_elf.SECTION_HEADER.size
        )
        struct.pack_into(
            "<Q",
            image,
            rodata_header + 16,
            check_user_elf.USER_END,
        )
        self.assertIn(
            "allocatable section .rodata is not closed",
            "\n".join(validation_errors(image)),
        )

    def test_rejects_shortened_text_and_file_backed_bss(self):
        image, metadata = build_elf()
        text_header = (
            metadata["section_offset"]
            + check_user_elf.SECTION_HEADER.size
        )
        struct.pack_into("<Q", image, text_header + 32, 16)
        self.assertIn(
            ".text does not match its exported linker range",
            "\n".join(validation_errors(image)),
        )

        image, metadata = build_elf()
        writable_program = (
            metadata["program_offset"]
            + 2 * check_user_elf.PROGRAM_HEADER.size
        )
        struct.pack_into("<Q", image, writable_program + 32, 0x1000)
        self.assertIn(
            "writable PT_LOAD file range must end at BSS start",
            "\n".join(validation_errors(image)),
        )

    def test_rejects_raw_stub_startup_return_and_gp_tp_mutations(self):
        mutations = (
            (0x1000 + 24, 0x00100073, "ecall; ret"),
            (0x1000 + 16, 0x00000013, "ebreak and loop"),
            (0x1000, 0x00000213, "initialize gp and tp"),
            (0x1000 + 32, 0x00000193, "outside startup"),
        )
        for offset, value, expected in mutations:
            with self.subTest(offset=offset):
                image, _ = build_elf()
                struct.pack_into("<I", image, offset, value)
                self.assertIn(expected, "\n".join(validation_errors(image)))

    def test_rejects_truncated_or_malformed_tables(self):
        image, _ = build_elf()
        with self.assertRaises(check_user_elf.ElfFormatError):
            check_user_elf.parse_elf(bytes(image[:40]))

        image, _ = build_elf()
        struct.pack_into("<H", image, 54, 8)
        with self.assertRaises(check_user_elf.ElfFormatError):
            check_user_elf.parse_elf(bytes(image))


if __name__ == "__main__":
    unittest.main()
