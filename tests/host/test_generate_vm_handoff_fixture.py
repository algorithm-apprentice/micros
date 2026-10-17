from dataclasses import replace
import hashlib
import unittest

from tests.host.test_generate_bootstrap_fixture import bootstrap_image
from tools import check_user_elf
from tools import generate_vm_handoff_fixture
from tests.host import kernel_layout_source

GENERATOR_PATH = "tools/generate_vm_handoff_fixture.py"

def _render_fixture(*args, **kwargs):
    return kernel_layout_source.restore_generated_fixture(
        GENERATOR_PATH,
        generate_vm_handoff_fixture.render_fixture(*args, **kwargs),
    )


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
        output = _render_fixture(
            bootstrap_image(),
            vm_image(),
            probe_image(),
        )

        self.assertEqual(
            "6b988f6fc51404decf3411d27e8102b1d88f6f4af3400ac63490ee89fc53da8f",
            hashlib.sha256(output.encode("utf-8")).hexdigest(),
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
            _render_fixture(
                bootstrap_image(),
                bootstrap_image(),
                probe_image(),
            )

    def test_rejects_vm_boot_object_size_and_alignment(self):
        for change in (
            {"size": 364703},
            {"value": check_user_elf.USER_BASE + 3 * 4096 + 8},
        ):
            image = vm_image()
            image = replace(
                image,
                symbols=tuple(
                    replace(symbol, **change)
                    if symbol.name == "micros_vm_boot_info"
                    else symbol
                    for symbol in image.symbols
                ),
            )
            with self.subTest(change=change):
                with self.assertRaisesRegex(
                    check_user_elf.ElfFormatError,
                    "VM boot information has wrong size or alignment",
                ):
                    _render_fixture(
                        bootstrap_image(), image, probe_image()
                    )

    def test_rejects_vm_boot_object_configuration_overlap(self):
        image = vm_image()
        image = replace(
            image,
            symbols=tuple(
                replace(
                    symbol,
                    value=check_user_elf.USER_BASE + 3 * 4096,
                )
                if symbol.name == "micros_bootstrap_service_config"
                else symbol
                for symbol in image.symbols
            ),
        )
        with self.assertRaisesRegex(
            check_user_elf.ElfFormatError,
            "VM boot information overlaps bootstrap configuration",
        ):
            _render_fixture(
                bootstrap_image(), image, probe_image()
            )


if __name__ == "__main__":
    unittest.main()
