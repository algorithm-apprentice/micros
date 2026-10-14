from contextlib import redirect_stderr
import io
from pathlib import Path
import tempfile
import unittest

from tests.host.application_elf_test_fixture import build_application_elf
from tools import generate_application_seed
from tools import generate_ramfs_seed
from tools.user_elf_format import ElfFormatError
from tools.user_elf_parse import parse_elf


class ApplicationSeedTest(unittest.TestCase):
    def test_encodes_exact_checked_executable_snapshot_with_existing_seed_format(self):
        full = bytes(build_application_elf())
        image = parse_elf(full)
        end = max(program.offset + program.file_size for program in image.programs)
        runtime = full[:end]
        seed = generate_application_seed.build_application_seed(image, runtime)
        self.assertGreaterEqual(len(seed), generate_ramfs_seed.DATA_OFFSET)
        header = generate_ramfs_seed.HEADER.unpack_from(seed)
        self.assertEqual(generate_ramfs_seed.MAGIC, header[0])
        self.assertEqual(3, header[5])
        self.assertEqual(len(runtime), header[7])
        self.assertEqual(len(seed), header[8])
        self.assertEqual(generate_ramfs_seed.image_digest(seed), header[9])
        records = [
            generate_ramfs_seed.ENTRY.unpack_from(
                seed, generate_ramfs_seed.HEADER_SIZE
                + index * generate_ramfs_seed.ENTRY_SIZE,
            ) for index in range(3)
        ]
        self.assertEqual((0, 0, 0x4000 | 0o755), records[0][:3])
        self.assertEqual((0, 3, 0x4000 | 0o755), records[1][:3])
        self.assertEqual(b"bin", records[1][5][:3])
        self.assertEqual((1, 17, 0x8000 | 0o555, 0, len(runtime)), records[2][:5])
        self.assertEqual(b"application-probe", records[2][5][:17])
        self.assertEqual(
            runtime, seed[generate_ramfs_seed.DATA_OFFSET:],
        )
        self.assertEqual(seed, generate_application_seed.build_application_seed(image, runtime))

    def test_rejects_changed_load_bytes_before_any_seed_publication(self):
        full = bytes(build_application_elf())
        image = parse_elf(full)
        changed = bytearray(full)
        changed[0x2000] ^= 1
        with self.assertRaises(ElfFormatError):
            generate_application_seed.build_application_seed(image, changed)

    def test_cli_publishes_only_validated_seed_and_preserves_output_on_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            full = Path(temporary) / "link.elf"
            runtime = Path(temporary) / "runtime.elf"
            output = Path(temporary) / "generated" / "application-seed.bin"
            data = bytes(build_application_elf())
            full.write_bytes(data)
            runtime.write_bytes(data)
            self.assertEqual(0, generate_application_seed.main([
                "--elf", str(full), "--runtime-elf", str(runtime),
                "--output", str(output),
            ]))
            expected = output.read_bytes()
            runtime.write_bytes(data[:40])
            errors = io.StringIO()
            with redirect_stderr(errors):
                self.assertEqual(1, generate_application_seed.main([
                    "--elf", str(full), "--runtime-elf", str(runtime),
                    "--output", str(output),
                ]))
            self.assertIn("application seed generation failed:", errors.getvalue())
            self.assertEqual(expected, output.read_bytes())
