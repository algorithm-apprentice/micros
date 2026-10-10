import tempfile
import unittest
from pathlib import Path

from tools import embed_ramfs_seed


class EmbedRamfsSeedTests(unittest.TestCase):
    def test_renders_deterministic_read_only_seed_symbols(self):
        source = embed_ramfs_seed.render_source(b"\x00\x01\xff")

        self.assertEqual(source, embed_ramfs_seed.render_source(b"\x00\x01\xff"))
        self.assertIn(
            '#include "servers/ramfs/ramfs_embedded_seed.h"',
            source,
        )
        self.assertIn(
            "_Alignas(8) const uint8_t micros_ramfs_embedded_seed[]",
            source,
        )
        self.assertIn("0x00, 0x01, 0xff,", source)
        self.assertIn(
            "const size_t micros_ramfs_embedded_seed_size = 3;",
            source,
        )

    def test_writes_exact_rendered_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            seed = root / "seed.bin"
            output = root / "generated" / "seed.c"
            seed.write_bytes(b"RFS1")

            self.assertEqual(
                0,
                embed_ramfs_seed.main(
                    [
                        "--input",
                        str(seed),
                        "--output",
                        str(output),
                    ]
                ),
            )
            self.assertEqual(
                embed_ramfs_seed.render_source(b"RFS1"),
                output.read_text(encoding="utf-8"),
            )

    def test_rejects_empty_or_oversized_images(self):
        with self.assertRaisesRegex(ValueError, "must not be empty"):
            embed_ramfs_seed.render_source(b"")
        with self.assertRaisesRegex(ValueError, "exceeds"):
            embed_ramfs_seed.render_source(
                bytes(embed_ramfs_seed.RAMFS_SEED_IMAGE_MAX + 1)
            )


if __name__ == "__main__":
    unittest.main()
