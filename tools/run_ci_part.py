#!/usr/bin/env -S python3.15 -E -S -B

"""Execute one closed parallel-CI producer with live attributable output."""

import argparse
import os
from pathlib import Path
import shlex
import sys
import time

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.ci_environment import collect_environment
from tools.ci_execution import stream_command
from tools.ci_partition import PARTS, part_commands, part_profiles
from tools.ci_parts import validate_part_record
from tools.ci_records import file_inventory, owned_file, read_json
from tools.ci_stack_profile import require_profile_builds
from tools.python_host_runtime import python_command
from tools.stack_bound_manifest import encoded, publish_json


ROOT = Path(__file__).resolve().parent.parent


def run_part(root, part, environment):
    root = Path(root)
    if part not in PARTS or environment.get("CI_PART") != part:
        raise ValueError("CI part is outside its fixed hosted role")
    started = time.monotonic_ns()
    before = collect_environment(root, environment)
    output = owned_file(root, f"build/ci/parts/{part}")
    output.mkdir(parents=True, exist_ok=True)
    result_path = owned_file(output, "result.json")
    if result_path.exists():
        raise ValueError("CI part cannot reuse a previous completion")
    publish_json(owned_file(output, "environment.json"), before)
    print(f"CI_PART {part} START", flush=True)
    assigned = part_profiles(root, part)
    commands = []
    profiles = []
    for index, command in enumerate(part_commands(root, part, before["base"])):
        log = f"logs/{index:03d}.log"
        print(f"CI_COMMAND {part} START {shlex.join(command)}", flush=True)
        result = stream_command(command, root, owned_file(output, log))
        commands.append({**result, "log": log})
        print(f"CI_COMMAND {part} END exit={result['exit_code']} duration_ns={result['duration_ns']}", flush=True)
        if result["exit_code"] != 0:
            raise ValueError("CI canonical command failed; no complete part is publishable")
        if encoded(collect_environment(root, environment)) != encoded(before):
            raise ValueError("CI source/tool context changed during a canonical command")
        if not assigned:
            continue
        name = assigned[index].configure
        log = f"logs/stack-{name}.log"
        command = python_command(
            environment["MICROS_PYTHON"], "tools/ci_stack_profile.py", "--profile", name,
            environment=environment,
        )
        print(f"CI_PROFILE {part} {name} START", flush=True)
        result = stream_command(command, root, owned_file(output, log))
        if result["exit_code"] != 0:
            raise ValueError("CI stack profile failed; no complete part is publishable")
        profiles.append({
            "profile": name, "path": f"profiles/{name}.json", "command": {**result, "log": log},
        })
        print(f"CI_PROFILE {part} {name} END duration_ns={result['duration_ns']}", flush=True)
    after = collect_environment(root, environment)
    if encoded(before) != encoded(after):
        raise ValueError("CI part context changed during execution")
    payload = {
        "version": 1, "status": "PART_COMPLETE", "part": part,
        "before": before, "after": after, "commands": commands, "profiles": profiles,
        "files": file_inventory(output),
    }

    def validate():
        if encoded(collect_environment(root, environment)) != encoded(before):
            raise ValueError("CI context changed during part publication")
        for profile in profiles:
            require_profile_builds(root, before, read_json(owned_file(output, profile["path"])))
        validate_part_record(root, output, before, part, payload)

    publish_json(result_path, payload, validate=validate)
    print(
        f"CI_PART_COMPLETE {part} commands={len(commands)} profiles={len(profiles)} "
        f"duration_ns={time.monotonic_ns() - started}", flush=True,
    )
    return payload


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--part", choices=PARTS, required=True)
    arguments = parser.parse_args(argv)
    run_part(ROOT, arguments.part, dict(os.environ))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
