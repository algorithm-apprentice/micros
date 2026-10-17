"""Bounded, reference-bound terminal CMake timeout ownership (ADR-0064)."""

from dataclasses import dataclass
import hashlib
from pathlib import Path
import re
import stat
import subprocess
import sys


TEMPLATES = {
    "test-qemu-smoke": (
        (),
        ("test-qemu-smoke",),
        "7e0612c122cc577c91ede6be8877fc97cf80a2bc2f99d33c472e3dfc8127645a",
    ),
    "test-qemu-panic": (
        (("MICROS_BUILD_PANIC_TEST",),),
        ("test-qemu-panic",),
        "7cc475000e95f9d1d20148c0c9745b5dd5b949eaa2f20ce5bbbc5443356bcbdc",
    ),
    "test-qemu-scheduler-invalid-context": (
        ((
            "MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST",
            "OR",
            "MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST",
        ),),
        (
            "test-qemu-scheduler-invalid-outgoing",
            "test-qemu-scheduler-invalid-next",
        ),
        "772d293d6252819e24a3294c5f331f60f5a8f47e2db0dc9b40fed3adda5bd20f",
    ),
}

SCOPES = ("if", "function", "macro", "foreach", "while", "block")


@dataclass(frozen=True)
class Token:
    start: int
    end: int
    value: str


@dataclass(frozen=True)
class Command:
    start: int
    end: int
    name: str
    arguments: tuple


def _space(content, position):
    while position < len(content):
        if content[position].isspace():
            position += 1
        elif content[position] == "#":
            if re.match(r"#\[(=*)\[", content[position:]):
                raise ValueError("bracket comments are outside the bounded grammar")
            newline = content.find("\n", position)
            position = len(content) if newline < 0 else newline + 1
        else:
            break
    return position


def _token(content, position):
    start = position
    if content[position] == '"':
        position += 1
        while position < len(content):
            if content[position] == "\\":
                position += 2
            elif content[position] == '"':
                return Token(start, position + 1, content[start:position + 1])
            else:
                position += 1
        raise ValueError("unclosed quoted argument")
    while position < len(content):
        character = content[position]
        if character.isspace() or character in "()#":
            break
        if re.match(r"\[(=*)\[", content[position:]):
            raise ValueError("bracket arguments are outside the bounded grammar")
        if character == '"':
            raise ValueError("compound quoted arguments are unsupported")
        if character == "\\":
            position += 2
        else:
            position += 1
    if position == start or position > len(content):
        raise ValueError("invalid argument")
    return Token(start, position, content[start:position])


def _command(content, position):
    start = position
    name = re.match(r"[A-Za-z_][A-Za-z_0-9]*", content[position:])
    if name is None:
        raise ValueError("expected command")
    position = _space(content, position + name.end())
    if position >= len(content) or content[position] != "(":
        raise ValueError("expected argument list")
    position += 1
    depth = 1
    arguments = []
    while position < len(content):
        position = _space(content, position)
        if position >= len(content):
            break
        if content[position] in "()":
            opening = content[position] == "("
            depth += 1 if opening else -1
            if depth == 0:
                return Command(start, position + 1, name.group().lower(), tuple(arguments))
            arguments.append(Token(position, position + 1, content[position]))
            position += 1
        else:
            argument = _token(content, position)
            arguments.append(argument)
            position = argument.end
    raise ValueError("unclosed command")


def _update_scope(command, scopes):
    name = command.name
    values = tuple(token.value for token in command.arguments)
    if name in SCOPES:
        scopes.append((name, values, "body"))
    elif name in ("else", "elseif"):
        if not scopes or scopes[-1][0] != "if":
            raise ValueError("unmatched conditional branch")
        scopes[-1] = ("if", values, name)
    elif name.startswith("end") and name[3:] in SCOPES:
        if not scopes or scopes[-1][0] != name[3:]:
            raise ValueError("unmatched scope end")
        scopes.pop()


def _normalized(content, command):
    positions = [
        index for index, token in enumerate(command.arguments)
        if token.value == "--timeout"
    ]
    if len(positions) != 1 or positions[0] + 1 >= len(command.arguments):
        raise ValueError("expected one timeout argument")
    token = command.arguments[positions[0] + 1]
    if not re.fullmatch(r"[1-9][0-9]{0,2}", token.value) or int(token.value) > 600:
        raise ValueError("timeout is outside the bounded grammar")
    return (
        content[command.start:token.start]
        + "<TIMEOUT>"
        + content[token.end:command.end]
    )


def _snapshot(content):
    position = 0
    scopes = []
    targets = {}
    outside = []
    last = 0
    while (position := _space(content, position)) < len(content):
        command = _command(content, position)
        if command.name == "add_custom_target" and command.arguments:
            target = command.arguments[0].value.strip('"')
            if target in TEMPLATES:
                guard, _, digest = TEMPLATES[target]
                expected = tuple(("if", values, "body") for values in guard)
                if target in targets or tuple(scopes) != expected:
                    raise ValueError("ambiguous target or unknown scope")
                normalized = _normalized(content, command)
                if hashlib.sha256(normalized.encode("utf-8")).hexdigest() != digest:
                    raise ValueError("target differs from the reviewed template")
                targets[target] = content[command.start:command.end]
                outside.extend((content[last:command.start], f"<OWNED:{target}>"))
                last = command.end
        _update_scope(command, scopes)
        position = command.end
    if scopes or set(targets) != set(TEMPLATES):
        raise ValueError("unclosed scope or missing reviewed target")
    outside.append(content[last:])
    return targets, "".join(outside)


def classify(before, after):
    """Return explicit workflow owners, or None for full-matrix fallback."""
    if before == after:
        return None
    try:
        old_targets, old_outside = _snapshot(before)
        new_targets, new_outside = _snapshot(after)
    except ValueError:
        return None
    if old_outside != new_outside:
        return None
    owners = []
    for target, (_, workflows, _) in TEMPLATES.items():
        if old_targets[target] != new_targets[target]:
            owners.extend(workflows)
    return tuple(owners) if owners else None


@dataclass(frozen=True)
class Snapshot:
    mode: str
    content: str


def classify_layers(snapshots):
    if len(snapshots) != 4 or any(
        snapshot is None or snapshot.mode != "100644" for snapshot in snapshots
    ):
        return None
    owners = []
    for before, after in zip(snapshots, snapshots[1:]):
        if before == after:
            continue
        workflows = classify(before.content, after.content)
        if workflows is None:
            return None
        for name in workflows:
            if name not in owners:
                owners.append(name)
    return tuple(owners) if owners else None


def _git(root, *arguments):
    return subprocess.run(
        ("git", *arguments), cwd=root, check=True,
        stdout=subprocess.PIPE,
    ).stdout.decode("utf-8")


def _git_snapshot(root, reference):
    if reference is None:
        entries = _git(root, "ls-files", "--stage", "--", "CMakeLists.txt").splitlines()
        if len(entries) != 1:
            return None
        metadata = entries[0].partition("\t")[0].split()
        if len(metadata) != 3 or metadata[2] != "0":
            return None
        source = ":CMakeLists.txt"
    else:
        entries = _git(root, "ls-tree", reference, "--", "CMakeLists.txt").splitlines()
        if len(entries) != 1:
            return None
        metadata = entries[0].partition("\t")[0].split()
        if len(metadata) != 3 or metadata[1] != "blob":
            return None
        source = f"{reference}:CMakeLists.txt"
    if metadata[0] != "100644":
        return None
    return Snapshot(metadata[0], _git(root, "show", source))


def _worktree_snapshot(root):
    path = Path(root) / "CMakeLists.txt"
    try:
        mode = path.lstat().st_mode
    except FileNotFoundError:
        return None
    if not stat.S_ISREG(mode) or mode & 0o111:
        return None
    return Snapshot("100644", path.read_bytes().decode("utf-8"))


def discover(root, base):
    merge_base = _git(root, "merge-base", base, "HEAD").strip()
    try:
        snapshots = (
            _git_snapshot(root, merge_base),
            _git_snapshot(root, "HEAD"),
            _git_snapshot(root, None),
            _worktree_snapshot(root),
        )
    except UnicodeDecodeError:
        print("validation CMake impact: unsupported encoding; full matrix", file=sys.stderr)
        return None
    owners = classify_layers(snapshots)
    message = (
        "owned timeout edits: " + ", ".join(owners)
        if owners else "unverified/shared change; full matrix"
    )
    print("validation CMake impact: " + message, file=sys.stderr)
    return owners
