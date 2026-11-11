#!/usr/bin/env -S python3.15 -E -S -B

"""Check current ownership native semantics, work and validation boundaries."""

import argparse
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.source_snapshot import checked_source_view
from tests.host.selected_ownership_evidence import check_native_products
from tools.python_host_runtime import require_current_runtime


ROOT = Path(__file__).resolve().parents[1]


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--native", action="store_true", required=True)
    parser.add_argument("--executable", action="append", default=[])
    arguments = parser.parse_args(argv)
    if len(arguments.executable) != 6:
        parser.error("--native requires six current executables")
    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)
    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("MEM03 native execution requires the project root")
    with checked_source_view(ROOT):
        check_native_products(ROOT, arguments.executable)
    print("MICROS_MEM03_CURRENT_PASS cases=99 snapshots=495 guards=58 ordinary=99")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print("MICROS_MEM03_FAIL " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
