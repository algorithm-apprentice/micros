"""Private checker structural cases assigned by ADR-0063."""

import struct

from tools import check_user_elf
from tests.host.user_elf_test_fixture import build_elf, validation_errors


class UserElfStructureCases:
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
