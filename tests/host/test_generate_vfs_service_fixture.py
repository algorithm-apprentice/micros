from dataclasses import replace
import unittest

from tests.host.test_generate_bootstrap_fixture import bootstrap_image
from tests.host.test_generate_vm_handoff_fixture import vm_image
from tools import check_user_elf
from tools import generate_vfs_service_fixture


def application_image():
    image = bootstrap_image()
    report = check_user_elf.ElfSymbol(
        name="micros_vfs_service_test_report",
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


class VfsServiceFixtureGeneratorTest(unittest.TestCase):
    def render(self, ramfs=None, vfs=None, application=None):
        if ramfs is None:
            ramfs = bootstrap_image()
        if vfs is None:
            vfs = bootstrap_image()
        if application is None:
            application = application_image()
        return generate_vfs_service_fixture.render_fixture(
            bootstrap_image(),
            vm_image(),
            bootstrap_image(),
            bootstrap_image(),
            ramfs,
            vfs,
            application,
        )

    def test_renders_exact_seven_service_topology_and_profiles(self):
        output = self.render()

        self.assertIn("micros_vfs_service_test_images", output)
        self.assertIn(
            ".entry_count = MICROS_VFS_TEST_SERVICE_COUNT",
            output,
        )
        self.assertIn(
            ".service_id = MICROS_VFS_TEST_VFS_SERVICE_ID",
            output,
        )
        self.assertIn(".image_id = 106", output)
        self.assertIn(
            ".user_page_limit = MICROS_VFS_RESIDENT_PAGE_LIMIT",
            output,
        )
        self.assertIn(
            ".service_id = MICROS_VFS_TEST_APPLICATION_SERVICE_ID",
            output,
        )
        self.assertIn(
            ".image_id = MICROS_VFS_TEST_APPLICATION_IMAGE_ID",
            output,
        )
        self.assertIn(
            ".profile_id = MICROS_VFS_TEST_APPLICATION_PROFILE_ID",
            output,
        )
        self.assertIn(
            ".user_page_limit = "
            "MICROS_VFS_TEST_APPLICATION_RESIDENT_PAGE_LIMIT",
            output,
        )

    def test_requires_application_report_symbol(self):
        with self.assertRaisesRegex(
            check_user_elf.ElfFormatError,
            "report symbol",
        ):
            self.render(application=bootstrap_image())

    def test_emits_vm_boot_metadata_only_for_vm(self):
        output = self.render()

        self.assertEqual(
            output.count(
                ".vm_boot_info_address = "
                "UINT64_C(0x0000000000000000)"
            ),
            6,
        )
        self.assertEqual(
            output.count(
                ".vm_boot_info_size = UINT32_C(0x00000000)"
            ),
            6,
        )

    def test_rejects_vm_metadata_on_non_vm_image(self):
        with self.assertRaisesRegex(
            check_user_elf.ElfFormatError,
            "non-VM image",
        ):
            generate_vfs_service_fixture.render_fixture(
                bootstrap_image(),
                vm_image(),
                bootstrap_image(),
                bootstrap_image(),
                bootstrap_image(),
                vm_image(),
                application_image(),
            )

    def test_rejects_tty_uart_overlap(self):
        image = bootstrap_image()
        overlapping = replace(
            image,
            symbols=tuple(
                replace(
                    symbol,
                    value=generate_vfs_service_fixture.TTY_UART_VIRTUAL_BASE
                    + 1,
                )
                if symbol.name == "__micros_user_image_end"
                else symbol
                for symbol in image.symbols
            ),
        )

        with self.assertRaises(check_user_elf.ElfFormatError):
            generate_vfs_service_fixture.render_fixture(
                bootstrap_image(),
                vm_image(),
                bootstrap_image(),
                overlapping,
                bootstrap_image(),
                bootstrap_image(),
                application_image(),
            )

    def test_accepts_exact_fixed_resident_limits(self):
        ramfs = image_with_page_count(
            bootstrap_image(),
            generate_vfs_service_fixture.RAMFS_RESIDENT_PAGE_LIMIT - 1,
        )
        vfs = image_with_page_count(
            bootstrap_image(),
            generate_vfs_service_fixture.VFS_RESIDENT_PAGE_LIMIT - 1,
        )
        application = image_with_page_count(
            application_image(),
            generate_vfs_service_fixture.APPLICATION_RESIDENT_PAGE_LIMIT
            - 1,
        )

        output = self.render(ramfs, vfs, application)

        self.assertIn(
            ".user_page_limit = MICROS_RAMFS_RESIDENT_PAGE_LIMIT",
            output,
        )
        self.assertIn(
            ".user_page_limit = MICROS_VFS_RESIDENT_PAGE_LIMIT",
            output,
        )
        self.assertIn(
            ".user_page_limit = "
            "MICROS_VFS_TEST_APPLICATION_RESIDENT_PAGE_LIMIT",
            output,
        )

    def test_rejects_images_over_fixed_resident_limits(self):
        cases = (
            (
                "RAMFS resident page limit",
                {
                    "ramfs": image_with_page_count(
                        bootstrap_image(),
                        generate_vfs_service_fixture
                        .RAMFS_RESIDENT_PAGE_LIMIT,
                    )
                },
            ),
            (
                "VFS resident page limit",
                {
                    "vfs": image_with_page_count(
                        bootstrap_image(),
                        generate_vfs_service_fixture
                        .VFS_RESIDENT_PAGE_LIMIT,
                    )
                },
            ),
            (
                "application resident page limit",
                {
                    "application": image_with_page_count(
                        application_image(),
                        generate_vfs_service_fixture
                        .APPLICATION_RESIDENT_PAGE_LIMIT,
                    )
                },
            ),
        )

        for message, arguments in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(
                    check_user_elf.ElfFormatError,
                    message,
                ):
                    self.render(**arguments)

    def test_rejects_aggregate_user_page_limit(self):
        launcher = image_with_page_count(bootstrap_image(), 1000)
        vm = image_with_page_count(vm_image(), 1000)
        pm = image_with_page_count(bootstrap_image(), 1000)
        tty = image_with_page_count(bootstrap_image(), 900)

        with self.assertRaisesRegex(
            check_user_elf.ElfFormatError,
            "aggregate user page limit",
        ):
            generate_vfs_service_fixture.render_fixture(
                launcher,
                vm,
                pm,
                tty,
                bootstrap_image(),
                bootstrap_image(),
                application_image(),
            )


if __name__ == "__main__":
    unittest.main()
