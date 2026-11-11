#!/usr/bin/env -S python3.15 -E -S -B

"""Check current scheduler semantics, primitive work and passive observations."""

import argparse
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.common_scheduler_return_evidence import check_native_products
from tests.host.source_snapshot import checked_source_view
from tools.python_host_runtime import require_current_runtime


ROOT = Path(__file__).resolve().parents[1]


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--native", action="store_true", required=True)
    parser.add_argument("--executable", action="append", default=[])
    arguments = parser.parse_args(argv)
    if len(arguments.executable) != 2:
        parser.error("--native requires the current ordinary and observed executables")
    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)
    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("current scheduler checks require the project root")
    with checked_source_view(ROOT):
        check_native_products(ROOT, arguments.executable)
    print("MICROS_OBJ02_CURRENT_PASS products=2")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print("MICROS_OBJ02_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
