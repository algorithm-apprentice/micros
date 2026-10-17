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


class ProducerExecutionError(ExecutionError):
    def __init__(self, message, failure_code):
        super().__init__(message)
        self.failure_code = failure_code


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


def _producer_workflows(commands, workflows):
    selected = []
    for command in commands:
        name = _workflow(command, workflows)
        if (
            name is not None
            and name in validation_context.PROFILES
            and name not in selected
        ):
            selected.append(name)
    return tuple(selected)


def _readiness_entry(name, readiness):
    if readiness.outcome not in ("READY", "PREPARE", "UNSUPPORTED"):
        raise ExecutionError("unknown producer readiness outcome")
    if readiness.outcome == "READY":
        if (
            readiness.reason_code != "ready"
            or readiness.profile is None
            or readiness.preparation_argv is not None
        ):
            raise ExecutionError("invalid ready producer profile")
    elif readiness.outcome == "PREPARE":
        if (
            readiness.reason_code
            not in validation_context.PREPARATION_REASON_CODES
            or readiness.profile is not None
            or readiness.preparation_argv
                != ("cmake", "--workflow", "--preset", name)
        ):
            raise ExecutionError("invalid producer preparation result")
    elif (
        readiness.reason_code != "profile-unsupported"
        or readiness.profile is not None
        or readiness.preparation_argv is not None
    ):
        raise ExecutionError("invalid unsupported producer profile")
    return {
        "workflow": name,
        "outcome": readiness.outcome,
        "reason_code": readiness.reason_code,
        "preparation_argv": (
            list(readiness.preparation_argv)
            if readiness.preparation_argv is not None
            else None
        ),
    }


def _producer_barrier(root, names, source, report):
    profiles = {}
    blocked = False
    for name in names:
        readiness = validation_context.collect_readiness(root, name)
        entry = _readiness_entry(name, readiness)
        report["readiness"].append(entry)
        print(
            f"PRODUCER {readiness.outcome} {name}"
            f" reason={readiness.reason_code}",
            flush=True,
        )
        if readiness.preparation_argv is not None:
            print(
                "PREPARE " + shlex.join(readiness.preparation_argv),
                flush=True,
            )
        if readiness.detail:
            print(f"PRODUCER DETAIL {name}: {readiness.detail}", flush=True)
        if readiness.outcome == "READY":
            profile = readiness.profile
            if type(profile) is not dict or set(profile) != {
                "context", "inputs", "outputs",
            }:
                raise ExecutionError("closed profile schema cannot replace source authority")
            profiles[name] = profile
        else:
            blocked = True
    _source(root, source)
    if blocked:
        report["failure_code"] = "readiness-blocked"
        return None
    report["barrier_passed"] = True
    return profiles


def _producer_profile_matches(expected, actual):
    return _identical(expected["context"], actual["context"]) and _identical(
        expected["inputs"], actual["inputs"]
    )


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


def _fresh_workflow(
    root, name, source, before, store, entry, *, require_receipt=False,
):
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
    try:
        record = store.complete(attempt, identity, completed.returncode)
    except (validation_evidence.EvidenceError, OSError) as error:
        if require_receipt and attempt.recorded_log is not None:
            _completed_log(store, attempt, entry)
            raise ProducerExecutionError(
                "successful workflow did not publish its receipt",
                "receipt-not-published",
            ) from error
        raise
    _completed_log(store, attempt, entry)
    if require_receipt and record is None:
        raise ProducerExecutionError(
            "successful workflow did not publish its receipt",
            "receipt-not-published",
        )
    entry["status"] = "FRESH"
    if record is not None:
        _receipt_result(entry, record, "FRESH")
    return 0


def _execute_workflow(
    root, name, source, reuse, store, entry, *,
    producer_profile=None,
):
    _source(root, source)
    profile, reason = _profile(root, name, source)
    if producer_profile is not None:
        if profile is None or not _producer_profile_matches(
            producer_profile, profile
        ):
            entry["status"] = "FAILED"
            raise ProducerExecutionError(
                "producer profile changed before workflow launch",
                "profile-changed-before-launch",
            )
    if reuse and profile is not None:
        hit, reason = _reuse(root, name, source, profile, store, entry)
        if hit:
            return 0
    if reason:
        print(f"MISS {name}: {reason}", flush=True)
    return _fresh_workflow(
        root, name, source, profile, store, entry,
        require_receipt=producer_profile is not None,
    )


def _report(source, entries, *, gate_only, success, producer=None):
    report = {
        "candidate": source.source if source is not None else None,
        "authoritative": not gate_only,
        "final_candidate": bool(
            source is not None and source.clean and not gate_only and success
            and (producer is None or producer["complete"])
        ),
        "commands": entries,
    }
    if producer is not None:
        report["producer"] = producer
    print(
        "\nVALIDATION_REPORT " + json.dumps(
            report, sort_keys=True, separators=(",", ":"),
        ),
        flush=True,
    )


def _execute_nonworkflow(root, command, source, entry):
    _source(root, source)
    entry["status"] = "FAILED"
    print("FRESH " + shlex.join(command), flush=True)
    completed = subprocess.run(command, check=False, cwd=root)
    entry["exit_code"] = completed.returncode
    _source(root, source)
    if completed.returncode == 0:
        entry["status"] = "FRESH"
    return completed.returncode


def _producer_is_complete(
    commands, entries, workflows, source, report,
):
    selected = [
        entry
        for command, entry in zip(commands, entries)
        if _workflow(command, workflows) in report["selected_workflows"]
    ]
    return bool(
        selected
        and all(
            entry.get("status") == "FRESH"
            and entry.get("exit_code") == 0
            and entry.get("candidate") == source.source
            and entry.get("producer", {}).get("run_id") == report["run_id"]
            and "log" in entry
            for entry in selected
        )
    )


def _run_commands(
    root, commands, workflows, source, reuse, entries,
    producer_profiles, producer_report,
):
    needs_store = any(
        _workflow(command, workflows) is not None
        for command in commands
    )
    context = (
        validation_evidence.EvidenceStore(root, workflows)
        if needs_store else nullcontext(None)
    )
    producer_complete = False
    with context as store:
        if producer_report is not None:
            producer_report["run_id"] = store.run_id
        result = 0
        for command, entry in zip(commands, entries):
            name = _workflow(command, workflows)
            if name is not None:
                result = _execute_workflow(
                    root, name, source, reuse, store, entry,
                    producer_profile=(
                        producer_profiles.get(name)
                        if producer_profiles is not None
                        else None
                    ),
                )
            else:
                result = _execute_nonworkflow(
                    root, command, source, entry,
                )
            if result != 0:
                break
        _source(root, source)
        if producer_report is not None and result == 0:
            producer_complete = _producer_is_complete(
                commands, entries, workflows, source, producer_report,
            )
            if not producer_complete:
                producer_report["failure_code"] = "receipt-not-published"
                result = 1
        if (
            producer_report is not None
            and result != 0
            and producer_report["failure_code"] is None
        ):
            producer_report["failure_code"] = "plan-failed"
    if producer_report is not None and result == 0:
        producer_report["complete"] = producer_complete
    return result


def execute_plan(
    root, commands, workflows, *, reuse=False, producer=False,
    gate_only=False, expected_source=None,
):
    root = Path(root).resolve()
    commands = [tuple(command) for command in commands]
    workflows = frozenset(workflows)
    _validate_commands(commands, workflows)
    producer_names = _producer_workflows(commands, workflows)
    if producer and not producer_names:
        raise ExecutionError(
            "producer mode requires at least one selected reusable workflow"
        )
    if gate_only and (
        reuse or producer or len(commands) != 1
        or _workflow(commands[0], workflows) is None
    ):
        raise ExecutionError("gate-only recording requires exactly one fresh canonical workflow")
    entries = [{"command": list(command), "status": "NOT_RUN"} for command in commands]
    source = None
    result = 1
    producer_report = None
    producer_reportable = False
    producer_profiles = None
    if gate_only:
        print("GATE-ONLY: fresh workflow evidence, not final author validation", flush=True)
    try:
        source = _source(root, expected_source)
        if not source.clean and (reuse or gate_only):
            raise ExecutionError("reuse/record requires the exact clean staged candidate")
        if not source.clean:
            print("Fresh iteration only: dirty tracked input is not final/reusable evidence", flush=True)
        if producer:
            if not source.clean:
                raise ExecutionError(
                    "producer requires the exact clean staged candidate"
                )
            producer_report = {
                "selected_workflows": list(producer_names),
                "readiness": [],
                "barrier_passed": False,
                "run_id": None,
                "complete": False,
                "failure_code": None,
            }
            producer_profiles = _producer_barrier(
                root, producer_names, source, producer_report,
            )
            producer_reportable = True
            if producer_profiles is None:
                result = 1
                raise ProducerExecutionError(
                    "producer readiness blocked execution",
                    "readiness-blocked",
                )
        result = _run_commands(
            root, commands, workflows, source, reuse, entries,
            producer_profiles, producer_report,
        )
    except KeyboardInterrupt:
        if (
            producer_reportable
            and producer_report is not None
            and producer_report["failure_code"] is None
        ):
            producer_report["failure_code"] = "plan-failed"
        print("validation interrupted; no successful current attempt published", file=sys.stderr)
        result = 130
    except ProducerExecutionError as error:
        if producer_report is not None:
            producer_report["failure_code"] = error.failure_code
        print(f"validation failed: {error}", file=sys.stderr)
        result = 1
    except (
        ExecutionError, validation_context.UnsupportedContext,
        validation_evidence.EvidenceError, OSError,
    ) as error:
        if (
            producer_report is not None
            and producer_report["failure_code"] is None
        ):
            producer_report["failure_code"] = "plan-failed"
        print(f"validation failed: {error}", file=sys.stderr)
        result = 1
    if not producer or producer_reportable:
        _report(
            source, entries, gate_only=gate_only, success=result == 0,
            producer=producer_report,
        )
    return result if result >= 0 else 1
