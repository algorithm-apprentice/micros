from dataclasses import replace
import unittest

from tests.host.test_generate_bootstrap_fixture import bootstrap_image
from tests.host.test_generate_vm_handoff_fixture import vm_image
from tools import check_user_elf
from tools import generate_pm_service_fixture


def pm_image():
    image = bootstrap_image()
    report = check_user_elf.ElfSymbol(
        name="micros_pm_service_report",
        binding=check_user_elf.SYMBOL_BIND_GLOBAL,
        symbol_type=0,
        section_index=1,
        value=check_user_elf.USER_BASE + 52,
        size=4,
    )
    return replace(image, symbols=image.symbols + (report,))


class PmServiceFixtureGeneratorTest(unittest.TestCase):
    def test_renders_pm_topology_and_report(self):
        output = generate_pm_service_fixture.render_fixture(
            bootstrap_image(),
            vm_image(),
            pm_image(),
            bootstrap_image(),
        )

        self.assertIn("micros_pm_service_test_images", output)
        self.assertIn("MICROS_BOOTSTRAP_ROLE_PM", output)
        self.assertIn('                .service_name = "pm",', output)
        self.assertIn(
            "0x0000000040000034",
            output,
        )

    def test_rejects_pm_without_report_symbol(self):
        with self.assertRaises(check_user_elf.ElfFormatError):
            generate_pm_service_fixture.render_fixture(
                bootstrap_image(),
                vm_image(),
                bootstrap_image(),
                bootstrap_image(),
            )


if __name__ == "__main__":
    unittest.main()
