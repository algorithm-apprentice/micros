from dataclasses import replace
import unittest

from tests.host.test_generate_bootstrap_fixture import bootstrap_image
from tests.host.test_generate_vm_handoff_fixture import vm_image
from tools import check_user_elf
from tools import generate_ramfs_service_fixture


def vfs_image():
    image = bootstrap_image()
    report = check_user_elf.ElfSymbol(
        name="micros_ramfs_service_test_report",
        binding=check_user_elf.SYMBOL_BIND_GLOBAL,
        symbol_type=0,
        section_index=1,
        value=check_user_elf.USER_BASE + 60,
        size=4,
    )
    return replace(image, symbols=image.symbols + (report,))


def image_with_page_count(image, page_count):
    current = sum(
        program.memory_size // check_user_elf.PAGE_SIZE
        for program in image.programs
        if program.program_type == check_user_elf.PROGRAM_LOAD
    )
    extra_pages = page_count - current
    if extra_pages < 0:
        raise ValueError("requested page count is below the fixture minimum")
    programs = list(image.programs)
    data_index = max(
        index
        for index, program in enumerate(programs)
        if program.program_type == check_user_elf.PROGRAM_LOAD
    )
    programs[data_index] = replace(
        programs[data_index],
        memory_size=programs[data_index].memory_size
        + extra_pages * check_user_elf.PAGE_SIZE,
    )
    image_end = (
        programs[data_index].virtual_address
        + programs[data_index].memory_size
    )
    symbols = tuple(
        replace(symbol, value=image_end)
        if symbol.name
        in ("__micros_user_bss_end", "__micros_user_image_end")
        else symbol
        for symbol in image.symbols
    )
    sections = tuple(
        replace(
            section,
            size=section.size
            + extra_pages * check_user_elf.PAGE_SIZE,
        )
        if section.name == ".bss"
        else section
        for section in image.sections
    )
    return replace(
        image,
        programs=tuple(programs),
        sections=sections,
        symbols=symbols,
    )


class RamfsServiceFixtureGeneratorTest(unittest.TestCase):
    def render(self, ramfs=None):
        if ramfs is None:
            ramfs = bootstrap_image()
        return generate_ramfs_service_fixture.render_fixture(
            bootstrap_image(),
            vm_image(),
            bootstrap_image(),
            bootstrap_image(),
            ramfs,
            vfs_image(),
        )

    def test_renders_six_service_topology_and_ramfs_budget(self):
        output = self.render()

        self.assertIn("micros_ramfs_service_test_images", output)
        self.assertIn(
            ".entry_count = MICROS_RAMFS_TEST_SERVICE_COUNT",
            output,
        )
        self.assertIn(
            ".service_id = MICROS_RAMFS_TEST_RAMFS_SERVICE_ID",
            output,
        )
        self.assertIn(".image_id = 105", output)
        self.assertIn(
            ".user_page_limit = "
            "MICROS_RAMFS_RESIDENT_PAGE_LIMIT",
            output,
        )
        self.assertIn(
            ".service_id = MICROS_RAMFS_TEST_VFS_SERVICE_ID",
            output,
        )
        self.assertIn(".image_id = 106", output)

    def test_requires_vfs_report_symbol(self):
        with self.assertRaisesRegex(
            check_user_elf.ElfFormatError,
            "report symbol",
        ):
            generate_ramfs_service_fixture.render_fixture(
                bootstrap_image(),
                vm_image(),
                bootstrap_image(),
                bootstrap_image(),
                bootstrap_image(),
                bootstrap_image(),
            )

    def test_emits_vm_boot_metadata_only_for_vm(self):
        output = self.render()

        self.assertEqual(
            output.count(
                ".vm_boot_info_address = "
                "UINT64_C(0x0000000000000000)"
            ),
            5,
        )
        self.assertEqual(
            output.count(
                ".vm_boot_info_size = UINT32_C(0x00000000)"
            ),
            5,
        )

    def test_rejects_vm_metadata_on_non_vm_image(self):
        with self.assertRaisesRegex(
            check_user_elf.ElfFormatError,
            "non-VM image",
        ):
            generate_ramfs_service_fixture.render_fixture(
                bootstrap_image(),
                vm_image(),
                bootstrap_image(),
                bootstrap_image(),
                vm_image(),
                vfs_image(),
            )

    def test_rejects_tty_uart_overlap(self):
        image = bootstrap_image()
        overlapping = replace(
            image,
            symbols=tuple(
                replace(
                    symbol,
                    value=generate_ramfs_service_fixture.TTY_UART_VIRTUAL_BASE
                    + 1,
                )
                if symbol.name == "__micros_user_image_end"
                else symbol
                for symbol in image.symbols
            ),
        )

        with self.assertRaises(check_user_elf.ElfFormatError):
            generate_ramfs_service_fixture.render_fixture(
                bootstrap_image(),
                vm_image(),
                bootstrap_image(),
                overlapping,
                bootstrap_image(),
                vfs_image(),
            )

    def test_accepts_exact_ramfs_resident_limit(self):
        ramfs = image_with_page_count(
            bootstrap_image(),
            generate_ramfs_service_fixture.RAMFS_RESIDENT_PAGE_LIMIT - 1,
        )

        output = self.render(ramfs)

        self.assertIn(
            ".user_page_limit = "
            "MICROS_RAMFS_RESIDENT_PAGE_LIMIT",
            output,
        )

    def test_rejects_ramfs_image_over_resident_limit(self):
        ramfs = image_with_page_count(
            bootstrap_image(),
            generate_ramfs_service_fixture.RAMFS_RESIDENT_PAGE_LIMIT,
        )

        with self.assertRaisesRegex(
            check_user_elf.ElfFormatError,
            "RAMFS resident page limit",
        ):
            self.render(ramfs)

    def test_rejects_aggregate_user_page_limit(self):
        launcher = image_with_page_count(bootstrap_image(), 1000)
        vm = image_with_page_count(vm_image(), 1000)
        pm = image_with_page_count(bootstrap_image(), 1000)
        tty = image_with_page_count(bootstrap_image(), 500)
        ramfs = image_with_page_count(bootstrap_image(), 191)
        vfs = image_with_page_count(vfs_image(), 500)

        with self.assertRaisesRegex(
            check_user_elf.ElfFormatError,
            "aggregate user page limit",
        ):
            generate_ramfs_service_fixture.render_fixture(
                launcher,
                vm,
                pm,
                tty,
                ramfs,
                vfs,
            )


if __name__ == "__main__":
    unittest.main()
