"""Checked simultaneous fixed-frame composition, without host recursion."""

from collections import deque
import re

from tools.stack_bound_types import (
    CallSite, Certificate, EdgeKind, Frame, Root, StackEvidenceError,
    checked_add, require_function, require_uint64,
)


def frame_inventory(frames):
    if type(frames) is not tuple or not frames:
        raise StackEvidenceError("missing complete frame inventory")
    result = {}
    for frame in frames:
        if type(frame) is not Frame:
            raise StackEvidenceError("invalid frame record")
        require_function(frame.function)
        require_uint64(frame.bytes_used, "frame size")
        if frame.classification != "static":
            raise StackEvidenceError("frame is not fixed static storage")
        if frame.function in result:
            raise StackEvidenceError("duplicate qualified frame identity")
        result[frame.function] = frame.bytes_used
    return result


def root_inventory(roots, frames):
    if type(roots) is not tuple or not roots:
        raise StackEvidenceError("missing complete stack root inventory")
    identifiers = set()
    entries = set()
    for root in roots:
        if type(root) is not Root:
            raise StackEvidenceError("invalid stack root")
        require_function(root.function)
        require_uint64(root.capacity, "capacity")
        require_uint64(root.initial_bytes, "initial occupancy")
        if (
            type(root.identifier) is not str
            or not re.fullmatch(r"[a-z][a-z0-9-]{0,95}", root.identifier)
            or root.capacity not in (4096, 16384)
            or root.initial_bytes > root.capacity
        ):
            raise StackEvidenceError("invalid Accepted stack budget")
        if root.identifier in identifiers:
            raise StackEvidenceError("duplicate stack root identity")
        if root.function not in frames:
            raise StackEvidenceError("unknown root frame")
        identifiers.add(root.identifier)
        entries.add(root.function)
    return entries


def call_inventory(edges, frames, entries):
    if type(edges) is not tuple:
        raise StackEvidenceError("invalid call inventory")
    children = {key: set() for key in frames}
    seen = set()
    for edge in edges:
        if type(edge) is not CallSite or type(edge.kind) is not EdgeKind:
            raise StackEvidenceError("invalid machine call-site record")
        require_function(edge.caller)
        require_uint64(edge.offset, "call offset")
        if edge.caller not in frames:
            raise StackEvidenceError("unknown caller frame")
        if type(edge.targets) is not tuple or not edge.targets:
            raise StackEvidenceError("unbound machine call site")
        site = (edge.caller, edge.offset)
        if site in seen:
            raise StackEvidenceError("duplicate machine call site")
        seen.add(site)
        targets = set()
        for target in edge.targets:
            require_function(target)
            if target not in frames:
                raise StackEvidenceError("unknown callee frame")
            if target in targets:
                raise StackEvidenceError("duplicate call target")
            targets.add(target)
            if edge.kind is EdgeKind.RESET:
                if target not in entries:
                    raise StackEvidenceError("stack epoch lacks independently budgeted root")
            else:
                if (
                    edge.caller.variant != target.variant
                    or edge.caller.image != target.image
                ):
                    raise StackEvidenceError("ordinary call crossed an executable")
                children[edge.caller].add(target)
    return children


def fixed_bounds(frames, children):
    pending = {key: len(targets) for key, targets in children.items()}
    parents = {key: [] for key in frames}
    for key, targets in children.items():
        for target in targets:
            parents[target].append(key)
    ready = deque(key for key, count in pending.items() if count == 0)
    bounds = {}
    successors = {}
    while ready:
        key = ready.popleft()
        targets = children[key]
        if targets:
            selected = max(targets, key=lambda target: (bounds[target], target))
            bounds[key] = checked_add(frames[key], bounds[selected])
            successors[key] = selected
        else:
            bounds[key] = frames[key]
            successors[key] = None
        for parent in parents[key]:
            pending[parent] -= 1
            if pending[parent] == 0:
                ready.append(parent)
    if len(bounds) != len(frames):
        raise StackEvidenceError("recursive same-epoch call graph")
    return bounds, successors


def certify_stack_bounds(frames, edges, roots):
    sizes = frame_inventory(frames)
    entries = root_inventory(roots, sizes)
    children = call_inventory(edges, sizes, entries)
    bounds, successors = fixed_bounds(sizes, children)
    certificates = []
    for root in roots:
        used = checked_add(root.initial_bytes, bounds[root.function])
        if used > root.capacity:
            raise StackEvidenceError(
                f"{root.identifier}: stack budget exceeded ({used} > {root.capacity})"
            )
        witness = []
        current = root.function
        while current is not None:
            witness.append(current)
            current = successors[current]
        certificates.append(Certificate(
            root, used, root.capacity - used, tuple(witness),
        ))
    return tuple(certificates)
