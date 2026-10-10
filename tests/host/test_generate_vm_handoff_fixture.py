from dataclasses import replace
import unittest

from tests.host.test_generate_bootstrap_fixture import bootstrap_image
from tools import check_user_elf
from tools import generate_vm_handoff_fixture


def vm_image():
    image = bootstrap_image()
    programs = tuple(
        replace(program, memory_size=91 * 4096)
        if program.flags
        == (check_user_elf.PROGRAM_READ | check_user_elf.PROGRAM_WRITE)
        else program
        for program in image.programs
    )
    sections = tuple(
        replace(
            section,
            size=(
                check_user_elf.USER_BASE
                + 93 * 4096
                - section.address
            ),
        )
        if section.name == ".bss"
        else section
        for section in image.sections
    )
    symbols = []
    for symbol in image.symbols:
        if symbol.name in (
            "__micros_user_bss_end",
            "__micros_user_image_end",
        ):
            symbols.append(
                replace(
                    symbol,
                    value=check_user_elf.USER_BASE + 93 * 4096,
                )
            )
        else:
            symbols.append(symbol)
    symbols.append(
        check_user_elf.ElfSymbol(
            name="micros_vm_boot_info",
            binding=check_user_elf.SYMBOL_BIND_GLOBAL,
            symbol_type=1,
            section_index=4,
            value=check_user_elf.USER_BASE + 3 * 4096,
            size=364704,
        )
    )
    return replace(
        image,
        programs=programs,
        sections=sections,
        symbols=tuple(symbols),
    )


def probe_image():
    image = bootstrap_image()
    report = check_user_elf.ElfSymbol(
        name="micros_vm_handoff_probe_report",
        binding=check_user_elf.SYMBOL_BIND_GLOBAL,
        symbol_type=0,
        section_index=1,
        value=check_user_elf.USER_BASE + 44,
        size=4,
    )
    return replace(image, symbols=image.symbols + (report,))


class VmHandoffFixtureGeneratorTest(unittest.TestCase):
    def test_renders_vm_metadata_and_topology(self):
        output = generate_vm_handoff_fixture.render_fixture(
            bootstrap_image(),
            vm_image(),
            probe_image(),
        )

        self.assertIn("micros_vm_handoff_test_images", output)
        self.assertIn("MICROS_BOOTSTRAP_ROLE_VM", output)
        self.assertIn(
            ".vm_boot_info_address = UINT64_C(0x0000000040003000)",
            output,
        )
        self.assertIn(
            ".vm_boot_info_size = UINT32_C(0x000590a0)",
            output,
        )
        self.assertIn(".total_user_page_limit = 103", output)

    def test_rejects_vm_without_boot_object(self):
        with self.assertRaises(check_user_elf.ElfFormatError):
            generate_vm_handoff_fixture.render_fixture(
                bootstrap_image(),
                bootstrap_image(),
                probe_image(),
            )


if __name__ == "__main__":
    unittest.main()
