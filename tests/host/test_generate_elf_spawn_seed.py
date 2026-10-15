from contextlib import redirect_stderr
import io
from pathlib import Path
import tempfile
import unittest

from tests.host.application_elf_test_fixture import build_application_elf
from tools import check_application_elf
from tools import generate_elf_spawn_seed
from tools import generate_ramfs_seed
from tools.user_elf_format import ElfFormatError
from tools.user_elf_parse import parse_elf


def seed_files(seed):
    header = generate_ramfs_seed.HEADER.unpack_from(seed)
    paths = {}
    files = {}
    for index in range(header[5]):
        entry = generate_ramfs_seed.ENTRY.unpack_from(
            seed, generate_ramfs_seed.HEADER_SIZE
            + index * generate_ramfs_seed.ENTRY_SIZE,
        )
        parent, name_size, mode, offset, size, name = entry[:6]
        path = "/" if index == 0 else (
            paths[parent].rstrip("/") + "/" + name[:name_size].decode("ascii")
        )
        paths[index] = path
        if mode & generate_ramfs_seed.MODE_REGULAR:
            files[path] = (
                mode, seed[generate_ramfs_seed.DATA_OFFSET + offset:
                           generate_ramfs_seed.DATA_OFFSET + offset + size],
            )
    return files


class ElfSpawnSeedTest(unittest.TestCase):
    def images(self):
        init = bytes(build_application_elf())
        child = bytearray(init)
        child[0x2000] ^= 0x5a
        return parse_elf(init), parse_elf(bytes(child))

    def test_encodes_both_exact_executables_and_read_only_data(self):
        init, child = self.images()
        seed = generate_elf_spawn_seed.build_elf_spawn_seed(init, child)
        header = generate_ramfs_seed.HEADER.unpack_from(seed)
        self.assertEqual(generate_ramfs_seed.MAGIC, header[0])
        self.assertEqual(7, header[5])
        self.assertEqual(len(seed), header[8])
        self.assertEqual(generate_ramfs_seed.image_digest(seed), header[9])
        self.assertEqual({
            "/sbin/elf-spawn-init": (0x8000 | 0o555, init.data),
            "/bin/elf-spawn-child": (0x8000 | 0o555, child.data),
            "/etc/elf-spawn-data": (0x8000 | 0o444, b"micros ELF spawn fixture\n"),
        }, seed_files(seed))
        self.assertEqual(seed, generate_elf_spawn_seed.build_elf_spawn_seed(init, child))

    def test_rejects_either_invalid_image_before_seed_creation(self):
        init, child = self.images()
        malformed = bytearray(init.data)
        malformed[24] ^= 4
        bad = parse_elf(bytes(malformed))
        for first, second in ((bad, child), (init, bad)):
            with self.assertRaises(ElfFormatError):
                generate_elf_spawn_seed.build_elf_spawn_seed(first, second)

    def test_uses_validated_snapshots_without_reopening_original_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "source.elf"
            path.write_bytes(build_application_elf())
            image = check_application_elf.load_validated_elf(path)
            path.unlink()
            seed = generate_elf_spawn_seed.build_elf_spawn_seed(image, image)
        self.assertEqual(image.data, seed_files(seed)["/sbin/elf-spawn-init"][1])
        self.assertEqual(image.data, seed_files(seed)["/bin/elf-spawn-child"][1])

    def test_cli_preserves_previous_output_when_second_image_is_invalid(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            init = root / "init.elf"
            child = root / "child.elf"
            output = root / "generated" / "seed.bin"
            init.write_bytes(build_application_elf())
            child.write_bytes(build_application_elf())
            arguments = [
                "--init-elf", str(init), "--child-elf", str(child),
                "--output", str(output),
            ]
            self.assertEqual(0, generate_elf_spawn_seed.main(arguments))
            before = output.read_bytes()
            child.write_bytes(b"not ELF")
            errors = io.StringIO()
            with redirect_stderr(errors):
                self.assertEqual(1, generate_elf_spawn_seed.main(arguments))
            self.assertIn("ELF spawn seed generation failed:", errors.getvalue())
            self.assertEqual(before, output.read_bytes())
