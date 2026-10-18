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


def _checked_snapshot(image, role):
    snapshot = bytes(image.data)
    errors = check_application_elf.validate_seeded_elf(image, snapshot)
    if errors:
        raise ElfFormatError(f"{role} application: " + "; ".join(errors))
    return snapshot


def build_elf_spawn_seed(init_image, child_image):
    sources = {
        "init.elf": _checked_snapshot(init_image, "init"),
        "child.elf": _checked_snapshot(child_image, "child"),
        "data.txt": b"micros ELF spawn fixture\n",
    }
    declaration = {
        "version": 1,
        "entries": [
            {"path": "/", "type": "directory", "mode": "0755"},
            {"path": "/bin", "type": "directory", "mode": "0755"},
            {"path": "/etc", "type": "directory", "mode": "0755"},
            {"path": "/sbin", "type": "directory", "mode": "0755"},
            {
                "path": "/sbin/elf-spawn-init", "type": "file",
                "mode": "0555", "source": "init.elf",
            },
            {
                "path": "/bin/elf-spawn-child", "type": "file",
                "mode": "0555", "source": "child.elf",
            },
            {
                "path": "/etc/elf-spawn-data", "type": "file",
                "mode": "0444", "source": "data.txt",
            },
        ],
    }
    with tempfile.TemporaryDirectory(prefix="micros-elf-spawn-seed-") as temporary:
        root = Path(temporary)
        for name, data in sources.items():
            (root / name).write_bytes(data)
        manifest = root / "seed.json"
        manifest.write_text(json.dumps(declaration, sort_keys=True), encoding="utf-8")
        return generate_ramfs_seed.build_seed(manifest, root)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Seed the checked ELF spawn integration applications.")
    parser.add_argument("--init-elf", required=True, type=Path)
    parser.add_argument("--child-elf", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args(argv)
    try:
        init_image = check_application_elf.load_validated_elf(arguments.init_elf)
        child_image = check_application_elf.load_validated_elf(arguments.child_elf)
        seed = build_elf_spawn_seed(init_image, child_image)
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_bytes(seed)
    except (OSError, ElfFormatError, generate_ramfs_seed.SeedError, struct.error) as error:
        print(f"ELF spawn seed generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
