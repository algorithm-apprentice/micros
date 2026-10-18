#!/usr/bin/env -S python3.15 -E -S -B

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys
import tempfile

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools import check_application_elf
from tools import generate_ramfs_seed
from tools.user_elf_format import ElfFormatError


def build_application_seed(image, runtime_bytes):
    snapshot = bytes(runtime_bytes)
    errors = check_application_elf.validate_seeded_elf(image, snapshot)
    if errors:
        raise ElfFormatError("; ".join(errors))
    declaration = {
        "version": 1,
        "entries": [
            {"path": "/", "type": "directory", "mode": "0755"},
            {"path": "/bin", "type": "directory", "mode": "0755"},
            {
                "path": "/bin/application-probe", "type": "file",
                "mode": "0555", "source": "application.elf",
            },
        ],
    }
    with tempfile.TemporaryDirectory(prefix="micros-application-seed-") as temporary:
        root = Path(temporary)
        (root / "application.elf").write_bytes(snapshot)
        manifest = root / "seed.json"
        manifest.write_text(json.dumps(declaration, sort_keys=True))
        return generate_ramfs_seed.build_seed(manifest, root)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Seed one checked application snapshot in RAMFS.")
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--runtime-elf", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args(argv)
    try:
        image = check_application_elf.load_validated_elf(arguments.elf)
        runtime = (
            image.data if arguments.runtime_elf is None
            else arguments.runtime_elf.read_bytes()
        )
        seed = build_application_seed(image, runtime)
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_bytes(seed)
    except (OSError, ElfFormatError, generate_ramfs_seed.SeedError, struct.error) as error:
        print(f"application seed generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
