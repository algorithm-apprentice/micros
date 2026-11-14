#!/usr/bin/env -S python3.15 -E -S -B

"""Check current VM native semantics and independently bounded work."""

import argparse
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.selected_vm_evidence import check_native_products, work_totals
from tests.host.source_snapshot import checked_source_view
from tests.host.work_count_types import Kind
from tools.python_host_runtime import require_current_runtime


ROOT = Path(__file__).resolve().parents[1]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--executable", action="append", required=True)
    arguments = parser.parse_args(argv)
    if len(arguments.executable) != 2:
        parser.error("VM01 requires one ordinary and one observed executable")
    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("VM01 checks require the project root")
    with checked_source_view(ROOT):
        measured = check_native_products(ROOT, arguments.executable)
    for row, snapshot in zip(*measured["work"], strict=True):
        totals = work_totals(snapshot)
        print(f"MICROS_VM01_WORK_SUMMARY case={row['name']}"
              f" audits={totals.per_kind[Kind.COMPLETE_VALIDATOR_CALL]}"
              f" rows={totals.per_kind[Kind.RECORD_VISIT]}"
              f" index={totals.per_kind[Kind.INDEX_PROBE]}")
    print("MICROS_VM01_CURRENT_PASS products=2")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("MICROS_VM01_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
