#!/usr/bin/env -S python3.15 -E -S -B

"""One CI-owned stack profile; never a complete stack certificate."""

import argparse
import os
from pathlib import Path
import sys
import time

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.check_stack_evidence import static_configuration
from tools.ci_environment import collect_environment
from tools.ci_partition import part_profiles
from tools.ci_records import owned_file
from tools.ci_stack_records import observed_record, static_record, validate_profile
from tools.stack_artifact_files import read_regular
from tools.stack_bound_build import build_observed_profile
from tools.stack_bound_manifest import SEAL, encoded, publish_json, replace_bytes, verify_build_seal
from tools.stack_bound_run import run_observed_scope
from tools.stack_bound_scope import SCENARIOS
from tools.validation_context import capture_candidate


ROOT = Path(__file__).resolve().parent.parent


def require_profile_builds(root, context, record):
    candidate = capture_candidate(root)
    if (
        not candidate.clean or candidate.source != context["candidate"]
        or candidate.repository != context["repository_context"]
    ):
        raise ValueError("CI profile source changed after its proof")
    profile = record["profile"]
    identities = {
        "ordinary": record["static"]["ordinary_seal"],
        "metadata": record["static"]["metadata_seal"],
    }
    if record["observed"] is not None:
        identities["observed"] = record["observed"]["observed_seal"]
    for mode, identity in identities.items():
        directory = Path(root) / "build/stack-evidence" / profile / mode
        verify_build_seal(root, directory, profile, mode, candidate)
        if read_regular(directory / SEAL)[1] != identity:
            raise ValueError("CI profile replaced an originally proved build seal")
    return identities


def run_profile(root, name, environment):
    root = Path(root)
    part = environment.get("CI_PART")
    profiles = tuple(row for row in part_profiles(root, part) if row.configure == name)
    if len(profiles) != 1:
        raise ValueError("CI profile is not owned by this target part")
    profile, = profiles
    before = collect_environment(root, environment)
    candidate = capture_candidate(root)
    if not candidate.clean or candidate.source != before["candidate"]:
        raise ValueError("CI profile lacks its exact starting candidate")
    output = owned_file(root, f"build/ci/parts/{part}")
    result_path = owned_file(output, f"profiles/{name}.json")
    if result_path.exists():
        raise ValueError("CI profile cannot reuse a prior result")
    started = time.monotonic_ns()
    print(f"CI_PHASE {part} {name} ordinary-metadata-proof START", flush=True)
    static = static_configuration(root, profile, candidate)
    print(
        f"CI_PHASE {part} {name} ordinary-metadata-proof PASS duration_ns={time.monotonic_ns() - started}",
        flush=True,
    )
    observed = None
    if name in SCENARIOS:
        started = time.monotonic_ns()
        print(f"CI_PHASE {part} {name} observed-build-proof-guest START", flush=True)
        directory = build_observed_profile(root, profile, environment["MICROS_PYTHON"])
        observed = run_observed_scope(root, directory)
        print(observed.transcript, end="", flush=True)
        print(
            f"CI_PHASE {part} {name} observed-build-proof-guest PASS duration_ns={time.monotonic_ns() - started}",
            flush=True,
        )
    after = collect_environment(root, environment)
    if encoded(before) != encoded(after):
        raise ValueError("CI profile host/source/tool identity drifted")
    record = {
        "version": 1, "status": "PROFILE_COMPLETE", "part": part, "profile": name,
        "before": before, "after": after, "static": static_record(static),
        "observed": None if observed is None else observed_record(observed),
    }
    identities = require_profile_builds(root, before, record)
    for mode in identities:
        source = root / "build/stack-evidence" / name / mode / SEAL
        target = owned_file(output, f"seals/{name}/{mode}.json")
        target.parent.mkdir(parents=True, exist_ok=True)
        replace_bytes(target, read_regular(source)[0])
    result_path.parent.mkdir(parents=True, exist_ok=True)

    def validate():
        if encoded(collect_environment(root, environment)) != encoded(before):
            raise ValueError("CI profile context changed during publication")
        require_profile_builds(root, before, record)
        validate_profile(record, name, before, output)

    publish_json(result_path, record, validate=validate)
    print(f"CI_PROFILE_COMPLETE {part} {name}", flush=True)
    return record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True)
    arguments = parser.parse_args(argv)
    run_profile(ROOT, arguments.profile, dict(os.environ))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
