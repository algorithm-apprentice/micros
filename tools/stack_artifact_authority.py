"""Closed same-candidate metadata leaf authority from Accepted ADR-0085."""

from pathlib import Path
import re

from tools.stack_bound_types import StackEvidenceError, require_uint64


LEAVES = {
    "micros_elf_spawn_init": "applications/micros-elf-spawn-init.elf",
    "micros_elf_spawn_child": "applications/micros-elf-spawn-child.elf",
    "micros_application_runtime_probe": "applications/micros-application-runtime-probe.elf",
}
PAIR_FIELDS = {
    "version", "mode", "candidate", "profile", "tools",
    "ordinary_root", "metadata_root", "entries",
}
ENTRY_FIELDS = {"leaf", "path", "ordinary_identity", "recipe"}


def require_digest(value):
    if type(value) is not str or re.fullmatch(r"[0-9a-f]{64}", value) is None:
        raise StackEvidenceError("invalid sealed artifact digest")


def require_identity(identity):
    if type(identity) is not dict or set(identity) != {"kind", "mode", "size", "sha256"}:
        raise StackEvidenceError("incomplete ordinary file identity")
    if identity["kind"] != "file" or type(identity["mode"]) is not int or not 0 <= identity["mode"] <= 0o777:
        raise StackEvidenceError("unreviewed ordinary file kind or mode")
    require_uint64(identity["size"], "ordinary artifact size")
    if identity["size"] == 0:
        raise StackEvidenceError("empty ordinary ELF artifact")
    require_digest(identity["sha256"])


def validate_pair_entry(root, record, candidate, tools, recipes, leaf):
    root = Path(root)
    if type(record) is not dict or set(record) != PAIR_FIELDS:
        raise StackEvidenceError("metadata pair has an incomplete closed schema")
    if type(record["version"]) is not int or record["version"] != 1 or record["mode"] != "metadata":
        raise StackEvidenceError("artifact canonicalization is metadata-only")
    if (
        not candidate.clean or candidate.repository.get("root") != str(root)
        or record["candidate"] != candidate.source or candidate.source.get("missing_paths")
        or record["tools"] != tools
    ):
        raise StackEvidenceError("metadata pair source or tool authority changed")
    profile = record["profile"]
    if type(profile) is not str or re.fullmatch(r"riscv64-[a-z0-9-]+", profile) is None:
        raise StackEvidenceError("unreviewed metadata configuration")
    ordinary_root = "build/stack-evidence/" + profile + "/ordinary"
    metadata_root = "build/stack-evidence/" + profile + "/metadata"
    if record["ordinary_root"] != ordinary_root or record["metadata_root"] != metadata_root:
        raise StackEvidenceError("metadata pair escapes its exact build directories")
    if type(recipes) is not dict or not recipes or not set(recipes) <= set(LEAVES):
        raise StackEvidenceError("invalid current leaf recipe set")
    entries = record["entries"]
    if type(entries) is not list or not entries or len(entries) > len(LEAVES):
        raise StackEvidenceError("missing complete metadata leaf set")
    seen = {}
    for entry in entries:
        if type(entry) is not dict or set(entry) != ENTRY_FIELDS:
            raise StackEvidenceError("incomplete metadata leaf authority")
        identifier = entry["leaf"]
        if type(identifier) is not str or identifier not in LEAVES or identifier in seen:
            raise StackEvidenceError("unknown or duplicate metadata leaf")
        if entry["path"] != LEAVES[identifier] or identifier not in recipes:
            raise StackEvidenceError("metadata leaf path or recipe is unreviewed")
        require_digest(entry["recipe"])
        require_digest(recipes[identifier])
        if entry["recipe"] != recipes[identifier]:
            raise StackEvidenceError("metadata compilation or link recipe changed")
        require_identity(entry["ordinary_identity"])
        seen[identifier] = entry
    if set(seen) != set(recipes) or type(leaf) is not str or leaf not in seen:
        raise StackEvidenceError("omitted or unexpected metadata leaf")
    entry = seen[leaf]
    return (
        root / ordinary_root / entry["path"],
        root / metadata_root / entry["path"],
        dict(entry["ordinary_identity"]),
    )
