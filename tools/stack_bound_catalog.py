"""Qualified local/global call resolution from actual linked instructions."""

from dataclasses import dataclass

from tools.stack_bound_link import direct_call_target
from tools.stack_bound_types import (
    CallSite, FunctionKey, StackEvidenceError, SymbolCall, checked_add,
    require_function, require_uint64,
)


@dataclass(frozen=True)
class DefinedFunction:
    function: FunctionKey
    address: int
    size: int
    global_linkage: bool


def resolve_linked_calls(image, calls, definitions):
    if type(calls) is not tuple or type(definitions) is not tuple:
        raise StackEvidenceError("linked call catalog requires complete tuples")
    functions = {}
    globals_by_name = {}
    for definition in definitions:
        if type(definition) is not DefinedFunction or type(definition.global_linkage) is not bool:
            raise StackEvidenceError("invalid linked function definition")
        key = definition.function
        require_function(key)
        require_uint64(definition.address, "function address")
        require_uint64(definition.size, "function size")
        if definition.size == 0:
            raise StackEvidenceError("linked function has no instruction range")
        if key in functions:
            raise StackEvidenceError("duplicate qualified linked definition")
        functions[key] = definition
        if definition.global_linkage:
            name = (key.variant, key.image, key.symbol)
            globals_by_name.setdefault(name, []).append(key)
    resolved = []
    pending = []
    seen = set()
    for call in calls:
        if type(call) is not SymbolCall:
            raise StackEvidenceError("invalid symbolic linked call")
        require_function(call.caller)
        require_uint64(call.offset, "linked call offset")
        site = (call.caller, call.offset)
        if site in seen or call.caller not in functions:
            raise StackEvidenceError("duplicate or unowned linked caller")
        owner = functions[call.caller]
        if call.offset >= owner.size:
            raise StackEvidenceError("call leaves its qualified caller instruction range")
        seen.add(site)
        if call.target_symbol is None:
            pending.append(call)
            continue
        if owner.size - call.offset < 8:
            raise StackEvidenceError("direct call is truncated within its caller")
        local = FunctionKey(
            call.caller.variant, call.caller.image,
            call.caller.translation_unit, call.target_symbol,
        )
        require_function(local)
        if local in functions:
            target = local
        else:
            candidates = globals_by_name.get(
                (call.caller.variant, call.caller.image, call.target_symbol), (),
            )
            if not candidates:
                pending.append(call)
                continue
            if len(candidates) != 1:
                raise StackEvidenceError("ambiguous global linked callee")
            target = candidates[0]
        address = checked_add(functions[call.caller].address, call.offset)
        actual, kind = direct_call_target(image, address)
        if actual != functions[target].address or kind is not call.kind:
            raise StackEvidenceError("linked instruction disagrees with its qualified callee")
        resolved.append(CallSite(call.caller, call.offset, (target,), call.kind))
    return tuple(resolved), tuple(pending)
