#!/usr/bin/env -S python3.15 -E -S -B

"""Check current selected IPC semantics, work and passive observations."""

import argparse
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.selected_ipc_validation_evidence import check_native_products
from tests.host.source_snapshot import checked_source_view
from tools.python_host_runtime import require_current_runtime


ROOT = Path(__file__).resolve().parents[1]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--executable", action="append", required=True)
    arguments = parser.parse_args(argv)
    if len(arguments.executable) != 2:
        parser.error("IPC01 requires one ordinary and one observed executable")
    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("IPC01 checks require the project root")
    with checked_source_view(ROOT):
        check_native_products(ROOT, arguments.executable)
    print("MICROS_IPC01_CURRENT_PASS products=2")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("MICROS_IPC01_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
