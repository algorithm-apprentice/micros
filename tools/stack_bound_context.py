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
    PANIC_LATCHED = 2
    PANIC_LATCHED_UART_READY = 3


@dataclass(frozen=True, order=True)
class ContextNode:
    function: FunctionKey
    qualifier: Qualifier


@dataclass(frozen=True)
class ContextBindings:
    establishing: tuple
    preservers: tuple[FunctionKey, ...]
    infeasible: tuple[tuple[FunctionKey, int], ...]
    latching: tuple = ()
    latch_preservers: tuple[FunctionKey, ...] = ()


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
        type(binding.establishing) is not tuple or not binding.establishing or type(binding.preservers) is not tuple
        or type(binding.infeasible) is not tuple or len(binding.infeasible) != 4
        or type(binding.latching) is not tuple or len(binding.latching) != 1
        or type(binding.latch_preservers) is not tuple
    ):
        raise StackEvidenceError("invalid finite context declaration")
    established = {}
    for site in binding.establishing:
        if type(site) is not tuple or len(site) != 2:
            raise StackEvidenceError("invalid context establishing site")
        caller, offset = site
        require_function(caller)
        require_uint64(offset, "context establishing offset")
        if (
            site in established or site not in sites or sites[site].kind is EdgeKind.RESET
            or caller.translation_unit != "kernel/diagnostic/panic.c"
            or caller.symbol not in ("micros_panic_seize", "panic_report_claimed")
        ):
            raise StackEvidenceError("unknown, duplicate or borrowed establishing site")
        established[site] = Qualifier.UART_PANIC_READY
    for site in binding.latching:
        if type(site) is not tuple or len(site) != 2:
            raise StackEvidenceError("invalid first-entry latching site")
        caller, offset = site
        require_function(caller)
        require_uint64(offset, "latching call offset")
        edge = sites.get(site)
        if (
            site in established or edge is None or edge.kind is not EdgeKind.CALL
            or (caller.translation_unit, caller.symbol) != ("kernel/diagnostic/panic.c", "micros_panic_report")
            or len(edge.targets) != 1
            or (edge.targets[0].translation_unit, edge.targets[0].symbol) != (
                "kernel/diagnostic/panic.c", "panic_report_claimed",
            )
        ):
            raise StackEvidenceError("first-entry latch borrows another call or source owner")
        established[site] = Qualifier.PANIC_LATCHED
    preservation = {}
    for functions, fact in (
        (binding.preservers, Qualifier.UART_PANIC_READY),
        (binding.latch_preservers, Qualifier.PANIC_LATCHED),
    ):
        seen = set()
        for function in functions:
            require_function(function)
            if function not in frames or function in seen:
                raise StackEvidenceError("unknown or duplicate context preserver")
            seen.add(function)
            preservation[function] = preservation.get(function, 0) | fact
    required = {
        ("arch/riscv64/uart.c", "uart_write_direct"):
            ("arch/riscv64/panic.S", "micros_panic_entry", Qualifier.UART_PANIC_READY),
        ("arch/riscv64/uart.c", "uart_write"):
            ("arch/riscv64/uart.c", "uart_write_record", Qualifier.UART_PANIC_READY),
        ("arch/riscv64/uart.c", "uart_write_hex64"):
            ("arch/riscv64/uart.c", "uart_write_hex64_record", Qualifier.UART_PANIC_READY),
        ("kernel/diagnostic/panic.c", "micros_panic_report"):
            ("kernel/diagnostic/panic.c", "panic_report_claimed", Qualifier.PANIC_LATCHED),
    }
    excluded = {}
    callers = set()
    for site in binding.infeasible:
        if type(site) is not tuple or len(site) != 2:
            raise StackEvidenceError("invalid finite context exclusion site")
        guard, offset = site
        require_function(guard)
        require_uint64(offset, "context guard offset")
        edge = sites.get(site)
        identity = guard.translation_unit, guard.symbol
        declaration = required.get(identity)
        if (
            declaration is None or identity in callers or site in excluded
            or edge is None or edge.kind is not EdgeKind.CALL or len(edge.targets) != 1
            or (edge.targets[0].translation_unit, edge.targets[0].symbol) != declaration[:2]
            or not preservation.get(guard, 0) & declaration[2]
            or declaration[2] is Qualifier.PANIC_LATCHED and site not in binding.latching
        ):
            raise StackEvidenceError("context exclusion borrows an undeclared diagnostic edge")
        excluded[site] = declaration[2]
        callers.add(identity)
    if callers != set(required):
        raise StackEvidenceError("context exclusion lacks its complete four-site register")
    return established, preservation, excluded


def certify_context_bounds(frames, edges, roots, binding):
    sizes = frame_inventory(frames)
    entries = root_inventory(roots, sizes)
    call_inventory(edges, sizes, entries)
    establishing, preservation, excluded = binding_inventory(binding, sizes, edges)
    node_sizes = {
        ContextNode(function, Qualifier.UNQUALIFIED): size for function, size in sizes.items()
    }
    children = {node: set() for node in node_sizes}
    outgoing = {function: [] for function in sizes}
    for edge in edges:
        site = edge.caller, edge.offset
        if edge.kind is EdgeKind.RESET:
            if site in establishing:
                raise StackEvidenceError("context establishment cannot release its caller frame")
            continue
        outgoing[edge.caller].append(edge)
    pending = list(node_sizes)
    while pending:
        caller = pending.pop()
        qualifier = caller.qualifier
        for edge in outgoing[caller.function]:
            site = edge.caller, edge.offset
            if qualifier & excluded.get(site, 0):
                continue
            facts = Qualifier(qualifier | establishing.get(site, 0))
            for target in edge.targets:
                if facts & preservation.get(target, 0) != facts:
                    raise StackEvidenceError(
                        "qualified context reaches an unbound preservation effect: "
                        f"{edge.caller.translation_unit}:{edge.caller.symbol}+{edge.offset:#x} "
                        f"-> {target.translation_unit}:{target.symbol} [{facts.name}]"
                    )
                child = ContextNode(target, facts)
                if child not in node_sizes:
                    node_sizes[child] = sizes[target]
                    children[child] = set()
                    pending.append(child)
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
