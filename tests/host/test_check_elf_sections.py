import unittest

from tools import check_elf_sections


VALID_SECTIONS = """\
There are 4 section headers, starting at offset 0x1000:

Section Headers:
  [Nr] Name      Type      Address          Off    Size   ES Flg Lk Inf Al
  [ 0]           NULL      0000000000000000 000000 000000 00      0   0  0
  [ 1] .text     PROGBITS  0000000080200000 001000 002000 00  AX  0   0 16
  [ 2] .rodata   PROGBITS  0000000080202000 003000 001000 01 AMS  0   0 16
  [ 3] .bss      NOBITS    0000000080203000 004000 004000 00  WA  0   0 16
Key to Flags:
"""

VALID_SYMBOLS = """\
0000000080200000 T __kernel_start
0000000080200000 T __kernel_text_start
0000000080202000 T __kernel_text_end
0000000080202000 R __kernel_rodata_start
0000000080203000 R __kernel_rodata_end
0000000080203000 B __kernel_writable_start
0000000080207000 B __kernel_writable_end
0000000080207000 B __kernel_end
"""


class ElfSectionParsingTest(unittest.TestCase):
    def test_parses_allocatable_sections_and_permission_symbols(self):
        sections = check_elf_sections.parse_section_headers(VALID_SECTIONS)
        symbols = check_elf_sections.parse_symbol_addresses(VALID_SYMBOLS)

        self.assertEqual(
            [
                check_elf_sections.ElfSection(
                    name=".text",
                    address=0x80200000,
                    size=0x2000,
                    flags=frozenset(("A", "X")),
                ),
                check_elf_sections.ElfSection(
                    name=".rodata",
                    address=0x80202000,
                    size=0x1000,
                    flags=frozenset(("A", "M", "S")),
                ),
                check_elf_sections.ElfSection(
                    name=".bss",
                    address=0x80203000,
                    size=0x4000,
                    flags=frozenset(("A", "W")),
                ),
            ],
            sections,
        )
        self.assertEqual(0x80200000, symbols["__kernel_text_start"])
        self.assertEqual(0x80207000, symbols["__kernel_writable_end"])

    def test_rejects_malformed_section_or_symbol_output(self):
        with self.assertRaises(ValueError):
            check_elf_sections.parse_section_headers(
                VALID_SECTIONS.replace("002000", "nothex", 1)
            )
        with self.assertRaises(ValueError):
            check_elf_sections.parse_symbol_addresses(
                VALID_SYMBOLS
                + "0000000080201000 T __kernel_text_start\n"
            )


class ElfSectionValidationTest(unittest.TestCase):
    def validate(self, sections=VALID_SECTIONS, symbols=VALID_SYMBOLS):
        return check_elf_sections.validate_allocatable_sections(
            check_elf_sections.parse_section_headers(sections),
            check_elf_sections.parse_symbol_addresses(symbols),
        )

    def test_accepts_exact_permission_ranges(self):
        self.assertEqual([], self.validate())

    def test_rejects_allocatable_orphan(self):
        orphan = VALID_SECTIONS.replace(".bss", "unexpected")

        self.assertIn(
            "unexpected allocatable section",
            "\n".join(self.validate(orphan)),
        )

    def test_rejects_section_outside_permission_ranges(self):
        outside = VALID_SECTIONS.replace(
            "0000000080203000 004000 004000",
            "0000000080207000 004000 001000",
        )

        self.assertIn(
            "outside permission ranges",
            "\n".join(self.validate(outside)),
        )

    def test_rejects_boundary_crossing_section(self):
        crossing = VALID_SECTIONS.replace(
            "0000000080200000 001000 002000",
            "0000000080200000 001000 002001",
        )

        self.assertIn("crosses permission boundary", "\n".join(self.validate(crossing)))

    def test_rejects_incorrect_or_wx_flags(self):
        invalid_cases = (
            VALID_SECTIONS.replace("00  AX", "00 WAX"),
            VALID_SECTIONS.replace("01 AMS", "01 AMSX"),
            VALID_SECTIONS.replace("00  WA", "00   A"),
        )

        for sections in invalid_cases:
            with self.subTest(sections=sections):
                self.assertIn(
                    "flags do not match",
                    "\n".join(self.validate(sections)),
                )

    def test_rejects_missing_or_invalid_permission_ranges(self):
        missing = VALID_SYMBOLS.replace(
            "0000000080203000 R __kernel_rodata_end\n",
            "",
        )
        overlap = VALID_SYMBOLS.replace(
            "0000000080203000 R __kernel_rodata_end",
            "0000000080204000 R __kernel_rodata_end",
        )

        self.assertIn("missing symbol", "\n".join(self.validate(symbols=missing)))
        self.assertIn(
            "permission ranges are not contiguous",
            "\n".join(self.validate(symbols=overlap)),
        )

    def test_rejects_alias_mismatch_or_unaligned_boundary(self):
        start_mismatch = VALID_SYMBOLS.replace(
            "0000000080200000 T __kernel_start",
            "00000000801ff000 T __kernel_start",
        )
        end_mismatch = VALID_SYMBOLS.replace(
            "0000000080207000 B __kernel_end",
            "0000000080208000 B __kernel_end",
        )

        self.assertIn(
            "__kernel_start must equal __kernel_text_start",
            "\n".join(self.validate(symbols=start_mismatch)),
        )
        self.assertIn(
            "__kernel_end must equal __kernel_writable_end",
            "\n".join(self.validate(symbols=end_mismatch)),
        )

        for symbol in (
            "__kernel_start",
            "__kernel_text_start",
            "__kernel_text_end",
            "__kernel_rodata_start",
            "__kernel_rodata_end",
            "__kernel_writable_start",
            "__kernel_writable_end",
            "__kernel_end",
        ):
            with self.subTest(symbol=symbol):
                lines = []
                for line in VALID_SYMBOLS.splitlines():
                    if line.endswith(f" {symbol}"):
                        address, kind, name = line.split()
                        line = f"{int(address, 16) + 1:016x} {kind} {name}"
                    lines.append(line)
                unaligned = "\n".join(lines) + "\n"
                self.assertIn(
                    "must be page-aligned",
                    "\n".join(self.validate(symbols=unaligned)),
                )


if __name__ == "__main__":
    unittest.main()
