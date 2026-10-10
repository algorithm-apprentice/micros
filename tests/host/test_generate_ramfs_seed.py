import json
from pathlib import Path
import struct
import tempfile
import unittest

from tools import generate_ramfs_seed


ROOT = Path(__file__).resolve().parents[2]
HEADER = struct.Struct("<IHHHHHHIIQ4Q")
ENTRY = struct.Struct("<HHIII64s6Q")


def declaration(entries, version=1):
    return {"version": version, "entries": entries}


def directory(path, mode="0755"):
    return {"path": path, "type": "directory", "mode": mode}


def regular_file(path, source, mode="0644"):
    return {
        "path": path,
        "type": "file",
        "mode": mode,
        "source": source,
    }


def write_manifest(root, entries, version=1, name="seed.json"):
    path = root / name
    path.write_text(
        json.dumps(declaration(entries, version)),
        encoding="utf-8",
    )
    return path


class RamfsSeedGeneratorTest(unittest.TestCase):
    def assert_declaration_rejected(self, root, value):
        manifest = root / "seed.json"
        manifest.write_text(json.dumps(value), encoding="utf-8")
        with self.assertRaises(generate_ramfs_seed.SeedError):
            generate_ramfs_seed.build_seed(manifest, root)

    def assert_entries_rejected(self, root, entries, version=1):
        manifest = write_manifest(root, entries, version)
        with self.assertRaises(generate_ramfs_seed.SeedError):
            generate_ramfs_seed.build_seed(manifest, root)

    def test_production_seed_is_deterministic_and_canonical(self):
        manifest = ROOT / "servers/ramfs/seed.json"

        first = generate_ramfs_seed.build_seed(manifest, ROOT)
        second = generate_ramfs_seed.build_seed(manifest, ROOT)

        self.assertEqual(first, second)
        self.assertEqual(8269, len(first))
        header = HEADER.unpack_from(first)
        self.assertEqual(generate_ramfs_seed.MAGIC, header[0])
        self.assertEqual(1, header[1])
        self.assertEqual(64, header[2])
        self.assertEqual(128, header[3])
        self.assertEqual(64, header[4])
        self.assertEqual(3, header[5])
        self.assertEqual(0, header[6])
        self.assertEqual(13, header[7])
        self.assertEqual(len(first), header[8])
        self.assertEqual(
            header[9],
            generate_ramfs_seed.image_digest(first),
        )
        self.assertEqual((0, 0, 0, 0), header[10:14])

        root = ENTRY.unpack_from(first, 64)
        etc = ENTRY.unpack_from(first, 64 + 128)
        motd = ENTRY.unpack_from(first, 64 + 256)
        self.assertEqual((0, 0), root[:2])
        self.assertEqual(generate_ramfs_seed.MODE_DIRECTORY | 0o755, root[2])
        self.assertEqual((0, 3), (etc[0], etc[1]))
        self.assertEqual(b"etc", etc[5][:3])
        self.assertEqual((1, 4), (motd[0], motd[1]))
        self.assertEqual(b"motd", motd[5][:4])
        self.assertEqual(
            generate_ramfs_seed.MODE_REGULAR | 0o644,
            motd[2],
        )
        self.assertEqual((0, 13), motd[3:5])
        self.assertEqual(b"micros ramfs\n", first[8256:])
        self.assertEqual(bytes(128 * 61), first[64 + 128 * 3:8256])

    def test_accepts_exact_node_data_block_and_name_limits(self):
        with tempfile.TemporaryDirectory() as directory_name:
            root = Path(directory_name)
            payload = root / "payload"
            payload.write_bytes(bytes(generate_ramfs_seed.DATA_SIZE_MAX))
            entries = [directory("/")]
            entries.append(directory("/" + "n" * generate_ramfs_seed.NAME_MAX))
            entries.extend(
                directory(f"/d{index:02d}")
                for index in range(61)
            )
            entries.append(regular_file("/z", payload.name))

            image = generate_ramfs_seed.build_seed(
                write_manifest(root, entries),
                root,
            )

            header = HEADER.unpack_from(image)
            self.assertEqual(generate_ramfs_seed.ENTRY_CAPACITY, header[5])
            self.assertEqual(generate_ramfs_seed.DATA_SIZE_MAX, header[7])
            self.assertEqual(generate_ramfs_seed.IMAGE_SIZE_MAX, len(image))
            final_entry = ENTRY.unpack_from(
                image,
                generate_ramfs_seed.HEADER_SIZE
                + (generate_ramfs_seed.ENTRY_CAPACITY - 1)
                * generate_ramfs_seed.ENTRY_SIZE,
            )
            self.assertEqual(generate_ramfs_seed.DATA_SIZE_MAX, final_entry[4])
            self.assertEqual(
                header[9],
                generate_ramfs_seed.image_digest(image),
            )

    def test_canonicalizes_input_order(self):
        with tempfile.TemporaryDirectory() as directory_name:
            root = Path(directory_name)
            (root / "data").write_bytes(b"payload")
            entries = [
                directory("/"),
                directory("/a", "0750"),
                regular_file("/a/file", "data", "0640"),
                directory("/z", "0700"),
            ]
            first_image = generate_ramfs_seed.build_seed(
                write_manifest(root, entries),
                root,
            )
            second_image = generate_ramfs_seed.build_seed(
                write_manifest(
                    root,
                    list(reversed(entries)),
                    name="reordered.json",
                ),
                root,
            )
            self.assertEqual(first_image, second_image)

    def test_rejects_malformed_declaration_shape_fields_and_version(self):
        with tempfile.TemporaryDirectory() as directory_name:
            root = Path(directory_name)
            manifest = root / "seed.json"
            manifest.write_text(
                '{"version":1,"version":1,"entries":[]}',
                encoding="utf-8",
            )
            with self.assertRaises(generate_ramfs_seed.SeedError):
                generate_ramfs_seed.build_seed(manifest, root)

            invalid_declarations = [
                [],
                {"entries": []},
                {"version": 1},
                {"version": 1, "entries": [], "owner": "root"},
                {"version": 1, "entries": "not-an-array"},
                declaration([], True),
                declaration([], "1"),
                declaration([], 2),
                declaration([1]),
                declaration([{"path": "/", "type": "directory"}]),
                declaration(
                    [
                        {
                            "path": "/",
                            "type": "directory",
                            "mode": "0755",
                            "owner": "root",
                        }
                    ]
                ),
                declaration([]),
            ]
            for value in invalid_declarations:
                with self.subTest(value=value):
                    self.assert_declaration_rejected(root, value)

    def test_rejects_duplicate_noncanonical_and_invalid_root_paths(self):
        with tempfile.TemporaryDirectory() as directory_name:
            root = Path(directory_name)
            (root / "data").write_bytes(b"x")
            invalid_paths = [
                "relative",
                "/trailing/",
                "/repeated//separator",
                "/dot/./component",
                "/dotdot/../component",
                "/nul\x00component",
                "/" + "n" * (generate_ramfs_seed.NAME_MAX + 1),
                7,
            ]
            for path in invalid_paths:
                with self.subTest(path=path):
                    self.assert_entries_rejected(
                        root,
                        [directory("/"), directory(path)],
                    )

            invalid_trees = [
                [directory("/"), directory("/")],
                [directory("/child")],
                [regular_file("/", "data")],
            ]
            for entries in invalid_trees:
                with self.subTest(entries=entries):
                    self.assert_entries_rejected(root, entries)

    def test_rejects_missing_and_nondirectory_parents(self):
        with tempfile.TemporaryDirectory() as directory_name:
            root = Path(directory_name)
            (root / "data").write_bytes(b"x")
            invalid_trees = [
                [directory("/"), directory("/missing/child")],
                [
                    directory("/"),
                    regular_file("/parent", "data"),
                    directory("/parent/child"),
                ],
            ]
            for entries in invalid_trees:
                with self.subTest(entries=entries):
                    self.assert_entries_rejected(root, entries)

    def test_rejects_invalid_types_and_modes(self):
        with tempfile.TemporaryDirectory() as directory_name:
            root = Path(directory_name)
            invalid_entries = [
                [{"path": "/", "type": "device", "mode": "0755"}],
                [directory("/", "755")],
                [directory("/", "0888")],
                [directory("/", "1755")],
                [directory("/", 0o755)],
            ]
            for entries in invalid_entries:
                with self.subTest(entries=entries):
                    self.assert_entries_rejected(root, entries)

    def test_rejects_missing_forbidden_and_invalid_sources(self):
        with tempfile.TemporaryDirectory() as directory_name:
            workspace = Path(directory_name)
            root = workspace / "repo"
            root.mkdir()
            outside = workspace / "outside"
            outside.write_bytes(b"x")
            source_directory = root / "source-directory"
            source_directory.mkdir()
            target = root / "target"
            target.write_bytes(b"x")
            link = root / "link"
            link.symlink_to(target)

            invalid_files = [
                {"path": "/file", "type": "file", "mode": "0644"},
                regular_file("/file", ""),
                regular_file("/file", "missing"),
                regular_file("/file", "source-directory"),
                regular_file("/file", str(outside)),
                regular_file("/file", "../outside"),
                regular_file("/file", "link"),
            ]
            for entry in invalid_files:
                with self.subTest(entry=entry):
                    self.assert_entries_rejected(
                        root,
                        [directory("/"), entry],
                    )

            forbidden_directory = directory("/directory")
            forbidden_directory["source"] = "target"
            self.assert_entries_rejected(
                root,
                [directory("/"), forbidden_directory],
            )

    def test_rejects_manifest_outside_repository(self):
        with tempfile.TemporaryDirectory() as directory_name:
            workspace = Path(directory_name)
            root = workspace / "repo"
            root.mkdir()
            manifest = write_manifest(
                workspace,
                [directory("/")],
                name="outside.json",
            )
            with self.assertRaises(generate_ramfs_seed.SeedError):
                generate_ramfs_seed.build_seed(manifest, root)

    def test_rejects_node_file_and_aggregate_data_capacity(self):
        with tempfile.TemporaryDirectory() as directory_name:
            root = Path(directory_name)
            too_many = [directory("/")]
            too_many.extend(
                directory(f"/d{index:02d}")
                for index in range(generate_ramfs_seed.ENTRY_CAPACITY)
            )
            self.assert_entries_rejected(root, too_many)

            oversized = root / "oversized"
            oversized.write_bytes(
                bytes(generate_ramfs_seed.DATA_SIZE_MAX + 1)
            )
            self.assert_entries_rejected(
                root,
                [directory("/"), regular_file("/file", oversized.name)],
            )

            first = root / "first"
            second = root / "second"
            first.write_bytes(bytes(generate_ramfs_seed.DATA_SIZE_MAX // 2 + 1))
            second.write_bytes(bytes(generate_ramfs_seed.DATA_SIZE_MAX // 2))
            self.assert_entries_rejected(
                root,
                [
                    directory("/"),
                    regular_file("/first", first.name),
                    regular_file("/second", second.name),
                ],
            )

    def test_rejects_rounded_block_capacity(self):
        with tempfile.TemporaryDirectory() as directory_name:
            root = Path(directory_name)
            entries = [directory("/")]
            for index in range(63):
                source = root / f"file{index:02d}"
                source.write_bytes(bytes(8193 if index == 0 else 1))
                entries.append(
                    regular_file(f"/f{index:02d}", source.name)
                )
            self.assert_entries_rejected(root, entries)


if __name__ == "__main__":
    unittest.main()
