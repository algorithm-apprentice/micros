#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.application_elf_policy import validate_elf, validate_seeded_elf
from tools.user_elf_format import ElfFormatError
from tools.user_elf_parse import load_elf


def load_validated_elf(path):
    image = load_elf(path)
    errors = validate_elf(image)
    if errors:
        raise ElfFormatError("; ".join(errors))
    return image


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Validate a micros application ELF and optional seeded artifact.",
    )
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--runtime-elf", type=Path)
    arguments = parser.parse_args(argv)
    for path in (arguments.elf, arguments.runtime_elf):
        if path is not None and not path.is_file():
            parser.error(f"ELF image does not exist: {path}")
    try:
        image = load_elf(arguments.elf)
        if arguments.runtime_elf is None:
            errors = validate_elf(image)
        else:
            errors = validate_seeded_elf(image, arguments.runtime_elf.read_bytes())
    except (OSError, ElfFormatError, ValueError, struct.error) as error:
        print(f"application ELF validation failed: {error}", file=sys.stderr)
        return 1
    for error in errors:
        print(f"application ELF validation failed: {error}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
