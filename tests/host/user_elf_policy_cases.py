"""Private checker policy cases assigned by ADR-0063."""

import struct

from tools import check_user_elf
from tests.host.user_elf_test_fixture import build_elf, validation_errors


class UserElfPolicyCases:
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

    def test_rejects_undefined_symbols(self):
        image, _ = build_elf(undefined_symbol=True)

        self.assertIn(
            "undefined symbol host_dependency",
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
