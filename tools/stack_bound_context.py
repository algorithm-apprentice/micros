"""Finite composition primitive; bindings require separate artifact authority."""

from dataclasses import dataclass
from enum import IntEnum

from tools.stack_bound_graph import (
    call_inventory, fixed_bounds, frame_inventory, root_inventory,
)
from tools.stack_bound_types import (
    EdgeKind, FunctionKey, Root, StackEvidenceError, checked_add,
    require_function, require_uint64,
)


class Qualifier(IntEnum):
    UNQUALIFIED = 0
    UART_PANIC_READY = 1


@dataclass(frozen=True, order=True)
class ContextNode:
    function: FunctionKey
    qualifier: Qualifier


@dataclass(frozen=True)
class ContextBindings:
    establishing: tuple
    preservers: tuple[FunctionKey, ...]
    infeasible: tuple


@dataclass(frozen=True)
class ContextCertificate:
    root: Root
    bytes_used: int
    bytes_remaining: int
    witness: tuple[ContextNode, ...]


def binding_inventory(binding, frames, edges):
    if type(binding) is not ContextBindings:
        raise StackEvidenceError("missing finite context binding inventory")
    sites = {(edge.caller, edge.offset): edge for edge in edges}
    if (
        type(binding.establishing) is not tuple or type(binding.preservers) is not tuple
        or type(binding.infeasible) is not tuple or len(binding.infeasible) != 2
    ):
        raise StackEvidenceError("invalid finite context declaration")
    ready_sites = set()
    for site in binding.establishing:
        if type(site) is not tuple or len(site) != 2:
            raise StackEvidenceError("invalid context establishing site")
        caller, offset = site
        require_function(caller)
        require_uint64(offset, "context establishing offset")
        if (
            site in ready_sites or site not in sites or sites[site].kind is EdgeKind.RESET
            or caller.translation_unit != "kernel/diagnostic/panic.c"
            or caller.symbol not in ("micros_panic_seize", "micros_panic_report")
        ):
            raise StackEvidenceError("unknown, duplicate or borrowed establishing site")
        ready_sites.add(site)
    preservers = set()
    for function in binding.preservers:
        require_function(function)
        if function not in frames or function in preservers:
            raise StackEvidenceError("unknown or duplicate context preserver")
        preservers.add(function)
    guard, offset = binding.infeasible
    require_function(guard)
    require_uint64(offset, "context guard offset")
    edge = sites.get(binding.infeasible)
    if (
        guard.translation_unit != "arch/riscv64/uart.c" or guard.symbol != "uart_write"
        or guard not in preservers or edge is None or edge.kind is not EdgeKind.CALL
        or len(edge.targets) != 1 or edge.targets[0].translation_unit != "arch/riscv64/panic.S"
        or edge.targets[0].symbol != "micros_panic_entry"
    ):
        raise StackEvidenceError("context exclusion borrows an undeclared panic edge")
    return ready_sites, preservers


def certify_context_bounds(frames, edges, roots, binding):
    sizes = frame_inventory(frames)
    entries = root_inventory(roots, sizes)
    call_inventory(edges, sizes, entries)
    establishing, preservers = binding_inventory(binding, sizes, edges)
    node_sizes = {
        ContextNode(function, Qualifier.UNQUALIFIED): size for function, size in sizes.items()
    }
    node_sizes.update({
        ContextNode(function, Qualifier.UART_PANIC_READY): sizes[function] for function in preservers
    })
    children = {node: set() for node in node_sizes}
    for edge in edges:
        site = edge.caller, edge.offset
        if edge.kind is EdgeKind.RESET:
            if site in establishing:
                raise StackEvidenceError("context establishment cannot release its caller frame")
            continue
        for qualifier in Qualifier:
            caller = ContextNode(edge.caller, qualifier)
            if caller not in children:
                continue
            if qualifier is Qualifier.UART_PANIC_READY and site == binding.infeasible:
                continue
            ready = qualifier is Qualifier.UART_PANIC_READY or site in establishing
            for target in edge.targets:
                if ready and target not in preservers:
                    raise StackEvidenceError("ready context reaches an unbound preservation effect")
                child = ContextNode(
                    target, Qualifier.UART_PANIC_READY if ready else Qualifier.UNQUALIFIED,
                )
                children[caller].add(child)
    bounds, successors = fixed_bounds(node_sizes, children)
    proofs = []
    for root in roots:
        node = ContextNode(root.function, Qualifier.UNQUALIFIED)
        used = checked_add(root.initial_bytes, bounds[node])
        if used > root.capacity:
            raise StackEvidenceError(f"{root.identifier}: stack budget exceeded ({used} > {root.capacity})")
        witness = []
        while node is not None:
            witness.append(node)
            node = successors[node]
        proofs.append(ContextCertificate(root, used, root.capacity - used, tuple(witness)))
    return tuple(proofs)
