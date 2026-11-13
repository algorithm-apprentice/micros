#!/usr/bin/env -S python3.15 -E -S -B

"""Check current bootstrap lookup/transition semantics and actual work."""

import argparse
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.indexed_bootstrap_evidence import check_native_products, work_totals
from tests.host.indexed_bootstrap_source import validate_audit_boundaries
from tests.host.source_snapshot import checked_source_view
from tests.host.work_count_types import Kind, Owner
from tools.python_host_runtime import require_current_runtime


ROOT = Path(__file__).resolve().parents[1]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--executable", action="append", required=True)
    arguments = parser.parse_args(argv)
    if len(arguments.executable) != 2:
        parser.error("BOOT01 requires one ordinary and one observed executable")
    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("BOOT01 checks require the project root")
    with checked_source_view(ROOT):
        validate_audit_boundaries(ROOT)
        measured = check_native_products(ROOT, arguments.executable)
    for row, snapshot in zip(*measured["work"], strict=True):
        totals = work_totals(snapshot)
        owned = totals.per_owner[Owner.KERNEL_BOOT]
        runtime = totals.per_owner[Owner.RUNTIME]
        print(f"MICROS_BOOT01_WORK_SUMMARY case={row['name']} rows={owned[Kind.RECORD_VISIT]}"
              f" index={owned[Kind.INDEX_PROBE]} copied={owned[Kind.COPIED_BYTE] + runtime[Kind.COPIED_BYTE]}"
              f" copy_units={owned[Kind.COPY_UNIT] + runtime[Kind.COPY_UNIT]}"
              f" cleared={owned[Kind.CLEARED_BYTE] + runtime[Kind.CLEARED_BYTE]}"
              f" clear_units={owned[Kind.CLEAR_UNIT] + runtime[Kind.CLEAR_UNIT]}"
              f" boot_audits={owned[Kind.COMPLETE_VALIDATOR_CALL]}"
              f" all_audits={totals.per_kind[Kind.COMPLETE_VALIDATOR_CALL]}")
    print("MICROS_BOOT01_CURRENT_PASS products=2")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("MICROS_BOOT01_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
