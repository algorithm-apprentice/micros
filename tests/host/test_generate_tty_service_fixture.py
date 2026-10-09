from dataclasses import replace
import unittest

from tests.host.test_generate_bootstrap_fixture import bootstrap_image
from tests.host.test_generate_vm_handoff_fixture import vm_image
from tools import check_user_elf
from tools import generate_tty_service_fixture


def vfs_image():
    image = bootstrap_image()
    report = check_user_elf.ElfSymbol(
        name="micros_tty_service_test_report",
        binding=check_user_elf.SYMBOL_BIND_GLOBAL,
        symbol_type=0,
        section_index=1,
        value=check_user_elf.USER_BASE + 60,
        size=4,
    )
    return replace(image, symbols=image.symbols + (report,))


class TtyServiceFixtureGeneratorTest(unittest.TestCase):
    def test_renders_tty_topology_mapping_and_report(self):
        output = generate_tty_service_fixture.render_fixture(
            bootstrap_image(),
            vm_image(),
            bootstrap_image(),
            bootstrap_image(),
            vfs_image(),
        )

        self.assertIn("micros_tty_service_test_images", output)
        self.assertIn("MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER", output)
        self.assertIn("MICROS_TTY_UART_PHYSICAL_BASE", output)
        self.assertIn('                .service_name = "tty",', output)
        self.assertIn("0x000000004000003c", output)

    def test_rejects_vfs_without_report_symbol(self):
        with self.assertRaises(check_user_elf.ElfFormatError):
            generate_tty_service_fixture.render_fixture(
                bootstrap_image(),
                vm_image(),
                bootstrap_image(),
                bootstrap_image(),
                bootstrap_image(),
            )

    def test_rejects_tty_image_overlapping_uart_mapping(self):
        image = bootstrap_image()
        overlapping = replace(
            image,
            symbols=tuple(
                replace(
                    symbol,
                    value=generate_tty_service_fixture.TTY_UART_VIRTUAL_BASE
                        + 1,
                )
                if symbol.name == "__micros_user_image_end"
                else symbol
                for symbol in image.symbols
            ),
        )

        with self.assertRaises(check_user_elf.ElfFormatError):
            generate_tty_service_fixture.render_fixture(
                bootstrap_image(),
                vm_image(),
                bootstrap_image(),
                overlapping,
                vfs_image(),
            )


if __name__ == "__main__":
    unittest.main()
