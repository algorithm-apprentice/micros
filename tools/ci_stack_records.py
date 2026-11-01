"""Preserved per-image proofs and current-scope CI transport consistency."""

import hashlib

from tools.ci_records import owned_file, read_json, require_context, require_keys
from tools.stack_artifact_files import read_regular
from tools.qemu_smoke_command import build_qemu_command
from tools.stack_bound_context import ContextBindings, certify_context_bounds
from tools.stack_bound_graph import certify_stack_bounds
from tools.stack_bound_manifest import encoded
from tools.stack_bound_publish import image_record, json_value
from tools.stack_bound_reports import Producer, legacy_transcript, validate_observation_records
from tools.stack_bound_scope import SCENARIOS, expected_images, producer_roles
from tools.stack_bound_types import CallSite, EdgeKind, Frame, FunctionKey, Root, require_function, require_uint64


FUNCTION_FIELDS = ("variant", "image", "translation_unit", "symbol")
IMAGE_FIELDS = (
    "variant", "image", "mode", "frames", "calls", "roots", "epochs", "payloads",
    "faults", "continuations", "snapshots", "fatal_context",
)
KERNEL_ROOTS = {
    "boot": ("_start", 16384, 0),
    "supervisor": ("micros_trap_entry", 16384, 0),
    "emergency": ("micros_trap_nested_panic", 4096, 0),
    "primary-idle": ("micros_scheduler_idle_wait", 16384, 0),
    "kernel-main": ("kernel_main", 16384, 0),
    "trap-dispatch": ("micros_trap_dispatch", 16384, 288),
    "panic-report": ("micros_panic_report", 4096, 48),
    "idle-select": ("micros_scheduler_idle_select", 16384, 0),
}


def function(value, variant, image):
    require_keys(value, FUNCTION_FIELDS, "CI function")
    result = FunctionKey(**value)
    require_function(result)
    if result.variant != variant or result.image != image:
        raise ValueError("CI proof borrowed another variant or executable")
    return result


def validate_image(value, variant, image, mode, firmware):
    require_keys(value, IMAGE_FIELDS, "CI image proof")
    if (value["variant"], value["image"], value["mode"]) != (variant, image, mode):
        raise ValueError("CI image proof has a foreign identity or mode")
    pending = [value]
    while pending:
        item = pending.pop()
        if type(item) is dict:
            if set(FUNCTION_FIELDS) <= set(item):
                function(item, variant, image)
            pending.extend(item.values())
        elif type(item) is list:
            pending.extend(item)
    frames = []
    for row in value["frames"]:
        require_keys(row, ("function", "bytes_used", "classification"), "CI frame")
        frames.append(Frame(function(row["function"], variant, image), row["bytes_used"], row["classification"]))
    keys = [row.function for row in frames]

    def indexed(index):
        if type(index) is not int or not 0 <= index < len(keys):
            raise ValueError("CI call/witness refers to an unowned frame")
        return keys[index]

    edges = []
    for row in value["calls"]:
        require_keys(row, ("caller", "offset", "kind", "targets"), "CI call")
        edges.append(CallSite(indexed(row["caller"]), row["offset"],
                              tuple(indexed(index) for index in row["targets"]), EdgeKind(row["kind"])))
    roots = []
    for row in value["roots"]:
        require_keys(row, ("root", "bytes_used", "bytes_remaining", "witness"), "CI root proof")
        root = row["root"]
        require_keys(root, ("identifier", "function", "capacity", "initial_bytes"), "CI root")
        roots.append(Root(root["identifier"], function(root["function"], variant, image),
                          root["capacity"], root["initial_bytes"]))
    if image == "kernel/micros.elf":
        policy = dict(KERNEL_ROOTS)
        policy.update({f"user-payload-{index}": (row["function"]["symbol"], 4096, 0)
                       for index, row in enumerate(value["payloads"])})
    elif image.startswith("applications/"):
        policy = {
            "user-startup": ("_start", 4096, 2048),
            "application-start": ("micros_application_start", 4096, 2048),
            "application-main": ("micros_application_main", 4096, 2048),
        }
        if any(key.symbol == "micros_application_exit" for key in keys):
            policy["application-exit"] = ("micros_application_exit", 4096, 2048)
    else:
        policy = {"user-startup": ("_start", 4096, 0), "service-main": ("micros_service_main", 4096, 0)}
    if {root.identifier for root in roots} != set(policy) or any(
        (root.function.symbol, root.capacity, root.initial_bytes) != policy[root.identifier] for root in roots
    ):
        raise ValueError("CI root declaration is missing, borrowed or changes an Accepted budget")
    fatal = value["fatal_context"]
    if image == "kernel/micros.elf":
        if type(fatal) is not dict or encoded(fatal.get("firmware")) != encoded(firmware):
            raise ValueError("CI fatal proof lacks its exact producer firmware identity")
        binding = fatal["bindings"]
        require_keys(binding, ("establishing", "preservers", "infeasible"), "CI fatal binding")
        guard = binding["infeasible"]
        if type(guard) is not list or len(guard) != 2:
            raise ValueError("CI fatal guard is incomplete")
        bound = ContextBindings(
            tuple((function(row[0], variant, image), row[1]) for row in binding["establishing"]),
            tuple(function(row, variant, image) for row in binding["preservers"]),
            (function(guard[0], variant, image), guard[1]),
        )
        proofs = certify_context_bounds(tuple(frames), tuple(edges), tuple(roots), bound)
    else:
        if fatal is not None:
            raise ValueError("user image borrowed supervisor fatal context")
        proofs = certify_stack_bounds(tuple(frames), tuple(edges), tuple(roots))
    indexes = {key: index for index, key in enumerate(keys)}
    for row, proof in zip(value["roots"], proofs):
        require_uint64(row["bytes_used"], "CI root use")
        require_uint64(row["bytes_remaining"], "CI root remaining")
        witness = [
            {"function": indexes[item.function if hasattr(item, "qualifier") else item],
             "qualifier": item.qualifier.name if hasattr(item, "qualifier") else "UNQUALIFIED"}
            for item in proof.witness
        ]
        if (row["bytes_used"], row["bytes_remaining"], row["witness"]) != (
            proof.bytes_used, proof.bytes_remaining, witness,
        ):
            raise ValueError("CI proof arithmetic or witness differs from its preserved graph")


def static_record(value):
    return {
        "profile": value.profile, "ordinary_seal": value.ordinary_seal, "metadata_seal": value.metadata_seal,
        "images": [image_record(image) for image in value.images],
    }


def observed_record(value):
    return {
        "gap": value.gap, "outcome": "PASS", "profile": value.configure, "observed_seal": value.seal,
        "images": [image_record(image) for image in value.image_proofs],
        "observations": json_value(value.observations),
        "qemu": {"argv": list(value.command), "timeout": value.timeout},
        "transcript": value.transcript,
        "transcript_sha256": hashlib.sha256(value.transcript.encode("utf-8")).hexdigest(),
    }


def validate_observed(row, profile, context):
    require_keys(row, ("gap", "outcome", "profile", "observed_seal", "images", "observations",
                       "qemu", "transcript", "transcript_sha256"), "CI observed scope")
    if (row["gap"], row["outcome"], row["profile"]) != (SCENARIOS[profile].gap, "PASS", profile):
        raise ValueError("CI observed scope has a missing or foreign outcome")
    validate_images(row["images"], profile, "observed", context["firmware"])
    if hashlib.sha256(row["transcript"].encode()).hexdigest() != row["transcript_sha256"]:
        raise ValueError("CI observed transcript changed")
    legacy_transcript(row["transcript"])
    require_keys(row["qemu"], ("argv", "timeout"), "CI observed command")
    kernel = context["repository_context"]["root"] + "/build/stack-evidence/" + profile + "/observed/kernel/micros.elf"
    if row["qemu"]["argv"] != build_qemu_command(
        qemu=context["firmware"]["qemu"]["path"], kernel=kernel, memory="128M",
    ) or type(row["qemu"]["timeout"]) not in (int, float) or row["qemu"]["timeout"] <= 0:
        raise ValueError("CI observed command changed its exact guest identity")
    bounds = {
        (image["image"], proof["root"]["identifier"]): proof["bytes_used"]
        for image in row["images"] for proof in image["roots"]
    }
    roles = {value[:4]: value[4] for value in producer_roles(profile)}
    producers = []
    for observation in row["observations"]:
        require_keys(observation, ("producer", "samples", "use"), "CI observation")
        value = observation["producer"]
        require_keys(value, ("image", "owner", "slot", "generation", "bottom", "top", "bound", "required"), "CI producer")
        producer = Producer(**value)
        if producer.identity not in roles or roles[producer.identity] != producer.required:
            raise ValueError("CI observation borrowed another image/lifecycle generation")
        if producer.owner == "user":
            expected = bounds[producer.image, "user-startup"]
            if (producer.bottom, producer.top) != (0x7ffff000, 0x80000000):
                raise ValueError("CI user observation has a foreign interval")
        elif producer.owner == "boot":
            expected = bounds["kernel/micros.elf", "boot"]
        elif producer.owner == "primary":
            expected = max(bounds["kernel/micros.elf", "supervisor"], bounds["kernel/micros.elf", "primary-idle"])
        else:
            expected = bounds["kernel/micros.elf", "supervisor"]
        if producer.bound != expected:
            raise ValueError("CI observation borrowed a different static bound")
        producers.append(producer)
    identities = {producer.identity for producer in producers}
    if any(required and identity not in identities for identity, required in roles.items()):
        raise ValueError("CI observed scope omitted a required live producer")
    verified = validate_observation_records(row["transcript"], tuple(producers))
    if json_value(verified) != row["observations"]:
        raise ValueError("CI observation data differs from its exact complete transcript")


def validate_images(images, profile, mode, firmware):
    if type(images) is not list or sorted(image["image"] for image in images) != list(expected_images(profile)):
        raise ValueError("CI profile omitted or duplicated an executable image")
    for image in images:
        validate_image(image, profile, image["image"], mode, firmware)


def validate_profile(row, profile, context, directory):
    require_keys(row, ("version", "status", "part", "profile", "before", "after", "static", "observed"), "CI profile")
    if type(row["version"]) is not int or (row["version"], row["status"], row["part"], row["profile"]) != (
        1, "PROFILE_COMPLETE", context["part"], profile,
    ):
        raise ValueError("CI profile has a foreign or incomplete identity")
    require_context(row["before"], context, context["part"])
    if encoded(row["before"]) != encoded(context) or encoded(row["after"]) != encoded(context):
        raise ValueError("CI profile source/tool context changed within its producer")
    static = row["static"]
    require_keys(static, ("profile", "ordinary_seal", "metadata_seal", "images"), "CI static profile")
    if static["profile"] != profile:
        raise ValueError("CI static profile is borrowed")
    validate_images(static["images"], profile, "metadata", context["firmware"])
    identities = {"ordinary": static["ordinary_seal"], "metadata": static["metadata_seal"]}
    if profile in SCENARIOS:
        validate_observed(row["observed"], profile, context)
        identities["observed"] = row["observed"]["observed_seal"]
    elif row["observed"] is not None:
        raise ValueError("CI profile fabricated an undeclared observed scenario")
    for mode, identity in identities.items():
        path = owned_file(directory, f"seals/{profile}/{mode}.json")
        data = read_regular(path)[0]
        if len(data) != identity["size"] or hashlib.sha256(data).hexdigest() != identity["sha256"]:
            raise ValueError("CI profile replaced its original build seal")
        seal = read_json(path)
        if (seal["version"], seal["profile"], seal["mode"], seal["candidate"]) != (
            1, profile, mode, context["candidate"],
        ):
            raise ValueError("CI profile build seal has a foreign source or mode")
        for name in ("compiler", "linker"):
            actual, expected = seal["tools"][name], context["tools"][name]
            if any(actual[field] != expected[field] for field in ("path", "resolved", "identity")):
                raise ValueError("CI profile build seal borrowed another compiler/linker")
