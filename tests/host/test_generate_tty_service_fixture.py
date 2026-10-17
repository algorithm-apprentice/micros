from dataclasses import replace
import hashlib
import unittest
from unittest.mock import patch

from tests.host.test_generate_bootstrap_fixture import bootstrap_image
from tests.host.test_generate_vm_handoff_fixture import vm_image
from tools import check_user_elf
from tools import generate_tty_service_fixture
from tests.host import kernel_layout_source

GENERATOR_PATH = "tools/generate_tty_service_fixture.py"

def _render_fixture(*args, **kwargs):
    return kernel_layout_source.restore_generated_fixture(
        GENERATOR_PATH,
        generate_tty_service_fixture.render_fixture(*args, **kwargs),
    )


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
    def test_retains_complete_original_fixture_utf8_bytes(self):
        output = _render_fixture(
            bootstrap_image(), vm_image(), bootstrap_image(),
            bootstrap_image(), vfs_image(),
        ).encode("utf-8")

        self.assertEqual(271768, len(output))
        self.assertEqual(
            "0c3cc187b1affd99259d2eae58a546e38949568ad236e371a31095205cd1f0b9",
            hashlib.sha256(output).hexdigest(),
        )

    def test_retains_descriptor_validation_order_before_metadata_checks(self):
        owner = generate_tty_service_fixture.generate_bootstrap_fixture
        with patch.object(owner, "_validated_descriptor", wraps=owner._validated_descriptor) as validated:
            _render_fixture(
                bootstrap_image(), vm_image(), bootstrap_image(),
                bootstrap_image(), vfs_image(),
            )

        self.assertEqual(
            ["launcher", "vm", "pm", "tty", "vfs"],
            [call.args[1] for call in validated.call_args_list],
        )

    def test_retains_original_vm_non_vm_report_and_uart_error_precedence(self):
        owner = generate_tty_service_fixture.generate_bootstrap_fixture
        validate = owner._validated_descriptor

        def with_tty_bound_overlap(image, name):
            descriptor = validate(image, name)
            if name == "tty":
                descriptor["image_end"] = generate_tty_service_fixture.TTY_UART_VIRTUAL_BASE + 1
            return descriptor

        with patch.object(owner, "_validated_descriptor", side_effect=with_tty_bound_overlap):
            with self.assertRaisesRegex(check_user_elf.ElfFormatError, "VM image has no boot-information object"):
                _render_fixture(
                    bootstrap_image(), bootstrap_image(), bootstrap_image(),
                    bootstrap_image(), bootstrap_image(),
                )
            with self.assertRaisesRegex(check_user_elf.ElfFormatError, "non-VM image has VM boot-information metadata"):
                _render_fixture(
                    vm_image(), vm_image(), bootstrap_image(),
                    bootstrap_image(), bootstrap_image(),
                )
            with self.assertRaisesRegex(check_user_elf.ElfFormatError, "missing TTY service report symbol"):
                _render_fixture(
                    bootstrap_image(), vm_image(), bootstrap_image(),
                    bootstrap_image(), bootstrap_image(),
                )
            with self.assertRaisesRegex(check_user_elf.ElfFormatError, "TTY image overlaps its fixed UART mapping"):
                _render_fixture(
                    bootstrap_image(), vm_image(), bootstrap_image(),
                    bootstrap_image(), vfs_image(),
                )

    def test_renders_tty_topology_mapping_and_report(self):
        output = _render_fixture(
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
            _render_fixture(
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
            _render_fixture(
                bootstrap_image(),
                vm_image(),
                bootstrap_image(),
                overlapping,
                vfs_image(),
            )


if __name__ == "__main__":
    unittest.main()
