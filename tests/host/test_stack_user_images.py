"""User observation objects are owned, zero initialized and image bound."""

from dataclasses import replace
import unittest

from tests.host.user_elf_test_fixture import build_elf
from tools.user_elf_format import ElfSymbol
from tools.user_elf_parse import parse_elf
from tools.stack_bound_types import StackEvidenceError


def observed_fixture():
    data, _ = build_elf()
    image = parse_elf(bytes(data))
    bss = next(section for section in image.sections if section.name == ".bss")
    return replace(
        image,
        symbols=image.symbols + (
            ElfSymbol("micros_stack_user_observation", 1, 1, bss.index, bss.address, 64),
        ),
    )


class StackUserImagesTest(unittest.TestCase):
    def test_symbol_joins_its_exact_writable_image_and_initial_zero_storage(self):
        from tools.stack_bound_user_images import user_observer_descriptor

        image = observed_fixture()
        record = user_observer_descriptor(image, "bootstrap/probe.elf", 102)
        self.assertEqual(("bootstrap/probe.elf", 102, 64), (record.image, record.image_id, record.size))
        self.assertEqual(0x7ffff000, record.stack_bottom)
        self.assertEqual(0x80000000, record.stack_top)

    def test_duplicate_foreign_wrong_sized_and_executable_objects_fail(self):
        from tools.stack_bound_user_images import user_observer_descriptor

        image = observed_fixture()
        symbol = image.symbols[-1]
        for changed in (
            replace(image, symbols=image.symbols + (symbol,)),
            replace(image, symbols=image.symbols[:-1] + (replace(symbol, size=32),)),
            replace(image, symbols=image.symbols[:-1] + (replace(symbol, binding=0),)),
            replace(image, symbols=image.symbols[:-1] + (replace(symbol, section_index=1),)),
            replace(image, symbols=image.symbols[:-1] + (replace(symbol, value=symbol.value + 1),)),
        ):
            with self.subTest(symbols=changed.symbols[-1]), self.assertRaises(StackEvidenceError):
                user_observer_descriptor(changed, "bootstrap/probe.elf", 102)

    def test_catalog_requires_every_independent_image_id_including_shared_code_copies(self):
        from tools.stack_bound_user_images import render_user_catalog, user_observer_descriptor

        image = observed_fixture()
        rows = tuple(user_observer_descriptor(image, path, image_id) for image_id, path in (
            (101, "bootstrap/micros-bootstrap-launcher.elf"),
            (102, "bootstrap/micros-bootstrap-probe.elf"),
            (103, "bootstrap/micros-bootstrap-probe.elf"),
        ))
        rendered = render_user_catalog("riscv64-bootstrap-launcher-test", rows)
        self.assertIn("MICROS_STACK_USER_IMAGE_COUNT 3", rendered)
        self.assertEqual(2, rendered.count('"bootstrap/micros-bootstrap-probe.elf"'))
        with self.assertRaises(StackEvidenceError):
            render_user_catalog("riscv64-bootstrap-launcher-test", rows[:2])
        with self.assertRaises(StackEvidenceError):
            render_user_catalog("riscv64-bootstrap-launcher-test", rows + (rows[-1],))


if __name__ == "__main__":
    unittest.main()
