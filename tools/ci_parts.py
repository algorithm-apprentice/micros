"""Complete part and full-intent consistency; no imported local authority."""

from tools import validation_plan
from tools.ci_partition import PARTS, TARGET_PARTS, part_commands, part_profiles, target_partition
from tools.ci_records import data_identity, owned_file, read_json, require_context, require_files, require_keys
from tools.ci_stack_records import validate_profile
from tools.stack_bound_build import static_profiles
from tools.stack_bound_manifest import encoded
from tools.stack_bound_scope import SCENARIOS, expected_images
from tools.stack_bound_types import require_uint64


PART_FIELDS = ("version", "status", "part", "before", "after", "commands", "profiles", "files")


def require_command(row, command, log):
    require_keys(row, ("argv", "exit_code", "duration_ns", "log"), "CI command")
    require_uint64(row["duration_ns"], "CI command duration")
    if row["argv"] != list(command) or type(row["exit_code"]) is not int or row["exit_code"] != 0 or row["log"] != log:
        raise ValueError("CI command is missing, failed, borrowed or outside its canonical owner")


def validate_commands(root, part, context, commands):
    expected = part_commands(root, part, context["base"])
    if type(commands) is not list or len(commands) != len(expected):
        raise ValueError("CI part has incomplete canonical command coverage")
    for index, (row, command) in enumerate(zip(commands, expected)):
        require_command(row, command, f"logs/{index:03d}.log")


def validate_part_record(root, directory, context, part, row):
    require_keys(row, PART_FIELDS, "CI part")
    if type(row["version"]) is not int or (row["version"], row["status"], row["part"]) != (1, "PART_COMPLETE", part):
        raise ValueError("CI part has an unknown or incomplete outcome")
    require_context(row["before"], context, part)
    if encoded(row["before"]) != encoded(row["after"]):
        raise ValueError("CI part context changed during execution")
    before = row["before"]
    require_files(directory, row["files"])
    if encoded(read_json(owned_file(directory, "environment.json"))) != encoded(before):
        raise ValueError("CI part preflight file differs from its original context")
    validate_commands(root, part, before, row["commands"])
    profiles = part_profiles(root, part)
    if type(row["profiles"]) is not list or len(row["profiles"]) != len(profiles):
        raise ValueError("CI part omitted an assigned stack profile")
    files = {"environment.json", "diagnostics/bootstrap.log", "diagnostics/actionlint.log"}
    files.update(f"logs/{index:03d}.log" for index in range(len(row["commands"])))
    verified = []
    for entry, profile in zip(row["profiles"], profiles):
        name = profile.configure
        path = f"profiles/{name}.json"
        log = f"logs/stack-{name}.log"
        require_keys(entry, ("profile", "path", "command"), "CI profile reference")
        if (entry["profile"], entry["path"]) != (name, path):
            raise ValueError("CI profile reference has a foreign owner")
        require_command(
            entry["command"],
            (before["tools"]["python"]["path"], "-E", "-S", "-B", "tools/ci_stack_profile.py", "--profile", name),
            log,
        )
        value = read_json(owned_file(directory, path))
        validate_profile(value, name, before, directory)
        verified.append(value)
        files.update((path, log))
        files.update(f"seals/{name}/{mode}.json" for mode in (
            ("ordinary", "metadata", "observed") if name in SCENARIOS else ("ordinary", "metadata")
        ))
    if set(row["files"]) != files:
        raise ValueError("CI part contains missing or unregistered artifact files")
    require_files(directory, row["files"])
    return verified


def validate_part(root, directory, context, part):
    result_path = owned_file(directory, "result.json")
    result_identity = data_identity(result_path)
    row = read_json(result_path)
    verified = validate_part_record(root, directory, context, part, row)
    if data_identity(result_path) != result_identity:
        raise ValueError("CI part changed while its records were read")
    return {**row, "profile_records": verified, "result_identity": result_identity}


def assemble_completion(root, context, parts):
    if len(parts) != len(PARTS) or {row["part"] for row in parts} != set(PARTS):
        raise ValueError("CI completion requires every producer exactly once")
    by_part = {row["part"]: row for row in parts}
    partition = target_partition(root)
    commands, profiles = {}, {}
    toolset = None
    for name in PARTS:
        row = by_part[name]
        require_context(row["before"], context, name)
        validate_commands(root, name, row["before"], row["commands"])
        selected = {
            key: row["before"]["tools"][key]
            for key in ("compiler", "linker", "llvm-nm", "llvm-readelf")
        }
        selected["firmware"] = row["before"]["firmware"]
        if toolset is None:
            toolset = selected
        elif encoded(toolset) != encoded(selected):
            raise ValueError("CI producers used inconsistent target tools or firmware")
        expected = tuple(profile.configure for profile in partition.get(name, ()))
        actual = row["profile_records"]
        if tuple(profile["profile"] for profile in actual) != expected:
            raise ValueError("CI completion omitted or borrowed a profile")
        for entry in row["commands"]:
            key = tuple(entry["argv"])
            if key in commands:
                raise ValueError("CI completion duplicated a canonical command")
            commands[key] = {"part": name, **entry}
        for profile in actual:
            selected_profile = profile["profile"]
            if selected_profile in profiles or profile["static"]["profile"] != selected_profile:
                raise ValueError("CI completion duplicated or borrowed a static profile")
            if sorted(image["image"] for image in profile["static"]["images"]) != list(expected_images(selected_profile)):
                raise ValueError("CI completion omitted a static executable")
            observed = profile["observed"]
            if selected_profile in SCENARIOS:
                if observed is None or (observed["profile"], observed["gap"], observed["outcome"]) != (
                    selected_profile, SCENARIOS[selected_profile].gap, "PASS",
                ):
                    raise ValueError("CI completion omitted an observed scope")
            elif observed is not None:
                raise ValueError("CI completion fabricated an observed scope")
            profiles[selected_profile] = profile
    full = validation_plan.plan([], "full", context["base"])
    executed = [command for command in full if command != validation_plan.STACK_EVIDENCE]
    if set(commands) != set(executed):
        raise ValueError("CI completion differs from the canonical full intent set")
    return {
        "version": 1, "kind": "CI_VALIDATION_REPORT", "outcome": "PASS", "context": context,
        "parts": [
            {"part": name, "context": by_part[name]["before"],
             "result_identity": by_part[name]["result_identity"], "files": by_part[name]["files"]}
            for name in PARTS
        ],
        "executed_commands": [commands[command] for command in executed],
        "distributed_gates": [{
            "intent": list(validation_plan.STACK_EVIDENCE), "kind": "distributed-stack",
            "parts": list(TARGET_PARTS), "outcome": "PASS",
        }],
        "stack": {
            "version": 1, "scope": "current-b01-b12", "candidate": context["candidate"],
            "static": [profiles[profile.configure]["static"] for profile in static_profiles(root)],
            "certificates": [profiles[name]["observed"] for name in SCENARIOS],
            "future_admission": {
                "gap": "B13-E007", "outcome": "BLOCKED", "reason": "production-node-q-absent",
                "completion_barrier": "STACK13",
            },
        },
    }
