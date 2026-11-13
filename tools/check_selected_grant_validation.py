#!/usr/bin/env -S python3.15 -E -S -B

"""Check current selected-grant semantics, work and passive observations."""

import argparse
import json
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.selected_grant_validation_evidence import (
    SPAN_GEOMETRY, check_native_products, check_target_product, work_totals,
)
from tests.host.source_snapshot import checked_source_view
from tests.host.work_count_types import Kind, Owner
from tools.python_host_runtime import require_current_runtime


ROOT = Path(__file__).resolve().parents[1]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--executable", action="append")
    mode.add_argument("--target", action="store_true")
    arguments = parser.parse_args(argv)
    if not arguments.target and len(arguments.executable) != 2:
        parser.error("GRANT01 requires one ordinary and one observed executable")
    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("GRANT01 checks require the project root")
    with checked_source_view(ROOT):
        if arguments.target:
            target = check_target_product(ROOT)
        else:
            measured = check_native_products(ROOT, arguments.executable)
    if arguments.target:
        print("MICROS_GRANT03_TARGET_PASS " + json.dumps(target, sort_keys=True))
        return 0
    for row, work in zip(*measured["work"], strict=True):
        counts = work_totals(work)
        print(f"MICROS_GRANT01_WORK_SUMMARY case={row['name']}"
              f" audits={counts.per_kind[Kind.COMPLETE_VALIDATOR_CALL]}"
              f" grants={counts.per_owner[Owner.KERNEL_GRANT][Kind.RECORD_VISIT]}"
              f" records={counts.per_kind[Kind.RECORD_VISIT]}"
              f" index={counts.per_kind[Kind.INDEX_PROBE]}")
        if row["name"] in SPAN_GEOMETRY:
            print(f"MICROS_GRANT03_WORK_SUMMARY case={row['name']}"
                  f" range_visits={counts.per_kind[Kind.RANGE_VISIT]}"
                  f" units={counts.per_kind[Kind.COPY_UNIT]}"
                  f" copied_bytes={counts.per_kind[Kind.COPIED_BYTE]}"
                  f" runtime_bytes={counts.per_owner[Owner.RUNTIME][Kind.COPIED_BYTE]}")
    print("MICROS_GRANT01_CURRENT_PASS products=2")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("MICROS_GRANT01_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
