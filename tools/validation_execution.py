"""Canonical workflow execution and current-candidate provenance."""

from contextlib import nullcontext
import json
from pathlib import Path
import shlex
import subprocess
import sys

from tools import validation_context, validation_evidence


class ExecutionError(ValueError):
    pass


def _workflow(command, workflows):
    if (
        len(command) == 4 and command[:3] == ("cmake", "--workflow", "--preset")
        and command[3] in workflows
    ):
        return command[3]
    return None


def _validate_commands(commands, workflows):
    for command in commands:
        if _workflow(command, workflows) is not None:
            continue
        if command in (
            ("python3", "tools/check_docs.py"),
            ("git", "diff", "--cached", "--check"),
            ("git", "diff", "--check"),
        ):
            continue
        if (
            len(command) == 4 and command[:3] == ("git", "diff", "--check")
            and command[3].endswith("...HEAD") and not command[3].startswith("-")
            and command[3] != "...HEAD"
        ):
            continue
        raise ExecutionError("only fixed canonical validation commands may execute")


def _identical(first, second):
    options = {"sort_keys": True, "separators": (",", ":"), "allow_nan": False}
    return json.dumps(first, **options) == json.dumps(second, **options)


def _source(root, expected=None):
    current = validation_context.capture_candidate(root)
    if expected is not None and current != expected:
        raise ExecutionError("source identity changed; regenerate and validate the current plan")
    return current


def _profile(root, name, source):
    if not source.clean:
        return None, "dirty tracked iteration cannot publish reusable evidence"
    try:
        profile = validation_context.collect(root, name)
    except validation_context.UnsupportedContext as error:
        return None, str(error)
    if type(profile) is not dict or set(profile) != {"context", "inputs", "outputs"}:
        raise ExecutionError("closed profile schema cannot replace source/command authority")
    return profile, None


def _identity(source, profile):
    return {
        "repository": source.repository, "candidate": source.source, **profile,
    }


def _receipt_result(entry, record, status):
    entry.update(
        status=status, producer=record["producer"], log=record["log"],
        candidate=record["candidate"], exit_code=0,
    )


def _reuse(root, name, source, profile, store, entry):
    record, reason = store.lookup(name, _identity(source, profile))
    if record is None:
        return False, reason
    _source(root, source)
    after, failure = _profile(root, name, source)
    if after is None or not _identical(profile, after):
        return False, failure or "context/artifact changed during receipt verification"
    _source(root, source)
    _receipt_result(entry, record, "REUSED")
    print(
        f"REUSED {name} producer={record['producer']['run_id']}"
        f" HEAD={source.source['head']} tree={source.source['tree']}"
        f" log={record['log']['path']} sha256={record['log']['sha256']}",
        flush=True,
    )
    return True, reason


def _postflight(root, name, source, before):
    _source(root, source)
    if before is None:
        return None
    after, reason = _profile(root, name, source)
    if after is None:
        raise ExecutionError("execution context became ineligible: " + reason)
    if not _identical(before["context"], after["context"]) or not _identical(
        before["inputs"], after["inputs"]
    ):
        raise ExecutionError("execution context/input changed during the fresh workflow")
    _source(root, source)
    return _identity(source, after)


def _completed_log(store, attempt, entry):
    entry.update(
        producer={
            "run_id": store.run_id, "attempt_id": attempt.identifier,
            "tool_version": validation_evidence.TOOL_VERSION,
        },
        log=attempt.recorded_log,
    )
    store.replay_log(attempt.recorded_log, sys.stdout)


def _fresh_workflow(root, name, source, before, store, entry):
    attempt = store.start(name)
    entry["status"] = "FAILED"
    print("FRESH " + shlex.join(entry["command"]), flush=True)
    completed = subprocess.run(
        tuple(entry["command"]), cwd=root, check=False,
        stdout=attempt.log, stderr=subprocess.STDOUT,
    )
    entry["exit_code"] = completed.returncode
    if completed.returncode != 0:
        store.complete(attempt, None, completed.returncode)
        _completed_log(store, attempt, entry)
        return completed.returncode
    try:
        identity = _postflight(root, name, source, before)
    except (ExecutionError, validation_context.UnsupportedContext, OSError):
        store.complete(attempt, None, completed.returncode)
        _completed_log(store, attempt, entry)
        raise
    record = store.complete(attempt, identity, completed.returncode)
    _completed_log(store, attempt, entry)
    entry["status"] = "FRESH"
    if record is not None:
        _receipt_result(entry, record, "FRESH")
    return 0


def _execute_workflow(root, name, source, reuse, store, entry):
    _source(root, source)
    profile, reason = _profile(root, name, source)
    if reuse and profile is not None:
        hit, reason = _reuse(root, name, source, profile, store, entry)
        if hit:
            return 0
    if reason:
        print(f"MISS {name}: {reason}", flush=True)
    return _fresh_workflow(root, name, source, profile, store, entry)


def _report(source, entries, *, gate_only, success):
    print("\nVALIDATION_REPORT " + json.dumps({
        "candidate": source.source if source is not None else None,
        "authoritative": not gate_only,
        "final_candidate": bool(source is not None and source.clean and not gate_only and success),
        "commands": entries,
    }, sort_keys=True, separators=(",", ":")), flush=True)


def execute_plan(root, commands, workflows, *, reuse=False, gate_only=False):
    root = Path(root).resolve()
    commands = [tuple(command) for command in commands]
    workflows = frozenset(workflows)
    _validate_commands(commands, workflows)
    if gate_only and (
        reuse or len(commands) != 1 or _workflow(commands[0], workflows) is None
    ):
        raise ExecutionError("gate-only recording requires exactly one fresh canonical workflow")
    entries = [{"command": list(command), "status": "NOT_RUN"} for command in commands]
    source = None
    result = 1
    if gate_only:
        print("GATE-ONLY: fresh workflow evidence, not final author validation", flush=True)
    try:
        source = _source(root)
        if not source.clean and (reuse or gate_only):
            raise ExecutionError("reuse/record requires the exact clean staged candidate")
        if not source.clean:
            print("Fresh iteration only: dirty tracked input is not final/reusable evidence", flush=True)
        needs_store = any(_workflow(command, workflows) is not None for command in commands)
        context = validation_evidence.EvidenceStore(root, workflows) if needs_store else nullcontext(None)
        with context as store:
            result = 0
            for command, entry in zip(commands, entries):
                name = _workflow(command, workflows)
                if name is not None:
                    result = _execute_workflow(root, name, source, reuse, store, entry)
                else:
                    _source(root, source)
                    entry["status"] = "FAILED"
                    print("FRESH " + shlex.join(command), flush=True)
                    completed = subprocess.run(command, check=False, cwd=root)
                    entry["exit_code"] = completed.returncode
                    result = completed.returncode
                    _source(root, source)
                    if result == 0:
                        entry["status"] = "FRESH"
                if result != 0:
                    break
            _source(root, source)
    except KeyboardInterrupt:
        print("validation interrupted; no successful current attempt published", file=sys.stderr)
        result = 130
    except (
        ExecutionError, validation_context.UnsupportedContext,
        validation_evidence.EvidenceError, OSError,
    ) as error:
        print(f"validation failed: {error}", file=sys.stderr)
        result = 1
    _report(source, entries, gate_only=gate_only, success=result == 0)
    return result if result >= 0 else 1
