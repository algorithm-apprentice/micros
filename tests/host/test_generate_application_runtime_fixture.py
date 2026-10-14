import io
from contextlib import redirect_stderr
from pathlib import Path
import struct
import tempfile
import unittest

from tests.host.application_elf_test_fixture import build_application_elf
from tools import generate_application_runtime_fixture
from tools.user_elf_format import ElfFormatError
from tools.user_elf_parse import parse_elf


class ApplicationRuntimeFixtureTest(unittest.TestCase):
    def test_renders_only_checked_component_pages_and_request(self):
        image = parse_elf(bytes(build_application_elf()))
        output = generate_application_runtime_fixture.render_fixture(image)
        self.assertIn('#include "tests/qemu/application_runtime_fixture.h"', output)
        self.assertIn("micros_application_runtime_test_image", output)
        self.assertEqual(3, output.count(".file_size = "))
        self.assertIn(".entry = UINT64_C(0x0000000040000000)", output)
        self.assertIn(".request_size = sizeof(application_request_bytes)", output)
        self.assertIn("application_segment_0_bytes", output)

    def test_request_has_canonical_dense_references_and_zero_unused_storage(self):
        data = generate_application_runtime_fixture.application_request_bytes()
        self.assertEqual((1, 256, len(data), 0), struct.unpack_from("<4I", data))
        self.assertEqual((2, 1, 0), struct.unpack_from("<3H", data, 24))
        cursor = 256
        for reference, value in (
            (16, b"/bin/application-probe\0"),
            (32, b"probe\0"), (40, b"child\0"), (96, b"MODE=runtime\0"),
        ):
            offset, size, reserved = struct.unpack_from("<HHI", data, reference)
            self.assertEqual((cursor, len(value), 0), (offset, size, reserved))
            self.assertEqual(value, data[offset:offset + size])
            cursor += size
        self.assertEqual(len(data), cursor)
        self.assertEqual(bytes(48), data[48:96])
        self.assertEqual(bytes(152), data[104:256])

    def test_rejects_unchecked_application_instead_of_rendering(self):
        data = build_application_elf()
        struct.pack_into("<I", data, 0x1000, 0x00000013)
        with self.assertRaises(ElfFormatError):
            generate_application_runtime_fixture.render_fixture(parse_elf(bytes(data)))

    def test_cli_writes_deterministic_checked_source_and_surfaces_invalid_input(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "application.elf"
            output = Path(temporary) / "generated" / "application.c"
            path.write_bytes(build_application_elf())
            self.assertEqual(0, generate_application_runtime_fixture.main([
                "--elf", str(path), "--output", str(output),
            ]))
            expected = generate_application_runtime_fixture.render_fixture(
                parse_elf(path.read_bytes()),
            )
            self.assertEqual(expected, output.read_text())
            path.write_bytes(b"invalid")
            errors = io.StringIO()
            with redirect_stderr(errors):
                self.assertEqual(1, generate_application_runtime_fixture.main([
                    "--elf", str(path), "--output", str(output),
                ]))
            self.assertIn("application fixture generation failed:", errors.getvalue())
            self.assertEqual(expected, output.read_text())
