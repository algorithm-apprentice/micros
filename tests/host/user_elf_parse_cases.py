"""Private checker parsing cases assigned by ADR-0063."""

import struct

from tools import check_user_elf
from tests.host.user_elf_test_fixture import build_elf, validation_errors


class UserElfParseCases:
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

    def test_rejects_truncated_or_malformed_tables(self):
        image, _ = build_elf()
        with self.assertRaises(check_user_elf.ElfFormatError):
            check_user_elf.parse_elf(bytes(image[:40]))

        image, _ = build_elf()
        struct.pack_into("<H", image, 54, 8)
        with self.assertRaises(check_user_elf.ElfFormatError):
            check_user_elf.parse_elf(bytes(image))
