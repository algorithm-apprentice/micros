from dataclasses import replace
from contextlib import redirect_stderr
import io
from pathlib import Path
import struct
import tempfile
import unittest

from tests.host.application_elf_test_fixture import (
    _encode_application_symbols, build_application_elf,
)
from tests.host.test_generate_bootstrap_fixture import bootstrap_image
from tests.host.test_generate_vm_handoff_fixture import vm_image
from tests.host.test_generate_vfs_service_fixture import image_with_page_count
from tools import generate_elf_spawn_fixture
from tools.user_elf_format import ElfFormatError, ElfSymbol, SYMBOL_BIND_GLOBAL, USER_BASE
from tools.user_elf_parse import parse_elf
from tests.host import kernel_layout_source

GENERATOR_PATH = "tools/generate_elf_spawn_fixture.py"

def _render_fixture(*args, **kwargs):
    return kernel_layout_source.restore_generated_fixture(
        GENERATOR_PATH,
        generate_elf_spawn_fixture.render_fixture(*args, **kwargs),
    )


def service_image(role):
    image = vm_image() if role == "vm" else bootstrap_image()
    names = {
        "launcher": (), "vm": ("service_context",),
        "pm": ("service_context",), "tty": ("tty_service", "tty_uart"),
        "ramfs": ("ramfs_state",), "vfs": ("vfs_state",),
    }[role]
    objects = tuple(
        ElfSymbol(name, 0, 1, 4, USER_BASE + 0x2200 + index * 256, 128)
        for index, name in enumerate(names)
    )
    return replace(image, symbols=image.symbols + objects)


def application_image():
    data = build_application_elf()
    image = parse_elf(bytes(data))
    report = ElfSymbol(
        "micros_elf_spawn_report", SYMBOL_BIND_GLOBAL, 2, 1, USER_BASE + 36, 8,
    )
    struct.pack_into("<II", data, 0x1000 + 36, 0x00100073, 0x00008067)
    return parse_elf(bytes(_encode_application_symbols(
        data, image, image.symbols + (report,),
    )))


class ElfSpawnFixtureTest(unittest.TestCase):
    def images(self):
        images = {role: service_image(role)
                  for role in ("launcher", "vm", "pm", "tty", "ramfs", "vfs")}
        images.update(init=application_image(), child=application_image())
        return images

    def test_renders_exactly_six_production_images_and_dynamic_report_metadata(self):
        images = self.images()
        output = _render_fixture(images)
        self.assertIn("micros_elf_spawn_test_images", output)
        self.assertEqual(6, output.count(".version = MICROS_BOOTSTRAP_IMAGE_VERSION"))
        self.assertEqual(6, output.count(".stack_page_count = 1,"))
        self.assertIn(".entry_count = MICROS_ELF_SPAWN_SERVICE_COUNT", output)
        self.assertNotIn(".image_id = 107", output)
        self.assertNotIn("TEST_APPLICATION", output)
        self.assertIn("micros_elf_spawn_test_objects", output)
        self.assertIn("MICROS_ELF_SPAWN_OBJECT_VM_CONTEXT", output)
        self.assertIn("MICROS_ELF_SPAWN_OBJECT_TTY_UART", output)
        self.assertIn("micros_elf_spawn_report_addresses", output)
        self.assertIn("UINT64_C(0x0000000040000024)", output)
        self.assertEqual(output, _render_fixture(images))

    def test_rejects_extra_missing_or_misplaced_service_roles(self):
        for change in ("extra", "missing", "foreign-vm"):
            images = self.images()
            if change == "extra":
                images["application"] = application_image()
            elif change == "missing":
                del images["tty"]
            else:
                images["vfs"] = images["vm"]
            with self.subTest(change=change), self.assertRaises(ElfFormatError):
                _render_fixture(images)

    def test_requires_checked_writable_state_objects(self):
        for role in ("vm", "pm", "tty", "ramfs", "vfs"):
            images = self.images()
            image = images[role]
            names = {"service_context", "tty_service", "ramfs_state", "vfs_state"}
            images[role] = replace(image, symbols=tuple(
                symbol for symbol in image.symbols if symbol.name not in names
            ))
            with self.subTest(role=role), self.assertRaises(ElfFormatError):
                _render_fixture(images)
        images = self.images()
        images["vfs"] = replace(images["vfs"], symbols=tuple(
            replace(symbol, value=USER_BASE + 0x1000)
            if symbol.name == "vfs_state" else symbol
            for symbol in images["vfs"].symbols
        ))
        with self.assertRaises(ElfFormatError):
            _render_fixture(images)

    def test_requires_exact_bounded_report_instructions_for_both_applications(self):
        for role in ("init", "child"):
            for change in ("missing", "size", "instructions"):
                images = self.images()
                image = images[role]
                if change == "missing":
                    image = replace(image, symbols=tuple(
                        symbol for symbol in image.symbols
                        if symbol.name != "micros_elf_spawn_report"
                    ))
                elif change == "size":
                    image = replace(image, symbols=tuple(
                        replace(symbol, size=4)
                        if symbol.name == "micros_elf_spawn_report" else symbol
                        for symbol in image.symbols
                    ))
                else:
                    data = bytearray(image.data)
                    data[0x1000 + 36] ^= 1
                    image = replace(image, data=bytes(data))
                images[role] = image
                with self.subTest(role=role, change=change), self.assertRaises(ElfFormatError):
                    _render_fixture(images)

    def test_enforces_each_fixed_production_service_budget(self):
        for role, limit in (("pm", 32), ("vm", 160), ("ramfs", 192), ("vfs", 80)):
            images = self.images()
            images[role] = image_with_page_count(images[role], limit)
            with self.subTest(role=role), self.assertRaises(ElfFormatError):
                _render_fixture(images)

    def test_accepts_exact_fixed_limits_but_rejects_aggregate_excess(self):
        images = self.images()
        for role, limit in (("pm", 32), ("vm", 160), ("ramfs", 192), ("vfs", 80)):
            images[role] = image_with_page_count(images[role], limit - 1)
        self.assertIn(".user_page_limit = 192", _render_fixture(images))
        images["launcher"] = image_with_page_count(images["launcher"], 4096)
        with self.assertRaisesRegex(ElfFormatError, "aggregate"):
            _render_fixture(images)

    def test_requires_uart_state_and_excludes_the_tty_device_leaf(self):
        images = self.images()
        images["tty"] = replace(images["tty"], symbols=tuple(
            symbol for symbol in images["tty"].symbols if symbol.name != "tty_uart"
        ))
        with self.assertRaisesRegex(ElfFormatError, "tty_uart"):
            _render_fixture(images)
        images = self.images()
        images["tty"] = image_with_page_count(images["tty"], 0x3ffff)
        with self.assertRaisesRegex(ElfFormatError, "UART"):
            _render_fixture(images)

    def test_cli_failure_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "fixture.c"
            output.write_text("old fixture\n")
            arguments = []
            for role in ("launcher", "vm", "pm", "tty", "ramfs", "vfs", "init", "child"):
                arguments.extend(("--" + role + "-elf", str(Path(temporary) / (role + ".elf"))))
            arguments.extend(("--output", str(output)))
            errors = io.StringIO()
            with redirect_stderr(errors):
                self.assertEqual(1, generate_elf_spawn_fixture.main(arguments))
            self.assertIn("ELF spawn fixture generation failed:", errors.getvalue())
            self.assertEqual("old fixture\n", output.read_text())
