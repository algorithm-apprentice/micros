#!/usr/bin/env -S python3.15 -E -S -B

"""Check current release/handoff native semantics, work and audit boundaries."""

import argparse
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.source_snapshot import checked_source_view
from tests.host.selected_release_handoff_evidence import check_native_products, expected_guards
from tests.host.selected_release_handoff_source import STATE_WORK_CONTRACT
from tools.python_host_runtime import require_current_runtime


ROOT = Path(__file__).resolve().parents[1]


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--native", action="store_true", required=True)
    parser.add_argument("--executable", action="append", default=[])
    arguments = parser.parse_args(argv)
    if len(arguments.executable) != 5:
        parser.error("--native requires five current executables")
    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)
    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("MEM05 native execution requires the project root")
    with checked_source_view(ROOT):
        check_native_products(ROOT, arguments.executable)
    print(
        "MICROS_MEM05_CURRENT_PASS cases=" + str(STATE_WORK_CONTRACT["common_cases"])
        + " products=5 guards=" + str(len(expected_guards()))
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print("MICROS_MEM05_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
