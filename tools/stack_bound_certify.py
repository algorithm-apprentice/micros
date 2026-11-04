"""Closed per-image stack proofs reused by metadata and observed admission."""

from dataclasses import dataclass, replace
from pathlib import Path

from tools.stack_bound_assembly import collect_linked_assembly
from tools.stack_bound_bindings import bind_current_indirect_calls
from tools.stack_bound_collect import collect_linked_c
from tools.stack_bound_context import certify_context_bounds
from tools.stack_bound_fatal import derive_fatal_context
from tools.stack_bound_fatal_effects import TRACE_FAULT_FUNCTIONS
from tools.stack_bound_fatal_firmware import resolve_default_firmware
from tools.stack_bound_graph import certify_stack_bounds
from tools.stack_bound_observer import compiler_hook_inventory
from tools.stack_bound_objects import function_symbols, load_object
from tools.stack_bound_types import EdgeKind, Root, StackEvidenceError, checked_add
from tools.user_elf_parse import load_elf


@dataclass(frozen=True)
class ImageProof:
    variant: str
    image: str
    mode: str
    roots: tuple
    frames: tuple
    edges: tuple
    epochs: tuple
    payloads: tuple
    faults: tuple
    continuations: tuple
    snapshots: tuple = ()
    fatal: object = None


def root_functions(graph, entry, image_name):
    symbols = {}
    for definition in graph.definitions:
        if definition.global_linkage:
            symbols.setdefault(definition.function.symbol, []).append(definition)
    roots = []

    def include(identifier, name, capacity, initial=0):
        selected = symbols.get(name, ())
        if len(selected) != 1:
            raise StackEvidenceError(f"image lacks its exact active stack root: {name}")
        roots.append(Root(identifier, selected[0].function, capacity, initial))
        return selected[0]

    kernel = image_name == "kernel/micros.elf"
    start = include("boot" if kernel else "user-startup", "_start", 16384 if kernel else 4096,
                    2048 if image_name.startswith("applications/") else 0)
    if start.address != entry:
        raise StackEvidenceError("ELF entry is not its declared stack-start function")
    if kernel:
        include("supervisor", "micros_trap_entry", 16384)
        include("emergency", "micros_trap_nested_panic", 4096)
        include("primary-idle", "micros_scheduler_idle_wait", 16384)
        include("kernel-main", "kernel_main", 16384)
        include("trap-dispatch", "micros_trap_dispatch", 16384, 288)
        include("panic-report", "micros_panic_report", 4096, 48)
        include("idle-select", "micros_scheduler_idle_select", 16384)
        roots.extend(Root(f"user-payload-{index}", payload.function, 4096)
                     for index, payload in enumerate(graph.payloads))
    elif start.function.translation_unit == "lib/runtime/start.S":
        include("service-main", "micros_service_main", 4096)
    elif start.function.translation_unit == "lib/runtime/application_start.S":
        include("application-start", "micros_application_start", 4096, 2048)
        include("application-main", "micros_application_main", 4096, 2048)
        if "micros_application_exit" in symbols:
            include("application-exit", "micros_application_exit", 4096, 2048)
    else:
        raise StackEvidenceError("unregistered user startup stack contract")
    return tuple(roots)


def trace_fault_bounds(graph, proofs, fatal):
    fixture = dict(fatal.trace_register)["fixture"]
    if not fixture:
        if any(fault.function.translation_unit in ("arch/riscv64/panic_trace.S", "tests/qemu/panic_trace_kernel.S")
               for fault in graph.faults):
            raise StackEvidenceError("trace hardware fault has no complete version-four fixture register")
        return ()
    required = (
        "linked-inputs", "constructor", "setup-accessors", "qualified-observers-and-collector",
        "physical-stacks", "hardware-faults", "actual-trap-pivots",
        "post-fault-source-objects", "post-fault-c-prefix", "same-epoch-reentry",
    )
    if tuple(name for name, _ in fixture) != required:
        raise StackEvidenceError("trace fixture lacks its complete finite hardware-transition register")
    values = dict(fixture)
    if (
        tuple(row[0] for row in values["hardware-faults"]) != ("load", "nested", "outer")
        or len(graph.faults) != 3 or set(graph.faults) != {row[1] for row in values["hardware-faults"]}
    ):
        raise StackEvidenceError("trace epoch register does not bind every actual linked fault site")
    definitions = {row.function: row for row in graph.definitions}
    for _, fault, _, address, _ in values["hardware-faults"]:
        if fault.function not in definitions or definitions[fault.function].address + fault.offset != address:
            raise StackEvidenceError("trace epoch fault address differs from its actual linked definition")
    roots = {row.root.identifier: row for row in proofs}
    frames = {row.function: row.bytes_used for row in graph.frames}
    keys = {(key.translation_unit, key.symbol): key for key in frames}
    capture = keys["arch/riscv64/panic.S", "micros_trap_panic_entry"]
    entry = keys["arch/riscv64/panic.S", "micros_panic_entry"]
    report = keys["kernel/diagnostic/panic.c", "micros_panic_report"]
    trace = keys["kernel/diagnostic/panic_trace.c", "micros_panic_trace_report"]
    load = keys["arch/riscv64/panic_trace.S", "micros_panic_trace_load_word"]
    admitted = dict(fatal.trace_register)["entry"]
    entries = tuple(edge for edge in graph.edges if trace in edge.targets)
    words = tuple(edge for edge in graph.edges if load in edge.targets)
    if (
        len(entries) != 1 or (entries[0].caller, entries[0].offset) != (admitted.function, admitted.offset)
        or len(words) != 2 or any(edge.caller != trace or edge.kind is not EdgeKind.CALL for edge in words)
        or load not in fatal.bindings.preservers or load not in fatal.bindings.latch_preservers
    ):
        raise StackEvidenceError("trace load-fault source bypasses its actual claimed, qualified report path")
    if (
        frames[capture] != 48 or frames[entry] != 0
        or roots["panic-report"].root.initial_bytes != 48 or roots["panic-report"].root.function != report
        or roots["boot"].root.capacity != 16384 or roots["supervisor"].root.capacity != 16384
        or roots["emergency"].root.capacity != 4096
    ):
        raise StackEvidenceError("trace epoch borrowed a capture frame, root or stack budget")
    source_keys = tuple(row[0] for row in values["post-fault-source-objects"])
    expected_keys = tuple(keys[unit, name] for unit, name in TRACE_FAULT_FUNCTIONS)
    if source_keys != expected_keys or tuple(row[0] for row in values["post-fault-c-prefix"]) != tuple(range(8)):
        raise StackEvidenceError("trace post-fault prefix omitted a source owner or resolved hart slot")
    destinations = []
    for slot, hart, frame, witnesses in values["post-fault-c-prefix"]:
        if tuple((row[0], row[1]) for row in witnesses) != TRACE_FAULT_FUNCTIONS:
            raise StackEvidenceError("trace post-fault prefix borrowed a function-effect owner")
        bounds, edges = {}, []
        for unit, name, _, effects, frame_record in witnesses:
            key = keys[unit, name]
            if frame_record[0:2] != ("psabi-frame-record", frames[key]):
                raise StackEvidenceError("trace post-fault path changed its actual live C frame")
            children = []
            for offset, target, _ in effects.calls:
                matches = tuple(edge for edge in graph.edges if (
                    edge.caller == key and edge.offset == offset and len(edge.targets) == 1
                    and edge.targets[0].symbol == target and edge.kind is EdgeKind.CALL
                ))
                if len(matches) != 1:
                    raise StackEvidenceError("trace post-fault prefix borrowed or released an actual call frame")
                edge, = matches
                if edge.targets == (capture,):
                    children.append(roots["panic-report"].bytes_used)
                elif edge.targets[0] in bounds:
                    children.append(bounds[edge.targets[0]])
                else:
                    raise StackEvidenceError("trace post-fault prefix lacks its transitive frame/effect bound")
                edges.append(edge)
            bounds[key] = checked_add(frames[key], max(children, default=0))
        used = checked_add(288, bounds[keys["kernel/trap/trap.c", "micros_trap_dispatch"]])
        if used > roots["supervisor"].root.capacity:
            raise StackEvidenceError("trace actual load-fault destination exceeds its unchanged primary-stack budget")
        destinations.append((slot, hart, frame, used, tuple(edges)))
    reentry = values["same-epoch-reentry"]
    chain = (
        (capture, report),
        (report, keys["kernel/diagnostic/panic.c", "panic_report_claimed"]),
        (keys["kernel/diagnostic/panic.c", "panic_report_claimed"], keys["kernel/diagnostic/panic_trace.c", "micros_panic_trace_report"]),
        (keys["kernel/diagnostic/panic_trace.c", "micros_panic_trace_report"], reentry.function),
        (reentry.function, entry), (entry, capture),
    )
    retained = []
    for caller, target in chain:
        matches = tuple(edge for edge in graph.edges if edge.caller == caller and edge.targets == (target,))
        expected = EdgeKind.TAIL if caller == entry else EdgeKind.CALL
        if len(matches) != 1 or matches[0].kind is not expected or (
            caller == reentry.function and matches[0].offset != reentry.offset
        ):
            raise StackEvidenceError("trace explicit reentry released or substituted one of its two live captures")
        retained.append(matches[0])
    return (
        ("case-15", "boot-retained", roots["boot"], "primary-new-capture", tuple(destinations),
         "latched-tail-conservative-bound", roots["panic-report"], fatal.control, fatal.latch_establishment),
        ("case-5", "boot-retained", roots["boot"], "primary-capture-retained", 288,
         "emergency-new-epoch", roots["emergency"]),
        ("case-16", "same-boot-epoch", roots["boot"], "two-retained-captures", (48, 48), tuple(retained)),
    )


def certify_image(root, directory, image_name, variant, mode):
    root, directory = Path(root), Path(directory)
    if mode not in ("metadata", "observed"):
        raise StackEvidenceError("stack proof needs its actual compiler metadata mode")
    catalog = collect_linked_c(root, directory, image_name, variant)
    objects = {}
    if mode == "observed":
        for key, name in catalog.objects:
            if name not in objects:
                objects[name] = load_object(directory / name)
            obj = objects[name]
            compiler_hook_inventory(obj, function_symbols(obj)[key.symbol])
    callbacks, pending = bind_current_indirect_calls(root, directory, catalog)
    graph = collect_linked_assembly(
        root, directory, image_name, variant,
        replace(catalog, calls=catalog.calls + callbacks, pending=pending),
    )
    image = load_elf(directory / image_name)
    if mode == "metadata" and any(symbol.name.startswith((
        "__cyg_profile_", "micros_stack_observer", "micros_stack_observations",
        "micros_stack_user_observation",
    )) for symbol in image.symbols):
        raise StackEvidenceError("metadata image contains forbidden observation support")
    roots = root_functions(graph, image.header.entry, image_name)
    fatal = None
    if image_name == "kernel/micros.elf":
        qemu = tuple(line.partition("=")[2] for line in (directory / "CMakeCache.txt").read_text().splitlines()
                     if line.startswith("MICROS_QEMU:FILEPATH="))
        if len(qemu) != 1:
            raise StackEvidenceError("stack kernel proof lacks its configured QEMU")
        fatal = derive_fatal_context(root, directory, catalog, qemu=qemu[0])
        if (
            fatal.version != 4 or len(fatal.exclusions) != 4
            or tuple((row.edge.function, row.edge.offset) for row in fatal.exclusions) != fatal.bindings.infeasible
            or tuple(row.required for row in fatal.exclusions) != (1, 1, 1, 2)
            or tuple((row.function, row.offset) for row in fatal.latch_establishment) != fatal.bindings.latching
            or fatal.control.size != 4
            or tuple(name for name, _ in fatal.trace_register) != (
                "entry", "ownership", "portable", "target", "selected", "assembly", "frames", "compile-policy", "fixture",
            )
        ):
            raise StackEvidenceError("kernel stack proof lacks its complete version-four fatal register")
        proofs = certify_context_bounds(graph.frames, graph.edges, roots, fatal.bindings)
        fault_bounds = trace_fault_bounds(graph, proofs, fatal)
        if fault_bounds:
            fatal = replace(fatal, trace_register=tuple(
                (name, value + (("epoch-stack-bounds", fault_bounds),) if name == "fixture" else value)
                for name, value in fatal.trace_register
            ))
    else:
        proofs = certify_stack_bounds(graph.frames, graph.edges, roots)
    return ImageProof(
        variant, image_name, mode, proofs, graph.frames, graph.edges,
        graph.epochs, graph.payloads, graph.faults, graph.continuations,
        snapshots=graph.snapshots, fatal=fatal,
    )


def verify_firmware_proofs(proofs):
    current = {}
    for proof in proofs:
        if proof.fatal is None:
            continue
        expected = proof.fatal.firmware
        path = expected["qemu"]["path"]
        if path not in current:
            current[path] = resolve_default_firmware(path)
        if current[path] != expected:
            raise StackEvidenceError("QEMU, firmware or default configuration changed after its fatal proof")
