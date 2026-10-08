from dataclasses import replace
import unittest

from tests.host.test_check_user_elf import build_elf
from tools import check_user_elf
from tools import generate_bootstrap_fixture


def bootstrap_image():
    data, _ = build_elf()
    image = check_user_elf.parse_elf(bytes(data))
    config = check_user_elf.ElfSymbol(
        name="micros_bootstrap_service_config",
        binding=check_user_elf.SYMBOL_BIND_GLOBAL,
        symbol_type=1,
        section_index=4,
        value=check_user_elf.USER_BASE + 0x2080,
        size=128,
    )
    report = check_user_elf.ElfSymbol(
        name="micros_bootstrap_probe_report",
        binding=check_user_elf.SYMBOL_BIND_GLOBAL,
        symbol_type=0,
        section_index=1,
        value=check_user_elf.USER_BASE + 40,
        size=4,
    )
    return replace(image, symbols=image.symbols + (config, report))


class BootstrapFixtureGeneratorTest(unittest.TestCase):
    def test_renders_catalog_and_permuted_manifest(self):
        output = generate_bootstrap_fixture.render_fixture(
            bootstrap_image(),
            bootstrap_image(),
        )

        self.assertIn("micros_bootstrap_test_images", output)
        self.assertIn(".image_id = 101", output)
        self.assertIn(".image_id = 103", output)
        self.assertIn("micros_bootstrap_test_manifest", output)
        self.assertLess(
            output.index("MICROS_BOOTSTRAP_TEST_LAST_SERVICE_ID"),
            output.index("MICROS_BOOTSTRAP_TEST_LAUNCHER_SERVICE_ID"),
        )
        self.assertIn(".total_user_page_limit = 13", output)

    def test_rejects_missing_configuration(self):
        image = bootstrap_image()
        image = replace(
            image,
            symbols=tuple(
                symbol
                for symbol in image.symbols
                if symbol.name != "micros_bootstrap_service_config"
            ),
        )

        with self.assertRaises(check_user_elf.ElfFormatError):
            generate_bootstrap_fixture.render_fixture(image, bootstrap_image())

    def test_rejects_nonzero_file_backed_configuration(self):
        image = bootstrap_image()
        image = replace(
            image,
            symbols=tuple(
                replace(
                    symbol,
                    section_index=3,
                    value=check_user_elf.USER_BASE + 0x2000,
                )
                if symbol.name == "micros_bootstrap_service_config"
                else symbol
                for symbol in image.symbols
            ),
        )

        with self.assertRaises(check_user_elf.ElfFormatError):
            generate_bootstrap_fixture.render_fixture(image, bootstrap_image())


if __name__ == "__main__":
    unittest.main()
