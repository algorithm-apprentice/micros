from dataclasses import replace
import hashlib
import unittest

from tests.host.test_check_user_elf import build_elf
from tools import check_user_elf
from tools import generate_user_runtime_fixture
from tests.host import kernel_layout_source

GENERATOR_PATH = "tools/generate_user_runtime_fixture.py"

def _render_fixture(*args, **kwargs):
    return kernel_layout_source.restore_generated_fixture(
        GENERATOR_PATH,
        generate_user_runtime_fixture.render_fixture(*args, **kwargs),
    )


def fixture_image():
    data, _ = build_elf()
    image = check_user_elf.parse_elf(bytes(data))
    extras = (
        check_user_elf.ElfSymbol(
            name="micros_user_runtime_test_config",
            binding=check_user_elf.SYMBOL_BIND_GLOBAL,
            symbol_type=1,
            section_index=3,
            value=check_user_elf.USER_BASE + 0x2000,
            size=48,
        ),
        check_user_elf.ElfSymbol(
            name="micros_user_runtime_test_rodata",
            binding=check_user_elf.SYMBOL_BIND_GLOBAL,
            symbol_type=1,
            section_index=2,
            value=check_user_elf.USER_BASE + 0x1000,
            size=8,
        ),
        check_user_elf.ElfSymbol(
            name="micros_user_runtime_test_rodata_fault",
            binding=check_user_elf.SYMBOL_BIND_GLOBAL,
            symbol_type=0,
            section_index=1,
            value=check_user_elf.USER_BASE + 40,
            size=0,
        ),
        check_user_elf.ElfSymbol(
            name="micros_user_runtime_test_rodata_resume",
            binding=check_user_elf.SYMBOL_BIND_GLOBAL,
            symbol_type=0,
            section_index=1,
            value=check_user_elf.USER_BASE + 44,
            size=0,
        ),
    )
    return replace(image, symbols=image.symbols + extras)


class UserRuntimeFixtureGeneratorTest(unittest.TestCase):
    def test_renders_checked_segments_and_symbol_addresses(self):
        output = _render_fixture(
            fixture_image()
        )

        encoded = output.encode("utf-8")
        self.assertEqual(53812, len(encoded))
        self.assertEqual(
            "7a9432c01e562e89d270452df361e0610796c4cbb107a2bce9f1384b7b6a60ea",
            hashlib.sha256(encoded).hexdigest(),
        )
        self.assertIn("segment_0_bytes", output)
        self.assertIn(".segment_count = 3", output)
        self.assertIn(
            ".config_address = UINT64_C(0x0000000040002000)",
            output,
        )
        self.assertIn(
            ".rodata_fault_address = UINT64_C(0x0000000040000028)",
            output,
        )
        self.assertIn(".memory_size = UINT64_C(0x0000000000001000)", output)

    def test_rejects_missing_fixture_symbol(self):
        image = fixture_image()
        image = replace(
            image,
            symbols=tuple(
                symbol
                for symbol in image.symbols
                if symbol.name != "micros_user_runtime_test_config"
            ),
        )

        with self.assertRaises(check_user_elf.ElfFormatError):
            _render_fixture(image)

    def test_rejects_fixture_symbol_in_wrong_permission_class(self):
        image = fixture_image()
        image = replace(
            image,
            symbols=tuple(
                replace(
                    symbol,
                    value=check_user_elf.USER_BASE + 0x1000,
                )
                if symbol.name == "micros_user_runtime_test_config"
                else symbol
                for symbol in image.symbols
            ),
        )

        with self.assertRaises(check_user_elf.ElfFormatError):
            _render_fixture(image)

    def test_preserves_fixture_permission_error_order(self):
        cases = (
            (
                {"micros_user_runtime_test_rodata": 0x2000},
                "fixture rodata probe is not in the read-only segment",
            ),
            (
                {"micros_user_runtime_test_rodata_fault": 0x1000},
                "fixture symbol micros_user_runtime_test_rodata_fault "
                "is not executable",
            ),
            (
                {"micros_user_runtime_test_rodata_resume": 0x1000},
                "fixture symbol micros_user_runtime_test_rodata_resume "
                "is not executable",
            ),
            (
                {
                    "micros_user_runtime_test_config": 0x1000,
                    "micros_user_runtime_test_rodata": 0x2000,
                },
                "fixture configuration is not in the writable segment",
            ),
        )
        for offsets, expected_error in cases:
            image = fixture_image()
            image = replace(
                image,
                symbols=tuple(
                    replace(
                        symbol,
                        value=check_user_elf.USER_BASE + offsets[symbol.name],
                    )
                    if symbol.name in offsets
                    else symbol
                    for symbol in image.symbols
                ),
            )
            with self.subTest(offsets=offsets):
                with self.assertRaises(check_user_elf.ElfFormatError) as raised:
                    _render_fixture(image)
                self.assertEqual(expected_error, str(raised.exception))


if __name__ == "__main__":
    unittest.main()
