#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path


RAMFS_SEED_IMAGE_MAX = 270400
BYTES_PER_LINE = 12


def render_source(data: bytes):
    if not data:
        raise ValueError("RAMFS seed image must not be empty")
    if len(data) > RAMFS_SEED_IMAGE_MAX:
        raise ValueError(
            f"RAMFS seed image size {len(data)} exceeds "
            f"{RAMFS_SEED_IMAGE_MAX}"
        )
    lines = [
        '#include "servers/ramfs/ramfs_embedded_seed.h"',
        "",
        "_Alignas(8) const uint8_t micros_ramfs_embedded_seed[] = {",
    ]
    for offset in range(0, len(data), BYTES_PER_LINE):
        chunk = data[offset : offset + BYTES_PER_LINE]
        lines.append(
            "    " + " ".join(f"0x{byte:02x}," for byte in chunk)
        )
    lines.extend(
        [
            "};",
            "",
            "const size_t micros_ramfs_embedded_seed_size = "
            f"{len(data)};",
            "",
        ]
    )
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Embed a validated RAMFS seed image in C rodata."
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args(argv)
    try:
        source = render_source(arguments.input.read_bytes())
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(source, encoding="utf-8")
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
