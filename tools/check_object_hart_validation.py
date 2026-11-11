#!/usr/bin/env -S python3.15 -E -S -B

"""Check the current object/hart native products and complete-audit boundaries."""

import argparse
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.object_hart_validation_evidence import check_native_products
from tests.host.source_snapshot import checked_source_view
from tools.python_host_runtime import require_current_runtime


ROOT = Path(__file__).resolve().parents[1]


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--native", action="store_true", required=True)
    parser.add_argument("--executable", action="append", default=[])
    arguments = parser.parse_args(argv)
    if len(arguments.executable) != 3:
        parser.error("--native requires three current executables")
    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)
    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("OBJ01 native execution requires the project root")
    with checked_source_view(ROOT):
        check_native_products(ROOT, arguments.executable)
    print("MICROS_OBJ01_CURRENT_PASS cases=171 products=3")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print("MICROS_OBJ01_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
