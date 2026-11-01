#!/usr/bin/env -S python3.15 -E -S -B

"""Complete current-attempt CI fan-in without creating a local receipt."""

import argparse
import json
import os
from pathlib import Path
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.ci_environment import collect_environment
from tools.ci_partition import PARTS
from tools.ci_parts import assemble_completion, validate_part
from tools.ci_records import data_identity, owned_file, require_files
from tools.stack_bound_manifest import encoded, publish_json


ROOT = Path(__file__).resolve().parent.parent


def collect_results(root, environment):
    if environment.get("CI_PRODUCER_RESULT") != "success":
        raise ValueError("CI producer matrix did not completely succeed")
    root = Path(root)
    before = collect_environment(root, environment, role="collector")
    event = before["event"]
    names = {
        part: f"micros-ci-{event['run_id']}-{event['attempt']}-part-{part}" for part in PARTS
    }
    directory = owned_file(root, "build/ci/downloads")

    def require_directories():
        if directory.is_symlink() or not directory.is_dir() or {path.name for path in directory.iterdir()} != set(names.values()):
            raise ValueError("CI fan-in has missing, duplicate or foreign artifacts")
        for name in names.values():
            path = owned_file(directory, name)
            if not path.is_dir():
                raise ValueError("CI artifact is not its owned data directory")

    require_directories()
    parts = [validate_part(root, directory / names[part], before, part) for part in PARTS]
    result = assemble_completion(root, before, parts)
    result_path = owned_file(root, "build/ci/complete.json")
    if result_path.exists():
        raise ValueError("CI collector cannot borrow a previous completed result")

    def validate():
        if encoded(collect_environment(root, environment, role="collector")) != encoded(before):
            raise ValueError("CI collector source/context changed during publication")
        require_directories()
        for part, record in zip(PARTS, parts):
            path = directory / names[part]
            require_files(path, record["files"])
            if data_identity(owned_file(path, "result.json")) != record["result_identity"]:
                raise ValueError("CI part result changed during aggregate publication")

    publish_json(result_path, result, validate=validate)
    summary = {
        "outcome": "PASS", "candidate": before["candidate"], "event": event,
        "parts": list(PARTS), "executed_commands": len(result["executed_commands"]),
        "distributed_gates": len(result["distributed_gates"]),
        "static_configurations": len(result["stack"]["static"]),
        "static_images": sum(len(row["images"]) for row in result["stack"]["static"]),
        "observed_scopes": len(result["stack"]["certificates"]),
        "result": data_identity(result_path),
    }
    print("CI_VALIDATION_REPORT " + json.dumps(summary, sort_keys=True), flush=True)
    return result


if __name__ == "__main__":
    argparse.ArgumentParser(description=__doc__).parse_args()
    collect_results(ROOT, dict(os.environ))
