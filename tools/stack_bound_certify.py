"""Closed per-image stack proofs reused by metadata and observed admission."""

from dataclasses import dataclass, replace
from pathlib import Path

from tools.stack_bound_assembly import collect_linked_assembly
from tools.stack_bound_bindings import bind_current_indirect_calls
from tools.stack_bound_collect import collect_linked_c
from tools.stack_bound_context import certify_context_bounds
from tools.stack_bound_fatal import derive_fatal_context
from tools.stack_bound_fatal_firmware import resolve_default_firmware
from tools.stack_bound_graph import certify_stack_bounds
from tools.stack_bound_observer import compiler_hook_inventory
from tools.stack_bound_objects import function_symbols, load_object
from tools.stack_bound_types import Root, StackEvidenceError
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
        proofs = certify_context_bounds(graph.frames, graph.edges, roots, fatal.bindings)
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
